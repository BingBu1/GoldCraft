// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_bomb.h"
#include "scenario_monster_ordinary.h"
#include "../../../scenario/monsters/monster_appearances.hpp"

#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)

bool CSOL_UseMonsterZombieBomb(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    bool damageSuppressed, CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error)
{
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        auto* monster = guard.Current() ? call.Current() : nullptr;
        return monster && monster->Pool() && monster->Pool()->Borrowed(*monster) ? monster : nullptr;
    };
    if (!gpGlobals || !current()) return false;
    const auto clock = [](CSOLMonsterInterval& value, float interval)
    { value = {interval, gpGlobals->time + interval, gpGlobals->time}; };
    // MP113F0F06 calls the helper before these two clocks. Ordinary early
    // rejection and entity exhaustion still consume the exact attack interval.
    const auto finish = [&]
    {
        if (!current()) return false;
        clock(state.skillActive, 1.0f);
        clock(state.skill, spawn.attributes.attackInterval);
        return true;
    };
    if (damageSuppressed) return finish();
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();

    auto* monster = current();
    const Vector goal = target->pev->origin;
    const Vector delta = goal - monster->pev->origin;
    const float square = delta.y * delta.y + delta.x * delta.x;
    const float range = spawn.attributes.attackRange;
    if (!std::isfinite(square) || !std::isfinite(range))
    { error.message = "scenario zombie bomb target distance is not finite"; return false; }
    if (square > range * range) return finish();
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance)
    { error.message = "scenario zombie bomb appearance is unavailable"; return false; }

    const Vector velocity = monster->pev->velocity;
    const float speed = velocity.x * velocity.x + velocity.y * velocity.y;
    using csol::scenario::MonsterSequence;
    const auto sequence = speed > 25600.0f ? MonsterSequence::ShootGrenadeRun :
        speed > 225.0f ? MonsterSequence::ShootGrenadeWalk : MonsterSequence::ShootGrenadeIdle;
    struct Reset
    {
        CSOLMonsterSpawnCall& call;
        CSOLMonsterSkillGuard guard;
        static bool Valid(const void* context)
        {
            const auto& self = *static_cast<const Reset*>(context);
            const auto* monster = self.guard.Current() ? self.call.Current() : nullptr;
            return monster && monster->Pool() && monster->Pool()->Borrowed(*monster);
        }
        static bool Run(void* context)
        {
            auto& self = *static_cast<Reset*>(context);
            return CSOL_ResetMonsterSequenceInfo(self.call, Valid, context);
        }
    } reset{call, guard};
    if (!monster->Animation().SetSequenceGuarded(*monster, spawn.sequences[sequence],
        true, Reset::Run, &reset) || !current()) return false;
    clock(state.melee.attack, 1.0f);
    clock(state.melee.avoidance, 1.0f);
    current()->Animation().LockUntil(gpGlobals->time + 1.0f);
    clock(state.melee.idleObstacleCheck, 1.0f);
    // MP113F1618 resolves the target again after SetSequence. Retain the
    // earlier delta for the throw, but face the target's current position.
    if (auto* facing = state.target.Get<CBaseEntity>())
    {
        state.position.faceGoal = facing->pev->origin;
        state.position.faceRequested = true;
    }

    float distance = static_cast<float>(std::sqrt(static_cast<double>(square)));
    if (distance == 0) distance = 1.0f;
    const float inverse = 1.0f / distance;
    const Vector direction(delta.x * inverse, delta.y * inverse, std::min(0.5f, distance * 0.01f));
    const Vector eye = current()->pev->origin + Vector(0, 0, appearance->eyesZ);
    const Vector origin = eye + direction * 16.0f;
    const Vector launch = direction * std::min(750.0f, distance * 0.7f);
    if (!CSOL_CreateMonsterZombieBomb(call, runtime, origin, launch,
        spawn.attributes.attackDamage, error) || !current()) return false;
    // Unlike skill8, this helper neither stops movement nor resets findPath.
    return finish();
}
