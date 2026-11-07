// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_motion.h"

// Ordinary behavior-owned F4 state. Construction and pool reuse differ:
// routeTick starts at1 but survives reuse; ResetForSpawn invalidates only
// destination.x, clears planar motion, and preserves the jump/obstacle clocks.
// The concrete behavior owner supplies that reset order and the host gate.
struct CSOLMonsterPositionState
{
    CCSOLMonsterPathFollower follower;
    CSOLMonsterRouteMotion motion;
    Vector destination{}, faceGoal{};
    CNavArea* lastArea{};
    std::uint64_t worldGeneration{};
    std::uint32_t routeTick{1};
    // Host cancellation counter: nested F4 calls invalidate the outer call
    // even when they keep the same map and pool lease.
    std::uint64_t updateSerial{};
    bool updating{}, startingJump{};
    float updateAfter{-1.0f}, missingAreaUntil{-1.0f}, obstacleAfter{-1.0f};
    float updateInterval{}; // original ordinary +1D80; preserved on reuse
    float jumpUntil{-1.0f}, jumpStamp{-1.0f};
    float jumpInterval{};
    Vector2D jumpDirection{};
    bool faceRequested{}, jumpStarted{}, leftGround{};
    int moveFlags{};
};

enum class CSOLMonsterPositionResult { Skipped, Applied, Jumping, NavigationExpired, Cancelled, Failed };

// Ordinary F4: host/behavior gates -> area -> jump or face/path -> final speed.
// Uses the actual SDK area/route and entity velocity; does not advance origin
// in place of MOVETYPE_STEP. NAV and state outlive all callback invocations.
CSOLMonsterPositionResult CSOL_UpdateOrdinaryMonsterPosition(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world, CCSOLScenarioOccupancy& occupancy,
    const csol::scenario::MonsterAppearance& appearance, CSOLMonsterPositionState& state,
    float deltaTime, float hostMoveAfter, float avoidanceUntil,
    std::span<const CNavLadder* const> climberLadders);

// Direction preparation belongs to skill/obstacle actions. Jump itself does
// not retarget or impose an on-ground gate absent from the original.
bool CSOL_PrepareMonsterJumpDirection(CSOLMonsterSpawnCall& call, CSOLMonsterPositionState& state);
bool CSOL_StartMonsterJump(CSOLMonsterSpawnCall& call, CSOLMonsterPositionState& state,
    const csol::scenario::MonsterSequences& sequences);
