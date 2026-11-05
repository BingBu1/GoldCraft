// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production weapon/service/player/box methods with actual ReAPI
// hooks. Only the engine's controlled entity/message/trace edges are replaced.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/wpn_skull3.h"
#include "../../native/csol/server/weapons.h"
#include "../../native/csol/server/weapon_service.h"
#include "../../native/csol/server/weapon_slots.h"
#include "../../native/csol/server/paired_inventory.h"
#include "../../native/csol/skull3.hpp"
#include "../../native/include/goldcraft/csol_weapon_api.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace {
using sdk_test::Engine;
using sdk_test::Require;
namespace sk = csol::skull3;
unsigned assertions{}, failed{};
std::function<void()> damageCallback;
unsigned playbackEvents{};
unsigned reloadAnimations{};
std::function<void()> makeVectorsCallback;
std::function<void(CBasePlayer*,PLAYER_ANIM)> animationCallback;
std::function<void(CBasePlayerWeapon*)> afterDeploy, afterReload, afterWeaponAnim;
void Expect(bool value, const char* message)
{ ++assertions; if (!value) { ++failed; std::cerr << "ASSERT " << message << '\n'; } }
void Animation(IReGameHook_CBasePlayer_SetAnimation*, CBasePlayer* player, PLAYER_ANIM animation)
{ if(animation==PLAYER_RELOAD)++reloadAnimations;if(animationCallback)animationCallback(player,animation); }
std::function<BOOL(IReGameHook_CBasePlayer_AddPlayerItem*, CBasePlayer*, CBasePlayerItem*)> add;
std::function<BOOL(IReGameHook_CBasePlayer_RemovePlayerItem*, CBasePlayer*, CBasePlayerItem*)> removeItem;
bool rejectDeploy{};
BOOL Add(IReGameHook_CBasePlayer_AddPlayerItem* next, CBasePlayer* p, CBasePlayerItem* w)
{ return add ? add(next,p,w) : next->callNext(p,w); }
BOOL Remove(IReGameHook_CBasePlayer_RemovePlayerItem* next, CBasePlayer* p, CBasePlayerItem* w)
{ return removeItem ? removeItem(next,p,w) : next->callNext(p,w); }
BOOL Deploy(IReGameHook_CBasePlayerWeapon_DefaultDeploy* next,CBasePlayerWeapon* w,char* view,char* player,int sequence,char* extension,int skip)
{ const auto result=rejectDeploy?FALSE:next->callNext(w,view,player,sequence,extension,skip);if(afterDeploy)afterDeploy(w);return result; }
int Reload(IReGameHook_CBasePlayerWeapon_DefaultReload* next,CBasePlayerWeapon* w,int clip,int sequence,float delay)
{ const auto result=next->callNext(w,clip,sequence,delay);if(afterReload)afterReload(w);return result; }
void WeaponAnim(IReGameHook_CBasePlayerWeapon_SendWeaponAnim* next,CBasePlayerWeapon* w,int sequence,int skip)
{ next->callNext(w,sequence,skip);if(afterWeaponAnim)afterWeaponAnim(w); }

void Prepare(Engine& e, const std::filesystem::path& source)
{
    e.Prepare("0");g_pGameRules=InstallGameRules();
    Require(g_pGameRules&&!CSOL_RulesRejected(),"native rules installed");LinkUserMessages();
    g_engfuncs.pfnInfoKeyValue=[](char*,const char* name)->char* {
        return const_cast<char*>(!std::strcmp(name,"csol_protocol")?"1":"");};
    g_engfuncs.pfnMakeVectors=[](const float*) {
        gpGlobals->v_forward=Vector(1,0,0);gpGlobals->v_right=Vector(0,1,0);gpGlobals->v_up=Vector(0,0,1);
        if(makeVectorsCallback)makeVectorsCallback();};
    g_engfuncs.pfnCanSkipPlayer=[](const edict_t*){return 0;};
    e.onTrace=[](edict_t*,TraceResult&){};
    for(const char* name:{"skull1","skull2","skull3","skull5","m249ex","balrog11"}) {
        const auto relative=std::string(name)+"_damage.json";
        std::ifstream file(source/"native/csol/config"/relative,std::ios::binary);
        Require(bool(file),"frozen damage configuration exists");std::ostringstream contents;contents<<file.rdbuf();
        e.files["csol/weapons/"+relative]=contents.str();
    }
    CSOL_InitWeapons();e.Set("csol_weapons","1");CSOL_ResetWeaponRegistry();CSOL_PrecacheWeapons();
    UTIL_PrecacheOtherWeapon("weapon_usp");UTIL_PrecacheOtherWeapon("weapon_m4a1");
    Require(CSOL_WeaponsReady()&&CSOL_Skull3Ready(),"actual pair precache and registry ready");
    g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,192);
    g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.registerHook(Add,192);
    g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.registerHook(Remove,192);
    g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.registerHook(Deploy,192);
    g_ReGameHookchains.m_CBasePlayerWeapon_DefaultReload.registerHook(Reload,192);
    g_ReGameHookchains.m_CBasePlayerWeapon_SendWeaponAnim.registerHook(WeaponAnim,192);
}
CBasePlayer& Player(Engine& e,int index)
{
    auto* ed=e.Reserve(index,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
    ed->v.flags=FL_CLIENT|FL_ONGROUND;ed->v.health=100;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;
    p->m_iTeam=CT;p->m_iJoiningState=JOINED;p->m_iNumSpawns=1;p->m_bJustConnected=false;p->has_disconnected=false;
    p->m_bCanShoot=true;p->CSPlayer()->m_bCanShootOverride=true;p->m_iAccount=10000;
    p->m_iAutoWepSwitch=1;
    return *p;
}
csol::IWeaponService& Service(int index)
{
    auto* api=static_cast<csol::IWeaponService*>(CreateInterface(csol::WeaponServiceVersion,nullptr));
    Require(api,"actual weapon service exists");api->SetZombieMode(true);api->SetPlayerRole(index,1);return *api;
}
std::vector<CBasePlayerWeapon*> Contents(CBasePlayerItem* head)
{
    std::vector<CBasePlayerWeapon*> result;std::set<CBasePlayerItem*> seen;
    for(auto* item=head;item;item=item->m_pNext) {
        Require(seen.insert(item).second&&seen.size()<=4,"inventory is acyclic and bounded");
        auto* weapon=dynamic_cast<CBasePlayerWeapon*>(item);Require(weapon,"inventory entry is a real weapon");result.push_back(weapon);
    }
    return result;
}
struct Pair { CBasePlayerWeapon *a{},*d{}; };
Pair Buy(CBasePlayer& p)
{
    const bool initiallyUnarmed=p.m_pActiveItem==nullptr;
    const int initialReserve=p.m_rgAmmo[CBasePlayer::GetAmmoIndex(sk::AmmoName)];
    Require(Service(p.entindex()).Buy(p.entindex(),sk::Id)==1,"public purchase acquires a complete SKULL3 pair");
    Pair result;
    for(auto* w:Contents(p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT])) {
        if(w->m_iId==sk::Id)result.a=w;else if(w->m_iId==sk::DualId)result.d=w;
    }
    Require(result.a&&result.d&&CSOL_IsSkull3(*result.a)&&CSOL_IsSkull3(*result.d),"purchase creates exact A and D types");
    Expect(result.a->m_iClip==35&&result.d->m_iClip==70,"each initial magazine retains its own original capacity");
    Expect(result.a->m_iPrimaryAmmoType==result.d->m_iPrimaryAmmoType,"pair shares one real ammo registry entry");
    Expect(p.m_rgAmmo[result.a->m_iPrimaryAmmoType]==initialReserve,"initial pair creation fills its two magazines without inventing shared reserve");
    if(initiallyUnarmed)Expect(p.m_pActiveItem==result.a,"equal-weight D insertion does not replace automatically selected A");
    Expect(result.a->m_pPlayer==&p&&result.d->m_pPlayer==&p&&result.a->pev->owner==p.edict()&&result.d->pev->owner==p.edict(),"both entities have one player owner");
    return result;
}
void RememberCurrent(CBasePlayer& player)
{
    auto* weapon=dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem);
    Require(weapon&&CSOL_IsSkull3(*weapon),"remember test has an active actual pair member");
    const int clip=weapon->m_iClip;weapon->m_iClip=0;weapon->m_fFireOnEmpty=false;
    weapon->PrimaryAttack();weapon->m_iClip=clip;
    Expect(player.m_csolSlotPos[0]==sk::Position(weapon->m_iId==sk::DualId)&&player.m_csolValidSlotPos[0]==player.m_csolSlotPos[0],"actual original empty Primary attempt updates both remembered-position fields");
}
CWeaponBox& Drop(CBasePlayer& p,const Pair& pair,int selected=sk::DualId)
{
    p.SelectItem(selected==sk::Id?sk::Classname:sk::DualClassname);
    Require(p.m_pActiveItem==(selected==sk::Id?pair.a:pair.d),"real explicit selection reaches remembered pair member");
    RememberCurrent(p);
    pair.a->m_iClip=7;pair.d->m_iClip=18;p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]=23;
    auto* box=dynamic_cast<CWeaponBox*>(p.DropPlayerItem(""));Require(box,"native drop produces one box");
    box->pev->flags|=FL_ONGROUND;
    const auto packed=Contents(box->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    Expect(packed.size()==2&&packed[0]==(selected==sk::Id?pair.a:pair.d),"box prepends remembered position after packing both original entities");
    Expect(!p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]&&!pair.a->m_pPlayer&&!pair.d->m_pPlayer,"drop clears both inventory owners");
    Expect(pair.a->m_iClip==7&&pair.d->m_iClip==18&&p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]==23,"manual drop retains two clips and the owner's reserve");
    return *box;
}
void Normal(Engine& e)
{
    auto& p=Player(e,1);auto& recipient=Player(e,2);Service(2);
    const auto pair=Buy(p);auto& box=Drop(p,pair);
    box.Touch(&recipient);
    const auto received=Contents(recipient.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    Expect(received.size()==2&&pair.a->m_pPlayer==&recipient&&pair.d->m_pPlayer==&recipient,"real Touch accepts both entities despite first filling the primary slot");
    Expect(recipient.m_pActiveItem==pair.d,"pickup selects remembered box-head D and equal-weight A does not replace it");
    Expect(pair.a->m_iClip==7&&pair.d->m_iClip==18,"pickup does not replenish either magazine");
    Expect(!box.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]&&(box.pev->flags&FL_KILLME),"fully accepted box retires after unlinking all items");
    recipient.RemoveAllItems(FALSE);
    Expect(!recipient.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]&&!recipient.m_pActiveItem,"native RemoveAllItems clears the whole paired slot");
    Expect(pair.a->pev->nextthink>0&&pair.d->pev->nextthink>0,"native removal schedules both real entities");
}
void PickupFault(Engine& e,int fault)
{
    auto& p=Player(e,1);auto& recipient=Player(e,2);auto& third=Player(e,3);Service(2);Service(3);
    const auto pair=Buy(p);auto& box=Drop(p,pair);
    const EHANDLE a(pair.a),d(pair.d);unsigned calls{},reentries{};CBasePlayerItem* faultItem{};
    const int failing=(fault==2||fault==4||fault==6)?2:1;
    bool transfer{};
    add=[&](auto* chain,CBasePlayer* player,CBasePlayerItem* item)->BOOL {
        if(transfer)return chain->callNext(player,item);
        ++calls;
        if(int(calls)!=failing)return chain->callNext(player,item);
        faultItem=item;
        if(fault==1||fault==2)return FALSE;
        const auto accepted=chain->callNext(player,item);
        if(fault==3||fault==4||fault==10)return FALSE;
        if(fault==5||fault==6){Engine::Remove(item->edict());return FALSE;}
        if(fault==7){
            Require(player->RemovePlayerItem(item),"transfer removes the original link");transfer=true;
            Require(third.AddPlayerItem(item),"real alternate player accepts transferred item");transfer=false;
            item->AttachToPlayer(&third);return FALSE;
        }
        if(fault==8){++reentries;box.Touch(player);}
        if(fault==9){++player->m_iNumSpawns;}
        return accepted;
    };
    if(fault==10)removeItem=[&](auto* chain,CBasePlayer* player,CBasePlayerItem* item)->BOOL {
        return item==faultItem?FALSE:chain->callNext(player,item);};
    box.Touch(&recipient);add={};removeItem={};
    const auto retained=Contents(box.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    const auto received=Contents(recipient.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    if(fault==8){Expect(calls==2&&reentries==1&&received.size()==2&&retained.empty(),"same-box nested Touch cannot duplicate pickup");return;}
    if(fault==9){Expect(calls==1,"respawn identity change stops pickup before second Add");return;}
    Expect(calls==2,"rejected first item does not prevent trying its initially eligible companion");
    if(fault==5||fault==6){
        Expect(received.size()==1&&retained.empty(),"immediate release leaves no freed inventory link or boxed ghost");
        Expect((a.Get<CBaseEntity>()!=nullptr)+(d.Get<CBaseEntity>()!=nullptr)==1,"exactly the plugin-released entity is gone");
    }else if(fault==7){
        Expect(received.size()==1&&retained.empty()&&third.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]==faultItem,"rejected hook transfer is not stolen back or destroyed");
    }else{
        Expect(received.size()==1&&retained.size()==1&&retained[0]==faultItem,"pre/post rejection restores one item to its original box");
        Expect(!faultItem->m_pPlayer&&faultItem->pev->owner==box.edict()&&!(box.pev->flags&FL_KILLME),"partial box retains exact ownership and remains usable");
        Expect(pair.a->m_iClip==7&&pair.d->m_iClip==18,"rejected pickup preserves both original clips");
    }
    for(auto* item:received)Expect(item->m_pPlayer==&recipient&&item->pev->owner==recipient.edict(),"accepted companion has unique recipient ownership");
}
void DeathAndExpiry(Engine& e,bool expiry)
{
    auto& p=Player(e,1);const auto pair=Buy(p);p.SelectItem(sk::DualClassname);
    RememberCurrent(p);
    pair.a->m_iClip=7;pair.d->m_iClip=18;p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]=23;
    const auto start=e.nextEntity;
    p.PackDeadPlayerItems();
    CWeaponBox* box{};
    for(int i=start;i<e.nextEntity;++i)
        if(auto* candidate=dynamic_cast<CWeaponBox*>(CBaseEntity::Instance(Engine::At(i))))box=candidate;
    Require(box,"real death inventory packs a box");
    const auto packed=Contents(box->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    Expect(packed.size()==2&&packed[0]==pair.d,"death drop retains remembered pair order");
    Expect(pair.a->m_iClip==7&&pair.d->m_iClip==18,"death drop preserves both magazines");
    int reserve{};for(int n=0;n<MAX_AMMO_SLOTS;++n)reserve+=box->m_rgAmmo[n];
    Expect(reserve==46&&!p.m_rgAmmo[pair.a->m_iPrimaryAmmoType],"original per-entity ammo aggregation packs twice then native RemoveAll clears owner");
    if(expiry){box->Kill();Expect(pair.a->pev->nextthink>0&&pair.d->pev->nextthink>0,"expired box schedules both entities for destruction");}
    else{
        auto& receiver=Player(e,2);Service(2);box->pev->flags|=FL_ONGROUND;box->Touch(&receiver);
        Expect(Contents(receiver.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]).size()==2&&receiver.m_rgAmmo[pair.a->m_iPrimaryAmmoType]==46,"death box gives both original guns and stored shared ammo once");
    }
}
struct PrimaryTarget final : CBaseEntity
{
    unsigned hits{};
    BOOL TakeDamage(entvars_t* inflictor,entvars_t* attacker,float amount,int bits) override
    {
        ++hits;const auto result=CBaseEntity::TakeDamage(inflictor,attacker,amount,bits);
        if(damageCallback)damageCallback();return result;
    }
};
void PrimaryLifecycle(Engine& e,bool release)
{
    auto& p=Player(e,1);const auto pair=Buy(p);
    p.SelectItem(sk::Classname);Require(p.m_pActiveItem==pair.a,"real single SKULL3 deployed");
    const EHANDLE playerHandle(&p),weaponHandle(pair.a);auto* rules=g_pGameRules;
    auto* ed=e.Reserve(e.nextEntity++,"pair_primary_target");
    auto* target=GetClassPtr<CCSEntity>(reinterpret_cast<PrimaryTarget*>(&ed->v));
    target->pev->health=10000;target->pev->takedamage=DAMAGE_AIM;
    e.onTrace=[&](edict_t*,TraceResult& out){out.flFraction=.01f;out.vecEndPos=Vector(10,0,0);out.pHit=target->edict();};
    g_engfuncs.pfnTraceTexture=[](edict_t*,const float*,const float*)->const char*{return nullptr;};
    g_engfuncs.pfnPlaybackEvent=[](int,const edict_t*,unsigned short,float,float*,float*,float,float,int,int,int,int){++playbackEvents;};
    p.m_csolSlotPos[0]=p.m_csolValidSlotPos[0]=2;
    damageCallback=[&]{
        if(release){
            Require(p.RemovePlayerItem(pair.a),"hit callback unlinks the firing weapon before release");
            Engine::Remove(pair.a->edict());Engine::Remove(p.edict());
        }else g_pGameRules=nullptr;
    };
    pair.a->PrimaryAttack();damageCallback={};
    Expect(target->hits==1&&target->pev->health<10000,"actual Primary applies its first authoritative hit");
    Expect(playbackEvents==0,"retired actual Primary emits no stale event after its bullet callback");
    if(release)Expect(!playerHandle.Get()&&!weaponHandle.Get(),"actual Primary returns safely after player and gun private-data release");
    else Expect(p.m_csolSlotPos[0]==2&&p.m_csolValidSlotPos[0]==2,"retired Primary does not run its later RememberPosition tail");
    g_pGameRules=rules;ClearMultiDamage();
}
void SelectExisting(Engine& e)
{
    auto& p=Player(e,1);const auto pair=Buy(p);p.SelectItem(sk::Classname);
    pair.a->m_iClip=7;pair.d->m_iClip=18;p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]=23;
    p.SelectItem(sk::DualClassname);
    Expect(p.m_pActiveItem==pair.d&&pair.d->m_iClip==41&&pair.a->m_iClip==7&&p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]==0,"real cross-position SelectItem fills only D from shared reserve");
    Expect(p.pev->weaponanim==16&&p.m_flNextAttack==3,"actual dual conversion has original sequence and delay");
    Expect(p.m_csolSlotPos[0]==1,"explicit SelectItem preserves old position until the original Primary consumer updates it");
    RememberCurrent(p);
    p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]=5;p.SelectItem(sk::Classname);
    Expect(p.m_pActiveItem==pair.a&&pair.a->m_iClip==12&&pair.d->m_iClip==41&&p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]==0,"real reverse selection consumes five reserve without merging peer clip");
    Expect(p.pev->weaponanim==17&&p.m_flNextAttack==3,"actual single conversion has original sequence and delay");
}
void KeepActive(Engine& e)
{
    auto& p=Player(e,1);p.m_iAutoWepSwitch=0;
    auto* ed=Engine::Create(Engine::String("weapon_usp"));ed->v.spawnflags|=SF_NORESPAWN;
    Require(DispatchSpawn(ed)>=0,"actual native secondary spawns");DispatchTouch(ed,p.edict());
    auto* pistol=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
    Require(pistol&&p.m_pActiveItem==pistol,"native secondary automatically selected when unarmed");
    Buy(p);
    Expect(p.m_pActiveItem==pistol,"pair purchase respects disabled automatic switching with another weapon active");
}
struct UnknownPrimary final : CBasePlayerWeapon
{
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    BOOL CanDeploy() override { return TRUE; }
    BOOL Deploy() override { return TRUE; }
};
void OrdinaryPosition(Engine& e)
{
    auto& p=Player(e,1);const auto pair=Buy(p);p.SelectItem(sk::DualClassname);RememberCurrent(p);
    auto* ed=Engine::Create(Engine::String("weapon_m4a1"));ed->v.spawnflags|=SF_NORESPAWN;
    Require(DispatchSpawn(ed)>=0,"native M4 source spawns");
    auto* rifle=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));Require(rifle,"actual native M4 type");
    Require(p.AddPlayerItem(rifle),"native SDK can add the controlled comparison primary");rifle->AttachToPlayer(&p);
    const int hudColumn=rifle->CSPlayerWeapon()->m_ItemInfo.iPosition;
    Require(p.SwitchWeapon(rifle),"real automatic SwitchWeapon selects M4");
    Expect(p.m_csolSlotPos[0]==1&&p.m_csolValidSlotPos[0]==1,"stock M4 writes original primary position one after D remembered two");
    Expect(rifle->CSPlayerWeapon()->m_ItemInfo.iPosition==hudColumn,"original position tracking does not rewrite the native HUD column");
    pair.d->m_iClip=18;p.m_rgAmmo[pair.d->m_iPrimaryAmmoType]=23;p.SelectItem(sk::DualClassname);
    Expect(p.m_pActiveItem==pair.d&&pair.d->m_iClip==41&&p.m_rgAmmo[pair.d->m_iPrimaryAmmoType]==0&&p.pev->weaponanim==16,"explicit D selection after native M4 uses the cross-position branch");
    RememberCurrent(p);
    auto* unknownEd=e.Reserve(e.nextEntity++,"unknown_fixture_primary");
    auto* unknown=GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<UnknownPrimary*>(&unknownEd->v));
    unknown->m_pPlayer=&p;unknown->pev->owner=p.edict();unknown->m_iId=777;
    Require(p.SwitchWeapon(unknown),"actual SwitchWeapon reaches the controlled unknown weapon");
    Expect(p.m_csolSlotPos[0]==2&&p.m_csolValidSlotPos[0]==2,"unknown original metadata leaves remembered position untouched");
    p.m_pActiveItem=pair.d;p.m_pLastItem=nullptr;
}
void BeamPositionIdentity(Engine& e)
{
    auto& p=Player(e,1);const auto pair=Buy(p);
    for(const auto& identity:std::array<std::pair<int,const char*>,2>{{
        {725,"weapon_beamgun"},{726,"weapon_beamgunle"}}}) {
        p.SelectItem(sk::DualClassname);RememberCurrent(p);
        auto* ed=e.Reserve(e.nextEntity++,identity.second);
        // This isolates the metadata consumer inside actual SwitchWeapon.
        // Beam attack/Deploy behavior is outside this paired-inventory fixture.
        auto* weapon=GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<UnknownPrimary*>(&ed->v));
        weapon->m_pPlayer=&p;weapon->pev->owner=p.edict();weapon->m_iId=identity.first;
        weapon->CSPlayerWeapon()->m_ItemInfo.iPosition=4;
        Require(p.SwitchWeapon(weapon),"actual SwitchWeapon accepts the controlled Beam identity");
        Expect(p.m_csolSlotPos[0]==1&&p.m_csolValidSlotPos[0]==1,"original Beam/LE metadata writes remembered position one");
        Expect(weapon->CSPlayerWeapon()->m_ItemInfo.iPosition==4,"Beam original position leaves the host HUD column unchanged");
        p.m_csolSlotPos[0]=p.m_csolValidSlotPos[0]=2;
        p.m_pActiveItem=pair.d;weapon->pev->classname=Engine::String("unknown_fixture_primary");
        Require(p.SwitchWeapon(weapon),"actual SwitchWeapon reaches a mismatched original identity");
        Expect(p.m_csolSlotPos[0]==2&&p.m_csolValidSlotPos[0]==2,"recognized numeric ID alone cannot overwrite original position");
        p.m_pActiveItem=pair.d;p.m_pLastItem=nullptr;
    }
}
void PurchaseRollback(Engine& e,int fault)
{
    auto& p=Player(e,1);const auto prior=Buy(p);
    p.SelectItem(sk::Classname);prior.a->m_iClip=7;prior.d->m_iClip=18;
    p.m_rgAmmo[prior.a->m_iPrimaryAmmoType]=23;p.m_flNextAttack=25;
    prior.a->m_flNextPrimaryAttack=21;prior.a->m_flNextSecondaryAttack=22;prior.a->m_flTimeWeaponIdle=23;
    prior.d->m_flNextPrimaryAttack=31;prior.d->m_flNextSecondaryAttack=32;prior.d->m_flTimeWeaponIdle=33;
    const auto active=p.m_pActiveItem;const int position=p.m_csolSlotPos[0],valid=p.m_csolValidSlotPos[0];
    std::vector<EHANDLE> fresh;unsigned adds{},removes{};
    add=[&](auto* chain,CBasePlayer* player,CBasePlayerItem* item)->BOOL {
        if(item==prior.a||item==prior.d)return chain->callNext(player,item);
        ++adds;fresh.emplace_back(item);
        if((fault==1&&adds==1)||(fault==2&&adds==2))return FALSE;
        const auto result=chain->callNext(player,item);
        if((fault==3&&adds==1)||(fault==4&&adds==2))return FALSE;
        if((fault==10&&adds==1)||(fault==11&&adds==2)){Engine::Remove(item->edict());return FALSE;}
        return result;
    };
    removeItem=[&](auto* chain,CBasePlayer* player,CBasePlayerItem* item)->BOOL {
        if(item!=prior.a&&item!=prior.d)return chain->callNext(player,item);
        ++removes;
        if((fault==5&&removes==1)||(fault==7&&removes==2))return FALSE;
        const auto result=chain->callNext(player,item);
        if((fault==6&&removes==1)||(fault==8&&removes==2))return FALSE;
        return result;
    };
    rejectDeploy=fault==9;const int accepted=Service(1).Buy(1,sk::Id);rejectDeploy=false;add={};removeItem={};
    const auto inventory=Contents(p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);
    if(!fault){
        Expect(accepted==1&&inventory.size()==2&&inventory[0]!=prior.a&&inventory[0]!=prior.d,"real repurchase replaces the whole primary pair");
        Expect(prior.a->pev->owner==prior.d->pev->owner&&prior.a->pev->owner!=p.edict(),"old pair shares one dropped box after commit");
        return;
    }
    Expect(!accepted,"native callback failure rejects paired purchase");
    Expect(inventory.size()==2&&std::find(inventory.begin(),inventory.end(),prior.a)!=inventory.end()&&std::find(inventory.begin(),inventory.end(),prior.d)!=inventory.end(),"purchase rollback restores both exact original inventory entities");
    Expect(prior.a->m_pPlayer==&p&&prior.d->m_pPlayer==&p&&prior.a->pev->owner==p.edict()&&prior.d->pev->owner==p.edict(),"purchase rollback restores both owner links");
    Expect(prior.a->m_iClip==7&&prior.d->m_iClip==18&&p.m_rgAmmo[prior.a->m_iPrimaryAmmoType]==23,"purchase rollback preserves independent clips and shared reserve");
    Expect(p.m_pActiveItem==active&&p.m_flNextAttack==25&&p.m_csolSlotPos[0]==position&&p.m_csolValidSlotPos[0]==valid,"purchase rollback preserves selected entity, next attack and both position fields");
    Expect(prior.a->m_flNextPrimaryAttack==21&&prior.a->m_flNextSecondaryAttack==22&&prior.a->m_flTimeWeaponIdle==23&&prior.d->m_flNextPrimaryAttack==31&&prior.d->m_flNextSecondaryAttack==32&&prior.d->m_flTimeWeaponIdle==33,"purchase rollback restores timers of both paired entities");
    for(const auto& handle:fresh)if(auto* item=handle.Get<CBaseEntity>())
        Expect((item->pev->flags&FL_KILLME)!=0,"failed newly created pair members are retired");
}
void OtherWeaponLifecycle(Engine& e,int edge)
{
    auto& p=Player(e,1);const auto pair=Buy(p);p.SelectItem(sk::Classname);
    const EHANDLE weaponHandle(pair.a);auto* rules=g_pGameRules;
    pair.a->m_iClip=7;p.m_rgAmmo[pair.a->m_iPrimaryAmmoType]=23;
    pair.a->m_flAccuracy=.88f;pair.a->m_iShotsFired=17;p.m_flNextAttack=25;
    pair.a->m_flTimeWeaponIdle=0;p.m_csolSlotPos[0]=p.m_csolValidSlotPos[0]=2;
    unsigned callbacks{};reloadAnimations=0;
    const auto retire=[&] {
        ++callbacks;g_pGameRules=nullptr;p.m_flNextAttack=77;
    };
    const auto release=[&](CBasePlayerWeapon* w) {
        ++callbacks;Require(w==pair.a&&p.RemovePlayerItem(w),"actual native callback unlinks exact firing entity");
        Engine::Remove(w->edict());p.m_flNextAttack=77;
    };
    if(edge==0)afterDeploy=[&](auto*){retire();};
    else if(edge==1)afterReload=[&](auto*){retire();};
    else if(edge==2)animationCallback=[&](auto*,PLAYER_ANIM a){if(a==PLAYER_RELOAD)retire();};
    else if(edge==3)makeVectorsCallback=retire;
    else if(edge==4)afterWeaponAnim=[&](auto*){retire();};
    else if(edge==5)afterDeploy=release;
    else if(edge==6)afterReload=release;
    else if(edge==7||edge==8)afterWeaponAnim=[&](auto*){retire();};
    else if(edge==9||edge==10)afterWeaponAnim=release;
    const bool deploy=edge==0||edge==5||edge==7||edge==9;
    const bool reload=edge==1||edge==2||edge==6||edge==8||edge==10;
    if(deploy)pair.a->Deploy();else if(reload)pair.a->Reload();else pair.a->WeaponIdle();
    afterDeploy={};afterReload={};afterWeaponAnim={};animationCallback={};makeVectorsCallback={};
    Expect(callbacks==1,"actual selected native callback ran exactly once");
    if(deploy)Expect(p.m_flNextAttack==77,"retired Deploy cannot overwrite callback's surviving player state");
    if(reload&&edge!=2)Expect(reloadAnimations==0,"retired DefaultReload does not trigger later player animation");
    if(edge==1||edge==2||edge==8)Expect(pair.a->m_flAccuracy==.88f&&pair.a->m_iShotsFired==17,"retired Reload does not reset fire state after callback");
    if(edge==8)Expect(!pair.a->m_fInReload&&pair.a->m_flTimeWeaponIdle==0,"nested reload animation retirement stops the native reload state tail");
    if(edge==3)Expect(pair.a->m_flTimeWeaponIdle==0,"retired autoaim callback stops later idle deadline write");
    if(edge==3||edge==4)Expect(p.m_csolSlotPos[0]==2&&p.m_csolValidSlotPos[0]==2,"retired Idle does not run RememberPosition tail");
    if(edge==5||edge==6||edge>=9)Expect(!weaponHandle.Get(),"actual private weapon data was released during the native callback");
    g_pGameRules=rules;
}
unsigned lastItemSpeedCalls{};
void LastItemSpeed(IReGameHook_CBasePlayer_ResetMaxSpeed*,CBasePlayer*) { ++lastItemSpeedCalls; }
void LastItemLifecycle(Engine& e,int edge)
{
    auto& p=Player(e,1);const auto pair=Buy(p);p.SelectItem(sk::DualClassname);
    Require(p.m_pActiveItem==pair.d&&p.m_pLastItem==pair.a,"Q fixture uses the actual remembered prior pair member");
    const EHANDLE weapon(pair.a);auto* rules=g_pGameRules;unsigned callbacks{},callbackSpeedCalls{};
    lastItemSpeedCalls=0;
    g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.registerHook(LastItemSpeed,192);
    if(edge!=2)afterDeploy=[&](CBasePlayerWeapon* item) {
        Require(item==pair.a,"Q reaches the actual single member DefaultDeploy");++callbacks;
        if(edge==0)g_pGameRules=nullptr;
        else {
            Require(p.RemovePlayerItem(item),"Q post callback unlinks the deployed weapon");
            Engine::Remove(item->edict());
            // A hook may choose another surviving weapon after releasing A.
            // The abandoned Q call must not update that new selection.
            p.m_pActiveItem=pair.d;p.m_pLastItem=nullptr;
        }
        callbackSpeedCalls=lastItemSpeedCalls;
    };
    p.SelectLastItem();afterDeploy={};
    g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.unregisterHook(LastItemSpeed);
    if(edge==2)Expect(p.m_pActiveItem==pair.a&&p.m_pLastItem==pair.d&&lastItemSpeedCalls==1,"normal Q preserves swap, deploy and speed update");
    else {
        Expect(callbacks==1,"Q invokes the selected DefaultDeploy callback once");
        Expect(lastItemSpeedCalls==callbackSpeedCalls,"retired Q stops before updating the surviving or replaced selection");
        if(edge==1)Expect(!weapon.Get()&&p.m_pActiveItem==pair.d,"Q preserves the callback's surviving selection after immediate release");
    }
    g_pGameRules=rules;
}
struct ReloadMessage {int destination{},id{},recipient{};std::vector<int> bytes;};
std::vector<ReloadMessage> reloadMessages;
void ReloadAudience(Engine& e,bool emptyPrivate)
{
    e.globals.maxClients=10;e.nextEntity=16;
    auto& p=Player(e,1);const auto pair=Buy(p);
    if(emptyPrivate)e.Reserve(2,"player");
    else {auto* blank=e.Reserve(2,"player");blank->free=TRUE;}
    auto& closePlayer=Player(e,3);closePlayer.pev->origin=Vector(1.9f,0,0);
    auto& dormant=Player(e,4);dormant.pev->flags=FL_DORMANT;
    auto& mixed=Player(e,5);mixed.pev->flags=FL_DORMANT|FL_CLIENT;mixed.pev->origin=Vector(2.1f,0,0);
    auto& boundary=Player(e,6);boundary.pev->origin=Vector(512,0,0);
    auto& distantPlayer=Player(e,7);distantPlayer.pev->origin=Vector(512.01f,0,0);
    auto* freeSlot=e.Reserve(8,"player");freeSlot->free=TRUE;
    Player(e,9);auto& almost=Player(e,10);almost.pev->origin=Vector(511.99f,0,0);
    const int outsideIndex=e.nextEntity++;Player(e,outsideIndex);
    unsigned classnameSearches{};std::vector<int> indexes;
    e.onFind=[&](auto*,const char*,const char*){++classnameSearches;};
    e.onIndexLookup=[&](int index,auto*){indexes.push_back(index);};
    reloadMessages.clear();
    g_engfuncs.pfnMessageBegin=[](int destination,int id,const float*,edict_t* recipient) {
        reloadMessages.push_back({destination,id,Engine::Index(recipient),{}});};
    g_engfuncs.pfnWriteByte=[](int value) {
        Require(!reloadMessages.empty(),"reload byte belongs to an open message");
        reloadMessages.back().bytes.push_back(value&255);};
    g_engfuncs.pfnMessageEnd=[]{};
    pair.a->ReloadSound();e.onFind={};e.onIndexLookup={};
    Expect(indexes==std::vector<int>({1,2,3,4,5,6,7,8,9,10}),"original reload enumerates exactly 1 through maxClients and continues empty/free slots");
    Expect(classnameSearches==0,"original reload recipient lookup does not scan world classname entities");
    const std::map<int,int> expected{{3,255},{5,254},{6,255},{9,255},{10,0}};
    std::map<int,int> found;
    for(const auto& message:reloadMessages) {
        Expect(message.destination==MSG_ONE&&message.id==gmsgReloadSound,"reload uses the original per-recipient message channel");
        Expect(message.bytes.size()==2&&message.bytes[1]==1,"SKULL3 reload message has volume and non-shotgun type");
        if(message.bytes.size()==2)Expect(found.emplace(message.recipient,message.bytes[0]).second,"each eligible reload recipient receives one message");
    }
    Expect(found==expected,"original dormant sentinel, squared-distance boundary and truncate-before-half volumes match");
    Expect(!found.contains(outsideIndex),"player-class world entity beyond maxClients never receives reload audio");
}
}
int main(int argc,char** argv)
{
    try{
        Require(argc==2,"one pair lifecycle group");auto e=std::make_unique<Engine>();
        Prepare(*e,std::filesystem::path(argv[0]).parent_path()/"source");const int group=std::stoi(argv[1]);
        if(group==0)Normal(*e);else if(group<=10)PickupFault(*e,group);
        else if(group<=12)DeathAndExpiry(*e,group==12);else if(group<=14)PrimaryLifecycle(*e,group==14);
        else if(group==15)SelectExisting(*e);else if(group==16)KeepActive(*e);
        else if(group==17)OrdinaryPosition(*e);
        else if(group==18)BeamPositionIdentity(*e);
        else if(group==60||group==61)ReloadAudience(*e,group==60);
        else if(group>=51&&group<=53)LastItemLifecycle(*e,group-51);
        else if(group>=40)OtherWeaponLifecycle(*e,group-40);else PurchaseRollback(*e,group-20);
        add={};removeItem={};
        g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
        g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.unregisterHook(Add);
        g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.unregisterHook(Remove);
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.unregisterHook(Deploy);
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultReload.unregisterHook(Reload);
        g_ReGameHookchains.m_CBasePlayerWeapon_SendWeaponAnim.unregisterHook(WeaponAnim);
        e.reset();std::cout<<"behavior="<<assertions<<" failed="<<failed<<" sdk="<<sdk_test::checks<<" sdkFailed="<<sdk_test::failures<<'\n';
        return failed||sdk_test::failures?1:0;
    }catch(const std::exception& error){std::cerr<<"EXCEPTION "<<error.what()<<" behavior="<<assertions<<" failed="<<failed<<'\n';return 2;}
}
