// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_entity_lookup.h"
#include "scenario_point_manager.h"

#include <utility>
#include <vector>

namespace
{
struct PointOperation
{
    bool& busy;
    explicit PointOperation(bool& value) : busy(value) { busy = true; }
    ~PointOperation() { busy = false; }
};

bool Live(CBaseEntity* entity)
{
    return entity && UTIL_IsValidEntity(entity->edict());
}

bool RemoveMarkers(const std::vector<EHANDLE>& markers,
    const std::uint64_t& generation, std::uint64_t expectedGeneration,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    // Collect before removing: UTIL_Remove calls UpdateOnRemove, and a plugin
    // can change other entities. Do not retain raw pointers across that call.
    for (const auto& handle : markers)
    {
        if (!access->Allowed() || generation != expectedGeneration) return false;
        CBaseEntity* entity = handle;
        if (Live(entity)) UTIL_Remove(entity);
        if (!access->Allowed() || generation != expectedGeneration) return false;
    }
    return true;
}
}

void CCSOLSpawnPointManager::BeginMap()
{
    // Invalidate callbacks already in progress. Keep their busy guard intact
    // until they unwind; never let them commit old points into the new map.
    ++m_generation;
    m_hPlayerProxy = nullptr;
    m_points.Clear();
    m_status = CSOLPointCollection::NotCollected;
    m_nextPlayer = 0;
}

CCSOLPlayerSpawn* CCSOLSpawnPointManager::PlayerProxy() const
{
    if (!m_access || !m_access->Allowed()) return nullptr;
    auto* entity = m_hPlayerProxy.Get<CBaseEntity>();
    if (!Live(entity)) return nullptr;
    auto* proxy = dynamic_cast<CCSOLPlayerSpawn*>(entity);
    if (!proxy || proxy->m_iId != -1) return nullptr;
    if (auto* internal = dynamic_cast<CCSOLPlayerSpawnProxy*>(proxy))
        return FClassnameIs(proxy->pev, CCSOLPlayerSpawnProxy::Classname) &&
            internal->SpawnAccepted() ? proxy : nullptr;
    return FClassnameIs(proxy->pev, "playerspawn") ? proxy : nullptr;
}

CCSOLPlayerSpawn* CCSOLSpawnPointManager::EnsurePlayerProxy()
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return nullptr;
    const auto generation = m_generation;
    if (auto* proxy = PlayerProxy()) return proxy;
    m_hPlayerProxy = nullptr;
    CBaseEntity* entity = nullptr;
    while ((entity = CSOL_FindScenarioEntity(entity, "playerspawn", access)))
    {
        if (!access->Allowed() || generation != m_generation) return nullptr;
        if (!Live(entity)) continue;
        auto* point = dynamic_cast<CCSOLPlayerSpawn*>(entity);
        if (!point || point->m_iId != -1 ||
            dynamic_cast<CCSOLPlayerSpawnProxy*>(point)) continue;
        m_hPlayerProxy = point;
        return point;
    }
    if (!access->Allowed() || generation != m_generation) return nullptr;

    // A cancelled CREATE or DispatchSpawn may have left an internal proxy in
    // the same edict array. Finish it instead of creating another marker.
    entity = nullptr;
    while ((entity = CSOL_FindScenarioEntity(entity, CCSOLPlayerSpawnProxy::Classname, access)))
    {
        if (!access->Allowed() || generation != m_generation) return nullptr;
        if (!Live(entity)) continue;
        auto* proxy = dynamic_cast<CCSOLPlayerSpawnProxy*>(entity);
        if (!proxy) continue; // Preserve foreign objects without blocking a retry.
        if (!proxy->SpawnAccepted()) return AcceptPlayerProxy(*proxy);
        if (proxy->m_iId != -1) return nullptr;
        m_hPlayerProxy = proxy;
        return proxy;
    }
    if (!access->Allowed() || generation != m_generation) return nullptr;

    const auto name = ALLOC_STRING(CCSOLPlayerSpawnProxy::Classname);
    if (!access->Allowed() || generation != m_generation) return nullptr;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed() || generation != m_generation) return nullptr;
    const EHANDLE created(edict);
    const bool nullEntity = FNullEnt(edict);
    if (!access->Allowed() || generation != m_generation || nullEntity) return nullptr;
    if (created.Get() != edict) return nullptr;
    entity = GET_PRIVATE<CBaseEntity>(edict);
    if (!Live(entity) || !FClassnameIs(entity->pev, CCSOLPlayerSpawnProxy::Classname)) return nullptr;
    auto* proxy = dynamic_cast<CCSOLPlayerSpawnProxy*>(entity);
    return proxy ? AcceptPlayerProxy(*proxy) : nullptr;
}

CCSOLPlayerSpawn* CCSOLSpawnPointManager::AcceptPlayerProxy(CCSOLPlayerSpawnProxy& proxy)
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return nullptr;
    const auto generation = m_generation;
    const EntityHandle<CCSOLPlayerSpawn> created(&proxy);
    proxy.m_spawnAccepted = false;
    proxy.m_iId = -1;
    proxy.pev->origin = g_vecZero;
    proxy.pev->angles = g_vecZero;
    const auto spawnResult = DispatchSpawn(proxy.edict());
    // A true map change can replace the entire edict array; never even resolve
    // the old handle after that callback. Same-map retries find it by classname.
    if (!access->Allowed() || generation != m_generation) return nullptr;
    auto* entity = created.Get<CBaseEntity>();
    if (!Live(entity) || entity != &proxy ||
        !FClassnameIs(entity->pev, CCSOLPlayerSpawnProxy::Classname)) return nullptr;
    auto* accepted = dynamic_cast<CCSOLPlayerSpawnProxy*>(entity);
    if (!accepted) return nullptr;
    if (spawnResult < 0)
    {
        // IsAllowedToSpawn can reject without FL_KILLME. Remove only our
        // unchanged private object, never a replacement installed by a callback.
        UTIL_Remove(accepted);
        return nullptr;
    }
    accepted->m_iId = -1;
    accepted->m_spawnAccepted = true;
    m_hPlayerProxy = accepted;
    return accepted;
}

std::size_t CCSOLSpawnPointManager::PlayerPointCount(int team, int id) const
{
    if (team == 0) return 0;
    // Count does not use Select's non-1/2 team normalization. Queries do not
    // create the original implementation's unobservable empty map buckets.
    const auto* points = m_points.FindPlayerPoints(team, id);
    return points ? points->size() : 0;
}

CCSOLPlayerSpawn* CCSOLSpawnPointManager::SelectPlayerSpawn(int team, int id, float zOffset)
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return nullptr;
    if (m_busy || m_status != CSOLPointCollection::Ready) return nullptr;
    PointOperation operation(m_busy);
    const auto generation = m_generation;
    auto* proxy = EnsurePlayerProxy();
    if (!access->Allowed() || generation != m_generation || m_status != CSOLPointCollection::Ready ||
        !proxy || PlayerProxy() != proxy) return nullptr;
    // Proxy creation invokes engine/plugin callbacks. Only read the point
    // table after those calls and their lifecycle checks have completed.
    const auto* points = team == 0 ? nullptr :
        m_points.FindPlayerPoints(team == 1 ? 1 : 2, id);
    // MP uses a signed byte as an array index. Refuse oversized groups instead
    // of reproducing its out-of-bounds access when that byte becomes negative.
    if (points && points->size() > 128) return nullptr;
    if (!points || points->empty())
    {
        proxy->pev->origin = g_vecZero;
        proxy->pev->angles = g_vecZero;
        return proxy; // Missing/zero team ignores Z and does not advance cursor.
    }
    const auto index = static_cast<std::size_t>(m_nextPlayer) % points->size();
    const auto& point = (*points)[index];
    proxy->pev->origin = Vector(point.origin[0], point.origin[1], point.origin[2] + zOffset);
    proxy->pev->angles = Vector(point.angles[0], point.angles[1], point.angles[2]);
    m_nextPlayer = static_cast<std::uint8_t>((index + 1) % points->size());
    return proxy;
}

CSOLPointCollection CCSOLSpawnPointManager::CollectMap()
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return CSOLPointCollection::NotCollected;
    // Markers are consumed once. A new round resets only monster quotas; it
    // must not scan the now-removed markers and erase the saved positions.
    if (m_status == CSOLPointCollection::Ready ||
        m_status == CSOLPointCollection::NeedsSpawnFile) return m_status;
    if (m_busy) return m_status;
    PointOperation operation(m_busy);
    const auto generation = m_generation;
    auto* proxy = EnsurePlayerProxy();
    if (!access->Allowed() || generation != m_generation) return m_status;
    if (!proxy) return m_status = CSOLPointCollection::ProxyUnavailable;

    csol::scenario::SpawnPoints points;
    std::vector<EHANDLE> markers;
    CBaseEntity* entity = nullptr;
    while ((entity = CSOL_FindScenarioEntity(entity, "zombiespawn", access)))
    {
        if (!access->Allowed() || generation != m_generation) return m_status;
        if (!Live(entity)) continue;
        auto* point = dynamic_cast<CCSOLMonsterSpawn*>(entity);
        if (!point) continue;
        point->Collect(points);
        if (!access->Allowed() || generation != m_generation) return m_status;
        markers.emplace_back(entity);
    }
    if (!access->Allowed() || generation != m_generation || !RemoveMarkers(markers, m_generation, generation, access)) return m_status;
    markers.clear();

    entity = nullptr;
    while ((entity = CSOL_FindScenarioEntity(entity, "playerspawn", access)))
    {
        if (!access->Allowed() || generation != m_generation) return m_status;
        if (!Live(entity)) continue;
        auto* point = dynamic_cast<CCSOLPlayerSpawn*>(entity);
        const bool collected = point && point->Collect(points);
        if (!access->Allowed() || generation != m_generation) return m_status;
        if (collected) markers.emplace_back(entity);
    }
    if (!access->Allowed() || generation != m_generation) return m_status;
    if (markers.empty())
    {
        entity = nullptr;
        while ((entity = CSOL_FindScenarioEntity(entity, "info_player_start", access)))
        {
            if (!access->Allowed() || generation != m_generation) return m_status;
            if (!Live(entity)) continue;
            const auto& origin = entity->pev->origin;
            const auto& angles = entity->pev->angles;
            points.AppendPlayerPoint({{origin.x, origin.y, origin.z},
                {angles.x, angles.y, angles.z}, 2, 0,
                static_cast<std::size_t>(entity->entindex())});
            if (!access->Allowed() || generation != m_generation) return m_status;
            markers.emplace_back(entity);
        }
    }
    if (!access->Allowed() || generation != m_generation || !RemoveMarkers(markers, m_generation, generation, access)) return m_status;
    if (PlayerProxy() != proxy) return m_status = CSOLPointCollection::ProxyUnavailable;
    m_points = std::move(points);
    if (m_points.Players().empty())
    {
        // Original SP fallback clears both tables before reading its file.
        // Surface the missing stage; do not mix SP rows into valid BSP points
        // or report an empty player map as a playable scenario.
        m_points.Clear();
        return m_status = CSOLPointCollection::NeedsSpawnFile;
    }
    return m_status = CSOLPointCollection::Ready;
}
