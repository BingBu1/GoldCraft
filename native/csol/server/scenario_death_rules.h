// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_requests.h"
#include "scenario_map.h"

struct CSOLMonsterDrainResult
{
    bool valid{};
    std::size_t selected{}, requested{};
    csol::scenario::ConfigError error;
};

// The synchronous death owner calls ApplyRule after its mm_zb_die target Use
// loop and before Golden/postDeath. This implements the table tail, not those
// surrounding phases. All three owners must outlive this object and its calls.
class CCSOLScenarioDeathRules
{
public:
    CCSOLScenarioDeathRules(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
        CCSOLMonsterDeathRequests& deaths) : m_map(map), m_pool(pool), m_deaths(deaths) {}

    // Filter is the manager's current signed-byte team filter; each slot reads
    // it again, including changes made by a nested synchronous death. Zero
    // means all teams. Limit counts selected slots, including skipped Bosses,
    // never confirmed kills. An empty scan lets the manager clear its pending
    // drain request; this method does not pretend to own that separate state.
    CSOLMonsterDrainResult Drain(std::int32_t limit, bool includeBosses,
        const std::int8_t& teamFilter);
    bool ApplyRule(std::int32_t actualType, const std::int8_t& teamFilter,
        csol::scenario::ConfigError& error);

private:
    struct Session
    {
        std::uint64_t map{}, round{}, deaths{};
    };
    Session Capture() const;
    bool Current(const Session& session) const;

    CCSOLScenarioMap& m_map;
    CCSOLMonsterPool& m_pool;
    CCSOLMonsterDeathRequests& m_deaths;
};
