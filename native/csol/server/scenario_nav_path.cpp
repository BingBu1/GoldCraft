// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from ReGameDLL_CS game_shared/bot/nav_path.cpp (GPL-2.0-or-later
// with the upstream Half-Life Engine linking exception), with CSO mode 15's
// portal policy. Original source and license are retained in external/ReGameDLL_CS.
#include "precompiled.h"
#include "scenario_nav_path.h"
#include "scenario_nav_portal.h"
#include "scenario_nav_world.h"
#include "scenario_performance.h"

#include <cmath>

namespace
{
bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

bool CSOL_ComputeScenarioPathPositions(std::span<CNavPath::PathSegment, 256> path, int& count)
{
    const auto fail = [&count]() { count = 0; return false; };
    if (count < 1 || count > 255) return fail();
    for (int i = 0; i < count; ++i)
    {
        if (!path[i].area || !Finite(*path[i].area->GetCenter())) return fail();
        if (i && (path[i].how < GO_NORTH || path[i].how > GO_LADDER_DOWN)) return fail();
    }

    path[0].pos = *path[0].area->GetCenter();
    path[0].ladder = nullptr;
    path[0].how = NUM_TRAVERSE_TYPES;
    for (int i = 1; i < count; ++i)
    {
        const auto& from = path[i - 1];
        auto& to = path[i];
        if (to.how <= GO_WEST)
        {
            to.ladder = nullptr;
            if (!CSOL_ComputeScenarioPortal(*from.area, *to.area, to.how, to.pos)) return fail();
            AddDirectionVector(&to.pos, static_cast<NavDirType>(to.how), 5.0f);
            to.pos.z = from.area->GetZ(&to.pos);
            if (!Finite(to.pos)) return fail();

            // IsConnected(NUM_DIRECTIONS) also considers ladders and self;
            // checking only the reverse compass edge would change drop paths.
            if (!to.area->IsConnected(from.area, NUM_DIRECTIONS))
            {
                Vector2D direction;
                DirectionToVector2D(static_cast<NavDirType>(to.how), &direction);
                to.pos.x += 25.0f * direction.x;
                to.pos.y += 25.0f * direction.y;
                if (!Finite(to.pos)) return fail();
                if (count < 255)
                {
                    for (int j = count; j > i; --j) path[j] = path[j - 1];
                    ++count;
                    ++i;
                    path[i].pos.x = to.pos.x + 25.0f * direction.x;
                    path[i].pos.y = to.pos.y + 25.0f * direction.y;
                    path[i].pos.z = to.area->GetZ(&path[i].pos);
                    if (!Finite(path[i].pos)) return fail();
                }
            }
        }
        else if (to.how == GO_LADDER_UP)
        {
            to.ladder = nullptr;
            for (const auto* ladder : *from.area->GetLadderList(LADDER_UP))
            {
                if (!ladder) return fail();
                // The behind area is not an ascending exit in the original.
                if (ladder->m_topForwardArea != to.area && ladder->m_topLeftArea != to.area &&
                    ladder->m_topRightArea != to.area)
                    continue;
                if (ladder->m_dir < NORTH || ladder->m_dir > WEST) return fail();
                to.ladder = ladder;
                to.pos = ladder->m_bottom;
                AddDirectionVector(&to.pos, ladder->m_dir, 32.0f);
                break;
            }
            if (!to.ladder || !Finite(to.pos)) return fail();
        }
        else
        {
            to.ladder = nullptr;
            for (const auto* ladder : *from.area->GetLadderList(LADDER_DOWN))
            {
                if (!ladder) return fail();
                if (ladder->m_bottomArea != to.area) continue;
                if (ladder->m_dir < NORTH || ladder->m_dir > WEST) return fail();
                to.ladder = ladder;
                to.pos = ladder->m_top;
                AddDirectionVector(&to.pos, OppositeDirection(ladder->m_dir), 32.0f);
                break;
            }
            if (!to.ladder || !Finite(to.pos)) return fail();
        }
    }
    return true;
}

bool CSOL_BuildScenarioRoute(CSOLScenarioRoute& route, const CCSOLScenarioNavWorld& world,
    const Vector& start, const Vector& goal, bool climber,
    std::span<const CNavLadder* const> climberLadders)
{
    csol::scenario::perf::Scope performance(csol::scenario::perf::Timer::Route);
    route.Invalidate();
    if (!Finite(start) || !Finite(goal)) return false;
    const auto generation = world.Generation();
    route.generation = generation;
    auto* startArea = world.Nearest(start);
    if (world.Generation() != generation || !startArea) { route.Invalidate(); return false; }
    auto* goalArea = world.AreaAt(goal);
    const auto fail = [&] { route.Invalidate(); return false; };
    const auto trivial = [&]
    {
        // BuildTrivialPath performs its own start and goal queries. Keep
        // their order and defer them until this branch actually executes.
        auto* firstArea = world.Nearest(start);
        if (world.Generation() != generation || !firstArea) return fail();
        auto* nearestGoal = world.Nearest(goal);
        if (world.Generation() != generation || !nearestGoal) return fail();
        Vector first = start;
        Vector last = goal;
        first.z = firstArea->GetZ(&first);
        last.z = nearestGoal->GetZ(&last);
        if (!Finite(first) || !Finite(last)) return fail();
        route.segments[0] = {firstArea, NUM_TRAVERSE_TYPES, first, nullptr};
        route.segments[1] = {nearestGoal, NUM_TRAVERSE_TYPES, last, nullptr};
        route.count = 2;
        return true;
    };
    if (startArea == goalArea)
    {
        route.search = CSOLNavSearchResult::Reached;
        return trivial();
    }

    Vector endpoint = goal;
    if (goalArea) endpoint.z = goalArea->GetZ(&endpoint);
    else
    {
        // A failed ground query keeps the original goal.z. Cancellation is
        // different: it invalidates every borrowed area in this operation.
        world.GroundHeight(endpoint, endpoint.z);
        if (world.Generation() != generation) return fail();
    }
    if (!Finite(endpoint)) return fail();
    CNavArea* closest{};
    route.search = CSOL_FindScenarioRoute(startArea, goalArea, goal, climber, climberLadders, closest);
    if (route.search == CSOLNavSearchResult::Failed || !closest) return fail();

    // Keep the last 255 areas nearest the effective goal when the corridor is
    // longer. The original caps count before walking backwards into storage;
    // truncating the goal end instead would describe a different route.
    int count = 0;
    for (auto* area = closest; area && count < 255; area = area->GetParent()) ++count;
    if (count == 1) return trivial();
    route.count = count;
    for (auto* area = closest; area && count; area = area->GetParent())
    {
        auto& segment = route.segments[--count];
        segment.area = area;
        segment.how = area->GetParentHow();
    }
    if (!CSOL_ComputeScenarioPathPositions(route.segments, route.count)) return fail();
    route.segments[route.count++] = {closest, NUM_TRAVERSE_TYPES, endpoint, nullptr};
    return true;
}
