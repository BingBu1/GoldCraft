// SPDX-License-Identifier: GPL-3.0-or-later
// Production sender, real SDK ABI; engine calls are captured, not networked.
#include "precompiled.h"
#include "../../native/csol/server/scenario_messages.h"
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace
{
int checks{}, failures{}, messageId = 96;
std::vector<int> calls;
std::vector<unsigned char> bytes;
std::array<float, 3> capturedOrigin{};
std::function<void()> onBegin, onEnd;
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
int Register(const char*, int) { return messageId; }
void Begin(int destination, int id, const float* origin, edict_t* recipient)
{
    calls.push_back(1);
    Check(destination == MSG_PVS && id == messageId && !recipient && origin,
        "healing uses registered MPToCL PVS without a recipient");
    if (origin) std::copy_n(origin, 3, capturedOrigin.begin());
    const auto action = onBegin; if (action) action();
}
void Byte(int value) { calls.push_back(2); bytes.push_back(static_cast<unsigned char>(value)); }
void Short(int value)
{
    calls.push_back(3);
    Check(value >= -32768 && value <= 32767, "entity is passed as signed short retaining the ushort bits");
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(static_cast<unsigned>(value) >> 8));
}
void End() { calls.push_back(4); const auto action = onEnd; if (action) action(); }
void Clear() { calls.clear(); bytes.clear(); capturedOrigin = {}; }
void Install()
{
    g_engfuncs = {};
    g_engfuncs.pfnRegUserMsg = Register;
    g_engfuncs.pfnMessageBegin = Begin;
    g_engfuncs.pfnMessageEnd = End;
    g_engfuncs.pfnWriteByte = Byte;
    g_engfuncs.pfnWriteShort = Short;
    onBegin = {}; onEnd = {}; messageId = 96; Clear();
}
bool Packet(int index, bool self)
{
    return calls == std::vector<int>({1, 2, 3, 2, 4}) &&
        bytes == std::vector<unsigned char>({4, static_cast<unsigned char>(index),
            static_cast<unsigned char>(index >> 8), static_cast<unsigned char>(self ? 13 : 9)});
}
void Payload()
{
    Install(); CCSOLScenarioMessages messages;
    Check(messages.Register(), "message registration succeeds");
    const float origin[]{12.5f, -23.0f, 42.75f};
    for (int index : {1, 255, 256, 32767, 32768, 65535})
        for (bool self : {false, true})
        {
            Clear();
            Check(messages.SendMonsterHealing(index, origin, self) && Packet(index, self),
                "MPToCL 4/ushort/effect 9 or 13 has exact byte order");
            Check(capturedOrigin == std::array<float, 3>{12.5f, -23, 42.75f},
                "PVS origin is the healed target position");
        }
    messageId = 255; Check(messages.Register(), "message ID does not equal opcode");
    Clear(); Check(messages.SendMonsterHealing(45, origin, false) && Packet(45, false),
        "highest user message ID keeps exact payload");
}
void Invalid()
{
    Install(); CCSOLScenarioMessages messages; float origin[]{1, 2, 3};
    Check(!messages.SendMonsterHealing(1, origin, false) && calls.empty(), "unregistered sender is inert");
    Check(messages.Register(), "invalid fixture registers");
    for (int index : {-1, 0, 65536, std::numeric_limits<int>::max()})
    {
        Clear(); Check(!messages.SendMonsterHealing(index, origin, false) && calls.empty(),
            "out-of-range entity cannot open a partial message");
    }
    Clear(); Check(!messages.SendMonsterHealing(1, nullptr, false) && calls.empty(), "null origin is rejected");
    for (int axis = 0; axis < 3; ++axis)
        for (float value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            origin[axis] = value; Clear();
            Check(!messages.SendMonsterHealing(1, origin, false) && calls.empty(), "nonfinite PVS axis is rejected");
            origin[axis] = float(axis + 1);
        }
    const auto complete = g_engfuncs;
    for (int missing = 0; missing < 4; ++missing)
    {
        g_engfuncs = complete;
        if (missing == 0) g_engfuncs.pfnMessageBegin = nullptr;
        if (missing == 1) g_engfuncs.pfnMessageEnd = nullptr;
        if (missing == 2) g_engfuncs.pfnWriteByte = nullptr;
        if (missing == 3) g_engfuncs.pfnWriteShort = nullptr;
        Clear(); Check(!messages.SendMonsterHealing(1, origin, true) && calls.empty(),
            "all required engine functions are checked before Begin");
    }
    g_engfuncs = complete;
}
void Reentry()
{
    Install(); CCSOLScenarioMessages messages; Check(messages.Register(), "reentry fixture registers");
    float origin[]{1, 2, 3};
    onBegin = [&]
    {
        const auto size = calls.size();
        Check(!messages.SendMonsterHealing(2, origin, false) && calls.size() == size,
            "Begin callback cannot nest another message");
        origin[0] = 99;
    };
    onEnd = [&]
    {
        const auto size = calls.size();
        Check(!messages.SendMonsterHealing(3, origin, true) && calls.size() == size,
            "End callback remains inside the message guard");
    };
    Check(messages.SendMonsterHealing(1, origin, true) && Packet(1, true),
        "reentry rejection does not truncate outer message");
    Check(capturedOrigin == std::array<float, 3>{1, 2, 3}, "caller mutation cannot change captured PVS position");
    onBegin = {}; onEnd = {}; Clear();
    Check(messages.SendMonsterHealing(2, origin, false) && Packet(2, false), "guard clears after complete packet");
}
}
int main()
{
    Payload(); Invalid(); Reentry();
    std::cout << "monster-healing-message " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
