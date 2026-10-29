// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wall_status.h"
#include "scenario_breakable.h"
#include "game_rules_gate.h"
#include "../wall_status.hpp"

namespace
{
bool writing{};
struct MessageScope { MessageScope() { writing = true; } ~MessageScope() { writing = false; } };

void WriteValue(edict_t* recipient, int field, int value)
{
    MESSAGE_BEGIN(MSG_ONE, gmsgStatusValue, nullptr, recipient);
    WRITE_BYTE(field);
    WRITE_LONG(value);
    MESSAGE_END();
}
void ClearNativeStatus(CBasePlayer& player)
{
    Q_memset(player.m_izSBarState, 0, sizeof(player.m_izSBarState));
    player.m_SbarString0[0] = '\0';
    player.m_flStatusBarDisappearDelay = 0;
}
}

void CSOL_ResetWallStatus(CBasePlayer& player)
{
    // Stock's native status fields start at index 1. Index 0 is unused and
    // lives with the player allocation, so no cross-map pointer cache exists.
    if (player.m_izSBarState[0] == csol::wallstatus::WallType) ClearNativeStatus(player);
    else player.m_izSBarState[0] = 0;
}

bool CSOL_UpdateWallStatus(CBasePlayer& player, CBaseEntity* target)
{
    if (writing) return true;
    CSOLScenarioSdkScope scope;
    const bool wasWall = player.m_izSBarState[0] == csol::wallstatus::WallType;
    if (!scope.Scenario() && !wasWall) return false;
    if (scope.Scenario() && !scope.Current()) return true;
    const EHANDLE owner(&player);
    const auto current = [&]() {
        return (!scope.Scenario() || scope.Current()) && owner.Get<CBasePlayer>() == &player &&
            !(player.pev->flags & FL_KILLME);
    };
    int health{};
    // The first port is specifically CZBSBreak's type-3 information, not the
    // separate zbstarget icon/protection HUD or arbitrary func_breakable.
    const auto* wall = target && FClassnameIs(target->pev, "zbsbreak")
        ? dynamic_cast<CCSOLScenarioBreakable*>(target) : nullptr;
    const bool show = scope.Scenario() && player.pev->deadflag == DEAD_NO && player.pev->health > 0 &&
        player.pev->iuser1 == OBS_NONE && !(player.m_iHideHUD & HIDEHUD_ALL) &&
        wall && wall->StatusHealth(health);
    if (!current()) return true;
    if (!show && !wasWall) return false;
    if (!gmsgStatusValue || !gmsgStatusText || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteLong || !g_engfuncs.pfnWriteString) return show;
    const MessageScope packet;
    auto* recipient = player.edict();
    if (!show)
    {
        WriteValue(recipient, 0, 0);
        if (!current()) return true;
        ClearNativeStatus(player);
        return false;
    }
    const bool changed = !wasWall || player.m_izSBarState[1] != health;
    if (!wasWall)
    {
        MESSAGE_BEGIN(MSG_ONE, gmsgStatusText, nullptr, recipient);
        WRITE_BYTE(0);
        WRITE_STRING("");
        MESSAGE_END();
        if (!current()) return true;
        ClearNativeStatus(player);
        WriteValue(recipient, 0, csol::wallstatus::WallType);
        if (!current()) return true;
    }
    if (changed)
    {
        WriteValue(recipient, 1, health);
        if (!current()) return true;
    }
    player.m_izSBarState[0] = csol::wallstatus::WallType;
    player.m_izSBarState[1] = health;
    return true;
}
