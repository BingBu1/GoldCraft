// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_season5_entities.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "game_rules_gate.h"
#include "scenario_entity_access.h"

#include <charconv>
#include <cstdio>
#include <cstring>

#pragma clang fp contract(off)

LINK_ENTITY_TO_CLASS(trigger_nextmap, CCSOLNextMap, CCSEntity)

namespace
{
std::shared_ptr<CCSOLScenarioRuntime> Runtime(const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (!access || !access->Allowed()) return {};
    return CSOL_ScenarioRuntimeForAccess(access.get());
}

// 1152C520 (invalid edict) and 11409720 (flags & FL_DORMANT).
bool Unusable(CBaseEntity& entity)
{
    return !entity.pev || FNullEnt(entity.edict()) || (entity.pev->flags & FL_DORMANT);
}

// MP108F4FA0 counts the ZS player records whose player is valid, not
// dormant, has a resolved character class and is alive (vt+A8).
// MP108F5060 additionally requires origin inside the inclusive box.
int CountParticipants(const CCSOLScenarioPlayerRuntime& players, const Vector* mins, const Vector* maxs)
{
    int count{};
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = UTIL_PlayerByIndex(index);
        if (!player || Unusable(*player) || !players.HasCharacterClass(EHANDLE(player)) || !player->IsAlive())
            continue;
        if (mins && maxs)
        {
            const Vector& at = player->pev->origin;
            if (at.x < mins->x || at.y < mins->y || at.z < mins->z ||
                maxs->x < at.x || maxs->y < at.y || maxs->z < at.z) continue;
        }
        ++count;
    }
    return count;
}
}

// --- trigger_nextmap -------------------------------------------------------

void CCSOLNextMap::KeyValue(KeyValueData* data)
{
    // 108EE3D0: "map" is copied (the original only warns when it is 32+
    // characters; the host truncates instead of overflowing +164).
    if (!std::strcmp(data->szKeyName, "map"))
    {
        if (std::strlen(data->szValue) >= m_map.size())
            ALERT(at_error, "Map name '%s' too long (32 chars)\n", data->szValue);
        std::snprintf(m_map.data(), m_map.size(), "%s", data->szValue);
        data->fHandled = TRUE;
    }
    else if (!std::strcmp(data->szKeyName, "clear"))
    {
        // 108EE48A compares the value pointer, not its text, with null.
        m_clear = data->szValue != nullptr;
        data->fHandled = TRUE;
    }
    else CBaseTrigger::KeyValue(data);
}

void CCSOLNextMap::Spawn()
{
    // 108F1CF0: InitTrigger, Touch/Think, first think in 0.1 s.
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current()) return;
    m_access = scope.EntityAccess();
    InitTrigger();
    SetTouch(&CCSOLNextMap::NextMapTouch);
    SetThink(&CCSOLNextMap::NextMapThink);
    pev->nextthink = float(double(gpGlobals->time) + 0.1);
    m_start = 0.0f;
    m_done = false;
    m_inside = 0;
}

void CCSOLNextMap::NextMapTouch(CBaseEntity* other)
{
    // 108F2E50: a real, named, non-dormant player starts the countdown once.
    if (!other || Unusable(*other) || !other->pev->netname || !*STRING(other->pev->netname) ||
        other->IsPlayer() == FALSE || !m_map[0] || m_start != 0.0f) return;
    const auto runtime = Runtime(m_access);
    if (!runtime || !runtime->session || !runtime->players) return;
    m_start = gpGlobals->time;
    m_inside = 1;
    const int participants = CountParticipants(*runtime->players, nullptr, nullptr);
    auto& messages = runtime->session->Messages();
    messages.SendChangeLevelZone(m_inside, participants);
    messages.SendChangeLevelTimer(int(m_duration));
}

void CCSOLNextMap::NextMapThink()
{
    // 108EFA20: once per second; announce changes and the 10 s countdown.
    pev->nextthink = float(double(gpGlobals->time) + 1.0);
    const auto runtime = Runtime(m_access);
    if (!runtime || !runtime->session || !runtime->players) return;
    auto& messages = runtime->session->Messages();
    const auto& players = *runtime->players;
    const int inside = CountParticipants(players, &pev->absmin, &pev->absmax);
    if (m_inside != inside)
    {
        m_inside = inside;
        messages.SendChangeLevelZone(inside, CountParticipants(players, nullptr, nullptr));
    }
    if (m_start != 0.0f)
    {
        const int remaining = int(m_duration - (gpGlobals->time - m_start));
        if (remaining > 0 && remaining % 10 == 0) messages.SendChangeLevelTimer(remaining);
    }
    // 108EFAE9: all participants inside changes level even before a touch.
    const int participants = CountParticipants(players, nullptr, nullptr);
    if (participants && CountParticipants(players, &pev->absmin, &pev->absmax) ==
        CountParticipants(players, nullptr, nullptr))
    {
        Change();
        return;
    }
    if (m_start != 0.0f && gpGlobals->time > m_start + m_duration) Change();
}

void CCSOLNextMap::Change()
{
    // 108ED0C0. "clear" calls the manager's 11381BC0(2, 0); that consumer is
    // not reconstructed and no evacuate map sets it.
    if (m_clear)
    {
        ALERT(at_error, "CSOL: trigger_nextmap clear is not implemented\n");
        return;
    }
    // "zs_nextmap <map>" hands the maplist ID to CSO's room service.
    int mapId{};
    const char* end = m_map.data() + std::strlen(m_map.data());
    const auto [last, error] = std::from_chars(m_map.data(), end, mapId);
    if (error != std::errc{} || last != end || !CSOL_RequestScenarioChapterMap(mapId))
        ALERT(at_error, "CSOL: trigger_nextmap could not change to map %s\n", m_map.data());
    m_done = true;
    SetThink(nullptr);
    SetTouch(nullptr);
    pev->nextthink = 0;
    pev->solid = SOLID_NOT;
}
