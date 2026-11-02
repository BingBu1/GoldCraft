// SPDX-License-Identifier: GPL-3.0-or-later
// The transaction and inventory methods are extracted unchanged from their
// production sources. Only engine services and hook boundaries are controlled.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../native/csol/melee_identity.hpp"
#include "../../native/csol/melee_resources.hpp"
#include "../../native/csol/melee_prediction.hpp"

using BOOL=int; using WeaponIdType=int;
constexpr int TRUE=1,FALSE=0,MAX_ITEM_TYPES=6,PRIMARY_WEAPON_SLOT=1,KNIFE_SLOT=3;
constexpr int FL_KILLME=1<<30,FL_DORMANT=1<<31,SF_NORESPAWN=1,HIDEHUD_WEAPONS=1;
constexpr int CT=2,WEAPON_KNIFE=29,WPNSTATE_SHIELD_DRAWN=1,HITGROUP_SHIELD_ENABLED=1;
constexpr int DEFAULT_FOV=90,MOVETYPE_FOLLOW=12,SOLID_NOT=0,EF_NODRAW=128;
constexpr int USE_TOGGLE=3,MSG_ONE=1,CHAN_ITEM=3,VOL_NORM=1,ATTN_NORM=1;
#define REGAMEDLL_FIXES
#define EXT_FUNC
#define __API_HOOK(name) name##_OrigFunc
struct CBaseEntity; struct CBasePlayer; struct CBasePlayerItem; struct CBasePlayerWeapon;
struct edict_t;
struct Vars {
    std::array<float,3> origin{};
    int flags{},spawnflags{},weapons{},viewmodel{},weaponmodel{},movetype{},solid{},effects{},modelindex{},model{},gamestate{};
    float nextthink{},fov{};
    const char* classname="";
    edict_t* owner{},*aiment{},*containing{};
};
struct edict_t { Vars v{}; bool free{}; int serialnumber=1; CBaseEntity* pvPrivateData{}; };
bool validWorld=true,scenarioMode{},eligible=true,ready=true,identityAllowed=true,allowPickup=true,autoSwitch=true;
bool addReject{},addPostFalse{},freshRemoveReject{},oldRemoveReject{},oldRemovePostFalse{},spawnReject{},retireReject{};
int generation=1,checks{},failures{},creates{},removes{},pickups{},updates{},closedActions{},gmsgWeapPickup{};
std::function<void(const char*,CBaseEntity*)> callback;
void Event(const char* stage,CBaseEntity* object) { if(callback)callback(stage,object); }
void Boundary() { if(!validWorld){++closedActions;throw std::runtime_error("entity operation after closed map");} }
struct CBaseEntity {
    edict_t record{}; Vars* pev=&record.v;
    CBaseEntity(){record.pvPrivateData=this;record.v.containing=&record;}
    virtual ~CBaseEntity()=default;
    edict_t* edict()const{return const_cast<edict_t*>(&record);}
    static CBaseEntity* Instance(edict_t* value){Boundary();return value?value->pvPrivateData:nullptr;}
    virtual BOOL IsPlayer(){return FALSE;}
    virtual BOOL AddPlayerItem(CBasePlayerItem*){throw std::runtime_error("unexpected nonplayer Add");}
    template<class T>void SetThink(T){}
    template<class T>void SetTouch(T){}
    void SUB_UseTargets(CBaseEntity*,int,float){Event("targets",this);}
};
edict_t* ENT(Vars* v){return v->containing;}
int OFFSET(edict_t* value){return value?1:0;}
bool FNullEnt(int value){return value==0;}
bool FNullEnt(edict_t* value){return !value||value->free;}
template<class T>T* GET_PRIVATE(edict_t* value){return value?static_cast<T*>(value->pvPrivateData):nullptr;}
void UTIL_Remove(CBaseEntity*);
#define DbgAssertMsg(condition,text) do{if(!(condition))throw std::runtime_error(text);}while(false)
#include "../../external/ReGameDLL_CS/regamedll/dlls/ehandle.h"
struct CBasePlayerItem:CBaseEntity {
    CBasePlayerItem* m_pNext{}; CBasePlayer* m_pPlayer{}; int m_iId=29,slot=KNIFE_SLOT;
    bool deployable=true,holsterable=true,deployResult=true,pending=true;
    virtual int iItemSlot(){return slot;}
    virtual BOOL IsWeapon(){return FALSE;}
    virtual BOOL CanDeploy(){Event("canDeploy",this);return deployable;}
    virtual BOOL CanHolster(){return holsterable;}
    virtual BOOL Deploy();
    virtual void Holster(int=0){pending=false;Event("holster",this);}
    virtual int AddToPlayer(CBasePlayer*);
    BOOL AddDuplicate(CBasePlayerItem*){return FALSE;}
    void CheckRespawn(){}
    void UpdateItemInfo(){}
    void Kill(){pev->flags|=FL_KILLME;}
    void AttachToPlayer(CBasePlayer*);
    void DefaultTouch(CBaseEntity*);
};
struct CBasePlayerWeapon:CBasePlayerItem {
    float m_flNextPrimaryAttack=7,m_flNextSecondaryAttack=8,m_flTimeWeaponIdle=9,m_flLastFireTime=10,
        m_flPrevPrimaryAttack=11,m_flDecreaseShotsFired=12,m_flAccuracy=.4f,m_flNextReload=13;
    int m_iShotsFired=4,m_fInReload=1,m_fInSpecialReload=1,m_iWeaponState{},m_iPrimaryAmmoType{},m_iSecondaryAmmoType{};
    bool m_bDelayFire=true;float m_fMaxSpeed{};
    BOOL IsWeapon()override{return TRUE;}
    int AddToPlayer(CBasePlayer*)override;
    const char* pszAmmo1(){return nullptr;}const char* pszAmmo2(){return nullptr;}
    BOOL AddWeapon(){return TRUE;}
    BOOL UseDecrement(){return TRUE;}
    BOOL DefaultDeploy(char*,char*,int,char*,int){Event("defaultDeploy",this);return deployResult;}
};
class CCSOLMelee:public CBasePlayerWeapon {
public:
    csol::melee::Variant Variant()const{return csol::melee::WeaponVariant(m_iId);}
    BOOL Deploy()override;void Holster(int=0)override;
    bool DeploymentComplete()const{return deploymentComplete;}
private:
    void CancelAttack();
    csol::melee::Prediction prediction;
    bool deploymentComplete{};
};
struct CBasePlayer:CBaseEntity {
    std::array<CBasePlayerItem*,MAX_ITEM_TYPES> m_rgpPlayerItems{};
    CBasePlayerItem *m_pActiveItem{},*m_pLastItem{},*m_pClientActiveItem{};
    int index=1,userid=101,m_iNumSpawns=1,m_iTeam=CT,m_iHideHUD=33,m_iFOV=90,m_iLastZoom=90;
    bool alive=true,has_disconnected{},m_bIsVIP{},m_bHasPrimary{},m_bShieldDrawn{},m_bResumeZoom{},shield{};
    float m_flNextAttack=.12f;
    BOOL IsPlayer()override{return TRUE;}bool IsAlive(){return alive;}bool HasShield(){return shield;}
    bool IsDormant(){return pev->flags&FL_DORMANT;}int entindex(){return index;}
    int GetAmmoIndex(const char*){return -1;}
    int LookupSequence(const char*){return 1;}
    void ResetAutoaim(){} void ResetMaxSpeed(){} void UpdateShieldCrosshair(bool){}
    BOOL AddPlayerItem(CBasePlayerItem*)override;BOOL RemovePlayerItem(CBasePlayerItem*);
    BOOL AddPlayerItem_OrigFunc(CBasePlayerItem*);BOOL RemovePlayerItem_OrigFunc(CBasePlayerItem*);
    BOOL SwitchWeapon(CBasePlayerItem*);void SelectItem(const char*);
#include "purchase_foreach.inc"
    CBasePlayerItem* GetItemByName(const char*);CBasePlayerItem* GetItemById(int);
    CBaseEntity* GiveNamedItemEx(const char*){throw std::runtime_error("unexpected nonmelee grant");}
    CBaseEntity* DropPlayerItem(const char*){throw std::runtime_error("unexpected DropPlayerItem");}
};
struct Rules {
    bool CanHavePlayerItem(CBasePlayer*,CBasePlayerItem*){return allowPickup;}
    void PlayerGotWeapon(CBasePlayer*,CBasePlayerItem*){}
    bool FShouldSwitchWeapon(CBasePlayer*,CBasePlayerItem*){return autoSwitch;}
}rules,*g_pGameRules=&rules;
struct Globals{int maxClients=2,maxEntities=128;}globals,*gpGlobals=&globals;
std::array<CBasePlayer*,3> players{};
std::vector<std::unique_ptr<CBaseEntity>> objects;
CBasePlayerWeapon *initial{},*spawnedWeapon{};
template<class T>T* Make(){auto ptr=std::make_unique<T>();auto* value=ptr.get();objects.push_back(std::move(ptr));return value;}
const char* STRING(const char* value){return value;}
bool FClassnameIs(Vars* vars,const char* value){return std::strcmp(vars->classname,value)==0;}
bool IsSecondaryWeapon(int){return false;}
void CSOL_RemoveBeamWeapon(CBasePlayerItem*){}
bool CSOL_SendWeaponPickup(CBasePlayer*,int){++pickups;return true;}
bool CSOL_ExtendedWeapon(int id){return csol::melee::IsWeapon(id);}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* player){return eligible&&player&&player->alive;}
int CSOL_LegacyWeaponMask(int id){return id>0&&id<32?int(1u<<id):0;}
#define MESSAGE_BEGIN(...) ((void)0)
#define WRITE_BYTE(...) ((void)0)
#define MESSAGE_END(...) ((void)0)
#define EMIT_SOUND(...) ((void)0)
#include "purchase_sdk_methods.inc"
BOOL CBasePlayerItem::Deploy(){if(m_pPlayer)m_pPlayer->m_flNextAttack=.75f;Event("deploy",this);return deployResult;}
BOOL CBasePlayer::AddPlayerItem(CBasePlayerItem* item){
    Event("beforeAdd",item);if(addReject)return FALSE;
    const auto result=AddPlayerItem_OrigFunc(item);Event("afterAdd",item);return addPostFalse?FALSE:result;
}
BOOL CBasePlayer::RemovePlayerItem(CBasePlayerItem* item){
    Event(item==initial?"beforeRemoveOld":"beforeRemoveFresh",item);
    if((item==initial&&oldRemoveReject)||(item==spawnedWeapon&&freshRemoveReject))return FALSE;
    const auto result=RemovePlayerItem_OrigFunc(item);
    Event(item==initial?"afterRemoveOld":"afterRemoveFresh",item);
    return item==initial&&oldRemovePostFalse?FALSE:result;
}
struct PlayerState {std::uint64_t generation=1;};
struct Registry {
    std::shared_ptr<PlayerState> state=std::make_shared<PlayerState>();
    bool Active(){return true;}
    std::shared_ptr<PlayerState> Find(const EHANDLE& h){return h.Get()?state:nullptr;}
};
auto authority=std::make_shared<Registry>();
struct CSOLScenarioSdkScope {
    int epoch=generation;
    bool Current()const{return validWorld&&epoch==generation;}
    bool Scenario()const{return scenarioMode;}
    std::shared_ptr<int> EntityAccess()const{return {};}
};
auto CSOL_ScenarioPlayerAuthority(const void*){return authority;}
bool CSOL_ScenarioCanBuy(){return eligible;}
bool CSOL_ScenarioSetMeleeIdentity(CBasePlayerWeapon&){return identityAllowed;}
bool CSOL_WeaponsReady(){return ready;}
bool CSOL_MeleeReady(int){return ready;}
bool CSOL_ZombieHuman(CBasePlayer* p){return eligible&&p&&p->alive;}
bool CSOL_HasWeapon(CBasePlayer* p,int id){return p->GetItemById(id)!=nullptr;}
int CSOL_MeleeItemIdentity(const CBasePlayerWeapon& w){
    if(!csol::melee::IsWeapon(w.m_iId))return 0;
    const auto& r=csol::melee::Resource(csol::melee::WeaponVariant(w.m_iId));
    return FClassnameIs(w.pev,r.classname)?(w.m_iId==1022?483:363):0;
}
bool CSOL_MeleeDeploymentComplete(const CBasePlayerWeapon&);
void CSOL_UpdateWeaponInventory(CBasePlayer* p){++updates;Event("inventory",p);}
CBaseEntity* UTIL_PlayerByIndex(int index){Boundary();return index>0&&index<3?players[index]:nullptr;}
int GETPLAYERUSERID(edict_t* e){Boundary();return static_cast<CBasePlayer*>(e->pvPrivateData)->userid;}
const char* ALLOC_STRING(const char* value){return value;}
edict_t* CREATE_NAMED_ENTITY(const char* name){
    Boundary();++creates;spawnedWeapon=Make<CCSOLMelee>();spawnedWeapon->pev->classname=name;
    spawnedWeapon->m_iId=std::strcmp(name,"weapon_dragonsword")==0?1022:1023;
    Event("create",spawnedWeapon);return spawnedWeapon->edict();
}
void REMOVE_ENTITY(edict_t* e){Boundary();e->free=true;++e->serialnumber;++removes;}
int DispatchSpawn(edict_t* e){Boundary();Event("spawn",e->pvPrivateData);return spawnReject?-1:0;}
void DispatchTouch(edict_t* e,edict_t* other){
    Boundary();Event("touch",e->pvPrivateData);
    if(!validWorld)return;
    static_cast<CBasePlayerItem*>(e->pvPrivateData)->DefaultTouch(other->pvPrivateData);
}
void UTIL_Remove(CBaseEntity* e){
    Boundary();Event("retire",e);if(!validWorld||retireReject)return;
    e->pev->flags|=FL_KILLME;++removes;
}
std::uint16_t CompletionSerial(CBasePlayer&,csol::melee::Variant){return 17;}
#define Q_snprintf std::snprintf
#include "purchase_deploy.inc"
#include "purchase_actual.inc"

class IBaseInterface {public:virtual ~IBaseInterface()=default;};
#include "../../native/include/goldcraft/csol_weapon_api.hpp"
constexpr int CSOL_WEAPON_TKNIFEEX2=220,CSOL_WEAPON_BEAMGUN=725,CSOL_WEAPON_BEAMGUNLE=726,CSOL_WEAPON_CANNON=144,CSOL_WEAPON_K3=197;
struct Role{int userid{},value{};};std::array<Role,33>roles{};bool zombieMode{};
struct ItemInfo{const char* pszName="weapon";} itemInfo;
const ItemInfo* CSOL_ItemInfo(int){return &itemInfo;}
bool CSOL_CannonReady(){return true;}bool CSOL_K3Ready(){return true;}
struct CannonPurchaseBackup {explicit CannonPurchaseBackup(CBasePlayer*){}int Replace(){throw std::runtime_error("unexpected cannon");}};
int DeliverScenarioK3(CBasePlayer&){throw std::runtime_error("unexpected K3");}
void CVAR_SET_FLOAT(const char*,int){}void CSOL_ResetBeamGuard(CBasePlayer*){}void CSOL_CharacterRoleChanged(CBasePlayer*){}
#include "purchase_service.inc"

void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
void Reset(int existing=29){
    callback={};objects.clear();players={};spawnedWeapon=nullptr;
    validWorld=eligible=ready=identityAllowed=allowPickup=autoSwitch=true;scenarioMode=false;++generation;
    addReject=addPostFalse=freshRemoveReject=oldRemoveReject=oldRemovePostFalse=spawnReject=retireReject=false;
    creates=removes=pickups=updates=closedActions=0;authority=std::make_shared<Registry>();
    for(int i=1;i<=2;++i){players[i]=Make<CBasePlayer>();players[i]->index=i;players[i]->userid=100+i;}
    auto* p=players[1];initial=existing==29?Make<CBasePlayerWeapon>():Make<CCSOLMelee>();initial->m_iId=existing;
    initial->pev->classname=existing==29?"weapon_knife":csol::melee::Resource(csol::melee::WeaponVariant(existing)).classname;
    initial->m_pPlayer=p;initial->pev->owner=p->edict();p->m_rgpPlayerItems[KNIFE_SLOT]=initial;
    p->m_pActiveItem=p->m_pClientActiveItem=initial;p->pev->weapons=CSOL_LegacyWeaponMask(existing);
}
bool Deliver(int id=1022){return CSOL_DeliverMeleeWeapon(*players[1],id);}
void Preserved(){
    auto* p=players[1];Check(p->m_rgpPlayerItems[KNIFE_SLOT]==initial&&!initial->m_pNext,"original sole knife retained");
    Check(!(initial->pev->flags&FL_KILLME)&&initial->m_pPlayer==p&&initial->pev->owner==p->edict(),"original still owned and live");
    Check(initial->m_flNextPrimaryAttack==7&&initial->m_flNextSecondaryAttack==8&&p->m_flNextAttack==.12f,"public cooldown preserved");
    Check(p->m_pActiveItem==initial,"original active item restored");
    Check(p->pev->weapons==CSOL_LegacyWeaponMask(initial->m_iId),"original legacy bit retained");
    Check(!spawnedWeapon||(spawnedWeapon->pev->flags&FL_KILLME),"failed fresh is retired");
    Check(p->m_pClientActiveItem!=spawnedWeapon,"no dangling client active reference");
}
int main(){
    for(int id:{1022,1023}){
        Reset();Check(Deliver(id),"normal distinct knife delivery");
        Check(players[1]->m_rgpPlayerItems[KNIFE_SLOT]==spawnedWeapon&&!spawnedWeapon->m_pNext,"fresh is sole knife");
        Check(initial->pev->flags&FL_KILLME,"old knife retired at commit");
        Check(players[1]->m_pLastItem!=initial,"removed old knife not left as last item");
        Check(players[1]->m_pClientActiveItem!=initial,"removed old knife not left as client active item");
        Reset(id);Check(!Deliver(id),"same-kind delivery denied");Check(creates==0&&initial->pending,"same-kind no Spawn/Holster/pending reset");Preserved();
        Reset(id==1022?1023:1022);Check(Deliver(id),"different extended knife replaces existing one");
    }
    Reset();addReject=true;Check(!Deliver(),"Add pre-hook refusal");Preserved();
    Reset();addPostFalse=true;Check(!Deliver(),"Add post-hook FALSE");Preserved();
    Reset();addPostFalse=freshRemoveReject=true;Check(!Deliver(),"rollback removal veto");Preserved();
    Reset();callback=[](const char* stage,CBaseEntity* entity){if(!std::strcmp(stage,"afterAdd"))entity->pev->flags|=FL_KILLME;};
    Check(!Deliver(),"post-Add pending deletion rejects delivery");Preserved();
    Reset();oldRemoveReject=true;Check(!Deliver(),"old unlink veto");Preserved();
    Reset();oldRemovePostFalse=true;Check(!Deliver(),"old unlink post-hook FALSE");Preserved();
    Reset();oldRemovePostFalse=freshRemoveReject=true;Check(!Deliver(),"old post-FALSE plus fresh unlink veto");Preserved();
    Reset();spawnReject=true;Check(!Deliver(),"Spawn refuses");Preserved();
    Reset();allowPickup=false;Check(!Deliver(),"rules refuse pickup");Preserved();
    Reset();scenarioMode=true;identityAllowed=false;Check(!Deliver(),"scenario identity refuses");Preserved();
    Reset();scenarioMode=true;Check(Deliver(),"scenario identity accepts");
    for(bool automatic:{false,true}){
        Reset();autoSwitch=automatic;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"spawn"))spawnedWeapon->deployResult=false;};
        Check(!Deliver(),"actual Deploy FALSE despite selected active pointer rejects delivery");Preserved();
    }
    Reset();autoSwitch=false;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"spawn"))spawnedWeapon->deployable=false;};
    Check(!Deliver(),"actual SelectItem CanDeploy rejection");Preserved();
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"defaultDeploy"))players[1]->alive=false;};
    Check(!Deliver(),"death in actual melee DefaultDeploy rejects completion");Check(spawnedWeapon->pev->flags&FL_KILLME,"failed deploy after death retires fresh");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"inventory"))players[1]->alive=false;};
    Check(!Deliver(),"death during final inventory notification rejects");Check(!Contains(*players[1],spawnedWeapon)&&(spawnedWeapon->pev->flags&FL_KILLME),"final rejection compensates new inventory");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"create"))players[1]->alive=false;};
    Check(!Deliver(),"death inside CREATE refuses");Check(spawnedWeapon->pev->flags&FL_KILLME,"CREATE survivor retired after death");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterAdd"))players[1]->alive=false;};
    Check(!Deliver(),"death after Add refuses");Check(!Contains(*players[1],spawnedWeapon)&&(spawnedWeapon->pev->flags&FL_KILLME),"dead player's partial insertion removed");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterRemoveOld"))players[1]->alive=false;};
    Check(!Deliver(),"death after old unlink refuses");Check(initial->pev->flags&FL_KILLME,"detached dead owner's old knife retired");
    Check(spawnedWeapon->pev->flags&FL_KILLME,"dead owner's fresh retired");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"create"))validWorld=false;};
    Check(!Deliver(),"closed scope after CREATE refuses");Check(closedActions==0,"closed map leaves teardown to engine without entity access");validWorld=true;
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"spawn"))++generation;};
    Check(!Deliver(),"map generation changes during Spawn");Check(closedActions==0,"new map not traversed during rejection");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"spawn"))Check(!Deliver(1023),"direct nested helper denied");};
    Check(Deliver(),"outer delivery survives denied reentry");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"spawn")){
        WeaponService service;Check(!service.Buy(1,1023),"public nested melee purchase denied");
        Check(!service.Buy(1,144),"cross knife/cannon reentry denied before nonmelee grant");
    }};
    {WeaponService service;Check(service.Buy(1,1022)==1,"actual service forwards melee delivery");}
    Reset();{WeaponService service;Check(!service.CanBuy(1,9999),"actual service rejects unknown ID");}
    Reset(1022);{WeaponService service;Check(!service.CanBuy(1,1022)&&!service.Buy(1,1022)&&creates==0,"actual service same-kind pre-gate");}
    Reset();addPostFalse=true;{WeaponService service;Check(service.Buy(1,1022)==0,"actual service propagates false, no debit authorization");}Preserved();
    Reset();oldRemovePostFalse=true;callback=[](const char* stage,CBaseEntity* entity){if(!std::strcmp(stage,"afterRemoveOld")){
        auto* w=static_cast<CBasePlayerWeapon*>(entity);w->m_pNext=nullptr;w->m_pPlayer=players[2];w->pev->owner=players[2]->edict();players[2]->m_rgpPlayerItems[KNIFE_SLOT]=w;
    }};
    Check(!Deliver(),"transferred original refuses");Check(players[2]->m_rgpPlayerItems[KNIFE_SLOT]==initial&&!(initial->pev->flags&FL_KILLME),"foreign original never reclaimed or deleted");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"targets")){
        players[1]->RemovePlayerItem_OrigFunc(spawnedWeapon);spawnedWeapon->m_pNext=nullptr;spawnedWeapon->m_pPlayer=players[2];spawnedWeapon->pev->owner=players[2]->edict();players[2]->m_rgpPlayerItems[KNIFE_SLOT]=spawnedWeapon;
    }};
    Check(!Deliver(),"transferred fresh refuses");Check(players[2]->m_rgpPlayerItems[KNIFE_SLOT]==spawnedWeapon&&!(spawnedWeapon->pev->flags&FL_KILLME),"foreign fresh never deleted");
    Reset();callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"targets"))players[1]->m_pClientActiveItem=spawnedWeapon;};oldRemoveReject=true;
    Check(!Deliver(),"fresh client cache reference on rollback");Preserved();
    Reset();retireReject=true;Check(!Deliver(),"old retirement refusal rolls back");
    Check(players[1]->m_rgpPlayerItems[KNIFE_SLOT]==initial&&!Contains(*players[1],spawnedWeapon),"retirement veto leaves original inventory coherent");
    // Hooks which refuse UTIL_Remove can leave an unlinked entity for host
    // cleanup; this is explicitly outside the ordinary inventory guarantee.
    std::printf("%s melee purchase actual methods: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
