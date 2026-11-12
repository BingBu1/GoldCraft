// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/screen_shake.hpp"
#include <array>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t globals{};
globalvars_t* gpGlobals = &globals;
bool g_bAllowedCSBot{};
const Vector g_vecZero{};

namespace
{
using namespace guest::cof;
std::array<edict_t, 33> entities;
std::array<CBasePlayer, 33> players;
std::vector<unsigned> output;
unsigned checks{}, words{}, packets{}, edge{}, cancelAt{};
bool valid{}, packetOpen{}, nested{};
void Check(bool value) { ++checks; if (!value) { std::cerr << "check " << checks << " failed\n"; std::exit(2); } }
bool Current(void*) noexcept { return valid; }
void Edge() { if (++edge == cancelAt) valid = false; }
edict_t* Entity(int i) { Edge(); return &entities[i]; }
int Index(const edict_t* p) { return int(p-entities.data()); }
void Begin(int destination, int message, const float* origin, edict_t* player)
{
    Check(!packetOpen && !origin); packetOpen = true; words = 0;
    output.insert(output.end(), {unsigned(destination), unsigned(message), unsigned(Index(player))});
    Edge();
    if (nested) Check(ShakePlayers({{}, 1, 1, 1, 0}, 77, {nullptr, Current}).status == WeaponEffectStatus::Busy);
}
void Word(int value) { Check(packetOpen); output.push_back(unsigned(value)&65535); ++words; Edge(); }
void End() { Check(packetOpen && words == 3); packetOpen = false; ++packets; Edge(); }
void Reset()
{
    output.clear(); edge = words = packets = cancelAt = 0; valid = true; packetOpen = nested = false;
    globals = {}; globals.maxClients = 5; g_engfuncs = {};
    g_engfuncs.pfnPEntityOfEntIndex = Entity; g_engfuncs.pfnIndexOfEdict = Index;
    g_engfuncs.pfnMessageBegin = Begin; g_engfuncs.pfnMessageEnd = End; g_engfuncs.pfnWriteShort = Word;
    for (unsigned i = 0; i < entities.size(); ++i)
    {
        entities[i] = {}; entities[i].serialnumber = 8;
        entities[i].v.pContainingEntity = &entities[i]; entities[i].pvPrivateData = &players[i];
        players[i].pev = &entities[i].v; players[i].pev->flags = FL_ONGROUND;
    }
}
float Float(unsigned word) { return std::bit_cast<float>(word); }
}

int main(int argc, char** argv)
{
    if (argc != 3) return 3;
    std::ifstream cases(argv[1]), expected(argv[2]);
    if (!cases || !expected) return 3;
    std::string row, answer; unsigned count{};
    while (std::getline(cases, row))
    {
        Reset(); std::istringstream input(row); std::array<unsigned, 26> data{};
        for (auto& word : data) if (!(input >> std::hex >> word)) return 3;
        std::string extra; if (input >> extra) return 3;
        globals.maxClients = int(data[7]);
        for (unsigned i = 1; i <= 5; ++i)
        {
            entities[i].free = (data[9]>>i)&1;
            entities[i].pvPrivateData = data[10]&(1u<<i) ? nullptr : &players[i];
            players[i].pev->flags = data[8]&(1u<<i) ? FL_ONGROUND : 0;
            players[i].pev->origin = {Float(data[11+3*(i-1)]), Float(data[12+3*(i-1)]), Float(data[13+3*(i-1)])};
        }
        const auto result = ShakePlayers({{Float(data[4]), Float(data[5]), Float(data[6])},
            Float(data[0]), Float(data[1]), Float(data[2]), Float(data[3])}, 77, {nullptr, Current});
        Check(result.status == WeaponEffectStatus::Issued && result.packets == packets && !packetOpen);
        if (!std::getline(expected, answer)) return 4;
        std::istringstream reference(answer); std::vector<unsigned> wanted; unsigned word{};
        while (reference >> std::hex >> word) wanted.push_back(word);
        if (wanted != output) { std::cerr << "original row " << count << " differs\n"; return 5; }
        ++count;
    }
    if (count != 96 || std::getline(expected, answer)) return 6;
    Reset(); Check(ShakePlayers({{}, 1, 1, 1, 0}, 0, {nullptr, Current}).status == WeaponEffectStatus::MissingMessage && !edge);
    Reset(); nested = true; Check(ShakePlayers({{}, 1, 1, 1, 0}, 77, {nullptr, Current}).packets == 5);
    const unsigned edges = edge;
    for (unsigned i = 1; i <= edges; ++i)
    {
        Reset(); cancelAt = i;
        Check(ShakePlayers({{}, 1, 1, 1, 0}, 77, {nullptr, Current}).status == WeaponEffectStatus::Cancelled);
        Check(!packetOpen); // Opened packets always finish after a synchronous revocation.
    }
    std::cout << count << " original cases; " << checks << " SDK checks passed\n";
}

#include "sdk-abort-virtuals.inc"
