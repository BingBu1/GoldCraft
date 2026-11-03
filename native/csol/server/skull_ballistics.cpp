// SPDX-License-Identifier: GPL-3.0-or-later
// Trace pipeline adapted from ReGameDLL_CS CBaseEntity::FireBullets3 (GPL-3.0).
#include "precompiled.h"
#include "skull_ballistics.h"
#include "../m249ex.hpp"
#include "../skull_ballistics.hpp"

bool EXT_FUNC IsPenetrableEntity_default(Vector& source, Vector& end,
    entvars_t* attacker, edict_t* hit);

Vector CSOL_Fire556AZ(CBasePlayer& player, const Vector& initialSource, const Vector& forward,
    float spread, int damage, float rangeModifier, unsigned seed)
{
    return CSOL_Fire556AZ(player, initialSource, forward, spread,
        csol::m249ex::Distance, csol::m249ex::Penetration, damage, rangeModifier, seed);
}

Vector CSOL_Fire556AZ(CBasePlayer& player, const Vector& initialSource, const Vector& forward,
    float spread, float distance, int penetration, int damage, float rangeModifier, unsigned seed)
{
    const float x = UTIL_SharedRandomFloat(seed, -.5f, .5f) + UTIL_SharedRandomFloat(seed + 1, -.5f, .5f);
    const float y = UTIL_SharedRandomFloat(seed + 2, -.5f, .5f) + UTIL_SharedRandomFloat(seed + 3, -.5f, .5f);
    const Vector direction = forward + gpGlobals->v_right * (x * spread) + gpGlobals->v_up * (y * spread);
    Vector source = initialSource;
    const int initialPenetration = penetration;
    int power = csol::skull_ballistics::PenetrationPower;
    float materialDamage = .5f;
    bool hitMetal = false;

    while (penetration > 0)
    {
        ClearMultiDamage();
        gMultiDamage.type = DMG_BULLET | DMG_NEVERGIB;
        gpGlobals->trace_flags = FTRACE_BULLET;
        Vector end = source + direction * distance;
        TraceResult trace;
        UTIL_TraceLine(source, end, dont_ignore_monsters, player.edict(), &trace);
        if (trace.flFraction == 1) break;
        auto* target = CBaseEntity::Instance(trace.pHit);
        if (!target) break;
        if (TheBots) TheBots->OnEvent(EVENT_BULLET_IMPACT, &player, reinterpret_cast<CBaseEntity*>(&trace.vecEndPos));

        switch (UTIL_TextureHit(&trace, source, end))
        {
        case CHAR_TEX_METAL: hitMetal = true; power = int(power * .15f); materialDamage = .2f; break;
        case CHAR_TEX_CONCRETE: power = int(power * .25f); break;
        case CHAR_TEX_GRATE: hitMetal = true; power = int(power * .5f); materialDamage = .4f; break;
        case CHAR_TEX_VENT: hitMetal = true; power = int(power * .5f); materialDamage = .45f; break;
        case CHAR_TEX_TILE: power = int(power * .65f); materialDamage = .3f; break;
        case CHAR_TEX_COMPUTER: hitMetal = true; power = int(power * .4f); materialDamage = .45f; break;
        case CHAR_TEX_WOOD: materialDamage = .6f; break;
        }
        const int level = initialPenetration - penetration;
        --penetration;
        const float travelled = trace.flFraction * distance;
        const int hitDamage = int(damage * Q_pow(rangeModifier, travelled / 500));
        if (travelled > csol::skull_ballistics::PenetrationDistance) penetration = 0;
        if (!g_ReGameHookchains.m_IsPenetrableEntity.callChain(IsPenetrableEntity_default,
            source, trace.vecEndPos, player.pev, trace.pHit)) penetration = 0;
        if (trace.iHitgroup == HITGROUP_SHIELD)
        {
            target->HitShield(hitDamage, &trace);
            break;
        }

        const bool brush = target->pev->solid == SOLID_BSP && penetration > 0;
        const int step = brush ? power : 42;
        // Decal classification is visual only. 556AZ shares rifle wall
        // decals, while the trace and damage above retain its own contract.
        DecalGunshot(&trace, BULLET_PLAYER_556MM, RANDOM_LONG(0, 3), player.pev, hitMetal);
        target->SetDmgPenetrationLevel(level);
        target->TraceAttack(player.pev, hitDamage, direction, &trace, DMG_BULLET | DMG_NEVERGIB);
        damage = brush ? int(hitDamage * materialDamage) : csol::skull_ballistics::EntityContinuationDamage(hitDamage);
        ApplyMultiDamage(player.pev, player.pev);
        source = trace.vecEndPos + direction * step;
        distance = (distance - travelled) * (brush ? .5f : .75f);
    }
    return Vector(x * spread, y * spread, 0);
}
