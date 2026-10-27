// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_attack.h"
#include "scenario_monster_factory.h"

enum class CSOLMonsterMeleeResult { Hit, Miss, Cancelled, Failed };

// Shared by a target melee hit and the original Idle obstacle attack. It
// selects the real sequence then samples enhanced interval/time after hooks,
// updating all three clocks. Range/facing and damage rules belong to callers.
bool CSOL_AnimateOrdinaryMonsterAttack(CSOLMonsterSpawnCall& call,
    const CSOLMonsterSpawnState& state, const float& boostUntil,
    float deltaX, float deltaY, CSOLMonsterMeleeClocks& clocks,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);

// Invoke from the ordinary tree's Attack action, before its FindTarget action.
// This implements only propensity0 in mode15/map98, including its immediate
// real SDK TakeDamage call. Skills and nonordinary propensity are separate.
CSOLMonsterMeleeResult CSOL_TryOrdinaryMonsterMelee(CSOLMonsterSpawnCall& call,
    const CSOLMonsterSpawnState& state, const EHANDLE& target,
    const float& boostUntil,
    const bool& damageSuppressed, float deltaX, float deltaY,
    CSOLMonsterMeleeClocks& clocks,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
