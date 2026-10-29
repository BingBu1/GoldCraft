// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-actions/fixture.hpp"
#include "../../native/csol/server/scenario_monster_position.h"

namespace position_test
{
using actions_test::Check;
using actions_test::Require;
using actions_test::Equal;
using actions_test::Hook;
using actions_test::TraceCall;
using R = CSOLMonsterPositionResult;
inline bool Near(float a, float b) { return std::abs(a - b) < .0001f; }

struct NoClear final : CCSOLSpawnClearDeaths
{
    bool BeforeSpawnClear(CCSOLScenarioMonster&) override
    { throw std::runtime_error("unexpected spawn-clear operation in position test"); }
    bool RequestSpawnClearDeath(CCSOLScenarioMonster&) override
    { throw std::runtime_error("unexpected death request in position test"); }
};

// Reuse actual SDK animation hooks, edicts, map, leases, NAV storage and
// traces. Neighbor forces already have a dedicated verified suite; these
// cases keep their deadline in the future to isolate the F4 contract.
struct Fixture : actions_test::Fixture
{
    NoClear noClear;
    CCSOLScenarioOccupancy occupancy{pool, noClear};
    CSOLMonsterPositionState position;
    csol::scenario::MonsterSequences sequences;
    Fixture();
    ~Fixture();
    void OneArea();
    void LongRoute(bool build = false);
    R Update(float deltaTime = 0.0f, float hostMoveAfter = -1.0f);
    bool Jump();
};

void SchedulingCases();
void RouteCases();
void JumpCases();
void CancellationCases();
void LadderCases();
}
