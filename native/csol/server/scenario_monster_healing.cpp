// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_healing.h"
#include "scenario_monster_ordinary.h"
#include "scenario_messages.h"

#include <algorithm>
#include <array>

bool CSOL_UseMonsterHealing(CSOLMonsterSpawnCall& call, CCSOLMonsterPool& pool,
    const CCSOLScenarioMessages& messages, CSOLOrdinaryMonsterState& state,
    CSOLMonsterHealingGuard owner, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto current = [&] { return owner.Current() && call.Current(); };
    if (!gpGlobals || !current()) return false;

    // MP 1137E130 collects the first four solid manager slots in range.
    // The subsequent immunity check does not replace a skipped candidate.
    struct Target { EntityHandle<CCSOLScenarioMonster> entity; std::uint64_t lease{}; std::size_t slot{}; };
    std::array<Target, 4> targets{};
    std::size_t count{};
    const Vector origin = call.Current()->pev->origin;
    constexpr float radius = 157.4799957275390625f;
    call.Current()->pev->maxspeed = 1.0f;
    for (std::size_t i = 0; i < CCSOLMonsterPool::Capacity && count < targets.size(); ++i)
    {
        auto* target = pool.At(i);
        if (!target || target->pev->solid == SOLID_NOT) continue;
        const Vector delta = origin - target->pev->origin;
        const float distance = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
        if (distance < radius * radius)
            targets[count++] = {EntityHandle<CCSOLScenarioMonster>(target), pool.Lease(*target), i};
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!current()) return false;
        // Pool::At validates classname/private type before dereferencing.
        // A message hook may replace private data without changing serial.
        auto* target = pool.At(targets[i].slot);
        if (!target || !(targets[i].entity == target) || pool.Lease(*target) != targets[i].lease) continue;
        const double blocked = target->SpeedState().impulseProtectionUntil;
        if (blocked > 0 && blocked > gpGlobals->time) continue;
        const float restored = target->pev->health + target->pev->max_health * 0.5f;
        target->pev->health = std::min(restored, target->pev->max_health);
        const bool self = target == call.Current();
        const Vector position = target->pev->origin;
        const int index = target->entindex();
        if (!current()) return false;
        target = pool.At(targets[i].slot);
        if (!target || !(targets[i].entity == target) || pool.Lease(*target) != targets[i].lease) continue;
        const bool sent = messages.SendMonsterHealing(index, position, self);
        if (!current()) return false;
        if (!sent) { error.message = "scenario monster healing effect message failed"; return false; }
    }
    const float now = gpGlobals->time;
    state.skillActive = {1.0f, now + 1.0f, now};
    const float cooldown = 5.0f + state.jitter * 5.0f;
    state.skill = {cooldown, now + cooldown, now};
    return true;
}
