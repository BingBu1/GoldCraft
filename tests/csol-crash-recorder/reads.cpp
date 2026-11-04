// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real crash-path memory reader with inaccessible/corrupt pages.
#include "../../native/csol/client/crash_recorder.cpp"
#include <iostream>

int main()
{
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    const auto page = info.dwPageSize;
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    memory[page - 1] = 73;
    DWORD previous{};
    if (!VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous)) return 2;
    int checks{}, failed{};
    const auto check = [&](bool result) { ++checks; if (!result) ++failed; };
    unsigned char bytes[8]{};
    using csol::client::ReadMemory;
    const auto address = reinterpret_cast<std::uintptr_t>(memory);
    check(ReadMemory(address + page - 1, bytes, 1) && bytes[0] == 73);
    check(!ReadMemory(address + page - 1, bytes, 2));
    check(!ReadMemory(address + page, bytes, sizeof(bytes)));
    check(!ReadMemory(0, bytes, 1));
    check(!ReadMemory(0x10, bytes, 1));
    check(!ReadMemory(UINTPTR_MAX - 2, bytes, 8));
    check(!csol::client::AfterCall(2));
    check(!csol::client::AfterCall(address + page + 7));
    VirtualFree(memory, 0, MEM_RELEASE);
    check(!ReadMemory(address, bytes, 1));
    std::cout << "crash reader checks=" << checks << " failures=" << failed << '\n';
    return failed ? 1 : 0;
}
