// SPDX-License-Identifier: GPL-3.0-or-later
// NAV field/portal semantics adapted from ReGameDLL_CS (GPL-2.0-or-later
// with its Half-Life Engine linking exception); storage is local to CSOL.
#include "precompiled.h"
#include "scenario_navigation.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

// Do not call stock Initialize/constructors: they increment global IDs or
// register hiding spots. The scenario graph has its own file IDs and lifetime.
CNavArea::CNavArea(CSOLNavigationStorageTag)
    : m_id{}, m_extent{}, m_center{}, m_attributeFlags{},
      m_place{}, m_neZ{}, m_swZ{}, m_clearedTimestamp{}, m_danger{}, m_dangerTimestamp{},
      m_approach{}, m_approachCount{}, m_marker{}, m_parent{}, m_parentHow(GO_NORTH),
      m_totalCost{}, m_costSoFar{}, m_nextOpen{}, m_prevOpen{}, m_openMarker{},
      m_node{}, m_prevHash{}, m_nextHash{}, m_scenarioOwned(true)
{
}

HidingSpot::HidingSpot(CSOLNavigationStorageTag)
    : m_pos{}, m_id{}, m_marker{}, m_flags{}
{
}

namespace
{
Vector Point(const csol::scenario::NavPoint& point) { return {point.x, point.y, point.z}; }
bool MapName(std::string_view name)
{
    if (name.empty() || name.size() > 63) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
}

std::shared_ptr<CCSOLScenarioNavigation> CCSOLScenarioNavigation::Load(std::string_view mapName,
    csol::scenario::ResourceFileApi files, csol::scenario::ResourceError& error,
    std::optional<std::uint32_t> actualBspSize, PlaceResolver places)
{
    error = {};
    if (!MapName(mapName) || !files.load || !files.release)
    {
        error = {"", {0, "invalid scenario navigation map or file services"}};
        return {};
    }
    // Original mode15 selects .nav. Other modes' .znav selection is separate.
    const auto path = "maps/" + std::string(mapName) + ".nav";
    int length{};
    auto* bytes = files.load(path.c_str(), &length);
    const std::unique_ptr<unsigned char, void (*)(void*)> allocation(bytes, files.release);
    csol::scenario::NavigationFile file;
    if (!bytes || length < 0)
    {
        error = {path, {0, "scenario navigation file is unavailable"}};
        return {};
    }
    if (!csol::scenario::ReadNavigationV5({reinterpret_cast<const std::byte*>(bytes),
        static_cast<std::size_t>(length)}, file, error.detail))
    {
        error.path = path;
        return {};
    }
    auto owner = std::make_shared<CCSOLScenarioNavigation>();
    owner->m_bspSizeMismatch = actualBspSize && *actualBspSize != file.bspSize;
    if (!owner->Initialize(std::move(file), places, error.detail))
    {
        error.path = path;
        return {};
    }
    return owner;
}

bool CCSOLScenarioNavigation::Initialize(csol::scenario::NavigationFile file,
    PlaceResolver places, csol::scenario::ConfigError& error)
{
    m_file = std::move(file);
    std::unordered_map<std::uint32_t, CNavArea*> areas;
    std::unordered_map<std::uint32_t, HidingSpot*> spots;
    std::vector<Place> directory;
    // Resolve names without changing the CS bot phrase table. Unknown names
    // and duplicates are omitted before interpreting one-based place entries.
    for (const auto& name : m_file.places)
    {
        const Place place = places ? places(name.c_str()) : UNDEFINED_PLACE;
        if (place != UNDEFINED_PLACE && std::find(directory.begin(), directory.end(), place) == directory.end())
            directory.push_back(place);
    }
    m_areas.reserve(m_file.areas.size());
    m_areaPointers.reserve(m_file.areas.size());
    for (const auto& record : m_file.areas)
    {
        auto area = std::make_unique<CNavArea>(CSOLNavigationStorageTag{});
        area->m_id = record.id;
        area->m_attributeFlags = record.attributes;
        area->m_extent = {Point(record.low), Point(record.high)};
        area->m_center = (area->m_extent.lo + area->m_extent.hi) / 2.0f;
        area->m_neZ = record.northEastZ;
        area->m_swZ = record.southWestZ;
        if (record.place && record.place <= directory.size()) area->m_place = directory[record.place - 1];
        for (const auto& recordSpot : record.hidingSpots)
        {
            auto spot = std::make_unique<HidingSpot>(CSOLNavigationStorageTag{});
            spot->m_id = recordSpot.id;
            spot->m_pos = Point(recordSpot.position);
            spot->m_flags = recordSpot.flags;
            area->m_hidingSpotList.push_back(spot.get());
            spots.emplace(recordSpot.id, spot.get());
            m_spots.push_back(std::move(spot));
        }
        areas.emplace(record.id, area.get());
        m_areaPointers.push_back(area.get());
        m_areas.push_back(std::move(area));
    }
    const auto resolveArea = [&](std::uint32_t id, bool reportZero = false) -> CNavArea*
    {
        const auto found = areas.find(id);
        auto* area = id && found != areas.end() ? found->second : nullptr;
        if (!area && (id || reportZero)) ++m_unresolvedReferences;
        return area;
    };
    for (std::size_t index = 0; index < m_areas.size(); ++index)
    {
        auto& area = *m_areas[index];
        const auto& record = m_file.areas[index];
        for (std::size_t direction = 0; direction < record.connections.size(); ++direction)
            for (const auto id : record.connections[direction])
            {
                NavConnect connection{};
                connection.area = resolveArea(id);
                // Keep file order, duplicates and null connections, as Load
                // does; ConnectTo would silently deduplicate this sequence.
                area.m_connect[direction].push_back(connection);
            }
        area.m_approachCount = static_cast<unsigned char>(record.approaches.size());
        for (std::size_t item = 0; item < record.approaches.size(); ++item)
        {
            const auto& source = record.approaches[item];
            auto& target = area.m_approach[item];
            target.here.area = resolveArea(source.here);
            target.prev.area = resolveArea(source.previous);
            target.next.area = resolveArea(source.next);
            target.prevToHereHow = static_cast<NavTraverseType>(source.previousHow);
            target.hereToNextHow = static_cast<NavTraverseType>(source.nextHow);
        }
        for (const auto& source : record.encounters)
        {
            SpotEncounter encounter{};
            encounter.from.area = resolveArea(source.from, true);
            encounter.to.area = resolveArea(source.to, true);
            encounter.fromDir = static_cast<NavDirType>(source.fromDirection);
            encounter.toDir = static_cast<NavDirType>(source.toDirection);
            if (encounter.from.area && encounter.to.area)
            {
                float halfWidth{};
                area.ComputePortal(encounter.to.area, encounter.toDir, &encounter.path.to, &halfWidth);
                area.ComputePortal(encounter.from.area, encounter.fromDir, &encounter.path.from, &halfWidth);
                encounter.path.from.z = encounter.from.area->GetZ(&encounter.path.from) + 36.0f;
                encounter.path.to.z = encounter.to.area->GetZ(&encounter.path.to) + 36.0f;
            }
            for (const auto& sourceSpot : source.spots)
            {
                SpotOrder order{};
                order.t = static_cast<float>(sourceSpot.position) / 255.0f;
                const auto found = spots.find(sourceSpot.id);
                order.spot = found != spots.end() ? found->second : nullptr;
                if (!order.spot) ++m_unresolvedReferences;
                encounter.spotList.push_back(order);
            }
            area.m_spotEncounterList.push_back(std::move(encounter));
        }
        for (auto* other : m_areaPointers)
            if (other != &area && area.IsOverlapping(other)) area.m_overlapList.push_back(other);
    }
    // Original count0 remains an empty, valid file; Ready stays false so a
    // gameplay owner can explain why it cannot create a navigable round.
    if (!m_areas.empty() && !m_world.Initialize(m_areaPointers))
    {
        error = {0, "scenario navigation grid could not be initialized"};
        return false;
    }
    error = {};
    return true;
}

void CCSOLScenarioNavigation::Invalidate()
{
    m_world.Reset();
    // Retain areas/ladders until shared leases return; never free an active
    // path iterator or let an old query access a newly loaded map's edicts.
}

CCSOLScenarioNavigation::~CCSOLScenarioNavigation()
{
    // A successful A* can leave unvisited areas on the SDK's static open
    // list. Never leave that global head pointing at released owned storage.
    // Preserve a list belonging to an independent CS bot search.
    if (std::find(m_areaPointers.begin(), m_areaPointers.end(), CNavArea::m_openList) != m_areaPointers.end())
        CNavArea::ClearSearchLists();
}
