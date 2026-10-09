#pragma once
#include "goldcraft/map_mining.hpp"
struct edict_s;
void GoldCraft_MapMiningInit();
void GoldCraft_MapMiningReset(std::uint64_t epoch);
goldcraft::mining::Policy GoldCraft_MapMiningPolicy();
goldcraft::mining::Result GoldCraft_MapMiningApply(const goldcraft::mining::Request& request, edict_s* player);
