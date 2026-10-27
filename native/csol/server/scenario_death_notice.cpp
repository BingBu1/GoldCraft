// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_notice.h"

bool CCSOLScenarioDeathNotice::Send(CCSOLMonsterDeathStages& death,
    csol::scenario::ConfigError& error)
{
    error = {};
    const auto mapGeneration = m_map.SpawnPoints().Generation();
    const auto roundGeneration = m_map.RoundGeneration();
    const auto requestGeneration = m_requests.Generation();
    const auto current = [&]
    {
        // A map callback can release the old edict array. Compare generations
        // before resolving any handle retained by the death stages.
        return m_map.SpawnPoints().Generation() == mapGeneration &&
            m_map.RoundGeneration() == roundGeneration &&
            m_requests.Generation() == requestGeneration &&
            m_map.State() == CSOLScenarioMapState::RoundActive &&
            m_requests.Error().empty() && death.Current();
    };
    const auto fail = [&](const char* reason)
    {
        error.message = reason;
        return false;
    };
    if (!current()) return fail("scenario death notice requires the current death and round");
    if (!gpGlobals || !g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnIndexOfEdict ||
        !g_engfuncs.pfnEntOffsetOfPEntity)
        return fail("scenario death notice player lookup is unavailable");

    const int monsterIndex = ENTINDEX(death.Current()->edict());
    if (!current()) return fail("scenario changed while resolving death notice entity");
    if (monsterIndex <= 0 || monsterIndex > 0xffff)
        return fail("scenario death notice entity index is not representable");

    for (int slot = 1;; ++slot)
    {
        if (!current()) return fail("scenario changed during death notice traversal");
        // Re-read the upper bound, as the original does after each recipient.
        // Reject corrupt host capacities instead of querying outside the array.
        if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > MAX_CLIENTS ||
            gpGlobals->maxClients >= gpGlobals->maxEntities)
            return fail("scenario death notice has an invalid client capacity");
        if (slot > gpGlobals->maxClients) return true;

        auto* recipient = INDEXENT(slot);
        if (!current()) return fail("scenario changed while resolving death notice recipient");
        if (!recipient || recipient->free) continue;
        auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(recipient));
        if (!player || !player->pev || player->edict() != recipient) continue;
        EntityHandle<CBasePlayer> handle = player;
        const int offset = OFFSET(recipient);
        if (!current()) return fail("scenario changed while resolving death notice recipient offset");
        // Zero is a skipped recipient in the original, not a failed traversal.
        // The engine query can also remove/replace this player in a hook.
        if (!offset || handle.Get<CBaseEntity>() != player) continue;
        // No alive, team, dormant, attacker or ordinary-sound filter belongs
        // here. Each valid client gets a separate MSG_ONE notification.
        const bool sent = m_messages.WriteMonsterDeath(recipient, monsterIndex);
        if (!current()) return fail("scenario changed in a death notice callback");
        if (!sent) return fail("scenario death notice packet could not be sent");
    }
}
