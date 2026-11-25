// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_chapter_carry.h"
#include "game_rules_gate.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "scenario_player_upgrades.h"
#include "../../weapons/weapon_service.h"
#include "../../weapons/weapon_slots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace
{
// Snapshot 1071C910 (1348 bytes): only the fields that have a host owner.
struct Snapshot
{
    bool dead{};                    // [274] pev->deadflag != 0
    int health{}, armor{}, armorType{}, kevlar{}; // [275..278], alive only
    int money{};                    // [279] 114D4EF0 (+3240)
    int frags{};                    // [280] int(pev->frags)
    int deaths{};                   // [281] +6352
    std::vector<int> weapons;       // [2..4] 44-byte records, id field only
    std::array<int, MAX_AMMO_SLOTS> ammo{}; // part of the [5..273] block
    bool upgrades{};                // ZS record 108F6870
    std::array<csol::scenario::UpgradeAttribute, 5> attributes{};
    int experience{};               // record +92
};

std::map<int, Snapshot> table; // 11EE7654, keyed by the player's identity
bool applied{};                // 11EE7640

int Key(CBasePlayer& player)
{
    return g_engfuncs.pfnGetPlayerUserId ? GETPLAYERUSERID(player.edict()) : 0;
}

// 10824EE0 filter: present, OFFSET != 0, non-empty netname, flags >= 0.
bool Eligible(CBasePlayer* player)
{
    return player && player->pev && !FNullEnt(player->edict()) && OFFSET(player->edict()) &&
        STRING(player->pev->netname)[0] && player->pev->flags >= 0;
}
}

void CSOL_ChapterCarryCapture(CCSOLScenarioRuntime& runtime)
{
    // 10824EE0 starts by clearing the table and the applied flag.
    applied = false;
    table.clear();
    if (!gpGlobals || !runtime.players || !runtime.session) return;
    const auto upgrades = runtime.session->Players();
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* player = UTIL_PlayerByIndex(slot);
        if (!Eligible(player)) continue;
        const EHANDLE handle(player);
        // Record +160 (character definition) must be set.
        if (!runtime.players->HasCharacterClass(handle)) continue;
        const int key = Key(*player);
        if (key <= 0) continue;
        Snapshot snapshot;
        // 114D4700 / 114D48E0
        snapshot.dead = player->pev->deadflag != DEAD_NO;
        snapshot.money = player->m_iAccount;
        snapshot.frags = static_cast<int>(player->pev->frags);
        snapshot.deaths = player->m_iDeaths;
        if (!snapshot.dead)
        {
            snapshot.health = static_cast<int>(player->pev->health);
            snapshot.armor = static_cast<int>(player->pev->armorvalue);
            snapshot.armorType = static_cast<int>(player->pev->armortype);
            snapshot.kevlar = player->m_iKevlar;
            for (int type = 0; type < MAX_ITEM_TYPES; ++type)
                for (auto* item = player->m_rgpPlayerItems[type]; item; item = item->m_pNext)
                    snapshot.weapons.push_back(item->m_iId);
            std::copy(std::begin(player->m_rgAmmo), std::end(player->m_rgAmmo), snapshot.ammo.begin());
        }
        // 108F6870: level, cap, experience and per-attribute level/cap.
        if (const auto state = upgrades ? upgrades->Upgrades(handle) : nullptr;
            state && state->State())
        {
            snapshot.upgrades = true;
            snapshot.attributes = state->State()->attributes;
            snapshot.experience = state->State()->experience;
        }
        table[key] = std::move(snapshot); // later duplicates overwrite
    }
}

void CSOL_ChapterCarryRulesCreated()
{
    if (!applied) return;
    applied = false;
    table.clear();
}

void CSOL_ChapterCarryRoundStarted()
{
    // 108253B0 applies the world state only once. Its room/manager/rules
    // fields and MPToCL {27, 25} have no host owner (see the report).
    applied = true;
}

bool CSOL_ChapterCarryPending(CBasePlayer& player)
{
    return table.find(Key(player)) != table.end();
}

void CSOL_ChapterCarryRestore(CCSOLScenarioRuntime& runtime, CBasePlayer& player, bool gameStarted)
{
    // 10826140
    if (table.empty()) return;
    CSOLScenarioSdkScope sdk;
    if (!sdk.Current() || runtime.Closing()) return;
    const EHANDLE handle(&player);
    const int spawns = player.m_iNumSpawns;
    const auto current = [&] {
        return sdk.Current() && !runtime.Closing() && handle.Get<CBasePlayer>() == &player &&
            !player.has_disconnected && player.m_iNumSpawns == spawns && player.IsAlive();
    };
    const int key = Key(player);
    if (!current()) return;
    const auto found = table.find(key);
    if (found == table.end()) return;
    const Snapshot snapshot = found->second;
    int missingWeapon{};
    // 114D9A20: money (114EA380), frags, deaths.
    player.AddAccount(snapshot.money - player.m_iAccount, RT_NONE, true); // host: keep the HUD in sync
    if (!current()) return;
    player.pev->frags = static_cast<float>(snapshot.frags);
    player.m_iDeaths = snapshot.deaths;
    if (!snapshot.dead)
    {
        player.pev->health = std::fmax(1.0f, static_cast<float>(snapshot.health));
        player.pev->armorvalue = static_cast<float>(snapshot.armor);
        player.pev->armortype = static_cast<float>(snapshot.armorType);
        player.m_iKevlar = static_cast<ArmorType>(snapshot.kevlar);
        // 114D9BD0: RemoveAllItems(0), then each saved weapon; the clip is
        // not restored (a3 = 0), the ammo block is copied afterwards.
        player.RemoveAllItems(FALSE);
        if (!current()) return;
        for (const int weapon : snapshot.weapons)
        {
            if (!CSOL_RestoreScenarioWeapon(&player, weapon) && !missingWeapon) missingWeapon = weapon;
            if (!current()) return;
        }
        std::copy(snapshot.ammo.begin(), snapshot.ammo.end(), std::begin(player.m_rgAmmo));
        CSOL_UpdateWeaponInventory(&player);
        if (!current()) return;
    }
    // 108F7AF0
    if (snapshot.upgrades && runtime.session)
        if (const auto players = runtime.session->Players())
            if (const auto state = players->Upgrades(handle))
                state->RestoreProgress(snapshot.attributes, snapshot.experience);
    if (!current()) return;
    // Spawn tail: health = max(1, restored health).
    player.pev->health = std::fmax(1.0f, player.pev->health);
    // Consumed once the game has started, for a non-bot player.
    if (gameStarted && !player.IsBot()) table.erase(key);
    if (missingWeapon)
        ALERT(at_warning, "[CSOL] Scenario chapter could not restore weapon ID %d\n", missingWeapon);
}
