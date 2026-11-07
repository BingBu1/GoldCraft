// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_recovery.h"
#include "scenario_monster_factory.h"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include <cmath>

bool CSOL_RecoverMonsterSelf(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSpawnCall& call, float amount, const CSOLMonsterDeathContext&,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto session = runtime ? runtime->session : nullptr;
    auto* const recipient = call.Current();
    const auto generation = runtime ? runtime->Generation() : 0;
    const auto current = [&]
    {
        return runtime && !runtime->Closing() && runtime->Generation() == generation &&
            session && runtime->session == session && call.BelongsTo(session->Map()) &&
            recipient && call.Current() == recipient;
    };
    const auto fail = [&](const char* message) { error.message = message; return false; };
    if (!error.message.empty() || !current()) return false;
    if (!std::isfinite(amount)) return fail("monster recovery amount is not finite");

    // 11429B50 -> 1140A9C0. Bits are exactly zero at 10967B41, so the
    // original bits&0x3FFF clearing changes no state. This is not a player
    // v50: there is no player recovery blocker or ability callback registry.
    if (recipient->pev->takedamage == DAMAGE_NO) return true;
    const float previous = recipient->pev->health;
    const float maximum = recipient->pev->max_health;
    if (!std::isfinite(previous) || !std::isfinite(maximum))
        return fail("monster recovery health state is not finite");
    if (previous >= maximum) return true;
    float health = previous + amount;
    if (health > maximum) health = maximum;
    if (!std::isfinite(health)) return fail("monster recovery health result is not finite");
    recipient->pev->health = health;
    const float delta = health - previous;
    if (!std::isfinite(delta)) return fail("monster recovery delta is not finite");
    if (!(delta > 0)) return true;

    // 1140AA40 checks the room/mode notification policy even for self heals.
    // It then rejects this nonplayer source in 1152FCB0, before converting
    // the delta or sending a 206 packet. No fabricated player notification.
    if (!runtime->room.floatingDamageDisplay) return true;
    const auto modes = session->DamageDisplayModes();
    if (!modes) return fail("monster recovery display modes are unavailable");
    if (!modes->Contains(15)) return true;
    // 106E3560 resolves the supplied self entvars, with ENT(0) for a null
    // containing edict. A missing private object falls back to the recipient.
    auto* sourceEdict = recipient->pev->pContainingEntity;
    if (!sourceEdict)
    {
        if (!g_engfuncs.pfnPEntityOfEntOffset)
            return fail("monster recovery world-entity query is unavailable");
        sourceEdict = ENT(0);
        if (!current()) return false;
    }
    auto* source = sourceEdict ? GET_PRIVATE<CBaseEntity>(sourceEdict) : nullptr;
    if (!source) source = recipient;
    if (!source->pev || !source->pev->pContainingEntity) return true;
    const EHANDLE sourceHandle(source);
    const auto sourceCurrent = [&]
    { return current() && sourceHandle.Get<CBaseEntity>() == source; };
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
        return fail("monster recovery entity-offset query is unavailable");
    const int offset = OFFSET(source->pev->pContainingEntity);
    if (!sourceCurrent()) return false;
    if (!offset || (source->pev->flags & FL_DORMANT)) return true;
    // The two original dormant calls are plain reads, with no callback gap.
    if (source->pev->flags & FL_DORMANT) return true;
    const bool sourceIsPlayer = source->IsPlayer() != FALSE;
    if (!sourceCurrent()) return false;
    // The concrete pool class inherits IsPlayer=false. A replaced virtual
    // source must not be reinterpreted as CBasePlayer to force a HUD packet.
    return !sourceIsPlayer || fail("monster self recovery source changed classification");
}
