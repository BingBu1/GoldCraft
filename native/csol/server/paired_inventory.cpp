// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "paired_inventory.h"
#include "weapons/wpn_skull3.h"
#include "scenario/core/game_rules_gate.h"
#include "weapons/weapon_slots.h"
#include "scenario/core/scenario_player_runtime.h"
#include "scenario/core/scenario_runtime.h"
#include "scenario/core/scenario_session.h"
#include "../weapons/skull3.hpp"
#include <array>

namespace { const CBasePlayerItem* droppingGroup{}; }
CSOLWeaponGroupDropScope::CSOLWeaponGroupDropScope(const CBasePlayerItem* selected) : previous(droppingGroup)
{
    droppingGroup = CSOL_PairedWeapon(selected) ? selected : nullptr;
}
CSOLWeaponGroupDropScope::~CSOLWeaponGroupDropScope() { droppingGroup = previous; }
bool CSOL_ExcludedFromGroupDrop(const CBasePlayerItem* candidate, const CBasePlayerItem* current)
{
    return current == droppingGroup && CSOL_SameWeaponGroup(candidate, current);
}

bool CSOL_PairedWeapon(const CBasePlayerItem* item)
{
    const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(item);
    return weapon && CSOL_IsSkull3(*weapon);
}
bool CSOL_SameWeaponGroup(const CBasePlayerItem* first, const CBasePlayerItem* second)
{
    return CSOL_PairedWeapon(first) && CSOL_PairedWeapon(second);
}

namespace {
bool Contains(CBasePlayerItem* head, const CBasePlayerItem* wanted)
{
    for (int remaining = gpGlobals->maxEntities; head && remaining-- > 0; head = head->m_pNext)
        if (head == wanted) return true;
    return false;
}
bool Unlink(CBasePlayerItem*& head, CBasePlayerItem& item)
{
    auto** link = &head;
    for (int remaining = gpGlobals->maxEntities; *link && remaining-- > 0; link = &(*link)->m_pNext)
        if (*link == &item) { *link = item.m_pNext; item.m_pNext = nullptr; return true; }
    return false;
}
void UnlinkReleased(CBasePlayerItem*& head, const CBasePlayerItem* item, CBasePlayerItem* successor)
{
    // AddPlayerItem prepends before its post hook. A hook may free that exact
    // insertion without unlinking it; never read the released node's m_pNext.
    auto** link = &head;
    for (int remaining = gpGlobals->maxEntities; *link && remaining-- > 0; link = &(*link)->m_pNext)
        if (*link == item) { *link = successor; return; }
}
struct RoundGuard
{
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::uint64_t generation{};
    explicit RoundGuard(const CSOLScenarioSdkScope& scope)
        : runtime(scope.Scenario() ? CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get()) : nullptr),
          generation(runtime && runtime->session ? runtime->session->Map().RoundGeneration() : 0) {}
    bool Current(const CSOLScenarioSdkScope& scope) const
    {
        return scope.Current() && (!scope.Scenario() || (runtime && !runtime->Closing() &&
            runtime->session && runtime->session->Map().RoundGeneration() == generation));
    }
};
void StoreInBox(CWeaponBox& box, CBasePlayerItem& item)
{
    // Same native PackWeapon ownership/physics tail; callers have already
    // removed the player link and validated every callback's lifetime.
    item.pev->spawnflags |= SF_NORESPAWN;
    item.pev->movetype = MOVETYPE_NONE;
    item.pev->solid = SOLID_NOT;
    item.pev->effects = EF_NODRAW;
    item.pev->modelindex = item.pev->model = 0;
    item.pev->owner = box.edict();
    item.pev->aiment = nullptr;
    item.SetThink(nullptr); item.SetTouch(nullptr);
    item.m_pPlayer = nullptr;
}
struct TouchGuard
{
    inline static TouchGuard* active{};
    TouchGuard* previous{};
    CWeaponBox* box{};
    CBasePlayer* player{};
    TouchGuard(CWeaponBox& b, CBasePlayer& p) : previous(active), box(&b), player(&p) { active = this; }
    ~TouchGuard() { active = previous; }
    static bool Busy(const CWeaponBox& box, const CBasePlayer& player)
    {
        for (auto* guard = active; guard; guard = guard->previous)
            if (guard->box == &box || guard->player == &player) return true;
        return false;
    }
};
struct PickupState
{
    EHANDLE active, last, clientActive;
    float nextAttack, maxSpeed, primary{}, secondary{}, idle{}, accuracy{}, decreaseShots{}, nextReload{};
    float lastFire{}, lastAttempt{}, previousAttack{};
    int hide, view, world, animation, fov, lastZoom, shots{}, inReload{}, specialReload{};
    bool resumeZoom, delayed{};
    std::array<byte, MAX_ITEM_TYPES> position{}, valid{};
    std::array<char, 32> extension{};
    CSOLSkull3PurchaseState clip;
    int ammoIndex, reserve;
    explicit PickupState(CBasePlayer& p, CBasePlayerWeapon& item) : active(p.m_pActiveItem), last(p.m_pLastItem),
        clientActive(p.m_pClientActiveItem), nextAttack(p.m_flNextAttack), maxSpeed(p.pev->maxspeed), hide(p.m_iHideHUD),
        view(p.pev->viewmodel), world(p.pev->weaponmodel), animation(p.pev->weaponanim), fov(p.m_iFOV),
        lastZoom(p.m_iLastZoom), resumeZoom(p.m_bResumeZoom), clip(CSOL_SaveSkull3PurchaseState(item)),
        ammoIndex(item.m_iPrimaryAmmoType), reserve(ammoIndex >= 0 && ammoIndex < MAX_AMMO_SLOTS ? p.m_rgAmmo[ammoIndex] : 0)
    {
        std::copy_n(p.m_csolSlotPos, MAX_ITEM_TYPES, position.begin());
        std::copy_n(p.m_csolValidSlotPos, MAX_ITEM_TYPES, valid.begin());
        static_assert(sizeof(p.m_szAnimExtention) == sizeof(extension));
        std::copy_n(p.m_szAnimExtention, extension.size(), extension.begin());
        if (auto* weapon = dynamic_cast<CBasePlayerWeapon*>(p.m_pActiveItem))
        {
            primary = weapon->m_flNextPrimaryAttack; secondary = weapon->m_flNextSecondaryAttack;
            idle = weapon->m_flTimeWeaponIdle; accuracy = weapon->m_flAccuracy;
            decreaseShots = weapon->m_flDecreaseShotsFired; shots = weapon->m_iShotsFired;
            delayed = weapon->m_bDelayFire; inReload = weapon->m_fInReload;
            specialReload = weapon->m_fInSpecialReload; nextReload = weapon->m_flNextReload;
            lastFire = weapon->m_flLastFireTime; lastAttempt = weapon->m_flLastFire;
            previousAttack = weapon->m_flPrevPrimaryAttack;
        }
    }
    void Restore(CBasePlayer& player) const
    {
        if (ammoIndex >= 0 && ammoIndex < MAX_AMMO_SLOTS) player.m_rgAmmo[ammoIndex] = reserve;
        // Only restore pointers still actually owned by this player. A plugin
        // transfer never gives this failed pickup permission to reclaim them.
        const auto owned = [&](const EHANDLE& handle) -> CBasePlayerItem* {
            auto* value = handle.Get<CBasePlayerItem>();
            if (!value) return nullptr;
            for (auto* head : player.m_rgpPlayerItems)
                if (Contains(head, value) && value->m_pPlayer == &player) return value;
            return nullptr;
        };
        player.m_pActiveItem = owned(active); player.m_pLastItem = owned(last);
        player.m_pClientActiveItem = owned(clientActive);
        player.m_flNextAttack = nextAttack; player.m_iHideHUD = hide;
        player.pev->viewmodel = player.m_pActiveItem ? view : 0;
        player.pev->weaponmodel = player.m_pActiveItem ? world : 0;
        player.pev->weaponanim = animation;
        player.pev->fov = float(fov); player.m_iFOV = fov;
        player.m_iLastZoom = lastZoom; player.m_bResumeZoom = resumeZoom;
        player.pev->maxspeed = maxSpeed;
        std::copy(extension.begin(), extension.end(), player.m_szAnimExtention);
        std::copy(position.begin(), position.end(), player.m_csolSlotPos);
        std::copy(valid.begin(), valid.end(), player.m_csolValidSlotPos);
        if (auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem))
        {
            weapon->m_flNextPrimaryAttack = primary; weapon->m_flNextSecondaryAttack = secondary;
            weapon->m_flTimeWeaponIdle = idle; weapon->m_flAccuracy = accuracy;
            weapon->m_flDecreaseShotsFired = decreaseShots; weapon->m_iShotsFired = shots;
            weapon->m_bDelayFire = delayed; weapon->m_fInReload = inReload;
            weapon->m_fInSpecialReload = specialReload; weapon->m_flNextReload = nextReload;
            weapon->m_flLastFireTime = lastFire; weapon->m_flLastFire = lastAttempt;
            weapon->m_flPrevPrimaryAttack = previousAttack;
        }
        player.m_bHasPrimary = player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] != nullptr;
    }
};
}

bool CSOL_PackWeaponGroup(CWeaponBox& box, CBasePlayer& owner, CBasePlayerItem& selected, bool packAmmo)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Current() || !CSOL_PairedWeapon(&selected)) return false;
    const RoundGuard round(scope);
    const EHANDLE ownerHandle(&owner), boxHandle(&box);
    const int life = owner.m_iNumSpawns;
    const int slot = selected.iItemSlot();
    std::array<EHANDLE, 2> items;
    int count{};
    bool found{};
    for (auto* item = owner.m_rgpPlayerItems[slot]; item; item = item->m_pNext)
    {
        if (count == int(items.size()) || !CSOL_SameWeaponGroup(item, &selected) ||
            item->m_pPlayer != &owner || item->pev->owner != owner.edict()) return false;
        found |= item == &selected;
        items[count++] = item;
    }
    if (!found) return false;
    if (count == 2)
    {
        const auto* first = items[0].Get<CBasePlayerItem>();
        if (csol::skull3::Position(first->m_iId == csol::skull3::DualId) == owner.m_csolValidSlotPos[slot - 1])
            std::swap(items[0], items[1]);
    }
    const int ammoIndex = selected.PrimaryAmmoIndex();
    const int reserve = ammoIndex >= 0 && ammoIndex < MAX_AMMO_SLOTS ? owner.m_rgAmmo[ammoIndex] : 0;
    const auto current = [&]() {
        return round.Current(scope) && ownerHandle.Get<CBaseEntity>() == &owner && boxHandle.Get<CBaseEntity>() == &box &&
            !(box.pev->flags & FL_KILLME) && !(owner.pev->flags & (FL_KILLME | FL_DORMANT)) &&
            !owner.has_disconnected && owner.m_iNumSpawns == life;
    };
    for (int i = 0; i < count; ++i)
    {
        if (!current()) return false;
        auto* item = items[i].Get<CBasePlayerItem>();
        if (!item || !CSOL_PairedWeapon(item) || item->m_pPlayer != &owner ||
            item->pev->owner != owner.edict() || (item->pev->flags & FL_KILLME)) return false;
        if (box.HasWeapon(item)) return false;
        if (owner.m_pActiveItem == item)
        {
            item->Holster();
            if (!current() || items[i].Get<CBasePlayerItem>() != item) return false;
        }
        const bool removed = owner.RemovePlayerItem(item) != FALSE;
        if (!current() || items[i].Get<CBasePlayerItem>() != item || !removed ||
            Contains(owner.m_rgpPlayerItems[slot], item) || (item->pev->flags & FL_KILLME) ||
            item->m_pPlayer != &owner || item->pev->owner != owner.edict()) return false;
        item->m_pNext = box.m_rgpPlayerItems[slot]; box.m_rgpPlayerItems[slot] = item;
        StoreInBox(box, *item);
        if (packAmmo && reserve > 0)
        {
            // MP 11506460 calls PackAmmo for EACH entity and caps the shared
            // named entry. Do not collapse this to a single reserve transfer.
            box.GiveAmmo(reserve, const_cast<char*>(csol::skull3::AmmoName), csol::skull3::Reserve);
        }
    }
    if (!current()) return false;
    // Host ammodrop's explicit override also reaches this branch. Ordinary
    // manual CSO drop has flags0 and does not move reserves; death clears them
    // immediately afterward in RemoveAllItems.
    if (packAmmo && ammoIndex >= 0 && ammoIndex < MAX_AMMO_SLOTS) owner.m_rgAmmo[ammoIndex] = 0;
    return true;
}

bool CSOL_TouchWeaponGroup(CWeaponBox& box, CBasePlayer& player)
{
    constexpr int slot = PRIMARY_WEAPON_SLOT;
    if (!CSOL_PairedWeapon(box.m_rgpPlayerItems[slot])) return false;
    // Original 1154D273 records slot occupancy once, before both entities.
    CSOLScenarioSdkScope scope;
    if (!scope.Current() || player.m_rgpPlayerItems[slot] || player.HasShield() ||
        TouchGuard::Busy(box, player)) return true;
    const RoundGuard round(scope);
    TouchGuard lock(box, player);
    const EHANDLE boxHandle(&box), playerHandle(&player);
    const int life = player.m_iNumSpawns;
    const auto current = [&] {
        return round.Current(scope) && boxHandle.Get<CBaseEntity>() == &box &&
            playerHandle.Get<CBaseEntity>() == &player && !(box.pev->flags & FL_KILLME) &&
            !(player.pev->flags & (FL_KILLME | FL_DORMANT)) && player.m_iNumSpawns == life &&
            !player.has_disconnected && player.IsAlive();
    };
    if (!current()) return true;
    player.OnTouchingWeapon(&box);
    if (!current() || player.HasShield() || player.m_rgpPlayerItems[slot]) return true;
    std::array<EHANDLE, 2> items;
    int count{};
    for (auto* item = box.m_rgpPlayerItems[slot]; item; item = item->m_pNext)
    {
        if (count == int(items.size()) || !CSOL_PairedWeapon(item) || item->m_pPlayer ||
            item->pev->owner != box.edict()) return true;
        items[count++] = item;
    }
    EHANDLE given;
    std::array<EHANDLE, 2> acceptedItems;
    int acceptedCount{};
    const auto expectedInventory = [&] {
        int remaining = acceptedCount;
        for (auto* item = player.m_rgpPlayerItems[slot]; item; item = item->m_pNext)
        {
            if (remaining-- <= 0) return false;
            bool expected{};
            for (int i = 0; i < acceptedCount; ++i) expected |= acceptedItems[i].Get<CBasePlayerItem>() == item;
            if (!expected || item->m_pPlayer != &player || item->pev->owner != player.edict()) return false;
        }
        return remaining == 0;
    };
    for (int index = 0; index < count; ++index)
    {
        if (!current() || player.HasShield() || !expectedInventory()) return true;
        auto* item = items[index].Get<CBasePlayerWeapon>();
        if (!item || !Contains(box.m_rgpPlayerItems[slot], item) || item->m_pPlayer ||
            item->pev->owner != box.edict() || (item->pev->flags & FL_KILLME)) continue;
        if (player.IsBot() && TheCSBots() && !TheCSBots()->IsWeaponUseable(item)) return true;
        if (!current() || items[index].Get<CBasePlayerWeapon>() != item) return true;
        const bool restricted = player.HasRestrictItem(static_cast<ItemID>(item->m_iId), ITEM_TYPE_TOUCHED);
        if (!current() || items[index].Get<CBasePlayerWeapon>() != item) return true;
        if (restricted || player.HasShield() || !expectedInventory()) return true;
        if (!Contains(box.m_rgpPlayerItems[slot], item) || item->m_pPlayer ||
            item->pev->owner != box.edict() || (item->pev->flags & FL_KILLME)) return true;
        // Original BoxTouch v244 -> 11432860 rejects any nonzero byte21F2.
        // Reuse the existing authoritative scenario profile, not a new client
        // flag or a guessed substitution of the native CanHavePlayerItem API.
        if (scope.Scenario())
        {
            const auto players = round.runtime->players;
            const auto state = players ? players->Find(playerHandle) : nullptr;
            if (!state || state->profile.weaponEffectsBlocked21F2) continue;
        }
        PickupState before(player, *item);
        const EHANDLE previousHead(player.m_rgpPlayerItems[slot]);
        // m_pNext belongs to exactly one list. Detach before AddPlayerItem,
        // whose post hooks may return FALSE after inserting or freeing it.
        if (!Unlink(box.m_rgpPlayerItems[slot], *item)) return true;
        const bool accepted = player.AddPlayerItem(item) != FALSE;
        if (!current()) return true;
        if (items[index].Get<CBasePlayerWeapon>() != item)
        {
            UnlinkReleased(player.m_rgpPlayerItems[slot], item, previousHead.Get<CBasePlayerItem>());
            before.Restore(player);
            continue;
        }
        const bool linked = Contains(player.m_rgpPlayerItems[slot], item);
        if (accepted && linked && item->m_pPlayer == &player && !(item->pev->flags & FL_KILLME))
        {
            item->AttachToPlayer(&player);
            if (!current() || items[index].Get<CBasePlayerWeapon>() != item) return true;
            if (!Contains(player.m_rgpPlayerItems[slot], item) || item->m_pPlayer != &player ||
                item->pev->owner != player.edict()) return true;
            given = item;
            acceptedItems[acceptedCount++] = item;
            continue;
        }
        // Compensate a rejected post hook without stealing an item transferred
        // to somebody else. The remaining original box entities stay linked.
        if (item->m_pPlayer && item->m_pPlayer != &player)
        {
            UnlinkReleased(player.m_rgpPlayerItems[slot], item, previousHead.Get<CBasePlayerItem>());
            before.Restore(player);
            continue;
        }
        if (linked)
        {
            player.RemovePlayerItem(item);
            if (!current()) return true;
            if (items[index].Get<CBasePlayerWeapon>() != item)
            {
                UnlinkReleased(player.m_rgpPlayerItems[slot], item, previousHead.Get<CBasePlayerItem>());
                before.Restore(player);
                continue;
            }
            if (item->m_pPlayer && item->m_pPlayer != &player)
            {
                UnlinkReleased(player.m_rgpPlayerItems[slot], item, previousHead.Get<CBasePlayerItem>());
                before.Restore(player);
                continue;
            }
            Unlink(player.m_rgpPlayerItems[slot], *item);
        }
        if (item->pev->flags & FL_KILLME) { before.Restore(player); continue; }
        if (item->pev->owner && item->pev->owner != box.edict() && item->pev->owner != player.edict()) continue;
        CSOL_RestoreSkull3PurchaseState(*item, before.clip);
        before.Restore(player);
        StoreInBox(box, *item);
        // A rejected first item stays in front of its companion. If the
        // companion was already consumed, it remains the sole box item.
        item->m_pNext = box.m_rgpPlayerItems[slot]; box.m_rgpPlayerItems[slot] = item;
        if (index == 1 && items[0].Get<CBasePlayerItem>() == item->m_pNext)
        {
            auto* first = item->m_pNext;
            item->m_pNext = first->m_pNext; first->m_pNext = item; box.m_rgpPlayerItems[slot] = first;
        }
    }
    if (!current()) return true;
    bool empty = true;
    for (auto* item : box.m_rgpPlayerItems) empty &= item == nullptr;
    if (empty)
    {
        for (int i = 0; i < MAX_AMMO_SLOTS; ++i)
        {
            if (FStringNull(box.m_rgiszAmmo[i])) continue;
            const auto name = box.m_rgiszAmmo[i]; const int amount = box.m_rgAmmo[i];
            // Move ownership before the callback, preventing nested Touch from
            // granting the same box ammunition twice.
            box.m_rgiszAmmo[i] = iStringNull; box.m_rgAmmo[i] = 0;
            if (amount > 0) player.GiveAmmo(amount, STRING(name), amount);
            if (!current()) return true;
        }
    }
    auto* latest = given.Get<CBasePlayerItem>();
    if (latest && Contains(player.m_rgpPlayerItems[slot], latest) && latest->m_pPlayer == &player)
    {
        EMIT_SOUND(player.edict(), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM);
        if (!current()) return true;
        latest = given.Get<CBasePlayerItem>();
        if (latest && Contains(player.m_rgpPlayerItems[slot], latest) && player.m_pActiveItem != latest)
        {
            const bool shouldSwitch = CSGameRules()->FShouldSwitchWeapon(&player, latest);
            if (!current()) return true;
            latest = given.Get<CBasePlayerItem>();
            if (shouldSwitch && latest && Contains(player.m_rgpPlayerItems[slot], latest)) player.SwitchWeapon(latest);
            if (!current()) return true;
        }
    }
    CSOL_UpdateWeaponInventory(&player);
    if (!current()) return true;
    empty = true;
    for (auto* item : box.m_rgpPlayerItems) empty &= item == nullptr;
    for (const auto name : box.m_rgiszAmmo) empty &= FStringNull(name);
    if (empty) { box.SetTouch(nullptr); UTIL_Remove(&box); }
    return true;
}
