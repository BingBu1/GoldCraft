// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_requests.h"
#include "scenario_map.h"
#include "scenario_messages.h"
#include "../monsters/scenario_monster_death.h"

struct CSOLMonsterEffectGuard;
// MP114093D0 is also used before pool eviction, without executing a kill.
// The caller pins owners and supplies its current entity transaction.
bool CSOL_SendMonsterDeathNotice(const CCSOLScenarioMessages& messages,
    const CSOLMonsterEffectGuard& guard, csol::scenario::ConfigError& error);

// The synchronous death owner calls this once, before beforeDeath/Animate,
// including silent deaths and deaths without an attacker. It retains the
// map/round/request generations from death capture and validates them before
// entering Send; these local guards cover callbacks made during this traversal.
// All referenced owners and the captured stages must outlive those callbacks.
class CCSOLScenarioDeathNotice
{
public:
    CCSOLScenarioDeathNotice(CCSOLScenarioMap& map, CCSOLMonsterDeathRequests& requests,
        const CCSOLScenarioMessages& messages)
        : m_map(map), m_requests(requests), m_messages(messages) {}

    bool Send(CCSOLMonsterDeathStages& death, csol::scenario::ConfigError& error);

private:
    CCSOLScenarioMap& m_map;
    CCSOLMonsterDeathRequests& m_requests;
    const CCSOLScenarioMessages& m_messages;
};
