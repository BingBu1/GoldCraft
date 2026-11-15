// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/standalone_input.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "original_input.inc" // Generated privately from the verified original.

namespace
{
using csol::scenario::custom::StandaloneSprintEdge;
unsigned checks{}, cases{}, originalCalls{}, nativeCalls{};
const char *originalCommand{}, *nativeCommand{};
struct OriginalState { unsigned vtable{}; int state{}; } original;
StandaloneSprintEdge native;
void Check(bool condition, const char* why)
{ ++checks; if (!condition) { std::printf("FAIL %s\n", why); std::exit(1); } }
int OriginalCommand(const char* name)
{
    ++originalCalls; originalCommand = name;
    Check(original.state == (!std::strcmp(name, "sprintON") ? 1 : 0), "original stores its state before callback");
    return 0;
}
int (*route)(const char*) = OriginalCommand;
void NativeCommand(const char* name)
{
    ++nativeCalls; nativeCommand = name;
    Check(native.State() == (!std::strcmp(name, "sprintON") ? 1 : 0), "production stores its state before callback");
}
}
int main()
{
    using Original = bool (__thiscall*)(OriginalState*, int);
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(OriginalInput), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(code != nullptr, "private original window allocated");
    std::memcpy(code, OriginalInput, sizeof(OriginalInput));
    const std::array<const void*, 3> bindings{&route, "sprintON", "sprintOFF"};
    for (const auto fixup : OriginalInputFixups)
    {
        Check(fixup.offset + sizeof(void*) <= sizeof(OriginalInput) && fixup.binding < bindings.size(), "original absolute operand is bounded");
        std::memcpy(code + fixup.offset, &bindings[fixup.binding], sizeof(void*));
    }
    DWORD old{};
    Check(VirtualProtect(code, sizeof(OriginalInput), PAGE_EXECUTE_READ, &old) != 0 &&
        FlushInstructionCache(GetCurrentProcess(), code, sizeof(OriginalInput)), "original code has final read/execute permissions");
    const auto oracle = reinterpret_cast<Original>(code);
    constexpr std::array values{0, 1, -1, 2, -7, 1000, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    // Exhaustive5-event sequences include repeated states, invalid dispatcher
    // values and transitions back from them. Compare the original full body.
    for (unsigned sequence = 0; sequence != 32768; ++sequence)
    {
        original = {}; native.Reset(); unsigned remaining = sequence;
        for (unsigned step = 0; step != 5; ++step)
        {
            const int value = values[remaining & 7]; remaining >>= 3;
            originalCalls = nativeCalls = 0; originalCommand = nativeCommand = nullptr;
            const bool expected = oracle(&original, value);
            const bool actual = native.Dispatch(value, NativeCommand);
            Check(expected == actual, "original and production dispatch return match");
            Check(original.state == native.State(), "original and production integer state match");
            Check(originalCalls == nativeCalls, "original and production command count match");
            Check((!originalCommand && !nativeCommand) ||
                (originalCommand && nativeCommand && !std::strcmp(originalCommand, nativeCommand)), "original and production command identity match");
            ++cases;
        }
    }
    Check(VirtualFree(code, 0, MEM_RELEASE) != 0, "private oracle released");
    std::printf("CSOL original25-instruction input: %u transitions, %u checks, zero differences\n", cases, checks);
}
