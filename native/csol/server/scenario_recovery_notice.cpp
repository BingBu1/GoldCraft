// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_recovery_notice.h"
#include "scenario_monster_damage.h"
#include "scenario_runtime.h"
#include "damage_numbers.h"

#include <cmath>

bool CCSOLScenarioRecoveryNotice::NotifySelfRecovery(CSOLPlayerRecoveryCall& call,
    float amount, CSOLMonsterDeathContext& context, csol::scenario::ConfigError& error,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto runtime = m_runtime.lock();
    if (!runtime) return false;
    const auto notice = runtime->recoveryNotice; // Pin through message/virtual callbacks.
    const auto session = runtime->session;
    const auto players = runtime->players;
    const auto generation = runtime->Generation();
    const auto owned = [&]
    {
        return (!ownerValid || ownerValid(ownerContext)) && !runtime->Closing() &&
            runtime->Generation() == generation && runtime->recoveryNotice == notice &&
            notice.get() == this && session && runtime->session == session &&
            players && runtime->players == players && players->Active() &&
            session->ResourceScope() == CSOLScenarioResourceScope::Combat &&
            call.Runtime() == runtime && call.Current();
    };
    const auto fail = [&](const char* reason) { error.message = reason; return false; };
    if (!owned()) return false;
    if (!std::isfinite(amount) || !(amount > 0))
        return fail("self recovery notice requires a finite positive health delta");

    // Original v720 short-circuits on the room option, before reading MODE.
    if (!runtime->room.floatingDamageDisplay) return true;
    const auto modes = session->DamageDisplayModes();
    if (!modes) return fail("self recovery display modes are unavailable");
    // This owner belongs exclusively to the mode15 GameRules instance.
    if (!modes->Contains(15)) return true;

    auto* player = call.Player();
    if (!player) return true; // The original source-player gate declines it.
    const EHANDLE handle(player);
    const auto state = players->Find(handle);
    if (!state) return fail("self recovery player state is unavailable");
    const auto playerGeneration = state->generation;
    const auto current = [&]
    {
        // Validate the map and outer weapon before resolving any old handle.
        return owned() && players->Find(handle) == state &&
            state->generation == playerGeneration && call.Player() == player;
    };
    if (!current()) return call.Cancel();

    // The original 1152C520 prefix. Cancellation is separate from a normal
    // invalid-entity decision. Each invocation performs its own OFFSET query.
    const auto validEntity = [&](bool& valid)
    {
        valid = false;
        if (!current()) return call.Cancel();
        if (!player->pev || !player->pev->pContainingEntity) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
            return fail("self recovery entity-offset query is unavailable");
        const int offset = OFFSET(player->pev->pContainingEntity);
        if (!current()) return call.Cancel();
        valid = offset && player->pev && player->pev->pContainingEntity &&
            !(player->pev->flags & FL_DORMANT);
        return true;
    };

    bool valid{};
    if (!validEntity(valid)) return false;
    if (!valid) return true;
    // 1152FCB0 adds a second plain dormant read, then virtual IsPlayer.
    // Do not invent an extra callback between these two memory reads.
    if (player->pev->flags & FL_DORMANT) return true;
    const bool sourcePlayer = player->IsPlayer() != FALSE;
    if (!current()) return call.Cancel();
    if (!sourcePlayer) return true;

    // Keep truncation after the source gate. Unlike masked CVTTSS2SI, the
    // host rejects out-of-range input instead of sending the indefinite int.
    const auto value = CSOLTruncateDamageNumber(amount);
    if (!value) return fail("self recovery display delta exceeds signed32");
    const bool bot = player->IsBot() != FALSE;
    if (!current()) return call.Cancel();
    if (bot) return true;

    // v3EC checks target and source independently even for self recovery.
    if (!validEntity(valid)) return false;
    if (!valid) return true;
    if (!validEntity(valid)) return false;
    if (!valid) return true;
    const bool targetPlayer = player->IsPlayer() != FALSE;
    if (!current()) return call.Cancel();
    // A nonplayer result at this later virtual call permits the rule; it does
    // not reuse the earlier source-player decision or repeat its rejection.
    if (targetPlayer && state->profile.weaponEffectsBlocked21F2)
    {
        if (!state->recoveryDisplayState221B)
            return fail("self recovery special display state is unavailable");
        if (*state->recoveryDisplayState221B == 1)
        {
            if (!state->recoveryDisplayDeadline2F38)
                return fail("self recovery special display deadline is unavailable");
            if (!gpGlobals || !std::isfinite(gpGlobals->time))
                return fail("self recovery display clock is unavailable");
            if (*state->recoveryDisplayDeadline2F38 > gpGlobals->time)
            {
                bool allowed{};
                csol::scenario::ConfigError diagnostic;
                const bool queried = players->SpecialRecoveryDisplayAllowed(handle, allowed, diagnostic,
                    [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current);
                if (!current()) return call.Cancel();
                if (!queried)
                {
                    // This concrete query supplies diagnostics for every
                    // failure, including failed clones. Its empty failure
                    // exclusively means a retired directory/current node.
                    // Do not apply this contract to replaceable providers.
                    if (diagnostic.message.empty()) return call.Cancel();
                    error = std::move(diagnostic);
                    return false;
                }
                if (!allowed) return true;
            }
        }
    }
    if (context.bytes[0x1c]) return true;
    if (!g_engfuncs.pfnIndexOfEdict)
        return fail("self recovery entity-index query is unavailable");
    const int index = ENTINDEX(player->edict());
    if (!current()) return call.Cancel();
    if (index <= 0 || index > MAX_CLIENTS)
        return fail("self recovery player index is not representable");

    // Host safety adaptation: CSO queries ENTINDEX after starting its packet.
    // Resolve first here; once Begin fires, the writer uses scalar snapshots
    // and always completes the packet, even if a hook retires this owner.
    const bool sent = session->Messages().SendRecoveryNumber(player->edict(), index, *value);
    if (!current()) return call.Cancel();
    return sent || fail("self recovery number could not be sent");
}
