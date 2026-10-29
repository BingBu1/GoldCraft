// SPDX-License-Identifier: GPL-3.0-or-later
// Link with the production damage_numbers.cpp and the matching ReGameDLL SDK.
// This fixture checks engine writes, not real damage-producer acceptance.
#include "precompiled.h"
#include "../../native/csol/server/damage_numbers.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

enginefuncs_t g_engfuncs{};
namespace
{
std::vector<std::uint8_t> bytes;
std::vector<char> calls;
int begins{}, ends{}, destination{}, id{};
edict_t* receiver{};
bool nested{};
void Check(bool condition) { if (!condition) throw std::runtime_error("damage-number server boundary"); }
void Begin(int dest, int message, const float* origin, edict_t* recipient)
{
    ++begins; destination = dest; id = message; receiver = recipient;
    Check(!origin);
    if (nested) Check(!CSOLWriteDamageNumber(message, recipient, {9, 0, 0, 999}));
}
void End() { ++ends; }
void Byte(int value) { calls.push_back('b'); bytes.push_back(std::uint8_t(value)); }
void Short(int value)
{
    calls.push_back('s');
    const auto word = static_cast<std::uint16_t>(value);
    bytes.push_back(std::uint8_t(word)); bytes.push_back(std::uint8_t(word >> 8));
}
void Long(int value)
{
    calls.push_back('l');
    const auto word = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(std::uint8_t(word >> shift));
}
}
int main()
{
    try
    {
        g_engfuncs.pfnMessageBegin = Begin;
        g_engfuncs.pfnMessageEnd = End;
        g_engfuncs.pfnWriteByte = Byte;
        g_engfuncs.pfnWriteShort = Short;
        g_engfuncs.pfnWriteLong = Long;
        edict_t attacker{}; attacker.v.flags = FL_CLIENT;
        nested = true;
        Check(CSOLWriteDamageNumber(90, &attacker, {9, 4, 3, 335}));
        Check(begins == 1 && ends == 1 && destination == MSG_ONE_UNRELIABLE &&
            id == 90 && receiver == &attacker);
        Check(bytes == std::vector<std::uint8_t>{206, 9, 0, 4, 3, 79, 1, 0, 0});
        Check(calls == std::vector<char>{'b', 's', 'b', 'b', 'l'});
        Check(!CSOLWriteDamageNumber(63, &attacker, {9, 0, 0, 30}));
        Check(!CSOLWriteDamageNumber(256, &attacker, {9, 0, 0, 30}));
        Check(!CSOLWriteDamageNumber(90, nullptr, {9, 0, 0, 30}));
        Check(!CSOLWriteDamageNumber(90, &attacker, {0, 0, 0, 30}));
        attacker.free = TRUE;
        Check(!CSOLWriteDamageNumber(90, &attacker, {9, 0, 0, 30}));
        attacker.free = FALSE; attacker.v.flags = 0;
        Check(!CSOLWriteDamageNumber(90, &attacker, {9, 0, 0, 30}));
        attacker.v.flags = FL_CLIENT;
        g_engfuncs.pfnWriteLong = nullptr;
        Check(!CSOLWriteDamageNumber(90, &attacker, {9, 0, 0, 30}));
        Check(begins == 1 && ends == 1);
        std::cout << "engine boundary checks passed; producer/gameplay not exercised\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
