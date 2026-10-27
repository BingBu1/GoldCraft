// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_ordinary.h"

#include <cmath>

namespace
{
void StartClock(CSOLMonsterInterval& clock, float interval)
{
    clock.interval = interval;
    clock.deadline = gpGlobals->time + interval;
    clock.stamp = gpGlobals->time;
}
}

bool CCSOLOrdinaryMonsterBehavior::CheckObstacles(CSOLMonsterSpawnCall& call, const StatePtr& state)
{
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    if (!state->target.Get<CBaseEntity>() || !(gpGlobals->time > state->melee.idleObstacleCheck.deadline))
        return true;
    StartClock(state->melee.idleObstacleCheck, 0.3f);
    const float dx = state->lastObstacleOrigin.x - monster->pev->origin.x;
    const float dy = state->lastObstacleOrigin.y - monster->pev->origin.y;
    const float distance = dy * dy + dx * dx;
    if (!std::isfinite(distance))
        return Step(call, state, false, "ordinary obstacle movement is not finite");
    if (static_cast<double>(distance) > 247.99949218750004)
    {
        state->obstacleBlocked = false;
        state->lastObstacleOrigin = monster->pev->origin;
        return true;
    }

    const std::int16_t originalDamage = monster->Type()->attackDamage;
    bool shouldJump{};
    if (!state->obstacleBlocked) StartClock(state->obstacleRecovery, 6.0f);
    else if (gpGlobals->time > state->obstacleRecovery.deadline)
    {
        StartClock(state->obstacleRecovery, 999.0f);
        shouldJump = true; // Timeout uses the previously prepared direction.
    }
    if (!shouldJump)
    {
        if (!g_engfuncs.pfnTraceLine || !CSOL_PrepareMonsterJumpDirection(call, state->position))
            return Step(call, state, false, "ordinary obstacle trace services/direction unavailable");
        const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
        if (!appearance) return Step(call, state, false, "ordinary obstacle appearance unavailable");
        Vector start = monster->pev->origin;
        start.z += appearance->feetZ;
        start.z += 3.0f;
        // Original entvars metadata names +F8 as maxs. Propensity0 uses only
        // maxs.x+15; the separate propensity3 branch adds attackRange.
        const float range = monster->pev->maxs.x + 15.0f;
        const Vector direction(state->position.jumpDirection.x, state->position.jumpDirection.y, 0);
        const Vector end = start + direction * range;
        if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z))
            return Step(call, state, false, "ordinary obstacle trace endpoint is not finite");
        TraceResult trace{};
        // MP11533770 is the six-argument LINE wrapper, not a hull trace.
        UTIL_TraceLine(start, end, ignore_monsters, dont_ignore_glass, monster->edict(), &trace);
        monster = Current(call, state);
        if (!monster) return false;
        if (!std::isfinite(trace.flFraction) || !std::isfinite(trace.vecPlaneNormal.z))
            return Step(call, state, false, "ordinary obstacle trace result is not finite");
        if (trace.flFraction < 1.0f && trace.vecPlaneNormal.z < 0.5f)
        {
            shouldJump = true;
            CBaseEntity* hit{};
            if (trace.pHit && !trace.pHit->free)
            {
                hit = CBaseEntity::Instance(trace.pHit);
                // The original wrapper resolves world index0 if the hit has
                // no private data. Preserve that case without touching old
                // map edicts after the engine callback.
                if (!hit && g_engfuncs.pfnPEntityOfEntIndex)
                {
                    auto* world = INDEXENT(0);
                    if (!Current(call, state)) return false;
                    if (world && !world->free) hit = CBaseEntity::Instance(world);
                }
            }
            if (hit)
            {
                EHANDLE obstacle(hit);
                const int classification = hit->Classify();
                monster = Current(call, state);
                if (!monster) return false;
                hit = obstacle.Get<CBaseEntity>();
                // CSO zbsbreak returns16 only when one damage ratio is nonzero.
                // Stock CBreakable is CLASS_NONE: don't broaden this to glass.
                if (classification == 16 && hit)
                {
                    if (!Stop(call, *state)) return false;
                    struct AttackContext
                    {
                        CCSOLOrdinaryMonsterBehavior* owner;
                        CSOLMonsterSpawnCall* call;
                        StatePtr state;
                    } context{this, &call, state};
                    const bool animated = CSOL_AnimateOrdinaryMonsterAttack(call, *state->spawn,
                        monster->HostState().enhancedUntil, state->deltaX, state->deltaY,
                        state->melee, [](const void* pointer)
                        {
                            const auto& context = *static_cast<const AttackContext*>(pointer);
                            return context.owner->Current(*context.call, context.state) != nullptr;
                        }, &context);
                    monster = Current(call, state);
                    if (!monster) return false;
                    if (!Step(call, state, animated, "ordinary obstacle attack animation failed")) return false;
                    hit = obstacle.Get<CBaseEntity>();
                    if (hit)
                    {
                        // This branch has no root309 suppression or enhanced
                        // +10 damage. A type with zero melee damage uses99999.
                        const float damage = originalDamage != 0 ? state->spawn->attributes.attackDamage : 99999.0f;
                        if (!std::isfinite(damage))
                            return Step(call, state, false, "ordinary obstacle damage is not finite");
                        hit->TakeDamage(monster->pev, monster->pev, damage, DMG_BULLET);
                        monster = Current(call, state);
                        if (!monster) return false;
                    }
                    state->obstacleBlocked = true;
                    state->lastObstacleOrigin = monster->pev->origin;
                    return true;
                }
            }
        }
    }

    if (shouldJump && !state->position.motion.climbSequence)
    {
        CSOL_StartMonsterJump(call, state->position, state->spawn->sequences);
        if (!Current(call, state)) return false;
        // Original ignores an active Jump cooldown, and still delays the
        // next obstacle check by one second.
        StartClock(state->melee.idleObstacleCheck, 1.0f);
    }
    state->obstacleBlocked = true;
    state->lastObstacleOrigin = Current(call, state)->pev->origin;
    return true;
}
