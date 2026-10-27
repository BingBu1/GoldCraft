// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_map.h"

class CBasePlayer;
class Vector;

// The actual monster occupancy grid supplies both operations. MakeRoom is
// allowed to remove ordinary blocking monsters; it is not a pure overlap test.
// No default implementation may silently approve a blocked position.
class CCSOLScenarioSpawnSpace
{
public:
    virtual ~CCSOLScenarioSpawnSpace() = default;
    // Distinguish an ordinary occupied point from a broken map/pool. A failed
    // occupancy operation must not silently reach the untested +80 fallback.
    virtual bool Ready() const = 0;
    virtual bool RefreshOccupancy() = 0;
    virtual bool MakeRoom(const Vector& origin) = 0;
};

// Owned by the scenario rules alongside the map and occupancy grid. The same
// deadline is used by player selection and the manager's regular frame update.
// This returns a proxy only. EntSelectSpawnPoint must keep the existing ReAPI
// hook and let CGameRules::GetPlayerSpawnSpot perform the real player placement.
class CCSOLScenarioPlayerSpawns
{
public:
    explicit CCSOLScenarioPlayerSpawns(CCSOLScenarioSpawnSpace& space) : m_space(space) {}
    bool BeginMap();
    bool UpdateOccupancy();
    CCSOLPlayerSpawn* Select(CCSOLScenarioMap& map, CBasePlayer& player,
        const std::int32_t& groupOffset, csol::scenario::ConfigError& error);
    float NextOccupancyRefresh() const { return m_nextOccupancyRefresh; }

private:
    bool RefreshOccupancy();
    bool OccupiedByPlayer(const Vector& origin, int exceptIndex) const;
    CCSOLScenarioSpawnSpace& m_space;
    float m_nextOccupancyRefresh{};
    bool m_busy{};
};
