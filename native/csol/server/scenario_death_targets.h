// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_requests.h"
#include "scenario_map.h"
#include "scenario_monster_death.h"

// The synchronous owner calls this after the ordinary death state/effects and
// before ApplyRule. It must re-read actualType for that table after all Uses.
// The map, dispatcher and captured death stages outlive every target callback.
// The owner also retains the map/round/request generations from death capture
// and checks them before entering Fire. The local guards below cover callbacks
// made by Fire; they cannot validate a stale stage handed in by its caller.
class CCSOLScenarioDeathTargets
{
public:
    CCSOLScenarioDeathTargets(CCSOLScenarioMap& map, CCSOLMonsterDeathRequests& requests)
        : m_map(map), m_requests(requests) {}

    bool Fire(CCSOLMonsterDeathStages& death, csol::scenario::ConfigError& error);

private:
    CCSOLScenarioMap& m_map;
    CCSOLMonsterDeathRequests& m_requests;
};
