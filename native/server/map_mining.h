#pragma once
#include "goldcraft/map_mining.hpp"
#include "goldcraft/map_edits.hpp"
struct edict_s;
void GoldCraft_MapMiningInit();
void GoldCraft_MapMiningReset(std::uint64_t epoch);
void GoldCraft_MapMiningFrame();
goldcraft::mining::Policy GoldCraft_MapMiningPolicy();
const goldcraft::edits::Ledger& GoldCraft_MapEdits();
goldcraft::mining::Result GoldCraft_MapMiningApply(const goldcraft::mining::Request& request, edict_s* player);
goldcraft::mining::Surface GoldCraft_MapMiningSample(const goldcraft::mining::Request& request, edict_s* player);
