// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_spawn_points.h"
#include "ehandle.h"

enum class CSOLPointCollection
{
    NotCollected,
    Ready,
    ProxyUnavailable,
    NeedsSpawnFile
};

// One instance belongs to one scenario map session. Collect only after the
// engine has registered all map entities in its classname lookup table.
class CCSOLSpawnPointManager
{
public:
    // Forget old handles without accessing edicts from the previous map.
    // The engine owns/removes those edicts. Also call this on same-map reload.
    void BeginMap();
    CSOLPointCollection CollectMap();
    CSOLPointCollection Status() const { return m_status; }
    std::uint64_t Generation() const { return m_generation; }
    csol::scenario::SpawnPoints& Points() { return m_points; }
    const csol::scenario::SpawnPoints& Points() const { return m_points; }
    CCSOLPlayerSpawn* PlayerProxy() const;
    std::size_t PlayerPointCount(int team, int id) const;
    // Caller chooses the group and Z offset. A single cursor is shared by all
    // groups and survives ordinary rounds, just like the original manager.
    CCSOLPlayerSpawn* SelectPlayerSpawn(int team, int id, float zOffset);

private:
    CCSOLPlayerSpawn* EnsurePlayerProxy();
    CCSOLPlayerSpawn* AcceptPlayerProxy(CCSOLPlayerSpawnProxy& proxy);
    csol::scenario::SpawnPoints m_points;
    EntityHandle<CCSOLPlayerSpawn> m_hPlayerProxy;
    CSOLPointCollection m_status{CSOLPointCollection::NotCollected};
    std::uint8_t m_nextPlayer{};
    std::uint64_t m_generation{};
    bool m_busy{};
};
