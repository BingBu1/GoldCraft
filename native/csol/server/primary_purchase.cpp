// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "primary_purchase.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "wpn_m249ex.h"
#include "wpn_skull1.h"
#include "wpn_skull2.h"
#include "wpn_skull5.h"
#include "wpn_balrog11.h"
#include "game_rules_gate.h"
#include "scenario_player_runtime.h"
#include "../m249ex.hpp"
#include "../skull1.hpp"
#include "../skull2.hpp"
#include "../skull5.hpp"
#include "../balrog11.hpp"
#include <climits>
#include <vector>

namespace {
bool Contains(const CBasePlayer& player, const CBaseEntity* wanted)
{
    int remaining=gpGlobals->maxEntities;
    for(auto* first:player.m_rgpPlayerItems)
        for(auto* item=first;item && remaining-->0;item=item->m_pNext)
            if(item==wanted)return true;
    return false;
}
bool Linked(const CSOLScenarioSdkScope& sdk, const CBaseEntity* wanted)
{
    if(!sdk.Current())return true;
    for(int i=1;i<=gpGlobals->maxClients;++i) {
        auto* entity=UTIL_PlayerByIndex(i);
        if(!sdk.Current())return true;
        auto* player=dynamic_cast<CBasePlayer*>(entity);
        if(!player)continue;
        int remaining=gpGlobals->maxEntities;
        for(auto* first:player->m_rgpPlayerItems)
            for(auto* item=first;item;item=item->m_pNext)
                if(--remaining<=0 || item==wanted)return true;
    }
    return false;
}
bool Identity(const CBasePlayerWeapon& weapon,int id)
{
    return id==csol::skull1::Id ? CSOL_Skull1EmptyMonsterKillCallbacks(weapon) &&
        FClassnameIs(weapon.pev,csol::skull1::Classname) :
        id==csol::skull2::Id ? CSOL_IsSkull2(weapon) && FClassnameIs(weapon.pev,csol::skull2::Classname) :
        id==csol::skull5::Id ? CSOL_IsSkull5(weapon) && FClassnameIs(weapon.pev,csol::skull5::Classname) :
        id==csol::m249ex::Id ? CSOL_M249EXEmptyMonsterKillCallbacks(weapon) &&
        FClassnameIs(weapon.pev,csol::m249ex::Classname) :
        id==csol::balrog11::Id && CSOL_Balrog11EmptyMonsterKillCallbacks(weapon) &&
        FClassnameIs(weapon.pev,csol::balrog11::Classname);
}
bool Deployed(const CBasePlayerWeapon& weapon,int id)
{
    return id==csol::skull1::Id ? CSOL_Skull1DeploymentComplete(weapon) :
        id==csol::skull2::Id ? CSOL_Skull2DeploymentComplete(weapon) :
        id==csol::skull5::Id ? CSOL_Skull5DeploymentComplete(weapon) :
        id==csol::m249ex::Id ? CSOL_M249EXDeploymentComplete(weapon) :
        id==csol::balrog11::Id && CSOL_Balrog11DeploymentComplete(weapon);
}
// Restore surviving weapons after the automatic Select/Deploy calls made by
// native DropPlayerItem and AddPlayerItem, without resetting their cooldowns.
struct Timers {
    EHANDLE handle;
    float primary,secondary,idle,lastFire,lastAttempt,previousAttack,decreaseShots,accuracy,nextReload;
    int shots,inReload,specialReload;
    bool delay;
    CSOLSkull2PurchaseState skull2;
    explicit Timers(CBasePlayerWeapon& w):handle(&w),
        primary(w.m_flNextPrimaryAttack),secondary(w.m_flNextSecondaryAttack),idle(w.m_flTimeWeaponIdle),
        lastFire(w.m_flLastFireTime),lastAttempt(w.m_flLastFire),previousAttack(w.m_flPrevPrimaryAttack),decreaseShots(w.m_flDecreaseShotsFired),
        accuracy(w.m_flAccuracy),nextReload(w.m_flNextReload),shots(w.m_iShotsFired),inReload(w.m_fInReload),
        specialReload(w.m_fInSpecialReload),delay(w.m_bDelayFire),skull2(CSOL_SaveSkull2PurchaseState(w)) {}
    void Restore(CBasePlayer& player) const
    {
        auto* w=dynamic_cast<CBasePlayerWeapon*>(handle.Get<CBaseEntity>());
        if(!w || (w->pev->flags&FL_KILLME) || !Contains(player,w) ||
            w->m_pPlayer!=&player || w->pev->owner!=player.edict())return;
        w->m_flNextPrimaryAttack=primary;w->m_flNextSecondaryAttack=secondary;w->m_flTimeWeaponIdle=idle;
        w->m_flLastFireTime=lastFire;w->m_flLastFire=lastAttempt;w->m_flPrevPrimaryAttack=previousAttack;w->m_flDecreaseShotsFired=decreaseShots;
        w->m_flAccuracy=accuracy;w->m_flNextReload=nextReload;w->m_iShotsFired=shots;
        w->m_fInReload=inReload;w->m_fInSpecialReload=specialReload;w->m_bDelayFire=delay;
        CSOL_RestoreSkull2PurchaseState(*w,skull2);
    }
};
// AddPlayerItem's post hook can return FALSE after insertion; removal can
// also be vetoed. Compensate only the new entity owned by this transaction.
bool UnlinkFresh(CBasePlayer& player,CBasePlayerWeapon& weapon)
{
    int remaining=gpGlobals->maxEntities;
    for(auto& first:player.m_rgpPlayerItems) {
        auto** link=&first;
        while(*link) {
            if(--remaining<=0)return false;
            if(*link==&weapon) {*link=weapon.m_pNext;break;}
            link=&(*link)->m_pNext;
        }
    }
    if(Contains(player,&weapon))return false;
    if(player.m_pActiveItem==&weapon) {
        player.m_pActiveItem=nullptr;
        player.pev->viewmodel=player.pev->weaponmodel=0;
    }
    if(player.m_pLastItem==&weapon)player.m_pLastItem=nullptr;
    if(player.m_pClientActiveItem==&weapon)player.m_pClientActiveItem=nullptr;
    weapon.m_pNext=nullptr;
    return true;
}
}

bool CSOL_DeliverSkullBalrog(CBasePlayer& buyer,int weaponId)
{
    CSOLScenarioSdkScope sdk;
    if(!sdk.Current() || !gpGlobals ||
        (weaponId!=csol::m249ex::Id && weaponId!=csol::balrog11::Id && weaponId!=csol::skull1::Id &&
         weaponId!=csol::skull2::Id && weaponId!=csol::skull5::Id))return false;
    const int slot=weaponId==csol::skull1::Id||weaponId==csol::skull2::Id?PISTOL_SLOT:PRIMARY_WEAPON_SLOT;
    const EHANDLE playerHandle(&buyer);
    const int userid=GETPLAYERUSERID(buyer.edict()),spawns=buyer.m_iNumSpawns;
    const auto players=sdk.Scenario()?CSOL_ScenarioPlayerAuthority(sdk.EntityAccess().get()):nullptr;
    const auto state=players?players->Find(playerHandle):nullptr;
    if(!sdk.Current() || (sdk.Scenario() && !state))return false;
    const auto generation=state?state->generation:0;
    const auto samePlayer=[&]() {
        if(!sdk.Current() || playerHandle.Get<CBaseEntity>()!=&buyer)return false;
        const int now=GETPLAYERUSERID(buyer.edict());
        return sdk.Current() && playerHandle.Get<CBaseEntity>()==&buyer && now==userid &&
            !buyer.has_disconnected && buyer.m_iNumSpawns==spawns && !(buyer.pev->flags&(FL_KILLME|FL_DORMANT));
    };
    const auto current=[&]() {
        return samePlayer() && buyer.IsAlive() && !buyer.HasShield() && CSOL_WeaponsReady() &&
            CSOL_CanUseExtendedWeapons(&buyer) &&
            (weaponId==csol::skull1::Id?CSOL_Skull1Ready():weaponId==csol::skull2::Id?CSOL_Skull2Ready():weaponId==csol::skull5::Id?CSOL_Skull5Ready():
                weaponId==csol::m249ex::Id?CSOL_M249EXReady():CSOL_Balrog11Ready()) &&
            (sdk.Scenario()?players->Active() && players->Find(playerHandle)==state &&
                state->generation==generation && buyer.m_iTeam==CT && CSOL_ScenarioCanBuy():CSOL_ZombieHuman(&buyer)) && samePlayer();
    };
    if(!current())return false;
    auto* prior=dynamic_cast<CBasePlayerWeapon*>(buyer.m_rgpPlayerItems[slot]);
    if(buyer.m_rgpPlayerItems[slot] &&
        (!prior || prior->m_pNext || prior->m_pPlayer!=&buyer || prior->pev->owner!=playerHandle.Get()))return false;
    EHANDLE original(prior),active(buyer.m_pActiveItem),last(buyer.m_pLastItem),fresh,box;
    CBaseEntity* createdObject{};
    const float nextAttack=buyer.m_flNextAttack;
    const int hidden=buyer.m_iHideHUD&HIDEHUD_WEAPONS;
    const int priorMask=prior?CSOL_LegacyWeaponMask(prior->m_iId):0;
    const int priorMaskValue=buyer.pev->weapons&priorMask;
    const auto* info=CSOL_ItemInfo(weaponId);
    if(!info)return false;
    // A successful native drop can move SKULL2 reserves into its second
    // magazine. A rejected replacement must restore the old ammo type too.
    const int ammoSlots[]{info->pszAmmo1?buyer.GetAmmoIndex(info->pszAmmo1):-1,
        info->pszAmmo2?buyer.GetAmmoIndex(info->pszAmmo2):-1,
        prior?prior->m_iPrimaryAmmoType:-1,prior?prior->m_iSecondaryAmmoType:-1};
    int savedAmmo[4]{};
    for(int i=0;i<4;++i)
        if(ammoSlots[i]>=0 && ammoSlots[i]<MAX_AMMO_SLOTS)savedAmmo[i]=buyer.m_rgAmmo[ammoSlots[i]];
    std::vector<Timers> timers;
    int remaining=gpGlobals->maxEntities;
    for(auto* first:buyer.m_rgpPlayerItems)
        for(auto* item=first;item && remaining-->0;item=item->m_pNext)
            if(auto* w=dynamic_cast<CBasePlayerWeapon*>(item))timers.emplace_back(*w);
    if(remaining<=0)return false;
    const auto live=[&](const EHANDLE& handle)->CBaseEntity* {
        if(!sdk.Current())return nullptr;
        auto* entity=handle.Get<CBaseEntity>();
        return entity && !(entity->pev->flags&FL_KILLME)?entity:nullptr;
    };
    const auto acquired=[&]() {
        if(!current())return false;
        auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh));
        return w && w==createdObject && Identity(*w,weaponId) && w->m_pPlayer==&buyer &&
            w->pev->owner==playerHandle.Get() && buyer.m_rgpPlayerItems[slot]==w && !w->m_pNext;
    };
    const auto ownedFresh=[&]()->CBasePlayerWeapon* {
        const bool same=samePlayer();
        auto* w=sdk.Current()?dynamic_cast<CBasePlayerWeapon*>(fresh.Get<CBaseEntity>()):nullptr;
        return w && w==createdObject && ((!w->m_pPlayer && !w->pev->owner) ||
            (same && (!w->m_pPlayer || w->m_pPlayer==&buyer) &&
                (!w->pev->owner || w->pev->owner==playerHandle.Get())))?w:nullptr;
    };
    bool dropAttempted{};
    const auto reject=[&]() {
        auto* w=ownedFresh();
        if(w && samePlayer() && Contains(buyer,w))buyer.RemovePlayerItem(w);
        w=ownedFresh();
        if(w && samePlayer())UnlinkFresh(buyer,*w);
        w=ownedFresh();
        if(w && !Linked(sdk,w) && ownedFresh()==w) {
            w->SetTouch(nullptr);w->SetThink(nullptr);w->pev->nextthink=0;w->pev->solid=SOLID_NOT;
            if(!(w->pev->flags&FL_KILLME))UTIL_Remove(w);
        }
        if(!current())return false;
        auto* old=dynamic_cast<CBasePlayerWeapon*>(live(original));
        auto* container=dynamic_cast<CWeaponBox*>(live(box));
        // PackWeapon returns FALSE if a post hook rejects RemovePlayerItem,
        // even when that hook already unlinked it. CreateWeaponBox still
        // returns its empty box. Reattach only our unchanged, untransferred gun.
        if(dropAttempted && old && old==prior && !Contains(buyer,old) && !Linked(sdk,old) &&
            current() && live(original)==old && old->m_pPlayer==&buyer &&
            old->pev->owner==playerHandle.Get() && !buyer.m_rgpPlayerItems[slot]) {
            old->m_pNext=nullptr;
            buyer.m_rgpPlayerItems[slot]=old;
        }
        if(old && old==prior && !Contains(buyer,old) && !Linked(sdk,old) && current() &&
            live(original)==old && container && live(box)==container &&
            old->pev->owner==container->edict() && !old->m_pNext &&
            container->m_rgpPlayerItems[slot]==old && !buyer.m_rgpPlayerItems[slot]) {
            // Undo our own native drop; keep the original entity, ammo and
            // private state. No second AddPlayerItem or pickup notification.
            container->m_rgpPlayerItems[slot]=nullptr;
            buyer.m_rgpPlayerItems[slot]=old;
            old->m_pPlayer=&buyer;
            old->AttachToPlayer(&buyer);
            if(!current())return false;
            container=dynamic_cast<CWeaponBox*>(live(box));
            if(container) {
                // Native ammodrop may move reserves into this box. Return
                // its known ammo rather than restoring an entire player snapshot.
                for(int i=0;i<MAX_AMMO_SLOTS;++i) {
                    if(!container->m_rgiszAmmo[i] || container->m_rgAmmo[i]<=0)continue;
                    const int index=buyer.GetAmmoIndex(STRING(container->m_rgiszAmmo[i]));
                    if(index>=0 && index<MAX_AMMO_SLOTS && buyer.m_rgAmmo[index]<=INT_MAX-container->m_rgAmmo[i]) {
                        buyer.m_rgAmmo[index]+=container->m_rgAmmo[i];
                        container->m_rgAmmo[i]=0;container->m_rgiszAmmo[i]=0;
                    }
                }
                if(container->IsEmpty())UTIL_Remove(container);
            }
        }
        if(!current())return false;
        auto* previous=dynamic_cast<CBasePlayerItem*>(live(active));
        if(previous && Contains(buyer,previous) && previous->pev->owner==playerHandle.Get() && buyer.m_pActiveItem!=previous)
            buyer.SwitchWeapon(previous);
        if(!current())return false;
        auto* previousLast=dynamic_cast<CBasePlayerItem*>(live(last));
        if(!previousLast || (Contains(buyer,previousLast) && previousLast->pev->owner==playerHandle.Get()))
            buyer.m_pLastItem=previousLast;
        old=dynamic_cast<CBasePlayerWeapon*>(live(original));
        if(old && Contains(buyer,old) && old->pev->owner==playerHandle.Get())
            buyer.pev->weapons=(buyer.pev->weapons&~priorMask)|priorMaskValue;
        buyer.m_bHasPrimary=buyer.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]!=nullptr;
        buyer.m_flNextAttack=nextAttack;
        buyer.m_iHideHUD=(buyer.m_iHideHUD&~HIDEHUD_WEAPONS)|hidden;
        // AddToPlayer can give ammunition before a post hook rejects Touch.
        // Restore only this weapon's ammo types after returning the old box.
        for(int i=0;i<4;++i)
            if(ammoSlots[i]>=0 && ammoSlots[i]<MAX_AMMO_SLOTS)buyer.m_rgAmmo[ammoSlots[i]]=savedAmmo[i];
        for(const auto& saved:timers)saved.Restore(buyer);
        CSOL_UpdateWeaponInventory(&buyer);
        return false;
    };
    const char* classname=weaponId==csol::skull1::Id?csol::skull1::Classname:
        weaponId==csol::skull2::Id?csol::skull2::Classname:
        weaponId==csol::skull5::Id?csol::skull5::Classname:
        weaponId==csol::m249ex::Id?csol::m249ex::Classname:csol::balrog11::Classname;
    const auto name=ALLOC_STRING(classname);
    if(!current())return false;
    auto* created=CREATE_NAMED_ENTITY(name);
    if(!current() || FNullEnt(created))return false;
    fresh=EHANDLE(created);
    created->v.origin=buyer.pev->origin;created->v.spawnflags|=SF_NORESPAWN;
    const int spawned=DispatchSpawn(created);
    if(sdk.Current())createdObject=fresh.Get<CBaseEntity>();
    if(!current())return reject();
    auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh));
    if(spawned<0 || !w || !Identity(*w,weaponId))return reject();
    if(prior) {
        if(live(original)!=prior || !Contains(buyer,prior) || prior->pev->owner!=playerHandle.Get())return reject();
        dropAttempted=true;
        box=buyer.DropPlayerItem(STRING(prior->pev->classname));
        if(!current() || !live(fresh) || buyer.m_rgpPlayerItems[slot])return reject();
        auto* old=live(original);
        auto* container=dynamic_cast<CWeaponBox*>(live(box));
        if(old!=prior || !container || prior->pev->owner!=container->edict() ||
            container->m_rgpPlayerItems[slot]!=prior || prior->m_pNext)return reject();
    }
    DispatchTouch(fresh.Get(),playerHandle.Get());
    if(!acquired())return reject();
    buyer.SelectItem(classname);
    if(!acquired())return reject();
    w=dynamic_cast<CBasePlayerWeapon*>(live(fresh));
    if(!w || buyer.m_pActiveItem!=w || !Deployed(*w,weaponId))return reject();
    CSOL_UpdateWeaponInventory(&buyer);
    return acquired() ? true : reject();
}
