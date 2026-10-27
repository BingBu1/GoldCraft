// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "wpn_beamgun.h"
#include "wpn_cannon.h"
#include "weapon_service.h"
#include "characters.h"
#include "interface.h"
#include "../../include/goldcraft/csol_weapon_api.hpp"
#include <array>
#include <vector>

namespace {
struct Role { int userid{}, value{}; };
std::array<Role, 33> roles{};
bool zombieMode{};
// Touch/Deploy hooks may synchronously request another purchase. In particular,
// the original cannon is temporarily outside the inventory during replacement.
class CannonPurchaseGuard {
public:
    explicit CannonPurchaseGuard(int index)
    {
        if(index>0 && index<int(busy.size())) {
            flag=&busy[index];
            if(!*flag) { *flag=true;entered=true; }
        }
    }
    ~CannonPurchaseGuard() { if(entered)*flag=false; }
    bool Allowed() const { return !flag || entered; }
private:
    inline static std::array<bool,33> busy{};
    bool* flag{};
    bool entered{};
};
CBasePlayer* Player(int index)
{
    if (index < 1 || index > gpGlobals->maxClients || index >= int(roles.size())) return nullptr;
    auto* player = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
    return player && !player->IsDormant() ? player : nullptr;
}
const char* WeaponName(int id)
{
    switch (id) {
    case CSOL_WEAPON_TKNIFEEX2: return "weapon_tknifeex2";
    case CSOL_WEAPON_BEAMGUN: return "weapon_beamgun";
    case CSOL_WEAPON_BEAMGUNLE: return "weapon_beamgunle";
    case CSOL_WEAPON_CANNON: return "weapon_cannon";
    default: return nullptr;
    }
}
// A rejected replacement must not consume the cannon already owned by the
// buyer. Keep entity identities across plugin callbacks; never copy entvars or
// manufacture a replacement, which would lose the original ammunition/timers.
class CannonPurchaseBackup {
public:
    explicit CannonPurchaseBackup(CBasePlayer* player)
    {
        auto* item=player->GetItemById(static_cast<WeaponIdType>(CSOL_WEAPON_CANNON));
        if(!item || !item->IsWeapon() || !CSOL_ClipAmmo(static_cast<CBasePlayerWeapon*>(item)))return;
        auto* weapon=static_cast<CBasePlayerWeapon*>(item);
        buyer=player;
        original=weapon;active=player->m_pActiveItem;last=player->m_pLastItem;
        index=player->entindex();userid=GETPLAYERUSERID(player->edict());
        hiddenWeapons=player->m_iHideHUD & HIDEHUD_WEAPONS;
        // DropPlayerItem can deploy another gun even when the cannon was not
        // active. Preserve those timers as well as the cannon's own cooldown.
        for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
            for(auto* held=player->m_rgpPlayerItems[slot];held;held=held->m_pNext)
                if(held->IsWeapon())timers.emplace_back(static_cast<CBasePlayerWeapon*>(held));
        clip=weapon->m_iClip;nextAttack=player->m_flNextAttack;
    }
    int Replace()
    {
        auto* player=Buyer();
        if(!player || !Live(original))return 0;
        edict_t* spawned=CREATE_NAMED_ENTITY(ALLOC_STRING("weapon_cannon"));
        if(FNullEnt(spawned))return 0;
        EHANDLE fresh{};fresh.Set(spawned);
        spawned->v.origin=player->pev->origin;
        spawned->v.spawnflags|=SF_NORESPAWN;
        DispatchSpawn(spawned);
        player=Buyer();
        auto* entity=Live(fresh);
        if(!player || !entity) { Discard(fresh);return 0; }

        EHANDLE dropped{};dropped=player->DropPlayerItem("weapon_cannon");
        player=Buyer();entity=Live(fresh);
        if(player && entity && !CSOL_HasWeapon(player,CSOL_WEAPON_CANNON))
            DispatchTouch(entity->edict(),player->edict());
        player=Buyer();entity=Live(fresh);
        if(!Acquired(player,entity))return Reject(fresh,dropped);
        std::vector<EHANDLE> otherPrimary;
        for(auto* held=player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT];held;held=held->m_pNext)
            if(held!=entity) { EHANDLE handle{};handle=held;otherPrimary.push_back(handle); }
        for(auto& handle:otherPrimary) {
            auto* held=Live(handle);
            if(held && Contains(player,held))player->DropPlayerItem(STRING(held->pev->classname));
            player=Buyer();entity=Live(fresh);
            if(!Acquired(player,entity))return Reject(fresh,dropped);
        }
        player->SelectItem("weapon_cannon");
        player=Buyer();entity=Live(fresh);
        if(!Acquired(player,entity))return Reject(fresh,dropped);
        CSOL_UpdateWeaponInventory(player);
        return 1;
    }
private:
    struct WeaponTimers {
        EHANDLE item{};
        float primary,secondary,idle,lastFire,previousAttack,decreaseShots,accuracy,nextReload;
        int shots,inReload,specialReload;
        bool delay;
        explicit WeaponTimers(CBasePlayerWeapon* weapon):
            primary(weapon->m_flNextPrimaryAttack),secondary(weapon->m_flNextSecondaryAttack),
            idle(weapon->m_flTimeWeaponIdle),lastFire(weapon->m_flLastFireTime),
            previousAttack(weapon->m_flPrevPrimaryAttack),decreaseShots(weapon->m_flDecreaseShotsFired),
            accuracy(weapon->m_flAccuracy),nextReload(weapon->m_flNextReload),shots(weapon->m_iShotsFired),
            inReload(weapon->m_fInReload),specialReload(weapon->m_fInSpecialReload),delay(weapon->m_bDelayFire)
        { item=weapon; }
        void Restore(CBasePlayer* player)
        {
            auto* entity=Live(item);
            if(!entity || !Contains(player,entity) || !static_cast<CBasePlayerItem*>(entity)->IsWeapon() ||
                entity->pev->owner!=player->edict())return;
            auto* weapon=static_cast<CBasePlayerWeapon*>(entity);
            weapon->m_flNextPrimaryAttack=primary;weapon->m_flNextSecondaryAttack=secondary;
            weapon->m_flTimeWeaponIdle=idle;weapon->m_flLastFireTime=lastFire;
            weapon->m_flPrevPrimaryAttack=previousAttack;weapon->m_flDecreaseShotsFired=decreaseShots;
            weapon->m_flAccuracy=accuracy;weapon->m_iShotsFired=shots;weapon->m_bDelayFire=delay;
            weapon->m_flNextReload=nextReload;weapon->m_fInReload=inReload;weapon->m_fInSpecialReload=specialReload;
        }
    };
    static CBaseEntity* Live(EHANDLE& handle)
    {
        auto* edict=handle.Get();
        if(!edict || edict->free || (edict->v.flags & FL_KILLME))return nullptr;
        return static_cast<CBaseEntity*>(handle);
    }
    CBasePlayer* Buyer()
    {
        auto* entity=Live(buyer);
        auto* player=Player(index);
        return player && entity==player && GETPLAYERUSERID(player->edict())==userid &&
            player->IsAlive() && CSOL_ZombieHuman(player) ? player : nullptr;
    }
    static bool Contains(CBasePlayer* player,CBaseEntity* entity)
    {
        for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
            for(auto* item=player->m_rgpPlayerItems[slot];item;item=item->m_pNext)
                if(item==entity)return true;
        return false;
    }
    static bool InInventory(CBaseEntity* entity)
    {
        for(int i=1;i<=gpGlobals->maxClients;++i)
            if(auto* player=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));player && Contains(player,entity))return true;
        return false;
    }
    static bool Acquired(CBasePlayer* player,CBaseEntity* entity)
    {
        return player && entity && Contains(player,entity) && static_cast<CBasePlayerItem*>(entity)->IsWeapon() &&
            CSOL_ClipAmmo(static_cast<CBasePlayerWeapon*>(entity)) &&
            player->GetItemById(static_cast<WeaponIdType>(CSOL_WEAPON_CANNON))==entity &&
            static_cast<CBasePlayerWeapon*>(entity)->m_pPlayer==player && entity->pev->owner==player->edict();
    }
    int Reject(EHANDLE& fresh,EHANDLE& dropped)
    {
        Discard(fresh);
        if(!Restore(dropped))
            SERVER_PRINT("CSOL: cannon purchase rejected; original weapon could not be fully restored\n");
        return 0;
    }
    void Discard(EHANDLE& fresh)
    {
        // AddPlayerItem hooks can reject after inserting the item, before
        // DefaultTouch attaches it. Unlink first, even for a pending removal.
        auto* edict=fresh.Get();
        auto* entity=edict && !edict->free ? static_cast<CBaseEntity*>(fresh) : nullptr;
        if(!entity || !FClassnameIs(entity->pev,"weapon_cannon"))return;
        auto* player=Buyer();
        if(player && Contains(player,entity)) {
            player->RemovePlayerItem(static_cast<CBasePlayerItem*>(entity));
            entity=Live(fresh);
        }
        if(!entity || InInventory(entity))return;
        // A hook may transfer the new gun to another player or a weaponbox.
        // Only remove the unowned item created by this purchase.
        if(entity->pev->owner && entity->pev->owner!=buyer.Get())return;
        UTIL_Remove(entity);
    }
    bool Restore(EHANDLE& dropped)
    {
        auto* player=Buyer();
        auto* entity=Live(original);
        if(!player || !entity || !FClassnameIs(entity->pev,"weapon_cannon"))return false;
        auto* weapon=static_cast<CBasePlayerWeapon*>(entity);
        const int slot=weapon->iItemSlot();
        if(!CSOL_ClipAmmo(weapon) || slot!=PRIMARY_WEAPON_SLOT)return false;
        if(!Contains(player,weapon)) {
            auto* container=Live(dropped);
            if(!container || !FClassnameIs(container->pev,"weaponbox") || InInventory(weapon) ||
                player->GetItemById(static_cast<WeaponIdType>(CSOL_WEAPON_CANNON)))return false;
            auto* box=static_cast<CWeaponBox*>(container);
            if(weapon->pev->owner!=box->edict() || box->m_rgpPlayerItems[slot]!=weapon || weapon->m_pNext)return false;
            // AddPlayerItem rewrites m_pNext and can call other plugins. The
            // box must not share that chain with the player's inventory.
            box->m_rgpPlayerItems[slot]=nullptr;
            player->AddPlayerItem(weapon);
            player=Buyer();entity=Live(original);container=Live(dropped);
            if(!entity)return false;
            weapon=static_cast<CBasePlayerWeapon*>(entity);
            if(!player || !Contains(player,weapon)) {
                // A refused AddToPlayer can still set m_pPlayer. Restore only
                // the unchanged, detached item, never a transferred entity.
                if(container && FClassnameIs(container->pev,"weaponbox") && !InInventory(weapon) &&
                    weapon->pev->owner==container->edict() && !weapon->m_pNext &&
                    !static_cast<CWeaponBox*>(container)->m_rgpPlayerItems[slot]) {
                    weapon->m_pPlayer=nullptr;
                    static_cast<CWeaponBox*>(container)->m_rgpPlayerItems[slot]=weapon;
                }
                return false;
            }
            if(weapon->m_pPlayer!=player ||
                (weapon->pev->owner!=dropped.Get() && weapon->pev->owner!=player->edict()))return false;
            weapon->AttachToPlayer(player);
            player=Buyer();entity=Live(original);container=Live(dropped);
            if(!player || !entity || !Contains(player,entity) || entity->pev->owner!=player->edict())return false;
            if(container && FClassnameIs(container->pev,"weaponbox") && static_cast<CWeaponBox*>(container)->IsEmpty())
                UTIL_Remove(container);
        }
        // Drop can fail after GetNextBestWeapon already changed the active
        // item. This path also restores that case without requiring a box.
        auto* previous=Live(active);
        if(previous && Contains(player,previous) && static_cast<CBasePlayerItem*>(previous)->IsWeapon() && previous->pev->owner==player->edict() &&
            player->m_pActiveItem!=previous)
            player->SwitchWeapon(static_cast<CBasePlayerItem*>(previous));
        player=Buyer();entity=Live(original);
        if(!player || !entity || !Contains(player,entity) || entity->pev->owner!=player->edict())return false;
        weapon=static_cast<CBasePlayerWeapon*>(entity);
        auto* previousLast=Live(last);
        if(!previousLast || (Contains(player,previousLast) && previousLast->pev->owner==player->edict()))
            player->m_pLastItem=static_cast<CBasePlayerItem*>(previousLast);
        weapon->m_iClip=clip;player->m_flNextAttack=nextAttack;
        for(auto& saved:timers)saved.Restore(player);
        player->m_iHideHUD=(player->m_iHideHUD & ~HIDEHUD_WEAPONS) | hiddenWeapons;
        CSOL_UpdateWeaponInventory(player);
        return !Live(active) || player->m_pActiveItem==Live(active);
    }
    EHANDLE buyer{},original{},active{},last{};
    std::vector<WeaponTimers> timers;
    int index{},userid{},clip{},hiddenWeapons{};
    float nextAttack{};
};
class WeaponService final : public csol::IWeaponService {
public:
    int CanBuy(int index, int weapon) override
    {
        auto* player = Player(index);
        const auto* info = CSOL_ItemInfo(weapon);
        return WeaponName(weapon) && player && player->IsAlive() && CSOL_ZombieHuman(player) &&
            CSOL_WeaponsReady() && CSOL_CanUseExtendedWeapons(player) && info && info->pszName &&
            !player->HasShield() && (weapon != CSOL_WEAPON_CANNON || CSOL_CannonReady());
    }
    int Buy(int index, int weapon) override
    {
        if (!CanBuy(index, weapon)) return 0;
        CannonPurchaseGuard guard(weapon==CSOL_WEAPON_CANNON ? index : 0);
        if(!guard.Allowed())return 0;
        auto* player = Player(index);
        const char* name = WeaponName(weapon);
        if(weapon==CSOL_WEAPON_CANNON && CSOL_HasWeapon(player,weapon))
            return CannonPurchaseBackup(player).Replace();
        CBaseEntity* entity;
        if (CSOL_HasWeapon(player, weapon)) {
            // Spawn before dropping the duplicate, so an allocation/spawn
            // failure cannot cost the current weapon. Use normal Touch and
            // DropPlayerItem: ZP hooks and weaponbox ammo rules still apply.
            edict_t* fresh = CREATE_NAMED_ENTITY(ALLOC_STRING(name));
            if (FNullEnt(fresh)) return 0;
            fresh->v.origin = player->pev->origin;
            fresh->v.spawnflags |= SF_NORESPAWN;
            DispatchSpawn(fresh);
            entity = GET_PRIVATE<CBaseEntity>(fresh);
            if (!entity || (fresh->v.flags & FL_KILLME)) return 0;
            player->DropPlayerItem(name);
            if (CSOL_HasWeapon(player, weapon)) { UTIL_Remove(entity); return 0; }
            DispatchTouch(fresh, player->edict());
            if (fresh->v.owner != player->edict()) {
                UTIL_Remove(entity);
                return 0;
            }
        } else {
            // For a different gun, deliver first; a rejected Touch keeps it.
            entity = player->GiveNamedItemEx(name);
        }
        auto* added = player->GetItemById(static_cast<WeaponIdType>(weapon));
        if (!entity || added != entity || !added->IsWeapon()) return 0;
        auto* item = player->m_rgpPlayerItems[added->iItemSlot()];
        while (item) {
            auto* next = item->m_pNext;
            if (item != added) player->DropPlayerItem(STRING(item->pev->classname));
            item = next;
        }
        player->SelectItem(name);
        CSOL_UpdateWeaponInventory(player);
        return 1;
    }
    void SetZombieMode(bool enabled) override
    {
        zombieMode = enabled;
        roles = {};
        CVAR_SET_FLOAT("csol_beam_damage_mode", enabled ? 1 : 0);
        for (int i = 1; i <= gpGlobals->maxClients; ++i)
            if (auto* player = Player(i)) CSOL_ResetBeamGuard(player);
    }
    void SetPlayerRole(int index, int role) override
    {
        auto* player = Player(index);
        if (!player) return;
        roles[index] = {GETPLAYERUSERID(player->edict()), role >= 0 && role <= 2 ? role : 0};
        CSOL_ResetBeamGuard(player);
        CSOL_CharacterRoleChanged(player);
    }
};
using csol::IWeaponService;
EXPOSE_SINGLE_INTERFACE(WeaponService, IWeaponService, csol::WeaponServiceVersion);
class WeaponAmmoService final : public csol::IWeaponAmmoService {
public:
    int Refill(int index) override
    {
        auto* player = Player(index);
        if (!player || !player->IsAlive() || !CSOL_ZombieHuman(player) ||
            !CSOL_WeaponsReady() || !CSOL_CanUseExtendedWeapons(player)) return 0;
        std::array<bool, MAX_AMMO_SLOTS> filled{};
        int added = 0;
        for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
            for (auto* item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext) {
                if (!item->IsWeapon()) continue;
                auto* weapon = static_cast<CBasePlayerWeapon*>(item);
                if (!CSOL_IsWeapon(weapon)) continue;
                const auto* info = CSOL_ItemInfo(weapon->m_iId);
                const int ammo = weapon->m_iPrimaryAmmoType;
                int price = 0, count = 0;
                if (!info || !info->pszAmmo1 || ammo < 0 || ammo >= MAX_AMMO_SLOTS || filled[ammo] ||
                    !CSOL_AmmoPurchase(weapon->m_iId, price, count)) continue;
                filled[ammo] = true;
                if (CSOL_ClipAmmo(weapon)) {
                    added += CSOL_GiveClipAmmo(player, weapon, count);
                    continue;
                }
                const int before = player->m_rgAmmo[ammo];
                player->GiveAmmo(count, const_cast<char*>(info->pszAmmo1), info->iMaxAmmo1);
                added += Q_max(0, player->m_rgAmmo[ammo] - before);
            }
        if (added) CSOL_UpdateWeaponInventory(player);
        return added;
    }
};
using csol::IWeaponAmmoService;
EXPOSE_SINGLE_INTERFACE(WeaponAmmoService, IWeaponAmmoService, csol::WeaponAmmoServiceVersion);
}
bool CSOL_ZombieHuman(CBasePlayer* player)
{
    if (!zombieMode || !player || !player->IsAlive()) return false;
    const int index = player->entindex();
    return index > 0 && index < int(roles.size()) && roles[index].value == 1 &&
        roles[index].userid == GETPLAYERUSERID(player->edict());
}
void CSOL_ResetWeaponService() { zombieMode = false; roles = {}; }
bool CSOL_ZombieMode() { return zombieMode; }
