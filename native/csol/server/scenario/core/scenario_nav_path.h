// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Include the actual game SDK/precompiled.h first, as for nav_path.h itself.
#include "game_shared/bot/nav_path.h"
#include "scenario_nav_search.h"
#include <array>
#include <cstdint>
#include <span>

class CCSOLScenarioNavWorld;

// Build movement positions for a resolved area corridor, before appending the
// final goal. The final slot is reserved for that goal, as in CNavPath::Compute.
// Uses actual SDK segment/area/ladder objects. On failure count is invalidated;
// partially written positions are not a usable path. Does not run A* or move
// an entity, and does not modify the SDK's ordinary CS path policy.
bool CSOL_ComputeScenarioPathPositions(std::span<CNavPath::PathSegment, 256> path,
    int& count);

struct CSOLScenarioRoute
{
    std::array<CNavPath::PathSegment, 256> segments{};
    int count{};
    std::uint64_t generation{};
    CSOLNavSearchResult search{CSOLNavSearchResult::Failed};
    void Invalidate() { count = 0; generation = 0; search = CSOLNavSearchResult::Failed; }
};

// Original world queries -> graph search -> portal positions -> endpoint.
// Uses the map's CSO query policy, never the stock nearest-area fallback.
// Ground/trace callbacks can reset the world; no old area is read afterwards.
bool CSOL_BuildScenarioRoute(CSOLScenarioRoute& route, const CCSOLScenarioNavWorld& world,
    const Vector& start, const Vector& goal, bool climber,
    std::span<const CNavLadder* const> climberLadders);
