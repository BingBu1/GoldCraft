// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "primary_purchase.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "wpn_m249ex.h"
#include "wpn_skull1.h"
#include "wpn_skull2.h"
#include "wpn_skull3.h"
#include "paired_inventory.h"
#include "wpn_skull5.h"
#include "wpn_balrog11.h"
#include "game_rules_gate.h"
#include "scenario_player_runtime.h"
#include "../m249ex.hpp"
#include "../skull1.hpp"
#include "../skull2.hpp"
#include "../skull3.hpp"
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
        csol::skull3::IsWeapon(id) ? CSOL_IsSkull3(weapon) && weapon.m_iId==id :
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
        csol::skull3::IsWeapon(id) ? CSOL_Skull3DeploymentComplete(weapon) :
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
    CSOLSkull3PurchaseState skull3;
    explicit Timers(CBasePlayerWeapon& w):handle(&w),
        primary(w.m_flNextPrimaryAttack),secondary(w.m_flNextSecondaryAttack),idle(w.m_flTimeWeaponIdle),
        lastFire(w.m_flLastFireTime),lastAttempt(w.m_flLastFire),previousAttack(w.m_flPrevPrimaryAttack),decreaseShots(w.m_flDecreaseShotsFired),
        accuracy(w.m_flAccuracy),nextReload(w.m_flNextReload),shots(w.m_iShotsFired),inReload(w.m_fInReload),
        specialReload(w.m_fInSpecialReload),delay(w.m_bDelayFire),skull2(CSOL_SaveSkull2PurchaseState(w)),
        skull3(CSOL_SaveSkull3PurchaseState(w)) {}
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
        CSOL_RestoreSkull3PurchaseState(*w,skull3);
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
         weaponId!=csol::skull2::Id && weaponId!=csol::skull3::Id && weaponId!=csol::skull5::Id))return false;
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
            (weaponId==csol::skull1::Id?CSOL_Skull1Ready():weaponId==csol::skull2::Id?CSOL_Skull2Ready():
             weaponId==csol::skull3::Id?CSOL_Skull3Ready():weaponId==csol::skull5::Id?CSOL_Skull5Ready():
             weaponId==csol::m249ex::Id?CSOL_M249EXReady():CSOL_Balrog11Ready()) &&
            (sdk.Scenario()?players->Active() && players->Find(playerHandle)==state &&
                state->generation==generation && buyer.m_iTeam==CT && CSOL_ScenarioCanBuy():CSOL_ZombieHuman(&buyer)) && samePlayer();
    };
    if(!current())return false;
    struct Prior { EHANDLE handle; CBasePlayerWeapon* object; };
    std::vector<Prior> prior;
    for(auto* item=buyer.m_rgpPlayerItems[slot];item;item=item->m_pNext) {
        auto* w=dynamic_cast<CBasePlayerWeapon*>(item);
        if(!w || prior.size()>=2 || w->m_pPlayer!=&buyer || w->pev->owner!=playerHandle.Get() ||
            (!prior.empty() && !CSOL_SameWeaponGroup(prior.front().object,w)))return false;
        prior.push_back({EHANDLE(w),w});
    }
    EHANDLE active(buyer.m_pActiveItem),last(buyer.m_pLastItem),box;
    const float nextAttack=buyer.m_flNextAttack;
    const int hidden=buyer.m_iHideHUD&HIDEHUD_WEAPONS;
    const auto slotPosition=buyer.m_csolSlotPos[slot-1],validPosition=buyer.m_csolValidSlotPos[slot-1];
    int priorMask{};
    for(const auto& old:prior)priorMask|=CSOL_LegacyWeaponMask(old.object->m_iId);
    const int priorMaskValue=buyer.pev->weapons&priorMask;
    const auto* info=CSOL_ItemInfo(weaponId);
    if(!info)return false;
    const auto* first=prior.empty()?nullptr:prior.front().object;
    const int ammoSlots[]{info->pszAmmo1?buyer.GetAmmoIndex(info->pszAmmo1):-1,
        info->pszAmmo2?buyer.GetAmmoIndex(info->pszAmmo2):-1,
        first?first->m_iPrimaryAmmoType:-1,first?first->m_iSecondaryAmmoType:-1};
    int savedAmmo[4]{};
    for(int i=0;i<4;++i)
        if(ammoSlots[i]>=0 && ammoSlots[i]<MAX_AMMO_SLOTS)savedAmmo[i]=buyer.m_rgAmmo[ammoSlots[i]];
    std::vector<Timers> timers;
    int remaining=gpGlobals->maxEntities;
    for(auto* head:buyer.m_rgpPlayerItems)
        for(auto* item=head;item && remaining-->0;item=item->m_pNext)
            if(auto* w=dynamic_cast<CBasePlayerWeapon*>(item))timers.emplace_back(*w);
    if(remaining<=0)return false;
    const auto live=[&](const EHANDLE& handle)->CBaseEntity* {
        if(!sdk.Current())return nullptr;
        auto* entity=handle.Get<CBaseEntity>();
        return entity && !(entity->pev->flags&FL_KILLME)?entity:nullptr;
    };
    struct Fresh { int id; const char* classname; EHANDLE handle; CBaseEntity* object{}; };
    const char* classname=weaponId==csol::skull1::Id?csol::skull1::Classname:
        weaponId==csol::skull2::Id?csol::skull2::Classname:weaponId==csol::skull3::Id?csol::skull3::Classname:
        weaponId==csol::skull5::Id?csol::skull5::Classname:
        weaponId==csol::m249ex::Id?csol::m249ex::Classname:csol::balrog11::Classname;
    std::array<Fresh,2> fresh{{{weaponId,classname,{}},{csol::skull3::DualId,csol::skull3::DualClassname,{}}}};
    const int freshCount=weaponId==csol::skull3::Id?2:1;
    const auto acquired=[&]() {
        if(!current())return false;
        int count{};
        for(auto* item=buyer.m_rgpPlayerItems[slot];item;item=item->m_pNext) {
            if(++count>freshCount)return false;
            bool found{};
            for(int i=0;i<freshCount;++i)found|=item==live(fresh[i].handle) && item==fresh[i].object;
            if(!found)return false;
        }
        if(count!=freshCount)return false;
        for(int i=0;i<freshCount;++i) {
            auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh[i].handle));
            if(!w || !Identity(*w,fresh[i].id) || w->m_pPlayer!=&buyer || w->pev->owner!=playerHandle.Get())return false;
        }
        return true;
    };
    const auto ownedFresh=[&](const Fresh& entry)->CBasePlayerWeapon* {
        const bool same=samePlayer();
        auto* w=sdk.Current()?dynamic_cast<CBasePlayerWeapon*>(entry.handle.Get<CBaseEntity>()):nullptr;
        return w && w==entry.object && ((!w->m_pPlayer && !w->pev->owner) ||
            (same && (!w->m_pPlayer || w->m_pPlayer==&buyer) &&
                (!w->pev->owner || w->pev->owner==playerHandle.Get())))?w:nullptr;
    };
    bool dropAttempted{};
    const auto reject=[&]() {
        for(int i=freshCount-1;i>=0;--i) {
            const auto& entry=fresh[i];
            auto* w=ownedFresh(entry);
            if(w && samePlayer() && Contains(buyer,w))buyer.RemovePlayerItem(w);
            w=ownedFresh(entry);
            if(w && samePlayer())UnlinkFresh(buyer,*w);
            w=ownedFresh(entry);
            if(w && !Linked(sdk,w) && ownedFresh(entry)==w) {
                w->SetTouch(nullptr);w->SetThink(nullptr);w->pev->nextthink=0;w->pev->solid=SOLID_NOT;
                if(!(w->pev->flags&FL_KILLME))UTIL_Remove(w);
            }
        }
        if(!current())return false;
        // Return only original surviving entities still in our drop box or
        // detached by its RemovePlayerItem hook. Never steal a transferred gun.
        for(auto it=prior.rbegin();dropAttempted && it!=prior.rend();++it) {
            auto* old=dynamic_cast<CBasePlayerWeapon*>(live(it->handle));
            auto* container=dynamic_cast<CWeaponBox*>(live(box));
            if(!old || old!=it->object || Contains(buyer,old) || Linked(sdk,old) || !current() || live(it->handle)!=old)continue;
            bool slotCompatible=true;int bound=gpGlobals->maxEntities;
            for(auto* item=buyer.m_rgpPlayerItems[slot];item;item=item->m_pNext) {
                if(--bound<=0){slotCompatible=false;break;}
                bool original{};for(const auto& saved:prior)original|=item==live(saved.handle);
                if(!original){slotCompatible=false;break;}
            }
            if(!slotCompatible)continue;
            bool detached=old->m_pPlayer==&buyer && old->pev->owner==playerHandle.Get();
            if(container && live(box)==container && old->pev->owner==container->edict()) {
                auto** link=&container->m_rgpPlayerItems[slot];bound=gpGlobals->maxEntities;
                while(*link && *link!=old && --bound>0)link=&(*link)->m_pNext;
                if(*link==old){*link=old->m_pNext;detached=true;}
            }
            if(!detached)continue;
            old->m_pNext=buyer.m_rgpPlayerItems[slot];buyer.m_rgpPlayerItems[slot]=old;
            old->m_pPlayer=&buyer;old->AttachToPlayer(&buyer);
            if(!current())return false;
        }
        if(!current())return false;
        auto* container=dynamic_cast<CWeaponBox*>(live(box));
        if(container) {
            // Only remove an empty container owned by this transaction.
            // Saved ammo below compensates native ammodrop and new extraction.
            bool weapons{};for(auto* item:container->m_rgpPlayerItems)weapons|=item!=nullptr;
            if(!weapons) {
                for(int i=0;i<MAX_AMMO_SLOTS;++i){container->m_rgAmmo[i]=0;container->m_rgiszAmmo[i]=0;}
                UTIL_Remove(container);
            }
        }
        auto* previous=dynamic_cast<CBasePlayerItem*>(live(active));
        if(previous && Contains(buyer,previous) && previous->pev->owner==playerHandle.Get() && buyer.m_pActiveItem!=previous)
            buyer.SwitchWeapon(previous);
        if(!current())return false;
        auto* previousLast=dynamic_cast<CBasePlayerItem*>(live(last));
        if(!previousLast || (Contains(buyer,previousLast) && previousLast->pev->owner==playerHandle.Get()))buyer.m_pLastItem=previousLast;
        bool retained{};
        for(const auto& saved:prior)if(auto* old=live(saved.handle);old && Contains(buyer,old) && old->pev->owner==playerHandle.Get())retained=true;
        if(retained)buyer.pev->weapons=(buyer.pev->weapons&~priorMask)|priorMaskValue;
        buyer.m_bHasPrimary=buyer.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]!=nullptr;
        buyer.m_flNextAttack=nextAttack;
        buyer.m_iHideHUD=(buyer.m_iHideHUD&~HIDEHUD_WEAPONS)|hidden;
        for(int i=0;i<4;++i)if(ammoSlots[i]>=0 && ammoSlots[i]<MAX_AMMO_SLOTS)buyer.m_rgAmmo[ammoSlots[i]]=savedAmmo[i];
        for(const auto& saved:timers)saved.Restore(buyer);
        buyer.m_csolSlotPos[slot-1]=slotPosition;buyer.m_csolValidSlotPos[slot-1]=validPosition;
        CSOL_UpdateWeaponInventory(&buyer);
        return false;
    };
    // Allocate both entities before displacing the old slot. Acquisition
    // remains in the original A->D order and uses normal native Touch hooks.
    for(int i=0;i<freshCount;++i) {
        auto& entry=fresh[i];const auto name=ALLOC_STRING(entry.classname);
        if(!current())return reject();
        auto* created=CREATE_NAMED_ENTITY(name);
        if(!current() || FNullEnt(created))return reject();
        entry.handle=EHANDLE(created);
        // Capture private data before Spawn so a rejecting spawn can also be
        // reclaimed, without treating a later reused edict as our allocation.
        entry.object=CBaseEntity::Instance(created);
        created->v.origin=buyer.pev->origin;created->v.spawnflags|=SF_NORESPAWN;
        const int spawned=DispatchSpawn(created);
        if(!current())return reject();
        auto* w=dynamic_cast<CBasePlayerWeapon*>(live(entry.handle));
        if(spawned<0 || !w || w!=entry.object || !Identity(*w,entry.id))return reject();
    }
    if(!prior.empty()) {
        auto* old=prior.front().object;
        if(live(prior.front().handle)!=old || !Contains(buyer,old) || old->pev->owner!=playerHandle.Get())return reject();
        dropAttempted=true;box=buyer.DropPlayerItem(STRING(old->pev->classname));
        if(!current() || buyer.m_rgpPlayerItems[slot])return reject();
        auto* container=dynamic_cast<CWeaponBox*>(live(box));
        if(!container)return reject();
        for(const auto& saved:prior) {
            auto* item=live(saved.handle);
            if(!item || item!=saved.object || item->pev->owner!=container->edict())return reject();
        }
    }
    for(int i=0;i<freshCount;++i) {
        if(!current() || !live(fresh[i].handle))return reject();
        DispatchTouch(fresh[i].handle.Get(),playerHandle.Get());
        if(!current())return reject();
        auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh[i].handle));
        if(!w || w!=fresh[i].object || !Contains(buyer,w) || w->m_pPlayer!=&buyer || w->pev->owner!=playerHandle.Get())return reject();
    }
    if(!acquired())return reject();
    if(weaponId==csol::skull3::Id) {
        // MP114CE4A0 -> 114BB9D0: each normal AddPlayerItem decides whether
        // to switch. A and D have equal weight, and auto-switch can be off.
        // A successful purchase must not override that choice afterward.
        for(int i=0;i<freshCount;++i) {
            auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh[i].handle));
            if(!w || (buyer.m_pActiveItem==w && !Deployed(*w,fresh[i].id)))return reject();
        }
    } else {
        buyer.SelectItem(classname);
        if(!acquired())return reject();
        auto* w=dynamic_cast<CBasePlayerWeapon*>(live(fresh[0].handle));
        if(!w || buyer.m_pActiveItem!=w || !Deployed(*w,weaponId))return reject();
    }
    CSOL_UpdateWeaponInventory(&buyer);
    return acquired()?true:reject();
}
