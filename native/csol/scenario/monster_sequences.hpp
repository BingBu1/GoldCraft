// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace csol::scenario
{
enum class MonsterSequence : std::size_t
{
    Idle, Walk, Run, Jump, AttackWalk, AttackRun1, AttackRun2,
    AimGrenadeIdle, AimGrenadeWalk, AimGrenadeRun,
    ShootGrenadeIdle, ShootGrenadeWalk, ShootGrenadeRun,
    HeadFlinch, GutFlinch, Head, Death1, Death2, Death3,
    Gutshot, Left, Back, Right, Forward,
    // The original type has a second slot for zbs_shoot_grenade_idle1.
    AlternateShootGrenadeIdle, Dash, SkillIdle, SkillWalk, SkillRun,
    Skill1, Climb, TurnLeft, TurnRight, Count
};

inline constexpr auto MonsterSequenceCount = static_cast<std::size_t>(MonsterSequence::Count);
struct MonsterSequences
{
    MonsterSequences() { values.fill(-1); }
    std::array<std::int16_t, MonsterSequenceCount> values;
    std::int16_t& operator[](MonsterSequence sequence) { return values[static_cast<std::size_t>(sequence)]; }
    std::int16_t operator[](MonsterSequence sequence) const { return values[static_cast<std::size_t>(sequence)]; }
};

std::span<const std::string_view, MonsterSequenceCount> MonsterSequenceNames();
using MonsterSequenceLookup = int (*)(void* context, const char* label);

// Populate a type's cache only after setting its actual model. Missing names
// retain -1 except the original death/climb/turn fallbacks. The caller owns
// the per-type/map cache lifetime; this function has no process-global cache.
// Initial spawn still selects sequence 1 (Oberon's scene_appear), not Idle.
bool ResolveMonsterSequences(MonsterSequenceLookup lookup, void* context, MonsterSequences& output);
}
