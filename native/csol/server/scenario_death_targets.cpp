// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_targets.h"
#include "../scenario/death_rules.hpp"

#include <cstdio>

bool CCSOLScenarioDeathTargets::Fire(CCSOLMonsterDeathStages& death,
    csol::scenario::ConfigError& error)
{
    error = {};
    const auto mapGeneration = m_map.SpawnPoints().Generation();
    const auto roundGeneration = m_map.RoundGeneration();
    const auto requestGeneration = m_requests.Generation();
    const auto current = [&]
    {
        // A callback may release the entire old edict array. Check the owner
        // generations before DeathStages resolves its retained entity handle.
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
    if (!current()) return fail("scenario death targets require the current death and round");
    const auto* monster = death.Current();
    const auto suffix = csol::scenario::MonsterDeathTarget(*monster->Type(), monster->TypeId());
    if (!suffix) return true;
    if (!gpGlobals || !g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnIndexOfEdict)
        return fail("scenario death target lookup is unavailable");

    // The original constructs this name once. Target callbacks may change the
    // monster type; neither that change nor nested deaths rewrites this name.
    char name[32];
    const int length = std::snprintf(name, sizeof(name), "mm_zb_die_%d", static_cast<int>(*suffix));
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name))
        return fail("scenario death target name is too long");

    static_assert(USE_SET == 2 && USE_TOGGLE == 3);
    edict_t* previous = nullptr;
    int previousIndex = 0;
    for (;;)
    {
        if (!current()) return fail("scenario changed during death target traversal");
        auto* found = FIND_ENTITY_BY_STRING(previous, "targetname", name);
        if (!current()) return fail("scenario changed during death target lookup");
        if (!found) return true;
        const int index = ENTINDEX(found);
        if (!current()) return fail("scenario changed while resolving death target index");
        if (index == 0) return true; // Engine world slot is the end sentinel.
        if (index <= previousIndex || index >= gpGlobals->maxEntities || found->free)
            return fail("scenario death target lookup returned an invalid entity order");
        auto* target = CBaseEntity::Instance(found);
        // Match the original wrapper: a matching edict without a private
        // object ends this traversal instead of skipping ahead to later slots.
        if (!target) return true;

        // Direct virtual Use preserves the verified null/null/SET/zero tuple.
        // DispatchUse would replace it with the host's USE_TOGGLE instead.
        target->Use(nullptr, nullptr, USE_SET, 0.0f);
        if (!current()) return fail("scenario changed in a death target callback");
        // Keep only the edict-array slot, not the target's private object. The
        // engine search resumes after this slot even if Use removed/replaced
        // its entity. INDEXENT(index) filters free slots and would restart the
        // search at zero; never use it to reconstruct this cursor.
        previous = found;
        previousIndex = index;
    }
}
