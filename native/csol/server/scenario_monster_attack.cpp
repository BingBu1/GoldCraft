// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_attack.h"

#include <cmath>
#include <cstdlib>

bool CSOL_StartMonsterMelee(CBaseAnimating& monster, CCSOLMonsterAnimation& animation,
    const csol::scenario::MonsterSequences& sequences, float deltaX, float deltaY,
    float now, float attackInterval, bool boosted, CSOLMonsterMeleeClocks& clocks)
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(now) ||
        !std::isfinite(attackInterval))
        return false;
    const float xSquared = deltaX * deltaX;
    const float ySquared = deltaY * deltaY;
    const float distanceSquared = xSquared + ySquared;
    if (!std::isfinite(distanceSquared)) return false;

    const bool running = distanceSquared > 400.0f;
    const float lock = now + (running ? 0.5f : 1.0f);
    float interval = attackInterval;
    if (boosted)
    {
        const float reduced = interval - 0.5f;
        // MAXSS selects the second operand on equality.
        interval = 0.4f > reduced ? 0.4f : reduced;
    }
    const float deadline = now + interval;
    if (!std::isfinite(lock) || !std::isfinite(interval) || !std::isfinite(deadline))
        return false;

    if (!CSOL_AnimateMonsterMelee(monster, animation, sequences, deltaX, deltaY, now,
        nullptr, nullptr)) return false;
    clocks.attack = {interval, deadline, now};
    clocks.avoidance = {interval, deadline, now};
    clocks.idleObstacleCheck = {interval, deadline, now};
    return true;
}

bool CSOL_AnimateMonsterMelee(CBaseAnimating& monster, CCSOLMonsterAnimation& animation,
    const csol::scenario::MonsterSequences& sequences, float deltaX, float deltaY,
    float now, CCSOLMonsterAnimation::SequenceReset reset, void* context)
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(now)) return false;
    const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
    if (!std::isfinite(distanceSquared)) return false;
    const bool running = distanceSquared > 400.0f;
    const float lock = now + (running ? 0.5f : 1.0f);
    if (!std::isfinite(lock)) return false;
    using Sequence = csol::scenario::MonsterSequence;
    auto slot = Sequence::AttackWalk;
    if (running) slot = std::rand() % 2 ? Sequence::AttackRun2 : Sequence::AttackRun1;
    int sequence = sequences[slot];
    if (sequence == -1) sequence = sequences[Sequence::AttackWalk];

    animation.LockUntil(lock);
    return animation.SetSequenceGuarded(monster, sequence, true, reset, context);
}
