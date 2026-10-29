// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_spawn.h"
#include "../scenario/round_settings.hpp"

#include <bit>
#include <cmath>

namespace
{
struct SelectionScope
{
    bool& busy;
    explicit SelectionScope(bool& value) : busy(value) { busy = true; }
    ~SelectionScope() { busy = false; }
};

bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

bool CCSOLScenarioPlayerSpawns::BeginMap()
{
    if (m_busy) return false;
    m_nextOccupancyRefresh = 0;
    return true;
}

bool CCSOLScenarioPlayerSpawns::RefreshOccupancy()
{
    const auto access = m_access;
    if (!access || !access->Allowed() || !gpGlobals || !std::isfinite(gpGlobals->time) || !m_space.Ready() ||
        !m_space.RefreshOccupancy() || !access->Allowed() || !m_space.Ready()) return false;
    // Read time after refreshing: the original callback may change game time.
    const float next = gpGlobals->time + 0.06666667014360428f;
    if (!std::isfinite(next)) return false;
    m_nextOccupancyRefresh = next;
    return true;
}

bool CCSOLScenarioPlayerSpawns::UpdateOccupancy()
{
    if (!m_access || !m_access->Allowed() || m_busy || !gpGlobals || !std::isfinite(gpGlobals->time) || !m_space.Ready()) return false;
    if (gpGlobals->time < m_nextOccupancyRefresh) return true;
    SelectionScope operation(m_busy);
    return RefreshOccupancy();
}

bool CCSOLScenarioPlayerSpawns::OccupiedByPlayer(const Vector& origin, int exceptIndex,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access, bool& occupied) const
{
    occupied = false;
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        if (index == exceptIndex) continue;
        auto* entity = INDEXENT(index);
        if (!access->Allowed()) return false;
        if (!entity || entity->free) continue;
        auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity));
        if (!player || !player->pev) continue;
        const EHANDLE handle(player);
        const auto offset = OFFSET(player->pev);
        if (!access->Allowed()) return false;
        if (handle.Get<CBaseEntity>() != player) continue;
        if (!offset || static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) continue;
        const bool alive = player->IsAlive() != FALSE;
        if (!access->Allowed()) return false;
        if (handle.Get<CBaseEntity>() != player || !alive) continue;
        // No team, name or Z filter. Preserve the original float32 order and
        // strict radius boundary: exactly 64 units is allowed.
        const float dx = player->pev->origin.x - origin.x;
        const float dy = player->pev->origin.y - origin.y;
        const float dx2 = dx * dx;
        const float dy2 = dy * dy;
        const float distance2 = (dy2 + dx2) + 0.0f;
        if (4096.0f > distance2) { occupied = true; return true; }
    }
    return true;
}

CCSOLPlayerSpawn* CCSOLScenarioPlayerSpawns::Select(CCSOLScenarioMap& map,
    CBasePlayer& player, const std::int32_t& groupOffset, csol::scenario::ConfigError& error)
{
    error = {};
    const auto fail = [&](const char* message) -> CCSOLPlayerSpawn*
    {
        error.message = message;
        return nullptr;
    };
    if (m_busy) return fail("scenario player spawn selection is already in progress");
    const auto access = map.EntityAccess();
    if (!access->Allowed() || !gpGlobals || map.State() != CSOLScenarioMapState::RoundActive ||
        !map.RoundIndex() || !map.Tables())
        return fail("scenario player spawn selection requires an active round");
    auto* playerEdict = player.edict();
    const EntityHandle<CBasePlayer> spawningPlayer(playerEdict);
    const bool nullPlayer = FNullEnt(playerEdict);
    if (!access->Allowed() || nullPlayer || spawningPlayer.Get<CBaseEntity>() != &player)
        return fail("scenario spawning player is unavailable");
    const int playerIndex = ENTINDEX(playerEdict);
    if (!access->Allowed() || spawningPlayer.Get<CBaseEntity>() != &player) return nullptr;
    if (playerIndex < 1 || playerIndex > gpGlobals->maxClients)
        return fail("scenario spawning player is outside client slots");

    const auto roundIndex = map.RoundIndex();
    auto& points = map.SpawnPoints();
    const auto pointGeneration = points.Generation();
    const auto mapStillActive = [&]
    {
        return access->Allowed() && map.EntityAccess() == access &&
            map.State() == CSOLScenarioMapState::RoundActive &&
            map.RoundIndex() == roundIndex && points.Generation() == pointGeneration &&
            spawningPlayer.Get() == playerEdict &&
            CBaseEntity::Instance(playerEdict) == &player;
    };
    csol::scenario::RoundSettings settings;
    const auto readSettings = [&]
    {
        return csol::scenario::ReadRoundSettings(map.Tables()->rounds, *roundIndex, settings, error);
    };
    if (!readSettings()) return nullptr;
    SelectionScope operation(m_busy);
    const int team = player.m_iTeam;
    const auto adjustedGroup = [&]
    {
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(settings.playerSpawn)) +
            static_cast<std::uint32_t>(groupOffset));
    };
    const auto count = points.PlayerPointCount(team, adjustedGroup());
    // Even a missing group refreshes occupancy and updates the shared timer
    // before the original-group fallback. Count precedes that side effect.
    if (!RefreshOccupancy() || !mapStillActive()) return fail("scenario spawn occupancy refresh failed");
    if (count > 128) return fail("scenario player spawn group exceeds the safe byte cursor range");

    for (std::size_t attempt = 0; attempt < count; ++attempt)
    {
        if (!mapStillActive()) return fail("scenario changed during player spawn selection");
        // Count remains the pre-refresh snapshot, but every attempt re-reads
        // the current row and group offset, as does the original manager.
        if (!readSettings()) return nullptr;
        auto* point = points.SelectPlayerSpawn(team, adjustedGroup(), 0.0f);
        if (!mapStillActive()) return fail("scenario changed while creating player spawn proxy");
        if (!point) return fail("scenario player spawn proxy is unavailable");
        if (!Finite(point->pev->origin) || !Finite(point->pev->angles))
            return fail("scenario player spawn contains nonfinite coordinates");
        const EntityHandle<CCSOLPlayerSpawn> selected(point);
        const Vector origin = point->pev->origin;
        bool occupied{};
        if (!OccupiedByPlayer(origin, playerIndex, access, occupied) || !mapStillActive()) return nullptr;
        if (occupied) continue;
        const bool room = m_space.MakeRoom(origin);
        if (!m_space.Ready()) return fail("scenario spawn occupancy became unavailable");
        // The space callback may kill a blocking monster and invoke plugins.
        // Check even on refusal: a retry must not recreate a removed proxy and
        // conceal that a callback changed the map or spawning player.
        if (!mapStillActive() || points.PlayerProxy() != point ||
            selected.Get<CBaseEntity>() != point)
            return fail("scenario player spawn proxy changed while clearing space");
        if (!room) continue;
        return point;
    }
    if (!mapStillActive()) return fail("scenario changed before player spawn fallback");
    // No occupancy check on fallback, and no adjusted group or group-zero
    // substitution. The point manager itself handles missing groups as zero.
    if (!readSettings()) return nullptr;
    auto* point = points.SelectPlayerSpawn(team, settings.playerSpawn, 80.0f);
    if (!mapStillActive()) return fail("scenario changed while creating fallback spawn proxy");
    if (!point) return fail("scenario fallback player spawn proxy is unavailable");
    if (!Finite(point->pev->origin) || !Finite(point->pev->angles))
        return fail("scenario fallback player spawn contains nonfinite coordinates");
    return point;
}
