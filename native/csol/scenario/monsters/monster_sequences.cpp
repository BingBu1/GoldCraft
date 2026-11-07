// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_sequences.hpp"

#include <bit>

namespace csol::scenario
{
namespace
{
constexpr std::array<std::string_view, MonsterSequenceCount> Names{
    "zbs_idle1", "zbs_walk", "zbs_run", "zbs_jump", "zbs_attack_walk",
    "zbs_attack1_run", "zbs_attack2_run", "zbs_aim_grenade_idle1",
    "zbs_aim_grenade_walk", "zbs_aim_grenade_run", "zbs_shoot_grenade_idle1",
    "zbs_shoot_grenade_walk", "zbs_shoot_grenade_run", "head_flinch", "gut_flinch",
    "head", "death1", "death2", "death3", "gutshot", "left", "back", "right", "forward",
    "zbs_shoot_grenade_idle1", "zbs_skill_dash", "zbs_skill1_idle", "zbs_skill1_walk",
    "zbs_skill1_run", "skill1", "zbs_climb", "turnL", "turnR"
};

std::int16_t Narrow(int sequence)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(sequence));
}
}

std::span<const std::string_view, MonsterSequenceCount> MonsterSequenceNames()
{
    return Names;
}

bool ResolveMonsterSequences(MonsterSequenceLookup lookup, void* context, MonsterSequences& output)
{
    if (!lookup) return false;
    MonsterSequences candidate;
    for (std::size_t i = 0; i < Names.size(); ++i)
        candidate.values[i] = Narrow(lookup(context, Names[i].data()));

    if (candidate[MonsterSequence::Head] == -1)
    {
        const auto death = Narrow(lookup(context, "death"));
        // Original replaces every slot from head through forward, including
        // individually resolved death names. It is not a per-name fallback.
        for (auto i = static_cast<std::size_t>(MonsterSequence::Head);
             i <= static_cast<std::size_t>(MonsterSequence::Forward); ++i)
            candidate.values[i] = death;
    }
    if (candidate[MonsterSequence::Climb] == -1)
        candidate[MonsterSequence::Climb] = candidate[MonsterSequence::Walk];
    if (candidate[MonsterSequence::TurnLeft] == -1)
        candidate[MonsterSequence::TurnLeft] = candidate[MonsterSequence::Idle];
    if (candidate[MonsterSequence::TurnRight] == -1)
        candidate[MonsterSequence::TurnRight] = candidate[MonsterSequence::Idle];
    output = candidate;
    return true;
}
}
