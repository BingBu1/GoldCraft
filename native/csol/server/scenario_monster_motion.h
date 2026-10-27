// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_follow.h"

enum class CSOLMonsterMotionResult { Applied, Cancelled, Failed };

// The ordinary movement tail after route/ladder steering. The XY accumulator
// survives ticks independently of pev->velocity: recovery and list effects
// change the entity's final velocity without feeding back into that accumulator.
CSOLMonsterMotionResult CSOL_CommitMonsterMotion(CSOLMonsterSpawnCall& call,
    CCSOLScenarioOccupancy& occupancy, const csol::scenario::MonsterAppearance& appearance,
    CSOLMonsterRouteMotion& motion, float deltaTime, float avoidanceUntil,
    int& moveFlags);

struct CSOLMonsterMotionStep
{
    CSOLMonsterFollowResult follow{CSOLMonsterFollowResult::Empty};
    CSOLMonsterMotionResult motion{CSOLMonsterMotionResult::Cancelled};
};

// Concrete route-to-entity update. The ordinary owner applies F4's update
// gates and jump branch before this call, and services queued animation after
// it. Path arrival/failure stops steering but still processes friction/effects.
CSOLMonsterMotionStep CSOL_AdvanceMonsterMotion(CSOLMonsterSpawnCall& call,
    CCSOLMonsterPathFollower& follower, const CCSOLScenarioNavWorld& world,
    CCSOLScenarioOccupancy& occupancy, const csol::scenario::MonsterAppearance& appearance,
    CNavArea* lastArea, CSOLMonsterRouteMotion& motion, float deltaTime,
    std::int16_t skill, bool avoidObstacles, float avoidanceUntil, int& moveFlags);
