// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_animation.h"

#include <bit>
#include <cmath>

namespace
{
std::int16_t SequenceCache(int sequence)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(sequence));
}

// The original run factor is one float ULP above the nearest value of 0.03.
constexpr float RunFactor = std::bit_cast<float>(std::uint32_t{0x3cf5c290});
constexpr float WalkFactor = std::bit_cast<float>(std::uint32_t{0x3dcbfb16});
constexpr float RateBase = std::bit_cast<float>(std::uint32_t{0x3ecccccd});
}

void CCSOLMonsterAnimation::SynchronizeSequence(int sequence)
{
    m_sequence = SequenceCache(sequence);
    m_lockedUntil = 0;
}

bool CCSOLMonsterAnimation::LockUntil(float deadline)
{
    if (!std::isfinite(deadline)) return false;
    m_lockedUntil = deadline;
    return true;
}

void CCSOLMonsterAnimation::SetSequence(CBaseAnimating& monster, int sequence, bool resetFrame)
{
    SetSequenceGuarded(monster, sequence, resetFrame, nullptr, nullptr);
}

bool CCSOLMonsterAnimation::SetSequenceGuarded(CBaseAnimating& monster, int sequence, bool resetFrame,
    SequenceReset reset, void* context)
{
    if (sequence == -3) return true;
    if (sequence == -1) sequence = 0;
    if (resetFrame) monster.pev->frame = 0;
    monster.pev->framerate = 1;
    if (static_cast<int>(m_sequence) == sequence) return true;
    m_sequence = SequenceCache(sequence);
    monster.pev->sequence = sequence;
    if (reset) return reset(context);
    monster.ResetSequenceInfo();
    return true;
}

bool CCSOLMonsterAnimation::UpdateLocomotion(CBaseAnimating& monster,
    const csol::scenario::MonsterSequences& sequences, std::int16_t skill,
    float deltaX, float deltaY, float now, bool climbSequence)
{
    return UpdateLocomotionGuarded(monster, sequences, skill, deltaX, deltaY,
        now, climbSequence, nullptr, nullptr);
}

bool CCSOLMonsterAnimation::UpdateLocomotionGuarded(CBaseAnimating& monster,
    const csol::scenario::MonsterSequences& sequences, std::int16_t skill,
    float deltaX, float deltaY, float now, bool climbSequence,
    SequenceReset reset, void* context)
{
    if (!std::isfinite(now)) return false;
    if (now < m_lockedUntil) return true;
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return false;
    const float xSquared = deltaX * deltaX;
    const float ySquared = deltaY * deltaY;
    const float distanceSquared = xSquared + ySquared;
    if (!std::isfinite(distanceSquared)) return false;

    using Sequence = csol::scenario::MonsterSequence;
    Sequence sequence;
    float rate{};
    if (distanceSquared > 400.0f)
    {
        sequence = skill == 6 ? Sequence::AimGrenadeRun : Sequence::Run;
        const float distance = static_cast<float>(std::sqrt(static_cast<double>(distanceSquared)));
        const float scaled = distance * RunFactor;
        rate = scaled + RateBase;
    }
    else if (distanceSquared > 36.0f)
    {
        sequence = skill == 6 ? Sequence::AimGrenadeWalk : Sequence::Walk;
        const float distance = static_cast<float>(std::sqrt(static_cast<double>(distanceSquared)));
        const float scaled = distance * WalkFactor;
        rate = scaled + RateBase;
    }
    else
        sequence = skill == 6 ? Sequence::AimGrenadeIdle : Sequence::Idle;

    if (!SetSequenceGuarded(monster, sequences[sequence], false, reset, context)) return false;
    // Even if a moving slot contains the preserve sentinel, the original
    // selector writes its rate after the setter has returned.
    if (distanceSquared > 36.0f) monster.pev->framerate = rate;
    if (climbSequence && !SetSequenceGuarded(monster, sequences[Sequence::Climb], false, reset, context)) return false;
    return true;
}
