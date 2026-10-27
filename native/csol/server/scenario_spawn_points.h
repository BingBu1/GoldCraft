// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "../scenario/spawn_points.hpp"

// The scenario manager takes snapshots in engine traversal order. Entity Spawn
// only applies the original team default; Activate does not register/remove it.
class CCSOLPlayerSpawn : public CBaseEntity
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    // The full-width -1 ID is the movable player-spawn proxy, not a map point.
    bool Collect(csol::scenario::SpawnPoints& points);
    int m_iId{};
    int m_iTeam{};
};

// A runtime proxy must never enter the map-marker list, even when an engine
// callback cancels its creation before the manager receives the new edict.
class CCSOLPlayerSpawnProxy final : public CCSOLPlayerSpawn
{
public:
    static constexpr const char* Classname = "csol_playerspawn_proxy";
    CCSOLPlayerSpawnProxy() { m_iId = -1; }
    bool SpawnAccepted() const { return m_spawnAccepted; }

private:
    friend class CCSOLSpawnPointManager;
    // Spawn alone is not acceptance: DispatchSpawn can still reject afterward.
    bool m_spawnAccepted{};
};

class CCSOLMonsterSpawn : public CBaseEntity
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    void Collect(csol::scenario::SpawnPoints& points);
    int m_iId{};
    int m_iTeam{3};
    int m_iMaxSpawn{};
};
