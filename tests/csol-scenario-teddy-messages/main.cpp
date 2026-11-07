// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/scenario/core/scenario_messages.h"
#include "native/csol/server/damage_numbers.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
int gmsgShake{111};

// This executable covers three effect packets, never floating damage. Fail
// explicitly if a future change accidentally reaches that unrelated boundary.
bool CSOLWriteDamageNumber(int, edict_t*, CSOLDamageNumber)
{
    throw std::runtime_error("unexpected floating-damage sender");
}

namespace
{
int checks{}, begins{}, ends{}, nextId{96}, destination{}, message{};
edict_t* recipient{};
bool hasOrigin{};
std::array<float, 3> pvs{};
std::vector<unsigned char> bytes;
std::vector<float> coordinates;
std::map<std::string, int> registrations;
std::string failedRegistration;
std::function<void()> onBegin, onEnd;

void Check(bool condition, const std::source_location at = std::source_location::current())
{
    ++checks;
    if (!condition) throw std::runtime_error("teddy messages line " + std::to_string(at.line()));
}
int Register(const char* name, int)
{
    if (failedRegistration == name) return 0;
    return registrations[name] = nextId++;
}
void Begin(int target, int id, const float* origin, edict_t* player)
{
    ++begins; destination = target; message = id; recipient = player; hasOrigin = origin != nullptr;
    if (origin) std::copy_n(origin, 3, pvs.begin());
    const auto action = onBegin;
    if (action) action();
}
void Byte(int value) { bytes.push_back(static_cast<unsigned char>(value)); }
void Short(int value) { Byte(value); Byte(static_cast<unsigned>(value) >> 8); }
void Coord(float value) { coordinates.push_back(value); Short(static_cast<int>(value * 8)); }
void End() { ++ends; const auto action = onEnd; if (action) action(); }
void Clear()
{
    begins = ends = destination = message = 0; recipient = nullptr; hasOrigin = false;
    pvs = {}; bytes.clear(); coordinates.clear();
}
void Install()
{
    g_engfuncs = {}; g_engfuncs.pfnRegUserMsg = Register;
    g_engfuncs.pfnMessageBegin = Begin; g_engfuncs.pfnMessageEnd = End;
    g_engfuncs.pfnWriteByte = Byte; g_engfuncs.pfnWriteShort = Short; g_engfuncs.pfnWriteCoord = Coord;
    gmsgShake = 111; nextId = 96; registrations.clear(); failedRegistration.clear();
    onBegin = {}; onEnd = {}; Clear();
}
void Payloads()
{
    Install(); CCSOLScenarioMessages messages; edict_t player{};
    Check(!messages.SendBloodScreen(&player) && begins == 0);
    Check(messages.Register());
    for (int first : {64, 96, 244})
    {
        Clear(); nextId = first; Check(messages.Register());
        Check(messages.SendBloodScreen(&player));
        Check(begins == 1 && ends == 1 && destination == MSG_ONE && recipient == &player && !hasOrigin);
        Check(message == first && bytes == std::vector<unsigned char>{50});
    }
    for (int id : {64, 111, 255})
    {
        Clear(); gmsgShake = id; Check(messages.SendTeddyTentacleShake(&player));
        Check(begins == 1 && ends == 1 && destination == MSG_ONE && recipient == &player && !hasOrigin);
        Check(message == id && bytes == std::vector<unsigned char>{0, 64, 0, 16, 0, 3});
    }
    for (int count : {3, 4})
    {
        Clear(); const float position[]{-12.25f, 100.5f, 43.125f};
        Check(messages.SendTeddyTentacleDebris(position, 123, count));
        Check(begins == 1 && ends == 1 && destination == MSG_PVS && message == SVC_TEMPENTITY);
        Check(!recipient && hasOrigin && pvs == std::array<float, 3>{-12.25f, 100.5f, 43.125f});
        Check(coordinates == std::vector<float>{-12.25f, 100.5f, 43.125f, 6, 6, 2, 5, 5, 40});
        Check(bytes == std::vector<unsigned char>{108, 158, 255, 36, 3, 89, 1, 48, 0, 48, 0, 16, 0,
            40, 0, 40, 0, 64, 1, 5, 123, 0, static_cast<unsigned char>(count), 3, 64});
    }
}
void Invalid()
{
    Install(); CCSOLScenarioMessages messages; edict_t player{}; float position[]{1, 2, 3};
    Check(messages.Register());
    Check(!messages.SendBloodScreen(nullptr) && !messages.SendTeddyTentacleShake(nullptr));
    player.free = 1;
    Check(!messages.SendBloodScreen(&player) && !messages.SendTeddyTentacleShake(&player)); player.free = 0;
    for (int id : {-1, 0, 63, 256}) { gmsgShake = id; Check(!messages.SendTeddyTentacleShake(&player)); }
    gmsgShake = 111;
    Check(!messages.SendTeddyTentacleDebris(nullptr, 1, 3));
    for (int model : {-1, 0, 32768}) Check(!messages.SendTeddyTentacleDebris(position, model, 3));
    for (int count : {-1, 0, 2, 5, 255}) Check(!messages.SendTeddyTentacleDebris(position, 1, count));
    for (int axis = 0; axis != 3; ++axis)
    {
        for (float value : {-4096.125f, 4096.f, std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            position[axis] = value; Check(!messages.SendTeddyTentacleDebris(position, 1, 3));
        }
        position[axis] = float(axis + 1);
    }
    Check(begins == 0 && ends == 0);
    for (float edge : {-4096.f, 4095.875f})
    {
        const float p[]{edge, edge, edge}; Clear();
        Check(messages.SendTeddyTentacleDebris(p, 32767, 4) && begins == 1 && ends == 1);
    }
    const auto complete = g_engfuncs;
    for (int missing = 0; missing != 5; ++missing)
    {
        Clear(); g_engfuncs = complete;
        if (missing == 0) g_engfuncs.pfnMessageBegin = nullptr;
        if (missing == 1) g_engfuncs.pfnMessageEnd = nullptr;
        if (missing == 2) g_engfuncs.pfnWriteByte = nullptr;
        if (missing == 3) g_engfuncs.pfnWriteShort = nullptr;
        if (missing == 4) g_engfuncs.pfnWriteCoord = nullptr;
        if (missing < 3) Check(!messages.SendBloodScreen(&player));
        if (missing < 2 || missing == 3) Check(!messages.SendTeddyTentacleShake(&player));
        Check(!messages.SendTeddyTentacleDebris(position, 1, 3) && begins == 0 && ends == 0);
    }
    g_engfuncs = complete;
    for (const char* name : {"MPToCL", "ZBSVer2", "ValueSync"})
    {
        failedRegistration = name; Check(!messages.Register());
        Check(!messages.SendBloodScreen(&player) && begins == 0);
    }
    failedRegistration.clear(); nextId = 96; Check(messages.Register());
    Clear(); Check(messages.SendBloodScreen(&player) && begins == 1 && ends == 1);
}
void Reentry()
{
    for (int effect = 0; effect != 3; ++effect)
    {
        Install(); auto owner = std::make_unique<CCSOLScenarioMessages>(); CCSOLScenarioMessages other;
        Check(owner->Register()); const int bloodId = registrations.at("MPToCL");
        Check(other.Register()); edict_t player{}; float position[]{1, 2, 3};
        const auto reject = [&]
        {
            const auto before = begins;
            Check(!other.SendBloodScreen(&player)); Check(!other.SendTeddyTentacleShake(&player));
            Check(!other.SendTeddyTentacleDebris(position, 1, 3)); Check(begins == before);
        };
        const int originalId = effect == 0 ? bloodId : gmsgShake;
        onBegin = [&] { reject(); owner.reset(); position[0] = 999; player.free = 1; gmsgShake = 200; };
        onEnd = reject;
        auto* writer = owner.get();
        const bool result = effect == 0 ? writer->SendBloodScreen(&player) :
            effect == 1 ? writer->SendTeddyTentacleShake(&player) : writer->SendTeddyTentacleDebris(position, 1, 3);
        Check(result && !owner && begins == 1 && ends == 1);
        if (effect != 2) Check(message == originalId);
        else Check(coordinates == std::vector<float>{1, 2, 3, 6, 6, 2, 5, 5, 40} && pvs[0] == 1);
        onBegin = {}; onEnd = {}; Clear();
        Check(!other.SendBloodScreen(&player) && !other.SendTeddyTentacleShake(&player) && begins == 0);
        player.free = 0; Check(other.SendBloodScreen(&player) && begins == 1 && ends == 1);
    }
}
void Oracle(const char* path)
{
    std::ifstream input(path); Check(bool(input));
    std::size_t expectedCases{};
    std::string line;
    Check(bool(std::getline(input, line)));
    std::istringstream header(line);
    Check(bool(header >> expectedCases) && expectedCases > 0 && expectedCases <= 100000);
    header >> std::ws; Check(header.eof());
    int kind{}, id{}, model{}, count{}, target{}; std::array<float, 3> position{}; std::string hex;
    for (std::size_t row = 0; row != expectedCases; ++row)
    {
        Check(bool(std::getline(input, line)));
        std::istringstream fields(line);
        Check(bool(fields >> kind >> id >> position[0] >> position[1] >> position[2] >> model >> count >> target >> hex));
        fields >> std::ws; Check(fields.eof());
        Check(kind >= 0 && kind <= 2 && !hex.empty() && hex.size() % 2 == 0);
        Install(); nextId = id; gmsgShake = id; CCSOLScenarioMessages messages; edict_t player{};
        Check(messages.Register());
        const bool sent = kind == 0 ? messages.SendBloodScreen(&player) : kind == 1 ?
            messages.SendTeddyTentacleShake(&player) : messages.SendTeddyTentacleDebris(position.data(), model, count);
        Check(sent && begins == 1 && ends == 1 && destination == target);
        Check(message == (kind == 2 ? SVC_TEMPENTITY : id));
        Check(kind == 2 ? (hasOrigin && !recipient && pvs == position) : (!hasOrigin && recipient == &player));
        std::vector<unsigned char> expected;
        for (std::size_t i = 0; i < hex.size(); i += 2)
            expected.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        Check(bytes == expected);
    }
    input >> std::ws; Check(input.eof());
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2) Oracle(argv[1]); else { Payloads(); Invalid(); Reentry(); }
        std::cout << "teddy messages " << checks << " checks passed\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << " after " << checks << " checks\n"; return 1; }
}
