// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_skills.h"

// Ordinary skill6 owns a zombie bomb. Its damage, bounce and client effect
// are separate from both the HE grenade and Oberon's projectile.
bool CSOL_UseMonsterZombieBomb(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    bool damageSuppressed, CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error);

bool CSOL_CreateMonsterZombieBomb(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& velocity, float damage, csol::scenario::ConfigError& error);
