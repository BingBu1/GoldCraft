// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_recorder.hpp"
#include <dbghelp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace csol::client {
namespace {
using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
constexpr std::size_t MaxWatched = 4, KeepRecords = 2;
// hw.dll (sandbox 10210, 9ba9a2db...) Q_strcasecmp; 1BB099 reads its second argument.
constexpr std::uintptr_t CompareBegin = 0x1BB070, CompareEnd = 0x1BB0E0;
std::array<std::array<wchar_t, 64>, MaxWatched> watched{};
std::size_t watchedCount{};
wchar_t directory[MAX_PATH]{};
PVOID registration{};
HMODULE dbghelp{};
WriteDumpFn writeDump{};
std::atomic<bool> recorded{};

class Log
{
public:
    explicit Log(HANDLE file) : m_file(file) {}
#if defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    void Line(const char* format, ...)
    {
        char buffer[512];
        va_list arguments;
        va_start(arguments, format);
        const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        if (length <= 0) return;
        DWORD written{};
        WriteFile(m_file, buffer, DWORD(std::min<int>(length, int(sizeof(buffer)) - 1)), &written, nullptr);
        WriteFile(m_file, "\r\n", 2, &written, nullptr);
    }
private:
    HANDLE m_file;
};

bool ReadMemory(std::uintptr_t address, void* destination, std::size_t size)
{
    // The crashing thread may hold a corrupt pointer, and other threads can
    // unmap a page after VirtualQuery. Never fault again while reading the
    // original exception's stack or strings.
    if (!address || size > UINTPTR_MAX - address) return false;
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        destination, size, &copied) && copied == size;
}

// Module base name and offset for an address; false outside loaded images.
bool Locate(std::uintptr_t address, wchar_t (&name)[64], std::uintptr_t& offset)
{
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) return false;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return false;
    const wchar_t* base = std::wcsrchr(path, L'\\');
    std::wcsncpy(name, base ? base + 1 : path, 63);
    name[63] = 0;
    offset = address - reinterpret_cast<std::uintptr_t>(module);
    return true;
}

bool Watched(const wchar_t* name)
{
    for (std::size_t i = 0; i < watchedCount; ++i)
        if (!_wcsicmp(watched[i].data(), name)) return true;
    return false;
}

// A plausible return address follows a CALL: E8 rel32, FF /2 with modrm.
bool AfterCall(std::uintptr_t address)
{
    std::array<std::uint8_t, 7> copy{};
    if (address < copy.size() || !ReadMemory(address - copy.size(), copy.data(), copy.size())) return false;
    const auto* bytes = copy.data() + copy.size();
    if (bytes[-5] == 0xE8) return true;
    for (int length : {2, 3, 6, 7})
        if (bytes[-length] == 0xFF && ((bytes[-length + 1] >> 3) & 7) == 2) return true;
    return false;
}

void Where(Log& log, const char* label, std::uintptr_t address)
{
    wchar_t name[64]{};
    std::uintptr_t offset{};
    if (Locate(address, name, offset)) log.Line("%s %08IX %ls+%IX", label, address, name, offset);
    else log.Line("%s %08IX (no module)", label, address);
}

void Text(Log& log, const char* label, std::uintptr_t address)
{
    char copy[65]{};
    std::size_t i = 0;
    for (; i < 64; ++i)
    {
        char c{};
        if (i > UINTPTR_MAX - address || !ReadMemory(address + i, &c, 1))
        {
            if (!i) { log.Line("%s %08IX unreadable", label, address); return; }
            break;
        }
        if (!c) break;
        copy[i] = c >= 32 && c < 127 ? c : '?';
    }
    log.Line("%s %08IX \"%s\"", label, address, copy);
}

// Keep the newest records only; names sort by their timestamp.
void Prune()
{
    wchar_t pattern[MAX_PATH]{};
    std::swprintf(pattern, MAX_PATH, L"%ls\\csol-crash-*.log", directory);
    std::array<std::array<wchar_t, 96>, 64> names{};
    std::size_t count{};
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do if (count < names.size()) std::wcsncpy(names[count++].data(), found.cFileName, 95);
    while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(names.begin(), names.begin() + count,
        [](const auto& a, const auto& b) { return std::wcscmp(a.data(), b.data()) < 0; });
    for (std::size_t i = 0; i + (KeepRecords - 1) < count; ++i)
    {
        wchar_t path[MAX_PATH]{};
        std::swprintf(path, MAX_PATH, L"%ls\\%ls", directory, names[i].data());
        DeleteFileW(path);
        if (auto* dot = std::wcsrchr(path, L'.')) { std::wcscpy(dot, L".dmp"); DeleteFileW(path); }
    }
}

void Record(EXCEPTION_POINTERS* pointers, const wchar_t* module, std::uintptr_t offset)
{
    Prune();
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stem[MAX_PATH]{};
    std::swprintf(stem, MAX_PATH, L"%ls\\csol-crash-%04u%02u%02u-%02u%02u%02u-%lu", directory, now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    wchar_t path[MAX_PATH]{};
    std::swprintf(path, MAX_PATH, L"%ls.log", stem);
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    Log log(file);
    const auto& record = *pointers->ExceptionRecord;
    const auto& context = *pointers->ContextRecord;
    log.Line("CSOLSupport crash record (first-chance access violation; may still be handled)");
    log.Line("pid %lu tid %lu", GetCurrentProcessId(), GetCurrentThreadId());
    log.Line("fault %08IX %ls+%IX", reinterpret_cast<std::uintptr_t>(record.ExceptionAddress), module, offset);
    if (record.NumberParameters >= 2)
        log.Line("access %s %08IX", record.ExceptionInformation[0] == 1 ? "write" :
            record.ExceptionInformation[0] == 8 ? "execute" : "read", static_cast<std::uintptr_t>(record.ExceptionInformation[1]));
    log.Line("eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX", context.Eax, context.Ebx, context.Ecx,
        context.Edx, context.Esi, context.Edi);
    log.Line("eip %08lX esp %08lX ebp %08lX efl %08lX", context.Eip, context.Esp, context.Ebp, context.EFlags);
    std::array<std::uintptr_t, 4> arguments{};
    if (!_wcsicmp(module, L"hw.dll") && offset >= CompareBegin && offset < CompareEnd &&
        ReadMemory(context.Ebp, arguments.data(), sizeof(arguments)))
    {
        // The comparison keeps an EBP frame; 1BB099 precedes the [ebp+8] update.
        log.Line("hw Q_strcasecmp(first, second):");
        Text(log, "  first ", arguments[2]);
        Text(log, "  second", arguments[3]);
        Where(log, "  caller", arguments[1]);
    }
    log.Line("ebp chain:");
    std::uintptr_t frame = context.Ebp;
    for (int depth = 0; depth < 48; ++depth)
    {
        std::array<std::uintptr_t, 2> slot{};
        if (!ReadMemory(frame, slot.data(), sizeof(slot))) break;
        Where(log, "  ret", slot[1]);
        if (slot[0] <= frame) break;
        frame = slot[0];
    }
    log.Line("stack return candidates:");
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto top = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    int candidates{};
    for (std::uintptr_t at = context.Esp & ~std::uintptr_t(3); at + 4 <= top && at < context.Esp + 16384 && candidates < 96; at += 4)
    {
        std::uintptr_t value{};
        if (!ReadMemory(at, &value, sizeof(value))) break;
        wchar_t name[64]{};
        std::uintptr_t into{};
        if (!Locate(value, name, into) || !AfterCall(value)) continue;
        log.Line("  [esp+%04IX] %08IX %ls+%IX", static_cast<std::uintptr_t>(at - context.Esp), value, name, into);
        ++candidates;
    }
    CloseHandle(file);
    if (!writeDump) return;
    std::swprintf(path, MAX_PATH, L"%ls.dmp", stem);
    const HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), pointers, FALSE};
    writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
        MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory), &exception, nullptr, nullptr);
    CloseHandle(dump);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* pointers)
{
    if (!pointers || !pointers->ExceptionRecord ||
        pointers->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    wchar_t name[64]{};
    std::uintptr_t offset{};
    if (!Locate(reinterpret_cast<std::uintptr_t>(pointers->ExceptionRecord->ExceptionAddress), name, offset) ||
        !Watched(name) || recorded.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    Record(pointers, name, offset);
    return EXCEPTION_CONTINUE_SEARCH;
}

void Install()
{
    if (registration) return;
    wchar_t flag[8]{};
    if (GetEnvironmentVariableW(L"CSOL_CRASH_RECORDER", flag, 8) && !std::wcscmp(flag, L"0")) return;
    CreateDirectoryW(directory, nullptr);
    // Load before any fault: LoadLibrary is unsafe inside an exception path.
    if (!dbghelp) dbghelp = LoadLibraryW(L"dbghelp.dll");
    writeDump = dbghelp ? reinterpret_cast<WriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    recorded = false;
    registration = AddVectoredExceptionHandler(0, Handler);
}
}

void InstallCrashRecorderFor(std::initializer_list<HMODULE> modules, const wchar_t* path)
{
    watchedCount = 0;
    for (const auto module : modules)
    {
        wchar_t file[MAX_PATH]{};
        if (!module || watchedCount == MaxWatched || !GetModuleFileNameW(module, file, MAX_PATH)) continue;
        const wchar_t* base = std::wcsrchr(file, L'\\');
        std::wcsncpy(watched[watchedCount++].data(), base ? base + 1 : file, 63);
    }
    std::wcsncpy(directory, path, MAX_PATH - 1);
    Install();
}

void InstallCrashRecorder()
{
    // Names, not handles: client.dll loads after this plugin initializes.
    watchedCount = 0;
    for (const auto* name : {L"hw.dll", L"client.dll"}) std::wcsncpy(watched[watchedCount++].data(), name, 63);
    HMODULE self{};
    wchar_t file[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&InstallCrashRecorder), &self) && GetModuleFileNameW(self, file, MAX_PATH))
    {
        const wchar_t* base = std::wcsrchr(file, L'\\');
        std::wcsncpy(watched[watchedCount++].data(), base ? base + 1 : file, 63);
    }
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    if (auto* slash = std::wcsrchr(exe, L'\\')) *slash = 0;
    std::swprintf(directory, MAX_PATH, L"%ls\\csol-crash", exe);
    Install();
}

void RemoveCrashRecorder()
{
    if (registration) RemoveVectoredExceptionHandler(registration);
    registration = nullptr;
}
}
