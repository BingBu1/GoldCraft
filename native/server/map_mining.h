#pragma once
#include "goldcraft/map_mining.hpp"
#include "goldcraft/map_edits.hpp"
#include "goldcraft/host_explosion.hpp"
struct edict_s;
void GoldCraft_MapMiningInit();
void GoldCraft_MapMiningReset(std::uint64_t epoch);
void GoldCraft_MapMiningFrame();
void GoldCraft_MapMiningInvalidateSamples();
goldcraft::mining::Policy GoldCraft_MapMiningPolicy();
const goldcraft::edits::Ledger& GoldCraft_MapEdits();
bool GoldCraft_MapHostExplosion(const goldcraft::explosion::Event& event);
void GoldCraft_MapMiningResetExplosionSequence();
goldcraft::Bytes GoldCraft_MapMiningMaterials(std::span<const std::uint8_t> bsp);
goldcraft::mining::Result GoldCraft_MapMiningApply(const goldcraft::mining::Request& request, edict_s* player);
goldcraft::mining::Surface GoldCraft_MapMiningSample(const goldcraft::mining::Request& request, edict_s* player);
