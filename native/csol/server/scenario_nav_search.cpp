// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from ReGameDLL_CS game_shared/bot/nav_area.h (GPL-2.0-or-later
// with its Half-Life Engine linking exception). CSO's mode15 edge cost and
// climber ladder restrictions replace the ordinary CS navigation policy.
#include "precompiled.h"
#include "scenario_nav_search.h"
#include "scenario_performance.h"

#include <algorithm>
#include <cmath>

namespace
{
bool Finite(const Vector& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

float DistanceSquared(const Vector& a, const Vector& b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    const float xy = y * y + x * x;
    return xy + z * z;
}

float Remaining(const CNavArea& area, const Vector& destination)
{
    return static_cast<float>(std::sqrt(static_cast<double>(
        DistanceSquared(*area.GetCenter(), destination))));
}
}

CSOLNavSearchResult CSOL_FindScenarioRoute(CNavArea* start, CNavArea* goal,
    const Vector& destination, bool climber,
    std::span<const CNavLadder* const> climberLadders, CNavArea*& closest)
{
    using namespace csol::scenario::perf;
    Scope performance(Timer::Search);
    using Result = CSOLNavSearchResult;
    closest = nullptr;
    if (!start || !Finite(destination) || !Finite(*start->GetCenter()) ||
        (goal && !Finite(*goal->GetCenter()))) return Result::Failed;

    start->SetParent(nullptr);
    if (start == goal)
    {
        closest = start;
        Add(Counter::SearchReached);
        return Result::Reached;
    }

    const float initialDistance = Remaining(*start, destination);
    if (!std::isfinite(initialDistance)) return Result::Failed;
    CNavArea::ClearSearchLists();
    start->SetTotalCost(initialDistance);
    start->SetCostSoFar(0);
    start->AddToOpenList();
    Add(Counter::SearchInsert);
    closest = start;
    float closestDistance = initialDistance;
    bool valid = true;

    while (!CNavArea::IsOpenListEmpty())
    {
        CNavArea* area = CNavArea::PopOpenList();
        Add(Counter::SearchExpanded);
        if (area == goal)
        {
            closest = goal;
            Add(Counter::SearchReached);
            return Result::Reached;
        }

        const auto visit = [&](CNavArea* next, NavTraverseType how)
        {
            Add(Counter::SearchEdges);
            if (!valid || !next || next == area) return;
            if (!Finite(*next->GetCenter())) { valid = false; return; }
            // MP 109347DB: squared centre distance, including ladder edges.
            // The heuristic below is linear distance. Do not replace this
            // deliberate mismatch with the stock hostage cost functor.
            const float cost = DistanceSquared(*next->GetCenter(), *area->GetCenter()) +
                area->GetCostSoFar();
            if (!std::isfinite(cost)) { valid = false; return; }
            if ((next->IsOpen() || next->IsClosed()) && next->GetCostSoFar() <= cost) return;
            const float remaining = Remaining(*next, destination);
            const float total = cost + remaining;
            if (!std::isfinite(remaining) || !std::isfinite(total)) { valid = false; return; }
            if (remaining < closestDistance)
            {
                closest = next;
                closestDistance = remaining;
            }
            next->SetParent(area, how);
            next->SetCostSoFar(cost);
            next->SetTotalCost(total);
            if (next->IsOpen()) { Add(Counter::SearchDecrease); next->UpdateOnOpenList(); }
            else { Add(Counter::SearchInsert); next->AddToOpenList(); }
        };

        // Preserve N/E/S/W list order, then ascending ladders, then descending.
        for (int direction = NORTH; direction < NUM_DIRECTIONS; ++direction)
            for (const auto& connection : *area->GetAdjacentList(static_cast<NavDirType>(direction)))
                visit(connection.area, static_cast<NavTraverseType>(direction));

        const auto restricted = [&](const CNavLadder* ladder)
        {
            return !climber && std::find(climberLadders.begin(), climberLadders.end(), ladder) !=
                climberLadders.end();
        };
        for (const auto* ladder : *area->GetLadderList(LADDER_UP))
        {
            if (!ladder) { valid = false; break; }
            if (ladder->m_isDangling) continue;
            if (!restricted(ladder))
            {
                visit(ladder->m_topForwardArea, GO_LADDER_UP);
                visit(ladder->m_topLeftArea, GO_LADDER_UP);
                visit(ladder->m_topRightArea, GO_LADDER_UP);
            }
            // MP's top-direction cursor is initialized once for the entire
            // up list, not once per ladder (the stock SDK also does this).
            // Even a climber-only first ladder consumes that cursor.
            break;
        }
        for (const auto* ladder : *area->GetLadderList(LADDER_DOWN))
        {
            if (!ladder) { valid = false; break; }
            // The original +41 gate is shared by both ladder directions.
            if (!restricted(ladder)) visit(ladder->m_bottomArea, GO_LADDER_DOWN);
        }
        if (!valid)
        {
            closest = nullptr;
            CNavArea::ClearSearchLists();
            return Result::Failed;
        }
        area->AddToClosedList();
    }
    // The caller builds a partial path through closest; lack of a connected
    // goal is not a fabricated direct line through the obstacle.
    Add(Counter::SearchPartial);
    return Result::Partial;
}
