// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "melee_purchase.h"
#include "wpn_melee.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "game_rules_gate.h"
#include "scenario_player_runtime.h"
#include "../melee_identity.hpp"
#include "../melee_resources.hpp"
#include <array>
#include <vector>

namespace {
// This guard also covers direct callers of the delivery helper. The public
// weapon service additionally prevents cross-weapon reentry during its debit.
class PurchaseGuard {
public:
    explicit PurchaseGuard(int index)
    {
        if (index > 0 && index < int(busy.size()) && !busy[index]) {
            flag = &busy[index]; *flag = true;
        }
    }
    ~PurchaseGuard() { if (flag) *flag = false; }
    explicit operator bool() const { return flag != nullptr; }
private:
    inline static std::array<bool, 33> busy{};
    bool* flag{};
};
bool Contains(const CBasePlayer& player, const CBaseEntity* entity)
{
    int remaining = gpGlobals->maxEntities;
    for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
        for (auto* item = player.m_rgpPlayerItems[slot]; item && remaining-- > 0; item = item->m_pNext)
            if (item == entity) return true;
    return false;
}
bool Linked(const CSOLScenarioSdkScope& sdk, const CBaseEntity* entity)
{
    if (!sdk.Current() || !gpGlobals) return true;
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* found = UTIL_PlayerByIndex(slot);
        if (!sdk.Current()) return true;
        auto* player = dynamic_cast<CBasePlayer*>(found);
        if (!player) continue;
        int remaining = gpGlobals->maxEntities;
        for (auto* first : player->m_rgpPlayerItems)
            for (auto* item = first; item; item = item->m_pNext) {
                if (--remaining <= 0 || item == entity) return true;
            }
    }
    return false;
}
// Undo only this transaction's insertion. ReAPI may return FALSE after the
// native AddPlayerItem inserted the weapon, and may then veto removal. A
// failed grant must not leave a free item or a pointer to an entity we delete.
// Normal removal still goes through the hook before this local compensation.
bool UnlinkFresh(CBasePlayer& player, CBasePlayerWeapon& weapon)
{
    int remaining = gpGlobals->maxEntities;
    for (auto& first : player.m_rgpPlayerItems) {
        auto** link = &first;
        while (*link) {
            if (--remaining <= 0) return false;
            if (*link == &weapon) { *link = weapon.m_pNext; break; }
            link = &(*link)->m_pNext;
        }
    }
    if (Contains(player, &weapon)) return false;
    if (player.m_pActiveItem == &weapon) {
        player.m_pActiveItem = nullptr;
        player.pev->viewmodel = player.pev->weaponmodel = 0;
    }
    if (player.m_pLastItem == &weapon) player.m_pLastItem = nullptr;
    if (player.m_pClientActiveItem == &weapon) player.m_pClientActiveItem = nullptr;
    weapon.m_pNext = nullptr;
    return true;
}
// Native Deploy can change these timers even when Touch is later rejected by
// a hook. Restore only surviving weapons still owned by this same player.
struct Timers {
    EHANDLE handle;
    float primary, secondary, idle, lastFire, previousAttack, decreaseShots, accuracy, nextReload;
    int shots, inReload, specialReload;
    bool delay;
    explicit Timers(CBasePlayerWeapon& weapon) : handle(&weapon),
        primary(weapon.m_flNextPrimaryAttack), secondary(weapon.m_flNextSecondaryAttack),
        idle(weapon.m_flTimeWeaponIdle), lastFire(weapon.m_flLastFireTime),
        previousAttack(weapon.m_flPrevPrimaryAttack), decreaseShots(weapon.m_flDecreaseShotsFired),
        accuracy(weapon.m_flAccuracy), nextReload(weapon.m_flNextReload),
        shots(weapon.m_iShotsFired), inReload(weapon.m_fInReload),
        specialReload(weapon.m_fInSpecialReload), delay(weapon.m_bDelayFire) {}
    void Restore(CBasePlayer& player) const
    {
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(handle.Get<CBaseEntity>());
        if (!weapon || (weapon->pev->flags & FL_KILLME) || weapon->m_pPlayer != &player ||
            weapon->pev->owner != player.edict() || !Contains(player, weapon)) return;
        weapon->m_flNextPrimaryAttack = primary; weapon->m_flNextSecondaryAttack = secondary;
        weapon->m_flTimeWeaponIdle = idle; weapon->m_flLastFireTime = lastFire;
        weapon->m_flPrevPrimaryAttack = previousAttack; weapon->m_flDecreaseShotsFired = decreaseShots;
        weapon->m_flAccuracy = accuracy; weapon->m_flNextReload = nextReload;
        weapon->m_iShotsFired = shots; weapon->m_fInReload = inReload;
        weapon->m_fInSpecialReload = specialReload; weapon->m_bDelayFire = delay;
    }
};
}

bool CSOL_DeliverMeleeWeapon(CBasePlayer& buyer, int weaponId)
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Current() || !gpGlobals || !csol::melee::IsWeapon(weaponId) ||
        !CSOL_WeaponsReady() || !CSOL_MeleeReady(weaponId) || !CSOL_CanUseExtendedWeapons(&buyer)) return false;
    const PurchaseGuard guard(buyer.entindex());
    if (!guard || !sdk.Current()) return false;
    const EHANDLE playerHandle(&buyer);
    const int spawns = buyer.m_iNumSpawns;
    const int userid = GETPLAYERUSERID(buyer.edict());
    if (!sdk.Current() || playerHandle.Get<CBaseEntity>() != &buyer) return false;
    const auto players = sdk.Scenario() ? CSOL_ScenarioPlayerAuthority(sdk.EntityAccess().get()) : nullptr;
    const auto state = players ? players->Find(playerHandle) : nullptr;
    if (sdk.Scenario() && !state) return false;
    const auto generation = state ? state->generation : 0;
    const auto samePlayer = [&]() {
        if (!sdk.Current() || playerHandle.Get<CBaseEntity>() != &buyer) return false;
        const auto now = GETPLAYERUSERID(buyer.edict());
        return sdk.Current() && playerHandle.Get<CBaseEntity>() == &buyer &&
            !buyer.has_disconnected && now == userid && buyer.m_iNumSpawns == spawns &&
            !(buyer.pev->flags & (FL_KILLME | FL_DORMANT));
    };
    const auto current = [&]() {
        return samePlayer() &&
            buyer.IsAlive() && !buyer.HasShield() &&
            (sdk.Scenario() ? players->Active() && players->Find(playerHandle) == state &&
                state->generation == generation && buyer.m_iTeam == CT && CSOL_ScenarioCanBuy() :
                CSOL_ZombieHuman(&buyer)) && samePlayer();
    };
    // A knife has no replenishable ammunition. Reacquiring the same kind is
    // rejected before Spawn, so neither cooldown nor completion serial resets.
    if (!current() || CSOL_HasWeapon(&buyer, weaponId)) return false;
    auto* prior = dynamic_cast<CBasePlayerWeapon*>(buyer.m_rgpPlayerItems[KNIFE_SLOT]);
    if (buyer.m_rgpPlayerItems[KNIFE_SLOT] &&
        (!prior || prior->m_pNext || prior->m_pPlayer != &buyer || prior->pev->owner != buyer.edict())) return false;
    EHANDLE original(prior), active(buyer.m_pActiveItem), last(buyer.m_pLastItem), fresh;
    CBaseEntity* createdObject{};
    const float nextAttack = buyer.m_flNextAttack;
    const int priorMask = prior ? CSOL_LegacyWeaponMask(prior->m_iId) : 0;
    const int priorMaskValue = buyer.pev->weapons & priorMask;
    const int hiddenWeapons = buyer.m_iHideHUD & HIDEHUD_WEAPONS;
    std::vector<Timers> timers;
    int remaining = gpGlobals->maxEntities;
    for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
        for (auto* item = buyer.m_rgpPlayerItems[slot]; item && remaining-- > 0; item = item->m_pNext)
            if (auto* weapon = dynamic_cast<CBasePlayerWeapon*>(item)) timers.emplace_back(*weapon);
    if (remaining <= 0) return false;
    const auto live = [&](const EHANDLE& handle) -> CBasePlayerWeapon* {
        if (!sdk.Current()) return nullptr;
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(handle.Get<CBaseEntity>());
        return weapon && !(weapon->pev->flags & FL_KILLME) ? weapon : nullptr;
    };
    const auto acquired = [&]() {
        if (!current()) return false;
        auto* weapon = live(fresh);
        return weapon && weapon == createdObject && CSOL_MeleeItemIdentity(*weapon) && weapon->m_iId == weaponId &&
            weapon->m_pPlayer == &buyer && weapon->pev->owner == playerHandle.Get() && Contains(buyer, weapon);
    };
    const auto selected = [&]() {
        if (!acquired()) return false;
        auto* weapon = live(fresh);
        return weapon && buyer.m_pActiveItem == weapon && CSOL_MeleeDeploymentComplete(*weapon);
    };
    const auto freshOwned = [&]() -> CBasePlayerWeapon* {
        const bool ownPlayer = samePlayer();
        // Pending deletion still leaves private data alive until the engine
        // reclaims it. Unlink that exact object now; live() deliberately hides
        // FL_KILLME and must not be used for inventory compensation.
        auto* weapon = sdk.Current() ? dynamic_cast<CBasePlayerWeapon*>(fresh.Get<CBaseEntity>()) : nullptr;
        return weapon && weapon == createdObject &&
            ((!weapon->m_pPlayer && !weapon->pev->owner) ||
                (ownPlayer && (!weapon->m_pPlayer || weapon->m_pPlayer == &buyer) &&
                    (!weapon->pev->owner || weapon->pev->owner == playerHandle.Get()))) ? weapon : nullptr;
    };
    bool unlinkAttempted{};
    const auto reject = [&]() {
        auto* weapon = freshOwned();
        // Death/role loss rejects purchase, but the same surviving player can
        // still have this transaction's partial insertion removed safely.
        if (weapon && samePlayer() && Contains(buyer, weapon)) buyer.RemovePlayerItem(weapon);
        weapon = freshOwned();
        if (weapon && samePlayer()) UnlinkFresh(buyer, *weapon);
        weapon = freshOwned();
        if (weapon && !Linked(sdk, weapon) && freshOwned() == weapon) {
            weapon->SetTouch(nullptr);
            weapon->SetThink(nullptr);
            weapon->pev->nextthink = 0;
            weapon->pev->solid = SOLID_NOT;
            if (!(weapon->pev->flags & FL_KILLME)) UTIL_Remove(weapon);
        }
        if (!current()) {
            // Do not resurrect an unlinked knife after death/infection. It
            // still needs retirement if our own removal detached it before a
            // post hook invalidated purchase eligibility.
            if (unlinkAttempted && samePlayer()) {
                auto* old = live(original);
                if (old && old == prior && !Linked(sdk, old) && samePlayer() &&
                    live(original) == old && old->m_pPlayer == &buyer &&
                    old->pev->owner == playerHandle.Get()) UTIL_Remove(old);
            }
            return false;
        }
        auto* old = live(original);
        if (unlinkAttempted && old && old == prior && !Contains(buyer, old) && !Linked(sdk, old) &&
            current() && live(original) == old &&
            old->m_pPlayer == &buyer && old->pev->owner == playerHandle.Get()) {
            // Native RemovePlayerItem leaves owner, m_pPlayer and m_pNext
            // intact. Restore the original empty slot without another grant,
            // pickup notification or hookable AddPlayerItem transaction.
            if (!buyer.m_rgpPlayerItems[KNIFE_SLOT]) {
                old->m_pNext = nullptr;
                buyer.m_rgpPlayerItems[KNIFE_SLOT] = old;
            }
        }
        if (!current()) return false;
        if (auto* previous = live(active); previous && Contains(buyer, previous) &&
            previous->pev->owner == playerHandle.Get() && buyer.m_pActiveItem != previous)
            buyer.SwitchWeapon(previous);
        if (!current()) return false;
        auto* previousLast = live(last);
        if (!previousLast || (Contains(buyer, previousLast) && previousLast->pev->owner == playerHandle.Get()))
            buyer.m_pLastItem = previousLast;
        buyer.m_flNextAttack = nextAttack;
        for (const auto& saved : timers) saved.Restore(buyer);
        if (old && old == prior && live(original) == old && Contains(buyer, old))
            buyer.pev->weapons = (buyer.pev->weapons & ~priorMask) | priorMaskValue;
        buyer.m_iHideHUD = (buyer.m_iHideHUD & ~HIDEHUD_WEAPONS) | hiddenWeapons;
        CSOL_UpdateWeaponInventory(&buyer);
        return false;
    };
    const auto& resource = csol::melee::Resource(csol::melee::WeaponVariant(weaponId));
    const auto name = ALLOC_STRING(resource.classname);
    if (!current()) return false;
    auto* entity = CREATE_NAMED_ENTITY(name);
    // A closed map token forbids dereferencing the returned edict. Engine map
    // teardown owns old-map entities; never remove an edict from a new map.
    if (!sdk.Current() || FNullEnt(entity)) return false;
    fresh = EHANDLE(entity);
    createdObject = CBaseEntity::Instance(entity);
    if (!createdObject) { REMOVE_ENTITY(entity); return false; }
    if (!current()) return reject();
    entity->v.origin = buyer.pev->origin;
    entity->v.spawnflags |= SF_NORESPAWN;
    const int spawned = DispatchSpawn(entity);
    if (!current()) return reject();
    auto* weapon = live(fresh);
    if (spawned < 0 || !weapon || weapon->m_iId != weaponId || !CSOL_MeleeItemIdentity(*weapon) ||
        (sdk.Scenario() && !CSOL_ScenarioSetMeleeIdentity(*weapon))) return reject();
    DispatchTouch(fresh.Get(), playerHandle.Get());
    if (!acquired()) return reject();
    buyer.SelectItem(resource.classname);
    if (!selected()) return reject();
    if (auto* old = live(original)) {
        if (!Contains(buyer, old) || old->m_pPlayer != &buyer || old->pev->owner != playerHandle.Get()) return reject();
        // RemovePlayerItem unlinks but does not destroy the old entity. Keep
        // that entity available for rollback through the hook's post stage.
        unlinkAttempted = true;
        const BOOL removed = buyer.RemovePlayerItem(old);
        if (!current()) return reject();
        old = live(original);
        const bool detached = old && old == prior && !Contains(buyer, old) && !Linked(sdk, old) &&
            current() && live(original) == old &&
            old->m_pPlayer == &buyer && old->pev->owner == playerHandle.Get();
        if (!removed || !detached || !selected()) return reject();
    }
    auto* added = live(fresh);
    if (!selected() || buyer.m_rgpPlayerItems[KNIFE_SLOT] != added || added->m_pNext) return reject();
    // Commit. The new knife is the sole slot owner before the original is
    // destroyed; no DropPlayerItem call can leave an undroppable duplicate.
    if (auto* old = live(original); unlinkAttempted && old) {
        buyer.pev->weapons &= ~CSOL_LegacyWeaponMask(old->m_iId);
        if (buyer.m_pClientActiveItem == old) buyer.m_pClientActiveItem = nullptr;
        UTIL_Remove(old);
        if (live(original)) return reject();
    }
    if (current()) CSOL_UpdateWeaponInventory(&buyer);
    return selected() ? true : reject();
}
