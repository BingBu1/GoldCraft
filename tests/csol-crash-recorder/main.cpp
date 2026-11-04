// SPDX-License-Identifier: GPL-3.0-or-later
// Production crash recorder, exercised with real unhandled faults in child
// processes: the record is written, the exception still reaches the existing
// top-level filter, the environment switch disables it, and two records remain.
#include "../../native/csol/client/crash_recorder.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
unsigned checks{}, failures{};
void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

__declspec(noinline) int Compare(const char* first, const char* second)
{
    // Mirrors the engine fault: the second argument is non-null but invalid.
    return first[0] - second[0];
}

LONG WINAPI Existing(EXCEPTION_POINTERS*)
{
    // Stands in for the game's own crash handling, which must still run.
    ExitProcess(7);
}

std::vector<std::wstring> Records(const std::wstring& directory, const wchar_t* pattern)
{
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((directory + L"\\" + pattern).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return names;
    do if (found.nFileSizeLow || found.nFileSizeHigh || std::wcsstr(found.cFileName, L"2000")) names.push_back(found.cFileName);
    while (FindNextFileW(search, &found));
    FindClose(search);
    return names;
}

std::string Read(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text; text << file.rdbuf(); return text.str();
}

DWORD Child(const wchar_t* self, const std::wstring& directory, bool disabled)
{
    std::wstring command = L"\"" + std::wstring(self) + L"\" \"" + directory + L"\" child";
    SetEnvironmentVariableW(L"CSOL_CRASH_RECORDER", disabled ? L"0" : nullptr);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 0;
    WaitForSingleObject(process.hProcess, 60000);
    DWORD code{};
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    SetEnvironmentVariableW(L"CSOL_CRASH_RECORDER", nullptr);
    Sleep(1100); // Distinct second-resolution record names.
    return code;
}
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) return 2;
    const std::wstring directory = argv[1];
    if (argc > 2 && !std::wcscmp(argv[2], L"child"))
    {
        SetUnhandledExceptionFilter(Existing);
        csol::client::InstallCrashRecorderFor({GetModuleHandleW(nullptr)}, directory.c_str());
        return Compare("weapon", reinterpret_cast<const char*>(0x10)); // unhandled
    }
    CreateDirectoryW(directory.c_str(), nullptr);
    for (const wchar_t* old : {L"csol-crash-20000101-000000-1", L"csol-crash-20000102-000000-1"})
        for (const wchar_t* extension : {L".log", L".dmp"})
            CloseHandle(CreateFileW((directory + L"\\" + old + extension).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));

    Check(Child(argv[0], directory, false) == 7, "fault still reaches the existing top-level handler");
    auto logs = Records(directory, L"csol-crash-*.log");
    Check(logs.size() == 2 && Records(directory, L"csol-crash-*.dmp").size() == 2, "new record plus newest older record");
    std::wstring first;
    for (const auto& name : logs) if (name.find(L"2000") == std::wstring::npos) first = name;
    const auto text = Read(directory + L"\\" + first);
    Check(text.find("first-chance access violation") != std::string::npos, "log header");
    Check(text.find("access read 00000010") != std::string::npos, "fault address and access type");
    Check(text.find(".exe+") != std::string::npos, "fault module offset");
    Check(text.find("eip ") != std::string::npos && text.find("ebp chain:") != std::string::npos, "registers and frame chain");
    Check(text.find("stack return candidates:") != std::string::npos, "raw stack scan");
    std::wstring dump = first; dump.replace(dump.size() - 4, 4, L".dmp");
    Check(Read(directory + L"\\" + dump).size() > 1024, "minidump written");

    Check(Child(argv[0], directory, true) == 7, "disabled recorder leaves the crash path unchanged");
    Check(Records(directory, L"csol-crash-*.log").size() == 2, "CSOL_CRASH_RECORDER=0 writes nothing");

    Check(Child(argv[0], directory, false) == 7, "second crashing process");
    logs = Records(directory, L"csol-crash-*.log");
    bool old = false;
    for (const auto& name : logs) old |= name.find(L"2000") != std::wstring::npos;
    Check(logs.size() == 2 && !old, "only the two newest records are kept");
    std::printf("crash recorder: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
