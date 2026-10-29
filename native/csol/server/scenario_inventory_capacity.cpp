// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_inventory_capacity.h"
#include "game_rules_gate.h"
#include "scenario_player_runtime.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool ReadStat(CBasePlayer& player, csol::scenario::UpgradeStat stat, float& value)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current() || player.has_disconnected ||
        player.m_iTeam != CT || (player.pev->flags & (FL_SPECTATOR | FL_DORMANT | FL_KILLME))) return false;
    const auto authority = CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get());
    const EHANDLE handle(&player);
    return authority && authority->ReadUpgradeStat(handle, stat, value) && scope.Current() &&
        handle.Get<CBaseEntity>() == &player && std::isfinite(value) && value >= 0 &&
        static_cast<double>(value) < static_cast<double>(std::numeric_limits<int>::max());
}
}

int CSOL_PlayerAmmoCapacity(CBasePlayer* player, const char* ammo, int nativeCapacity)
{
    if (!player || !ammo || nativeCapacity < 0 || !CSOL_UsesScenarioRules()) return nativeCapacity;
    // GoldCraft's throwing knife keeps one loaded blade plus 29 reserve, and
    // cannon charges are held entirely in the clip. They retain those adapters.
    // CS grenade inventory uses separate one/two-item limits, not firearm reserve.
    if (!Q_strcmp(ammo, "TKnife") || !Q_strcmp(ammo, "cannon") ||
        !Q_strcmp(ammo, "beamgun_c") || !Q_strcmp(ammo, "beamgunle_c") ||
        !Q_stricmp(ammo, "HEGrenade") || !Q_stricmp(ammo, "Flashbang") ||
        !Q_stricmp(ammo, "SmokeGrenade") || !Q_stricmp(ammo, "C4")) return nativeCapacity;

    float tableValue{};
    if (!ReadStat(*player, csol::scenario::UpgradeStat::Ammo, tableValue)) return nativeCapacity;
    // MP scenario vC0 (108237A0), followed by base vC0's mode15 overrides.
    struct Fixed { const char* name; int capacity; };
    static constexpr Fixed fixed[]{
        {"at4rocket", 10}, {"M32grenades", 36}, {"m32grenade_venom", 36},
        {"fglauncher_rocket", 40}, {"tknife", 30}, {"buckshot_dfpistol", 540},
        {"50bmg_lockongunma", 200}, {"50bmg_lockongunmb", 300},
        {"50bmg_lockongunmc", 400}, {"50bmg_lockongunmd", 500}
    };
    for (const auto& entry : fixed)
        if (!Q_strcmp(ammo, entry.name)) return entry.capacity;
    // MP 1152C520 reaches the fake-client flag predicate 11409720; its
    // scenario fallback is 600 after the named fixed-ammunition branches.
    if (player->pev->flags & FL_FAKECLIENT) return 600;
    return static_cast<int>(tableValue);
}

int CSOL_PlayerArmorCapacity(CBasePlayer* player)
{
    if (!player || !CSOL_UsesScenarioRules()) return MAX_NORMAL_BATTERY;
    float tableValue{};
    if (!ReadStat(*player, csol::scenario::UpgradeStat::Armor, tableValue)) return MAX_NORMAL_BATTERY;
    // MP 108239C0 uses max(current armor, stat3 * rules.v258). This scenario
    // inherits the base v258 multiplier 1; querying a new cap never fills armor.
    const float maximum = std::max(player->pev->armorvalue, tableValue);
    if (!std::isfinite(maximum) || maximum < 0 ||
        static_cast<double>(maximum) >= static_cast<double>(std::numeric_limits<int>::max())) return MAX_NORMAL_BATTERY;
    return static_cast<int>(maximum);
}
