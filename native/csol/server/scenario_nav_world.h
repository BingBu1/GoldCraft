// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "game_shared/bot/nav_area.h"
#include <cstdint>
#include <span>
#include <vector>

// Indexes the map owner's actual SDK NAV areas in load order. Reset before
// deleting those areas or releasing map edicts; callbacks may reset this
// object, but its owner must outlive an in-progress query.
class CCSOLScenarioNavWorld
{
public:
    bool Initialize(std::span<CNavArea* const> areas);
    void Reset();
    std::uint64_t Generation() const { return m_generation; }
    // Reset/failed Initialize also advance generation. A nonzero generation
    // is a cancellation identity, never proof that usable areas were loaded.
    bool Ready() const { return !m_cells.empty(); }
    CNavArea* AreaAt(const Vector& point, float beneath = 120.0f) const;
    CNavArea* Nearest(const Vector& point, bool anyZ = false, bool skipGround = false) const;
    bool GroundHeight(const Vector& point, float& height, Vector* normal = nullptr) const;
    bool SimpleGroundHeight(const Vector& point, float& height, Vector* normal = nullptr) const;

private:
    int GridX(float x) const;
    int GridY(float y) const;
    std::vector<std::vector<CNavArea*>> m_cells;
    float m_minX{}, m_minY{};
    int m_sizeX{}, m_sizeY{};
    std::uint64_t m_generation{};
};
