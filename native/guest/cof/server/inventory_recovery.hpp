// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "external/ReGameDLL_CS/regamedll/dlls/weapons.h"
#include "native/include/goldcraft/inventory_restore.hpp"

namespace guest::cof::server
{
// Native inventory authority is renewed independently from an old transaction
// snapshot or delayed task. The weapon's cold provider authenticates the life.
template<class Weapon>
bool RecoverOwnedInventoryBinding(Weapon& weapon) noexcept
{
    auto* player=weapon.m_pPlayer;
    if (!weapon.pev || !player || !gpGlobals) return false;
    auto* owner=weapon.pev->owner;
    if (!owner || owner->free || owner->pvPrivateData!=player ||
        player->pev!=&owner->v ||
        player->pev->deadflag!=DEAD_NO || player->pev->health<=0) return false;
    const int slot=weapon.iItemSlot();
    if (slot<0 || slot>=MAX_ITEM_TYPES) return false;
    int remaining=gpGlobals->maxEntities;
    for (auto* item=player->m_rgpPlayerItems[slot];item && remaining-->0;item=item->m_pNext)
        if (item==&weapon) return weapon.RestoreInventoryBinding(*player);
    return false;
}

struct InventoryRecoveryResult
{
    unsigned restored{}, deferred{};
    bool current{}, complete{};
};

// Called only after the real spawn/model lifecycle prepared the new backend.
// A deferred item must not revoke that backend for the rest of the inventory.
template<class Current, class Guest>
InventoryRecoveryResult RecoverRetainedGuestInventory(CBasePlayer& player,
    Current current, Guest guest) noexcept
{
    InventoryRecoveryResult result{};
    if (!gpGlobals || !current()) return result;
    int remaining=gpGlobals->maxEntities;
    for (const int slot:{PISTOL_SLOT,PRIMARY_WEAPON_SLOT})
        for (auto* item=player.m_rgpPlayerItems[slot];item && remaining-->0;)
        {
            const EHANDLE next(item->m_pNext);
            auto* weapon=dynamic_cast<CBasePlayerWeapon*>(item);
            if (weapon && weapon->m_pPlayer==&player && guest(*weapon))
            {
                auto* restorable=dynamic_cast<goldcraft::inventory::RestorableWeapon*>(weapon);
                const bool restored=restorable && restorable->RestoreInventoryBinding(player);
                if (!current()) return result;
                if (restored) ++result.restored;
                else ++result.deferred;
            }
            item=next.Get<CBasePlayerItem>();
        }
    result.current=current();
    result.complete=remaining>=0;
    return result;
}
}
