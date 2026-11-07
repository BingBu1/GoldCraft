// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_spawn_points.h"

#include <cstdlib>

LINK_ENTITY_TO_CLASS(playerspawn, CCSOLPlayerSpawn, CCSEntity)
LINK_ENTITY_TO_CLASS(csol_playerspawn_proxy, CCSOLPlayerSpawnProxy, CCSEntity)
LINK_ENTITY_TO_CLASS(zombiespawn, CCSOLMonsterSpawn, CCSEntity)

namespace
{
csol::scenario::SpawnPoint Snapshot(CBaseEntity& entity, int team, int id)
{
    const auto& origin = entity.pev->origin;
    const auto& angles = entity.pev->angles;
    return {{origin.x, origin.y, origin.z}, {angles.x, angles.y, angles.z},
        team, id, static_cast<std::size_t>(entity.entindex())};
}
}

void CCSOLPlayerSpawn::Spawn()
{
    if (m_iTeam == 0) m_iTeam = 2;
}

void CCSOLPlayerSpawn::KeyValue(KeyValueData* data)
{
    if (FStrEq(data->szKeyName, "id")) m_iId = std::atoi(data->szValue);
    else if (FStrEq(data->szKeyName, "team2")) m_iTeam = std::atoi(data->szValue);
    else
    {
        CBaseEntity::KeyValue(data);
        return;
    }
    data->fHandled = TRUE;
}

bool CCSOLPlayerSpawn::Collect(csol::scenario::SpawnPoints& points)
{
    if (m_iId == -1) return false;
    points.AppendPlayerPoint(Snapshot(*this, m_iTeam, m_iId));
    return true;
}

void CCSOLMonsterSpawn::Spawn()
{
    if (m_iTeam == 3) m_iTeam = 1;
}

void CCSOLMonsterSpawn::KeyValue(KeyValueData* data)
{
    if (FStrEq(data->szKeyName, "id")) m_iId = std::atoi(data->szValue);
    else if (FStrEq(data->szKeyName, "team2")) m_iTeam = std::atoi(data->szValue);
    else if (FStrEq(data->szKeyName, "maxspawn")) m_iMaxSpawn = std::atoi(data->szValue);
    else
    {
        CBaseEntity::KeyValue(data);
        return;
    }
    data->fHandled = TRUE;
}

void CCSOLMonsterSpawn::Collect(csol::scenario::SpawnPoints& points)
{
    const int maximum = m_iMaxSpawn;
    points.RegisterMonsterPoint(Snapshot(*this, m_iTeam, m_iId), maximum);
}
