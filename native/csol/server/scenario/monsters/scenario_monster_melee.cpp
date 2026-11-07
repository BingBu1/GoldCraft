// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_melee.h"
#include "scenario_monster_heading.h"

#include <cmath>

bool CSOL_AnimateOrdinaryMonsterAttack(CSOLMonsterSpawnCall& call,
    const CSOLMonsterSpawnState& state, const float& boostUntil,
    float deltaX, float deltaY, CSOLMonsterMeleeClocks& clocks,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        if (ownerValid && !ownerValid(ownerContext)) return nullptr;
        auto* monster = call.Current();
        return monster && monster->pev->deadflag != DEAD_DEAD &&
            monster->Pool()->Borrowed(*monster) ? monster : nullptr;
    };
    auto* monster = current();
    if (!monster || !gpGlobals || !state.lease || monster->Pool()->Lease(*monster) != state.lease)
        return false;
    struct ResetContext
    {
        CSOLMonsterSpawnCall* call;
        bool (*valid)(const void*);
        const void* context;
    } resetContext{&call, ownerValid, ownerContext};
    const bool animated = CSOL_AnimateMonsterMelee(*monster, monster->Animation(), state.sequences,
        deltaX, deltaY, gpGlobals->time, [](void* context)
        {
            const auto& reset = *static_cast<ResetContext*>(context);
            return CSOL_ResetMonsterSequenceInfo(*reset.call, reset.valid, reset.context);
        }, &resetContext);
    if (!current() || !animated) return false;
    // MP 113EBE87 reads interval/boost/time only after SetSequence returns.
    float interval = state.attributes.attackInterval;
    if (boostUntil > gpGlobals->time)
    {
        const float reduced = interval - 0.5f;
        interval = 0.4f > reduced ? 0.4f : reduced;
    }
    const float clockNow = gpGlobals->time;
    const float deadline = clockNow + interval;
    if (!std::isfinite(interval) || !std::isfinite(clockNow) || !std::isfinite(deadline)) return false;
    clocks.attack = {interval, deadline, clockNow};
    clocks.avoidance = {interval, deadline, clockNow};
    clocks.idleObstacleCheck = {interval, deadline, clockNow};
    return true;
}

CSOLMonsterMeleeResult CSOL_TryOrdinaryMonsterMelee(CSOLMonsterSpawnCall& call,
    const CSOLMonsterSpawnState& state, const EHANDLE& target,
    const float& boostUntil,
    const bool& damageSuppressed, float deltaX, float deltaY,
    CSOLMonsterMeleeClocks& clocks,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    using Result = CSOLMonsterMeleeResult;
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        if (ownerValid && !ownerValid(ownerContext)) return nullptr;
        auto* monster = call.Current();
        return monster && monster->pev->deadflag != DEAD_DEAD &&
            monster->Pool()->Borrowed(*monster) ? monster : nullptr;
    };
    auto* monster = current();
    if (!monster || !gpGlobals) return Result::Cancelled;
    if (monster->Type()->propensity != 0 || !state.lease ||
        monster->Pool()->Lease(*monster) != state.lease || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(boostUntil)) return Result::Failed;
    const float now = gpGlobals->time;
    if (state.skill == 6 || monster->Type()->attackDamage == 0)
    {
        clocks.attack = {99999.0f, now + 99999.0f, now};
        return Result::Miss;
    }
    if (!(now > clocks.attack.deadline)) return Result::Miss;
    clocks.attack = {0.3f, now + 0.3f, now};
    auto* victim = target.Get<CBaseEntity>();
    if (!victim || !(victim->pev->health > 0)) return Result::Miss;

    // MP player, CMonster and CZSFollower v128 all resolve to 10612440,
    // which returns their entvars origin reference, not Center or eyes.
    const Vector position = victim->pev->origin;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) return Result::Failed;
    const float dx = position.x - monster->pev->origin.x;
    const float dy = position.y - monster->pev->origin.y;
    const float distance = dy * dy + dx * dx;
    const float rangeSquared = state.attributes.attackRange * state.attributes.attackRange;
    if (!std::isfinite(distance) || !std::isfinite(rangeSquared)) return Result::Failed;
    if (!(rangeSquared >= distance)) return Result::Miss;
    const float bottom = monster->pev->origin.z + monster->pev->mins.z;
    const float top = monster->pev->origin.z + monster->pev->maxs.z;
    const float victimTop = victim->pev->origin.z + victim->pev->maxs.z;
    const float victimHeight = victim->pev->maxs.z - victim->pev->mins.z;
    if (!(victimTop > bottom - 5.0f && victimTop < (victimHeight + top) + 5.0f)) return Result::Miss;
    const bool player = victim->IsPlayer() != FALSE;
    monster = current();
    if (!monster) return Result::Cancelled;
    victim = target.Get<CBaseEntity>();
    if (!victim) return Result::Miss;
    if (player && victim->pev->rendermode == kRenderTransAlpha && victim->pev->renderamt < 1.0f)
        return Result::Miss;
    bool facing{};
    if (!CSOL_MonsterFacingTarget(monster->pev->angles.y, dx, dy, facing)) return Result::Failed;
    if (!facing) return Result::Miss;
    const bool animated = CSOL_AnimateOrdinaryMonsterAttack(call, state, boostUntil,
        deltaX, deltaY, clocks, ownerValid, ownerContext);
    monster = current();
    if (!monster) return Result::Cancelled;
    if (!animated) return Result::Failed;
    // Re-read the rule flag after animation hooks, as the original does.
    if (damageSuppressed) return Result::Hit;
    victim = target.Get<CBaseEntity>();
    if (!victim) return Result::Miss;
    float damage = state.attributes.attackDamage;
    if (boostUntil > gpGlobals->time) damage += 10.0f;
    if (!std::isfinite(damage)) return Result::Failed;
    // The original default-context wrapper reaches this virtual damage call
    // in the same update; no delayed animation event or generic radius hit.
    victim->TakeDamage(monster->pev, monster->pev, damage, DMG_BULLET);
    return current() ? Result::Hit : Result::Cancelled;
}
