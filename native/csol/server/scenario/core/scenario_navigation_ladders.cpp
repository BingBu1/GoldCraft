// SPDX-License-Identifier: GPL-3.0-or-later
// Based on ReGameDLL_CS BuildLadders (GPL-2.0-or-later with the Half-Life
// Engine linking exception), using CSO ground queries and climber marking.
#include "precompiled.h"
#include "scenario_navigation.h"

#include <array>
#include <cmath>

namespace
{
bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float Length(const Vector& value)
{
    const float xy = value.x * value.x + value.y * value.y;
    return static_cast<float>(std::sqrt(static_cast<double>(xy + value.z * value.z)));
}
}

bool CCSOLScenarioNavigation::BuildLadders(csol::scenario::ConfigError& error)
{
    error = {};
    if (m_building) { error = {0, "scenario ladder build is already active"}; return false; }
    if (m_laddersBuilt) return m_world.Ready();
    if (!m_world.Ready() || !gpGlobals || !gpGlobals->pStringBase ||
        !g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnTraceLine)
    {
        error = {0, "scenario ladder map or engine services are unavailable"};
        return false;
    }
    struct Building
    {
        bool& value;
        explicit Building(bool& busy) : value(busy) { value = true; }
        ~Building() { value = false; }
    } building(m_building);
    const auto generation = m_world.Generation();
    const auto current = [&]() { return generation == m_world.Generation() && m_world.Ready(); };
    const auto fail = [&](const char* reason)
    {
        error = {0, reason};
        Invalidate();
        return false;
    };
    edict_t* cursor{};
    while (true)
    {
        auto* edict = FIND_ENTITY_BY_STRING(cursor, "classname", "func_ladder");
        if (!current()) return fail("scenario navigation changed during ladder enumeration");
        if (!edict || FNullEnt(edict) || edict->free) break;
        auto* entity = CBaseEntity::Instance(edict);
        if (!entity) break;
        EHANDLE handle(entity);
        auto ladder = std::make_unique<CNavLadder>();
        const auto& vars = *entity->pev;
        if (!Finite(vars.absmin) || !Finite(vars.absmax)) return fail("invalid ladder bounds");
        ladder->m_top = {(vars.absmin.x + vars.absmax.x) / 2.0f,
            (vars.absmin.y + vars.absmax.y) / 2.0f, vars.absmax.z};
        ladder->m_bottom = {ladder->m_top.x, ladder->m_top.y, vars.absmin.z};
        ladder->m_isDangling = false;
        const auto trace = [&](const Vector& start, const Vector& end, TraceResult& result)
        {
            if (!current() || !handle.Get<CBaseEntity>() || !Finite(start) || !Finite(end)) return false;
            UTIL_TraceLine(start, end, ignore_monsters, dont_ignore_glass, edict, &result);
            return current() && handle.Get<CBaseEntity>() && std::isfinite(result.flFraction);
        };
        TraceResult result{};
        if (vars.absmax.x - vars.absmin.x > vars.absmax.y - vars.absmin.y)
        {
            if (!trace(ladder->m_bottom + Vector(0, 25, 25), ladder->m_top + Vector(0, 25, -25), result))
                return fail("scenario ladder facing trace cancelled");
            ladder->m_dir = result.flFraction == 1.0f && !result.fStartSolid ? SOUTH : NORTH;
        }
        else
        {
            if (!trace(ladder->m_bottom + Vector(25, 0, 25), ladder->m_top + Vector(25, 0, -25), result))
                return fail("scenario ladder facing trace cancelled");
            ladder->m_dir = result.flFraction == 1.0f && !result.fStartSolid ? EAST : WEST;
        }
        Vector along = ladder->m_top - ladder->m_bottom;
        const float length = Length(along);
        if (!std::isfinite(length)) return fail("invalid ladder length");
        along = length > 0 ? along / length : Vector(0, 0, 1);
        for (int endpoint = 0; endpoint < 2; ++endpoint)
        {
            for (float distance = 0; distance <= length; distance += 10.0f)
            {
                const Vector on = endpoint ? ladder->m_top - along * distance : ladder->m_bottom + along * distance;
                Vector out = on;
                AddDirectionVector(&out, ladder->m_dir, 32.0f);
                if (!trace(on, out, result)) return fail("scenario ladder clearance trace cancelled");
                if (result.flFraction == 1.0f && !result.fStartSolid)
                {
                    (endpoint ? ladder->m_top : ladder->m_bottom) = on;
                    break;
                }
                if (distance + 10.0f <= distance) return fail("ladder clearance distance overflow");
            }
        }
        ladder->m_length = Length(ladder->m_top - ladder->m_bottom);
        if (!std::isfinite(ladder->m_length)) return fail("invalid adjusted ladder length");
        DirectionToVector2D(ladder->m_dir, &ladder->m_dirVector);
        ladder->m_entity = handle.Get<CBaseEntity>();
        Vector center = ladder->m_bottom + Vector(0, 0, 25);
        AddDirectionVector(&center, ladder->m_dir, 16.0f);
        ladder->m_bottomArea = m_world.Nearest(center, true, false);
        if (!current() || !handle.Get<CBaseEntity>()) return fail("scenario ladder bottom query cancelled");
        center = ladder->m_top + Vector(0, 0, 25);
        AddDirectionVector(&center, ladder->m_dir, 16.0f);
        const std::array directions{OppositeDirection(ladder->m_dir), DirectionLeft(ladder->m_dir),
            DirectionRight(ladder->m_dir), ladder->m_dir};
        const std::array destinations{&ladder->m_topForwardArea, &ladder->m_topLeftArea,
            &ladder->m_topRightArea, &ladder->m_topBehindArea};
        for (std::size_t side = 0; side < directions.size(); ++side)
        {
            const float range = side == 3 ? 150.0f : 75.0f;
            for (float distance = 25; distance <= range; distance += 25)
            {
                Vector point = center;
                AddDirectionVector(&point, directions[side], distance);
                if (!trace(center, point, result)) return fail("scenario ladder top query cancelled");
                if (result.flFraction != 1.0f) break;
                if (auto* area = m_world.AreaAt(point, 120.0f))
                {
                    *destinations[side] = area == ladder->m_bottomArea ? nullptr : area;
                    break;
                }
            }
        }
        float topZ = -99999.9f;
        bool adjusted{};
        for (auto** destination : destinations)
            if (auto* area = *destination)
            {
                Vector closest;
                area->GetClosestPointOnArea(&ladder->m_top, &closest);
                if (closest.z > topZ) { topZ = closest.z; adjusted = true; }
            }
        if (adjusted) ladder->m_top.z = topZ;
        // Original does not recompute length after this top-area adjustment.
        if (ladder->m_bottomArea)
        {
            Vector closest;
            ladder->m_bottomArea->GetClosestPointOnArea(&ladder->m_bottom, &closest);
            ladder->m_isDangling = ladder->m_bottom.z - closest.z > 72.0f;
        }
        entity = handle.Get<CBaseEntity>();
        if (!current() || !entity || !gpGlobals || !gpGlobals->pStringBase)
            return fail("scenario ladder entity expired before registration");
        const bool climber = FStrEq(STRING(entity->pev->targetname), "climber");
        auto* registered = ladder.get();
        // Publish only after the trace callbacks have finished for this ladder.
        m_ladders.push_back(std::move(ladder));
        if (registered->m_bottomArea) registered->m_bottomArea->AddLadderUp(registered);
        for (auto** destination : destinations)
            if (*destination) (*destination)->AddLadderDown(registered);
        if (climber) m_climbers.push_back(registered);
        cursor = edict;
    }
    m_laddersBuilt = true;
    return true;
}
