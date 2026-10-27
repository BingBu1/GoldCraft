// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_rules.h"

#include <cstdlib>

CCSOLScenarioDeathRules::Session CCSOLScenarioDeathRules::Capture() const
{
    return {m_map.SpawnPoints().Generation(), m_map.RoundGeneration(), m_deaths.Generation()};
}

bool CCSOLScenarioDeathRules::Current(const Session& session) const
{
    // Check generations before resolving any old edict or pooled object.
    return session.deaths == m_deaths.Generation() &&
        session.map == m_map.SpawnPoints().Generation() &&
        m_map.State() == CSOLScenarioMapState::RoundActive &&
        session.round == m_map.RoundGeneration() &&
        m_pool.State() == CSOLMonsterPoolState::Ready && !m_pool.Busy();
}

CSOLMonsterDrainResult CCSOLScenarioDeathRules::Drain(std::int32_t limit,
    bool includeBosses, const std::int8_t& teamFilter)
{
    CSOLMonsterDrainResult result;
    const auto session = Capture();
    const auto fail = [&](const char* message)
    {
        result.valid = false;
        result.error.message = message;
        return result;
    };
    if (!Current(session)) return fail("scenario death drain requires an active map and pool");
    if (!m_deaths.Error().empty()) return fail(m_deaths.Error().c_str());
    // Original decrements a dword after selecting a slot and only stops at
    // zero. Preserve wrap for zero/negative input without signed overflow.
    auto remaining = static_cast<std::uint32_t>(limit);
    for (std::size_t slot = 0; slot < CCSOLMonsterPool::Capacity; ++slot)
    {
        if (!Current(session)) return fail("scenario changed during death drain");
        auto* monster = m_pool.At(slot);
        if (!monster) return fail("scenario death drain lost a pool entity");
        if (monster->pev->solid == SOLID_NOT) continue;
        if (teamFilter != 0 && monster->pev->team != teamFilter) continue;
        const auto* type = monster->Type();
        if (!type) return fail("scenario death drain has no monster type");
        // This RNG call precedes the Boss skip, and is absent for priority!=0.
        const bool silent = type->priority != 0 || std::rand() % 2 != 0;
        ++result.selected;
        if (includeBosses || type->skill < 1000)
        {
            CSOLMonsterDeathArguments arguments;
            arguments.suppressSound = silent;
            ++result.requested;
            const auto status = m_deaths.Request(*monster, arguments);
            if (!Current(session)) return fail("scenario changed during requested monster death");
            if (status == CSOLDeathRequestResult::Failed)
            {
                result.error.message = m_deaths.Error();
                return result;
            }
        }
        if (--remaining == 0) break;
    }
    result.valid = true;
    return result;
}

bool CCSOLScenarioDeathRules::ApplyRule(std::int32_t actualType,
    const std::int8_t& teamFilter, csol::scenario::ConfigError& error)
{
    error = {};
    const auto session = Capture();
    if (!Current(session))
    {
        error.message = "scenario death rule requires an active map and pool";
        return false;
    }
    const auto* row = m_map.DeathRules().Find(actualType);
    if (!row) return true;
    // The original saves the stop byte before nested deaths; never keep a row
    // pointer across callbacks that can clear or reload the map's table.
    const bool drain = row->drainMonsters, stop = row->stopWaves;
    if (drain)
    {
        const auto result = Drain(static_cast<std::int32_t>(CCSOLMonsterPool::Capacity),
            false, teamFilter);
        if (!result.valid)
        {
            error = result.error;
            return false;
        }
    }
    if (!Current(session))
    {
        error.message = "scenario changed before death-rule wave stop";
        return false;
    }
    if (stop && !m_map.StopWaveDispatch())
    {
        error.message = "scenario could not stop wave dispatch";
        return false;
    }
    error = {};
    return true;
}
