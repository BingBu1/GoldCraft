// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/settings.hpp"
#include "../../scenario/custom/phase.hpp"
#include "../../scenario/custom/night_lighting.hpp"
#include "../../scenario/custom/monster_buff.hpp"
#include "../../scenario/custom/monster_population.hpp"

void CSOL_InitGameplaySettings();
void CSOL_PollGameplaySettings();
void CSOL_ShutdownGameplaySettings();
bool CSOL_CustomFeatureEnabled(csol::scenario::custom::Feature feature);
csol::scenario::custom::SnapshotPtr CSOL_CustomSettings();
bool CSOL_ReadTimeCycleControls(csol::scenario::custom::TimeCycleControls& output);
bool CSOL_ReadNightLightingControls(csol::scenario::custom::NightLightingConfig& output);
csol::scenario::custom::MonsterPhaseBuff CSOL_ReadMonsterPhaseBuff(csol::scenario::custom::Phase phase);
std::optional<csol::scenario::custom::MonsterPopulationControls> CSOL_ReadMonsterPopulation(
    csol::scenario::custom::Phase phase);
