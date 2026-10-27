// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/monster_sequences.hpp"

class CBaseAnimating;
class CSOLMonsterSpawnCall;

// Original CMonster uses the fixed .1-second dispatch window; its default
// HandleAnimEvent is empty. This retains SDK event traversal with lifetime
// checks rather than inventing server sounds or damage for model event IDs.
bool CSOL_DispatchMonsterAnimationEvents(CSOLMonsterSpawnCall& call,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);

// One instance per monster, retained with its entity across behavior changes.
// The factory synchronizes the cache after its explicit initial sequence 1
// and ResetSequenceInfo; model installation must precede any of these calls.
class CCSOLMonsterAnimation
{
public:
    using SequenceReset = bool (*)(void* context);
    void SynchronizeSequence(int sequence);
    bool LockUntil(float deadline);
    float LockedUntil() const { return m_lockedUntil; }
    std::int16_t CachedSequence() const { return m_sequence; }

    // Matches the original sequence setter. -3 preserves everything; -1 maps
    // to sequence 0. Re-selecting a sequence does not reset its timing. Values
    // come from the model's resolved sequence cache, not client input.
    void SetSequence(CBaseAnimating& monster, int sequence, bool resetFrame);
    bool SetSequenceGuarded(CBaseAnimating& monster, int sequence, bool resetFrame,
        SequenceReset reset, void* context);

    // Called after ordinary behavior logic in the 0.1-second update. deltaX/Y
    // are origin differences from that update, never velocity or speed*dt.
    // The caller owns the origin history, climb state and scheduling. Returns
    // false for non-finite/overflow input without changing the monster.
    bool UpdateLocomotion(CBaseAnimating& monster,
        const csol::scenario::MonsterSequences& sequences, std::int16_t skill,
        float deltaX, float deltaY, float now, bool climbSequence);
    bool UpdateLocomotionGuarded(CBaseAnimating& monster,
        const csol::scenario::MonsterSequences& sequences, std::int16_t skill,
        float deltaX, float deltaY, float now, bool climbSequence,
        SequenceReset reset, void* context);

private:
    std::int16_t m_sequence{-1};
    float m_lockedUntil{};
};
