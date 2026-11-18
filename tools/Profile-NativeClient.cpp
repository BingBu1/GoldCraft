// SPDX-License-Identifier: GPL-3.0-or-later
// x86 user-mode statistical profiler. No injection, patches or kernel session.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

struct Sample {
    double ms{}, suspendUs{};
    CONTEXT context{};
    std::vector<unsigned char> stack;
    unsigned long long cpu{};
    IO_COUNTERS io{};
};
const Sample* current{};
bool Executable(HANDLE process, DWORD64 address) {
    MEMORY_BASIC_INFORMATION region{};
    return VirtualQueryEx(process, reinterpret_cast<const void*>(address), &region, sizeof(region)) &&
        (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
BOOL CALLBACK Read(HANDLE process, DWORD64 address, PVOID buffer, DWORD size, LPDWORD received) {
    if (current && address >= current->context.Esp &&
        address - current->context.Esp <= current->stack.size() &&
        size <= current->stack.size() - static_cast<std::size_t>(address - current->context.Esp)) {
        std::memcpy(buffer, current->stack.data() + static_cast<std::size_t>(address - current->context.Esp), size);
        if (received) *received = size; return TRUE;
    }
    // Unwinding runs after capture. Outside the retained stack, consult only
    // executable pages; live mutable stack/heap bytes describe a later frame.
    if (!Executable(process, address)) return FALSE;
    SIZE_T count{};
    const auto result = ReadProcessMemory(process, reinterpret_cast<const void*>(address), buffer, size, &count);
    if (received) *received = DWORD(count); return result;
}
unsigned long long Value(FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
bool Number(std::string_view text, unsigned& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4);
    if (argc != 5 && argc != 6) { std::cerr << "pid expected-exe output-json seconds [max-unwinds]\n"; return 2; }
    unsigned pid{}, seconds{}, maxUnwinds = 36000;
    if (!Number(argv[1], pid) || !pid || !Number(argv[4], seconds) || seconds < 1 || seconds > 60) return 2;
    if (argc == 6 && (!Number(argv[5], maxUnwinds) || maxUnwinds > 36000)) return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 3;
    std::array<char, 32768> exe{}; DWORD length = DWORD(exe.size());
    std::error_code pathError;
    if (!QueryFullProcessImageNameA(process, 0, exe.data(), &length) ||
        !std::filesystem::equivalent(exe.data(), argv[2], pathError) || pathError) return 4;
    BOOL debuggerAttached{};
    if (!CheckRemoteDebuggerPresent(process, &debuggerAttached)) return 12;
    // A debugger-created process may enable expensive heap checks. Record the
    // observation; do not equate these samples with ordinary-launch timings.
    USHORT machine{}, nativeMachine{};
    if (!IsWow64Process2(process, &machine, &nativeMachine) ||
        (machine != IMAGE_FILE_MACHINE_I386 &&
         !(machine == IMAGE_FILE_MACHINE_UNKNOWN && nativeMachine == IMAGE_FILE_MACHINE_I386))) return 13;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{sizeof(entry)};
    HANDLE thread{}; DWORD tid{}; unsigned long long earliest = ~0ull;
    for (BOOL found = Thread32First(snapshot, &entry); found; found = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE candidate = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (candidate && GetThreadTimes(candidate, &created, &exited, &kernel, &user) && Value(created) < earliest) {
            if (thread) CloseHandle(thread);
            thread = candidate; tid = entry.th32ThreadID; earliest = Value(created);
        } else if (candidate) CloseHandle(candidate);
    }
    CloseHandle(snapshot);
    if (!thread) return 5;
    // Resolve symbols only AFTER capture. DbgHelp can block and must never run
    // while the game thread is suspended. Snapshot stack bytes for later unwind.
    std::vector<Sample> samples;
    samples.reserve(seconds * 600);
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    unsigned failures{};
    FILETIME utc{}; GetSystemTimeAsFileTime(&utc);
    LARGE_INTEGER origin{}, frequency{}; QueryPerformanceCounter(&origin); QueryPerformanceFrequency(&frequency);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) return 9;
    std::cout << "CAPTURING pid=" << pid << " tid=" << tid << std::endl;
    while (Clock::now() - start < std::chrono::seconds(seconds)) {
        Sample sample; sample.context.ContextFlags = CONTEXT_FULL;
        sample.stack.resize(32768);
        const auto begin = Clock::now();
        if (SuspendThread(thread) == DWORD(-1)) { ++failures; break; }
        bool good = GetThreadContext(thread, &sample.context) != FALSE;
        SIZE_T count{};
        if (good) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQueryEx(process, reinterpret_cast<void*>(sample.context.Esp), &region, sizeof(region))) {
                const auto available = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize - sample.context.Esp;
                const auto size = std::min<std::size_t>(sample.stack.size(), available);
                ReadProcessMemory(process, reinterpret_cast<void*>(sample.context.Esp), sample.stack.data(), size, &count);
            }
        }
        // No allocation, symbols, disk output or exceptions inside suspension.
        const auto resumed = ResumeThread(thread);
        sample.suspendUs = std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
        if (resumed == DWORD(-1)) { std::cerr << "ResumeThread failed\n"; return 6; }
        sample.ms = std::chrono::duration<double, std::milli>(begin - start).count();
        FILETIME created{}, exited{}, kernel{}, user{};
        GetThreadTimes(thread, &created, &exited, &kernel, &user);
        sample.cpu = Value(kernel) + Value(user);
        GetProcessIoCounters(process, &sample.io);
        if (good && count) { sample.stack.resize(count); samples.push_back(std::move(sample)); }
        else ++failures;
        LARGE_INTEGER due{}; due.QuadPart = -20000;
        if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, 1000) != WAIT_OBJECT_0) return 10;
    }
    CloseHandle(timer);
    // Preserve contexts and exact stack bytes before calling DbgHelp.
    std::ofstream raw(std::string(argv[3]) + ".raw", std::ios::binary);
    auto save = [&](const auto& value) { raw.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    const uint32_t magic = 0x31505347, total = static_cast<uint32_t>(samples.size());
    save(magic); save(pid); save(tid); save(utc); save(origin); save(frequency); save(total);
    for (const auto& sample : samples) {
        save(sample.ms); save(sample.suspendUs); save(sample.context); save(sample.cpu); save(sample.io);
        const uint32_t size = static_cast<uint32_t>(sample.stack.size()); save(size);
        raw.write(reinterpret_cast<const char*>(sample.stack.data()), size);
    }
    raw.close(); if (!raw) return 11;
    std::cout << "CAPTURED " << samples.size() << "; resolving local symbols" << std::endl;
    const auto directory = std::filesystem::path(exe.data()).parent_path();
    const auto symbolPath = directory.string() + ";" + (directory / "cstrike/metahook/plugins").string();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitialize(process, symbolPath.c_str(), TRUE)) return 7;
    std::map<DWORD64, std::string> symbols;
    std::map<DWORD64, IMAGEHLP_MODULE64> modules;
    auto symbol = [&](DWORD64 address) -> const std::string& {
        auto [where, added] = symbols.try_emplace(address);
        if (added) {
            alignas(SYMBOL_INFO) std::array<char, sizeof(SYMBOL_INFO) + 2048> buffer{};
            auto* info = reinterpret_cast<SYMBOL_INFO*>(buffer.data()); info->SizeOfStruct = sizeof(SYMBOL_INFO); info->MaxNameLen = 2047;
            DWORD64 displacement{}; IMAGEHLP_MODULE64 module{}; module.SizeOfStruct = sizeof(module);
            const bool found = SymFromAddr(process, address, &displacement, info) != FALSE;
            SymGetModuleInfo64(process, address, &module);
            if (module.BaseOfImage) modules[module.BaseOfImage] = module;
            std::ostringstream text;
            text << module.ModuleName << '!';
            // Exports can name an unrelated function far before this PC.
            // Attribute callers only when DbgHelp loaded a matching PDB.
            if (found && !module.PdbUnmatched && (module.SymType == SymPdb || module.SymType == SymDia))
                text << info->Name << "+0x" << std::hex << displacement;
            else text << "0x" << std::hex << address - module.BaseOfImage;
            text << " [rva=0x" << std::hex << address - module.BaseOfImage << "]";
            where->second = text.str();
        }
        return where->second;
    };
    std::ofstream out(argv[3]);
    out << "{\"pid\":" << pid << ",\"tid\":" << tid << ",\"failures\":" << failures
        << ",\"debuggerAttached\":" << (debuggerAttached ? "true" : "false")
        << ",\"utc100ns\":" << Value(utc) << ",\"qpcStart\":" << origin.QuadPart << ",\"qpcFrequency\":" << frequency.QuadPart
        << ",\"maxUnwinds\":" << maxUnwinds
        << ",\"scope\":\"all main-thread leaf samples; bounded caller unwinds; CPU and process IO counters; not ETW or GPU\",\"samples\":[";
    bool first = true;
    const auto unwindCount = std::min<std::size_t>(maxUnwinds, samples.size());
    std::size_t sampleIndex{}, nextUnwind{};
    for (const auto& sample : samples) {
        current = &sample;
        const bool unwind = nextUnwind < unwindCount &&
            sampleIndex == nextUnwind * samples.size() / unwindCount;
        if (unwind) ++nextUnwind;
        ++sampleIndex;
        if (!first) out << ','; first = false;
        out << "{\"ms\":" << sample.ms << ",\"suspendUs\":" << sample.suspendUs << ",\"cpu100ns\":" << sample.cpu
            << ",\"readBytes\":" << sample.io.ReadTransferCount << ",\"reads\":" << sample.io.ReadOperationCount
            << ",\"callersSampled\":" << (unwind ? "true" : "false") << ",\"stack\":[";
        CONTEXT context = sample.context;
        STACKFRAME64 frame{};
        frame.AddrPC = {context.Eip, 0, AddrModeFlat}; frame.AddrStack = {context.Esp, 0, AddrModeFlat}; frame.AddrFrame = {context.Ebp, 0, AddrModeFlat};
        out << std::quoted(symbol(context.Eip));
        DWORD64 lastPc{}, lastSp{};
        for (unsigned n = 0; unwind && n < 48; ++n) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &context, Read,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !frame.AddrPC.Offset ||
                (frame.AddrPC.Offset == lastPc && frame.AddrStack.Offset == lastSp)) break;
            lastPc = frame.AddrPC.Offset; lastSp = frame.AddrStack.Offset;
            if (n == 0 && lastPc == sample.context.Eip) continue;
            if (!Executable(process, lastPc)) break;
            out << ',' << std::quoted(symbol(frame.AddrPC.Offset));
        }
        out << "]}";
    }
    out << "],\"modules\":[";
    first = true;
    for (const auto& [base, module] : modules) {
        if (!first) out << ','; first = false;
        out << "{\"name\":" << std::quoted(module.ModuleName)
            << ",\"image\":" << std::quoted(module.LoadedImageName)
            << ",\"base\":" << base << ",\"size\":" << module.ImageSize
            << ",\"timestamp\":" << module.TimeDateStamp
            << ",\"symbolType\":" << unsigned(module.SymType)
            << ",\"pdb\":" << std::quoted(module.LoadedPdbName)
            << ",\"pdbAge\":" << module.PdbAge
            << ",\"pdbUnmatched\":" << (module.PdbUnmatched ? "true" : "false") << '}';
    }
    out << "]}";
    SymCleanup(process); CloseHandle(thread); CloseHandle(process);
    std::cout << "WROTE " << argv[3] << std::endl;
    return out ? 0 : 8;
}
