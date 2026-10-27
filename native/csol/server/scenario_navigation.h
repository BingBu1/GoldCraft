// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_nav_world.h"
#include "../scenario/map_resources.hpp"
#include "../scenario/navigation.hpp"
#include <memory>
#include <optional>

// A map invalidates World before releasing edicts. Shared leases keep the
// complete graph alive until old callbacks return, including failed loads.
class CCSOLMonsterNavigation
{
public:
    virtual ~CCSOLMonsterNavigation() = default;
    virtual const CCSOLScenarioNavWorld& World() const = 0;
    virtual std::span<const CNavLadder* const> ClimberLadders() const = 0;
};

class CCSOLScenarioNavigation final : public CCSOLMonsterNavigation
{
public:
    using PlaceResolver = Place (*)(const char* name);
    ~CCSOLScenarioNavigation() override;
    static std::shared_ptr<CCSOLScenarioNavigation> Load(std::string_view mapName,
        csol::scenario::ResourceFileApi files, csol::scenario::ResourceError& error,
        std::optional<std::uint32_t> actualBspSize = {}, PlaceResolver places = nullptr);
    bool BuildLadders(csol::scenario::ConfigError& error);
    void Invalidate();
    const CCSOLScenarioNavWorld& World() const override { return m_world; }
    std::span<const CNavLadder* const> ClimberLadders() const override { return m_climbers; }
    std::span<CNavArea* const> Areas() const { return m_areaPointers; }
    bool BspSizeMismatch() const { return m_bspSizeMismatch; }
    std::size_t UnresolvedReferences() const { return m_unresolvedReferences; }
    std::size_t LadderCount() const { return m_ladders.size(); }
    std::uint32_t StoredBspSize() const { return m_file.bspSize; }

private:
    bool Initialize(csol::scenario::NavigationFile file, PlaceResolver places,
        csol::scenario::ConfigError& error);
    // Members are destroyed in reverse order; lists inside an owned area only
    // borrow spots/ladders. The owned-area destructor never notifies CS bots.
    csol::scenario::NavigationFile m_file;
    std::vector<std::unique_ptr<HidingSpot>> m_spots;
    std::vector<std::unique_ptr<CNavLadder>> m_ladders;
    std::vector<std::unique_ptr<CNavArea>> m_areas;
    std::vector<CNavArea*> m_areaPointers;
    std::vector<const CNavLadder*> m_climbers;
    CCSOLScenarioNavWorld m_world;
    std::size_t m_unresolvedReferences{};
    bool m_bspSizeMismatch{}, m_laddersBuilt{}, m_building{};
};
