// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_skills.h"
#include "scenario_monster_ordinary.h"
#include "scenario_runtime.h"
#include "scenario_session.h"

#include <cmath>

#pragma clang fp contract(off)

namespace
{
void Clock(CSOLMonsterInterval& clock, float interval)
{
    clock.interval = interval;
    clock.deadline = gpGlobals->time + interval;
    clock.stamp = gpGlobals->time;
}
}

bool CSOL_UseMonsterDisarm(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    // 113F0F20/113F1021: helper early returns always reach these clocks.
    const auto finish = [&]
    {
        if (!current()) return false;
        Clock(state.skillActive, 1.0f);
        Clock(state.skill, 10.0f);
        return true;
    };
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();
    auto* monster = current();
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance || !g_engfuncs.pfnTraceLine)
    { error.message = "scenario disarm requires its appearance and trace service"; return false; }
    const Vector origin = monster->pev->origin + Vector(0, 0, appearance->eyesZ);
    const Vector goal = target->pev->origin;
    const Vector delta = goal - origin;
    const float square = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(square)));
    if (!std::isfinite(length))
    { error.message = "scenario disarm direction is not finite"; return false; }
    Vector direction(0, 0, 1);
    if (length != 0)
    {
        const float inverse = 1.0f / length;
        direction = Vector(delta.x * inverse, delta.y * inverse, delta.z * inverse);
    }
    TraceResult trace{};
    UTIL_TraceLine(origin, goal, dont_ignore_monsters, monster->edict(), &trace);
    if (!current()) return false;
    if (trace.pHit)
    {
        const EHANDLE hitHandle(trace.pHit);
        auto* hit = hitHandle.Get<CBaseEntity>();
        if (!hit) return finish();
        const bool isPlayer = hit->IsPlayer() != FALSE;
        if (!current()) return false;
        hit = hitHandle.Get<CBaseEntity>();
        if (!hit || !isPlayer) return finish();
        const bool alive = hit->IsAlive() != FALSE;
        if (!current()) return false;
        auto* player = dynamic_cast<CBasePlayer*>(hitHandle.Get<CBaseEntity>());
        if (!player || !alive || static_cast<int>(player->m_iTeam) == current()->pev->team) return finish();
    }

    struct Reset
    {
        CSOLMonsterSpawnCall& call;
        CSOLMonsterSkillGuard guard;
        static bool Valid(const void* context)
        {
            const auto& self = *static_cast<const Reset*>(context);
            return self.guard.Current() && self.call.Current();
        }
        static bool Run(void* context)
        {
            auto& self = *static_cast<Reset*>(context);
            return CSOL_ResetMonsterSequenceInfo(self.call, Valid, context);
        }
    } reset{call, guard};
    monster = current();
    if (!monster->Animation().SetSequenceGuarded(*monster,
        spawn.sequences[csol::scenario::MonsterSequence::AlternateShootGrenadeIdle],
        true, Reset::Run, &reset) || !current()) return false;
    Clock(state.melee.attack, 1.0f);
    Clock(state.melee.avoidance, 1.0f);
    current()->Animation().LockUntil(gpGlobals->time + 1.0f);
    Clock(state.melee.idleObstacleCheck, 1.0f);
    state.position.faceGoal = goal;
    state.position.faceRequested = true;
    bool created{};
    if (!CSOL_CreateMonsterDisarm(call, runtime, origin, direction, created, error) || !current())
        return false;
    if (!created) return finish();
    auto* behavior = dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(current()->Behavior());
    if (!behavior || !behavior->Stop(call) || !current()) return false;
    state.position.destination.x = -99999.0f;
    state.position.motion.velocity = Vector2D(0, 0);
    state.deltaX = state.deltaY = state.motionRemainder = 0;
    Clock(state.findPath, 1.0f);
    return finish();
}
