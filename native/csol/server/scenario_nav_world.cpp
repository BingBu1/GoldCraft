// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_nav_world.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr float CellSize = 300.0f;
constexpr float GroundDepth = 9999.900390625f;
bool Finite(const Vector& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

int Coordinate(float value, float minimum, int size)
{
    const float cell = (value - minimum) / CellSize;
    // Clamp before the cast so finite but enormous input cannot overflow int.
    if (cell <= 0) return 0;
    if (cell >= static_cast<float>(size - 1)) return size - 1;
    return static_cast<int>(cell);
}

float DistanceSquared(const Vector& a, const Vector& b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    const float xy = x * x + y * y;
    return xy + z * z;
}
}

void CCSOLScenarioNavWorld::Reset()
{
    ++m_generation;
    m_cells.clear();
    m_sizeX = m_sizeY = 0;
    m_minX = m_minY = 0;
}

bool CCSOLScenarioNavWorld::Initialize(std::span<CNavArea* const> areas)
{
    Reset();
    if (areas.empty()) return false;
    float minX{}, maxX{}, minY{}, maxY{};
    bool first = true;
    for (const auto* area : areas)
    {
        if (!area) return false;
        const auto& extent = *area->GetExtent();
        // CSO NAV can retain reversed corners (zs_deadend area3148). Keep
        // their original order: its grid insertion loop may add no cells.
        if (!Finite(extent.lo) || !Finite(extent.hi)) return false;
        if (first)
        {
            minX = extent.lo.x; maxX = extent.hi.x;
            minY = extent.lo.y; maxY = extent.hi.y;
            first = false;
        }
        else
        {
            minX = std::min(minX, extent.lo.x); maxX = std::max(maxX, extent.hi.x);
            minY = std::min(minY, extent.lo.y); maxY = std::max(maxY, extent.hi.y);
        }
    }
    const float width = (maxX - minX) / CellSize + 1.0f;
    const float height = (maxY - minY) / CellSize + 1.0f;
    // A host allocation limit, not an original gameplay restriction.
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
        static_cast<double>(width) * static_cast<double>(height) > 1048576.0) return false;
    const int sizeX = static_cast<int>(width), sizeY = static_cast<int>(height);
    std::vector<std::vector<CNavArea*>> cells(static_cast<std::size_t>(sizeX) * sizeY);
    for (auto* area : areas)
    {
        const auto& extent = *area->GetExtent();
        const int lowX = Coordinate(extent.lo.x, minX, sizeX);
        const int highX = Coordinate(extent.hi.x, minX, sizeX);
        const int lowY = Coordinate(extent.lo.y, minY, sizeY);
        const int highY = Coordinate(extent.hi.y, minY, sizeY);
        for (int y = lowY; y <= highY; ++y)
            for (int x = lowX; x <= highX; ++x)
                cells[x + y * sizeX].push_back(area);
    }
    m_cells.swap(cells);
    m_minX = minX; m_minY = minY;
    m_sizeX = sizeX; m_sizeY = sizeY;
    return true;
}

int CCSOLScenarioNavWorld::GridX(float x) const { return Coordinate(x, m_minX, m_sizeX); }
int CCSOLScenarioNavWorld::GridY(float y) const { return Coordinate(y, m_minY, m_sizeY); }

CNavArea* CCSOLScenarioNavWorld::AreaAt(const Vector& point, float beneath) const
{
    if (m_cells.empty() || !Finite(point) || !std::isfinite(beneath)) return nullptr;
    CNavArea* selected{};
    float highest = -100000000.0f;
    for (auto* area : m_cells[GridX(point.x) + GridY(point.y) * m_sizeX])
    {
        const auto& extent = *area->GetExtent();
        if (point.x < extent.lo.x || point.x > extent.hi.x ||
            point.y < extent.lo.y || point.y > extent.hi.y) continue;
        const float z = area->GetZ(&point);
        if (z <= point.z + 5.0f && z >= point.z - beneath && z > highest)
        {
            highest = z;
            selected = area;
        }
    }
    return selected;
}

bool CCSOLScenarioNavWorld::GroundHeight(const Vector& point, float& height, Vector* normal) const
{
    if (!Finite(point) || !g_engfuncs.pfnTraceLine) return false;
    const auto generation = m_generation;
    struct Layer { float height; Vector normal; };
    std::array<Layer, 16> layers{};
    std::size_t count{};
    edict_t* ignored{};
    Vector end = point;
    end.z -= GroundDepth;
    if (!Finite(end)) return false;
    for (float offset = 1.0f; offset < 100.0f; offset += 10.0f)
    {
        Vector start = point;
        start.z += offset;
        TraceResult trace{};
        UTIL_TraceLine(start, end, ignore_monsters, dont_ignore_glass, ignored, &trace);
        // A map/round callback can release the NAV and the old edict array.
        // Do not inspect either a returned hit or an old cell iterator first.
        if (generation != m_generation) return false;
        if (trace.pHit)
        {
            if (!gpGlobals || !gpGlobals->pStringBase) return false;
            auto* vars = &trace.pHit->v;
            if (FClassnameIs(vars, "func_door") || FClassnameIs(vars, "func_door_rotating") ||
                (FClassnameIs(vars, "func_breakable") && vars->takedamage == DAMAGE_YES))
            {
                ignored = trace.pHit;
                continue;
            }
        }
        // No fraction!=1 gate: the original records a non-startsolid result.
        if (!trace.fStartSolid && (!count || trace.vecEndPos.z > layers[count - 1].height))
        {
            if (!Finite(trace.vecEndPos) || !Finite(trace.vecPlaneNormal)) return false;
            layers[count++] = {trace.vecEndPos.z, trace.vecPlaneNormal};
            if (count == layers.size()) break;
        }
    }
    if (!count) return false;
    std::size_t layer = 0;
    while (layer + 1 < count && layers[layer + 1].height - layers[layer].height < 36.0f) ++layer;
    height = layers[layer].height;
    if (normal) *normal = layers[layer].normal;
    return true;
}

bool CCSOLScenarioNavWorld::SimpleGroundHeight(const Vector& point, float& height, Vector* normal) const
{
    if (!Finite(point) || !g_engfuncs.pfnTraceLine) return false;
    const auto generation = m_generation;
    Vector end = point;
    end.z -= GroundDepth;
    if (!Finite(end)) return false;
    TraceResult trace{};
    UTIL_TraceLine(point, end, ignore_monsters, dont_ignore_glass, nullptr, &trace);
    if (generation != m_generation || trace.fStartSolid ||
        !Finite(trace.vecEndPos) || !Finite(trace.vecPlaneNormal)) return false;
    height = trace.vecEndPos.z;
    if (normal) *normal = trace.vecPlaneNormal;
    return true;
}

CNavArea* CCSOLScenarioNavWorld::Nearest(const Vector& point, bool anyZ, bool skipGround) const
{
    if (m_cells.empty() || !Finite(point)) return nullptr;
    if (auto* area = AreaAt(point)) return area;
    const auto generation = m_generation;
    Vector source = point;
    if (!skipGround)
    {
        if (!GroundHeight(point, source.z) || generation != m_generation) return nullptr;
        source.z += 36.0f;
    }
    if (!Finite(source) || (!anyZ && !g_engfuncs.pfnTraceLine)) return nullptr;
    const int centerX = GridX(source.x), centerY = GridY(source.y);
    const int limit = std::max(4, (std::min(m_sizeX, m_sizeY) - 1) / 2);
    CNavArea* closest{};
    float best = 100000000.0f;
    for (int radius = 1; radius < limit; ++radius)
    {
        const int lowX = std::max(0, centerX - radius), highX = std::min(m_sizeX - 1, centerX + radius);
        const int lowY = std::max(0, centerY - radius), highY = std::min(m_sizeY - 1, centerY + radius);
        for (int x = lowX; x <= highX; ++x)
            for (int y = lowY; y <= highY; ++y)
            {
                if (x != lowX && x != highX && y != lowY && y != highY) continue;
                for (auto* area : m_cells[x + y * m_sizeX])
                {
                    Vector position;
                    area->GetClosestPointOnArea(&source, &position);
                    const float distance = DistanceSquared(position, source);
                    if (!(distance < best)) continue;
                    if (!anyZ)
                    {
                        if (position.z > source.z) continue;
                        position.z += 36.0f;
                        TraceResult trace{};
                        UTIL_TraceLine(source, position, ignore_monsters, ignore_glass, nullptr, &trace);
                        if (generation != m_generation) return nullptr;
                        if (trace.flFraction != 1.0f) continue;
                    }
                    best = distance;
                    closest = area;
                }
            }
        if (closest) return closest;
    }
    return nullptr;
}
