// SPDX-License-Identifier: GPL-3.0-or-later
// Native engine boundary follows ReGameDLL_CS FireBullets3 (GPL-3.0).
// Original bullet28 evidence: docs/2026-11-05_分析-SKULL3射击弹道原证-report.md.
#include "precompiled.h"
#include "skull3_ballistics.h"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include <cmath>

bool EXT_FUNC IsPenetrableEntity_default(Vector& source, Vector& end,
    entvars_t* attacker, edict_t* hit);

Vector CSOL_Fire45ACPAZ(CBasePlayer& player, const Vector& initialSource, const Vector& forward,
    float spread, float distance, int penetration, int damage, float rangeModifier, unsigned seed)
{
    // 1141E98C: the original changes excessive absolute spread to positive 1.2.
    if (std::abs(spread) > 1.2f) spread = 1.2f;
    const float x1 = UTIL_SharedRandomFloat(seed + 1, -.5f, .5f);
    const float x = UTIL_SharedRandomFloat(seed, -.5f, .5f) + x1;
    const float y1 = UTIL_SharedRandomFloat(seed + 3, -.5f, .5f);
    const float y = UTIL_SharedRandomFloat(seed + 2, -.5f, .5f) + y1;
    const Vector direction = forward + gpGlobals->v_right * (x * spread) + gpGlobals->v_up * (y * spread);
    const Vector result(x * spread, y * spread, 0);
    // Host SDK/ReAPI callbacks may retire a map or release the shooter. These
    // tokens do not alter the original ballistic arithmetic; they stop work
    // owned by an operation that no longer exists after a plugin callback.
    CSOLScenarioSdkScope scope;
    const EHANDLE actor(&player), activeWeapon(player.m_pActiveItem);
    const bool hadWeapon = player.m_pActiveItem != nullptr;
    const int life = player.m_iNumSpawns;
    const auto runtime = scope.Scenario() ? CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get()) : nullptr;
    const auto round = runtime && runtime->session ? runtime->session->Map().RoundGeneration() : 0;
    const auto current = [&] {
        if (!scope.Current() || actor.Get<CBaseEntity>() != &player) return false;
        if (scope.Scenario() && (!runtime || runtime->Closing() || !runtime->session ||
            runtime->session->Map().RoundGeneration() != round)) return false;
        return !(player.pev->flags & (FL_KILLME | FL_DORMANT)) && !player.has_disconnected &&
            player.m_iNumSpawns == life && player.IsAlive() &&
            (!hadWeapon || (activeWeapon.Get<CBaseEntity>() && activeWeapon.Get<CBaseEntity>() == player.m_pActiveItem));
    };
    if (!current()) return result;
    Vector source = initialSource;
    const int initialPenetration = penetration;
    int power = 35;
    float penetrationDistance = 4000.f;
    float materialDamage = .5f;

    while (penetration > 0)
    {
        ClearMultiDamage();
        gMultiDamage.type = DMG_BULLET | DMG_NEVERGIB;
        // Native ReHLDS requires its bullet-trace flag for studio collision.
        gpGlobals->trace_flags = FTRACE_BULLET;
        Vector end = source + direction * distance;
        TraceResult trace;
        UTIL_TraceLine(source, end, dont_ignore_monsters, player.edict(), &trace);
        if (!current()) return result;
        if (trace.flFraction == 1.f) break;
        auto* target = CBaseEntity::Instance(trace.pHit);
        if (!target) break;
        const EHANDLE targetHandle(target);
        if (TheBots) TheBots->OnEvent(EVENT_BULLET_IMPACT, &player, reinterpret_cast<CBaseEntity*>(&trace.vecEndPos));
        if (!current() || targetHandle.Get<CBaseEntity>() != target) return result;

        // These modifiers persist across hits. Power uses the original double
        // constants before integer truncation; damage modifiers are floats.
        const auto material = UTIL_TextureHit(&trace, source, end);
        if (!current() || targetHandle.Get<CBaseEntity>() != target) return result;
        switch (material)
        {
        case 'Z': return result;
        case CHAR_TEX_METAL: power = int(double(power) * .15); materialDamage = .2f; break;
        case CHAR_TEX_CONCRETE: power = int(double(power) * .25); break;
        case CHAR_TEX_GRATE: power = int(double(power) * .5); materialDamage = .4f; break;
        case CHAR_TEX_VENT: power = int(double(power) * .5); materialDamage = .45f; break;
        case CHAR_TEX_TILE: power = int(double(power) * .65); materialDamage = .3f; break;
        case CHAR_TEX_COMPUTER: power = int(double(power) * .4); materialDamage = .45f; break;
        case CHAR_TEX_WOOD: materialDamage = .6f; break;
        }
        // Original 1141F1BA still calls the target's TraceAttack. In particular,
        // a non-player hitgroup 8 must not take the native HitShield shortcut.
        if (trace.iHitgroup == 8 || trace.iHitgroup == 9)
        {
            power = 0;
            penetrationDistance = materialDamage = 0.f;
        }

        const int level = initialPenetration - penetration;
        const float travelled = trace.flFraction * distance;
        const float exponent = travelled / 500.f;
        const float rangeDecay = float(std::pow(double(rangeModifier), double(exponent)));
        const int hitDamage = int(rangeDecay * float(damage));
        penetration = travelled <= penetrationDistance ? penetration - 1 : 0;
        // Additional host policy (including MC blockers), not the original
        // entity+70 field. Denying continuation never cancels this hit.
        if (!g_ReGameHookchains.m_IsPenetrableEntity.callChain(IsPenetrableEntity_default,
            source, trace.vecEndPos, player.pev, trace.pHit)) penetration = 0;
        if (!current() || targetHandle.Get<CBaseEntity>() != target) return result;

        const bool brush = target->pev->solid == SOLID_BSP && penetration > 0;
        source = trace.vecEndPos + direction * float(brush ? power : 42);
        distance = float(double(distance - travelled) * (brush ? .5 : .75));
        target->SetDmgPenetrationLevel(level);
        target->TraceAttack(player.pev, hitDamage, direction, &trace, DMG_BULLET | DMG_NEVERGIB);
        if (!current() || targetHandle.Get<CBaseEntity>() != target)
        {
            ClearMultiDamage();
            return result;
        }
        damage = brush ? int(float(hitDamage) * materialDamage) : int(double(hitDamage) * .75);
        ApplyMultiDamage(player.pev, player.pev);
        if (!current()) return result;
    }
    return result;
}
