// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK message writers. Original instruction packets are private inputs;
// engine callbacks below exercise only the documented host adaptation boundary.
#include "precompiled.h"
#include "native/csol/server/scenario/core/scenario_messages.h"
#include "native/csol/server/damage_numbers.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
int gmsgShake{111};
bool CSOLWriteDamageNumber(int, edict_t*, CSOLDamageNumber)
{ throw std::runtime_error("unexpected damage-number sender"); }

namespace
{
unsigned checks{}, calls{}, begins{}, ends{};
int destination{}, message{};
bool routed{};
std::array<float, 3> route{};
std::vector<unsigned char> bytes;
std::function<void()> callback;
void Check(bool value, std::source_location at = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("blackhole API line " + std::to_string(at.line()));
}
void Edge() { ++calls; const auto action = callback; if (action) action(); }
int Register(const char* name, int)
{
    Edge();
    return std::string_view(name) == "MPToCL" ? 96 : 97;
}
void Begin(int target, int id, const float* origin, edict_t*)
{
    ++begins; destination = target; message = id; routed = origin != nullptr;
    if (origin) std::copy_n(origin, 3, route.begin());
    Edge();
}
void Byte(int value) { bytes.push_back(static_cast<unsigned char>(value)); Edge(); }
void Short(int value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(static_cast<unsigned>(value) >> 8)); Edge();
}
void Long(int value)
{
    for (unsigned i = 0; i != 4; ++i) bytes.push_back(static_cast<unsigned char>(static_cast<unsigned>(value) >> (i * 8)));
    Edge();
}
void Coord(float value) { Short(static_cast<int>(value * 8.f)); }
void String(const char* value)
{
    do { bytes.push_back(static_cast<unsigned char>(*value)); } while (*value++);
    Edge();
}
void End() { ++ends; Edge(); }
void Clear()
{
    calls = begins = ends = 0; destination = message = 0;
    routed = false; route = {}; bytes.clear(); callback = {};
}
void Install()
{
    g_engfuncs = {}; g_engfuncs.pfnRegUserMsg = Register;
    g_engfuncs.pfnMessageBegin = Begin; g_engfuncs.pfnMessageEnd = End;
    g_engfuncs.pfnWriteByte = Byte; g_engfuncs.pfnWriteShort = Short;
    g_engfuncs.pfnWriteLong = Long; g_engfuncs.pfnWriteCoord = Coord;
    g_engfuncs.pfnWriteString = String; Clear();
}
std::vector<unsigned char> Hex(std::string_view hex)
{
    Check(!hex.empty() && hex.size() % 2 == 0);
    std::vector<unsigned char> result;
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
        const auto digit = [](char c) -> unsigned
        {
            if (c >= '0' && c <= '9') return unsigned(c - '0');
            if (c >= 'a' && c <= 'f') return unsigned(c - 'a') + 10;
            if (c >= 'A' && c <= 'F') return unsigned(c - 'A') + 10;
            throw std::runtime_error("invalid original packet hex");
        };
        result.push_back(static_cast<unsigned char>((digit(hex[i]) << 4) | digit(hex[i + 1])));
    }
    return result;
}
bool Send(CCSOLScenarioMessages& messages, bool smoke, const float* origin, int model = 123)
{ return smoke ? messages.SendTeddyBlackholeSmoke(origin) : messages.SendTeddyBlackholeDebris(origin, model); }
void Rejections()
{
    for (bool smoke : {false, true})
    {
        Install(); CCSOLScenarioMessages messages; const float good[]{1, 2, 3};
        Check(!messages.SendTeddyBlackholeSmoke(good) && !calls);
        Check(messages.Register()); Clear();
        const auto reject = [&](const float* origin, int model = 123)
        {
            Clear(); Check(!Send(messages, smoke, origin, model));
            Check(!calls && !begins && !ends && bytes.empty());
            Check(Send(messages, smoke, good) && begins == 1 && ends == 1);
        };
        reject(nullptr);
        if (!smoke) for (int model : {-1, 0, 32768}) reject(good, model);
        for (unsigned axis = 0; axis != 3; ++axis)
            for (float bad : {268435456.f, -268435488.f,
                std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()})
            {
                auto value = std::array<float, 3>{1, 2, 3}; value[axis] = bad;
                reject(value.data());
            }
        const auto complete = g_engfuncs;
        for (unsigned missing = 0; missing != (smoke ? 6u : 5u); ++missing)
        {
            Clear(); g_engfuncs = complete;
            if (missing == 0) g_engfuncs.pfnMessageBegin = nullptr;
            if (missing == 1) g_engfuncs.pfnMessageEnd = nullptr;
            if (missing == 2) g_engfuncs.pfnWriteByte = nullptr;
            if (missing == 3) g_engfuncs.pfnWriteCoord = nullptr;
            if (missing == 4 && !smoke) g_engfuncs.pfnWriteShort = nullptr;
            if (missing == 4 && smoke) g_engfuncs.pfnWriteLong = nullptr;
            if (missing == 5) g_engfuncs.pfnWriteString = nullptr;
            Check(!Send(messages, smoke, good) && !calls && !begins && !ends && bytes.empty());
            g_engfuncs = complete;
            Check(Send(messages, smoke, good) && begins == 1 && ends == 1);
        }
    }
}
void Coordinates()
{
    struct Test { float input; unsigned short wire; };
    constexpr Test cases[]{ {4096.1875f, 0x8001}, {-4096.1875f, 0x7fff},
        {8192.125f, 1}, {-8192.125f, 0xffff}, {.124f, 0}, {-.124f, 0},
        {-268435456.f, 0}, {268435440.f, 0xff80} };
    for (bool smoke : {false, true})
        for (unsigned axis = 0; axis != 3; ++axis)
            for (const auto& test : cases)
                for (int model : {1, 32767})
                {
                    Install(); CCSOLScenarioMessages messages; Check(messages.Register()); Clear();
                    auto value = std::array<float, 3>{1, 2, 3}; value[axis] = test.input;
                    Check(Send(messages, smoke, value.data(), model) && begins == 1 && ends == 1);
                    Check(destination == (smoke ? MSG_ALL : MSG_PVS) && message == (smoke ? 96 : SVC_TEMPENTITY));
                    Check(routed == !smoke && (smoke || route == value));
                    Check(bytes.size() == (smoke ? 99u : 25u));
                    const auto offset = (smoke ? 93u : 1u) + axis * 2;
                    Check(bytes[offset] == static_cast<unsigned char>(test.wire) &&
                        bytes[offset + 1] == static_cast<unsigned char>(test.wire >> 8));
                    if (!smoke) Check(bytes[20] == static_cast<unsigned char>(model) && bytes[21] == static_cast<unsigned char>(model >> 8));
                }
}
void Lifetime()
{
    for (bool smoke : {false, true})
    {
        Install(); CCSOLScenarioMessages reference; Check(reference.Register()); Clear();
        const std::array<float, 3> original{4096.1875f, -4096.1875f, 8192.125f};
        Check(Send(reference, smoke, original.data())); const auto expected = bytes;
        const auto edges = calls;
        for (unsigned retire = 1; retire <= edges; ++retire)
        {
            Clear(); auto owner = std::make_unique<CCSOLScenarioMessages>(); Check(owner->Register()); Clear();
            auto input = original; unsigned observed{};
            callback = [&]
            {
                const auto before = begins;
                Check(!reference.SendTeddyBlackholeDebris(input.data(), 123) &&
                    !reference.SendTeddyBlackholeSmoke(input.data()) && begins == before);
                input = {99, 98, 97};
                if (++observed == retire) owner.reset();
            };
            auto* writer = owner.get(); Check(Send(*writer, smoke, input.data()));
            Check(!owner && observed == edges && begins == 1 && ends == 1 && bytes == expected);
            Check(routed == !smoke && (smoke || route == original));
            Clear(); Check(Send(reference, smoke, original.data()) && begins == 1 && ends == 1);
        }
    }
}
void Original(const char* path)
{
    std::ifstream input(path); Check(bool(input)); std::string line;
    Check(bool(std::getline(input, line))); std::istringstream header(line); unsigned count{};
    Check(bool(header >> count) && count == 21); header >> std::ws; Check(header.eof());
    unsigned packets{};
    for (unsigned n = 0; n != count; ++n)
    {
        Check(bool(std::getline(input, line))); std::istringstream fields(line);
        int event{}, variant{}, created{}, model{}; float feet{};
        std::array<std::array<float, 3>, 3> centers{}; std::string debris, shake, smoke;
        Check(bool(fields >> event >> variant >> created >> feet >> model));
        for (auto& center : centers) for (auto& axis : center) Check(bool(fields >> axis) && std::isfinite(axis));
        Check(bool(fields >> debris >> shake >> smoke)); fields >> std::ws; Check(fields.eof());
        Check(variant >= 0 && variant < 3 && created == variant % 2 && model == 400 + variant && std::isfinite(feet));
        if (event != 1) { Check(debris == "-" && shake == "-" && smoke == "-"); continue; }
        for (int kind = 0; kind != 3; ++kind)
        {
            Install(); CCSOLScenarioMessages messages; Check(messages.Register()); Clear(); edict_t player{};
            const auto expected = Hex(kind == 0 ? debris : kind == 1 ? shake : smoke);
            Check(kind == 1 ? messages.SendTeddyTentacleShake(&player) : Send(messages, kind == 2, centers[1].data(), model));
            Check(begins == 1 && ends == 1 && bytes == expected);
            Check(destination == (kind == 0 ? MSG_PVS : kind == 1 ? MSG_ONE : MSG_ALL));
            Check(routed == (kind == 0) && (kind != 0 || route == centers[1])); ++packets;
        }
    }
    input >> std::ws; Check(input.eof() && packets == 9);
    std::cout << "original visual packets=" << packets << '\n';
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { Rejections(); Coordinates(); Lifetime(); }
        else if (argc == 2) Original(argv[1]); else return 2;
        std::cout << "blackhole API checks=" << checks << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "ERROR " << error.what() << '\n'; return 1; }
}
