// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_animation.h"

struct CSOLMonsterInterval
{
    float interval{};
    float deadline{-1.0f};
    float stamp{-1.0f};
};

// These clocks are independent of the locomotion animation lock. Successful
// melee renews all three; failed target/range checks only renew the attack
// check clock in the caller. This is not a health/damage implementation.
struct CSOLMonsterMeleeClocks
{
    CSOLMonsterInterval attack;
    CSOLMonsterInterval avoidance;
    CSOLMonsterInterval idleObstacleCheck;
};

// Invoke only after the ordinary melee eligibility checks have succeeded.
// Samples the original CRT rand()%2 only for running attacks; never substitute
// the engine RANDOM_LONG stream. Damage is applied by the caller in this tick,
// after animation and clocks, not from a later model animation event.
bool CSOL_StartMonsterMelee(CBaseAnimating& monster, CCSOLMonsterAnimation& animation,
    const csol::scenario::MonsterSequences& sequences, float deltaX, float deltaY,
    float now, float attackInterval, bool boosted, CSOLMonsterMeleeClocks& clocks);

// Live behavior reads its interval, boost and current time after animation
// hooks. Keep the animation stage separate from that clock update.
bool CSOL_AnimateMonsterMelee(CBaseAnimating& monster, CCSOLMonsterAnimation& animation,
    const csol::scenario::MonsterSequences& sequences, float deltaX, float deltaY,
    float now,
    CCSOLMonsterAnimation::SequenceReset reset, void* context);
