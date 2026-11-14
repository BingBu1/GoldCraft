// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/revolver_weapon.hpp"
#include "native/guest/cof/catalog.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t globals{};
globalvars_t* gpGlobals=&globals;
CBotManager* TheBots{};
bool g_bAllowedCSBot{};
CGameRules* g_pGameRules{};
const Vector g_vecZero{};

namespace
{
using namespace guest::cof;
using namespace guest::cof::server;
std::vector<std::uint32_t> events;
std::vector<std::byte> model;
RevolverOwnerFrame frame{};
bool current=true;
int cancel=-1, checks{}, meleeCalls{};
bool failEffect{}, observeReload{};
bool traceDeploy{};
std::vector<unsigned> deployOrder;
std::vector<std::pair<int,int>> deployRandom;
bool fidgetMode{};
unsigned fidgetChance{},fidgetChoice{};
std::vector<unsigned> fidgetOrder;
int shotLatch{};
unsigned csBallisticsCalls{};
float shotLatchDeadline{},reloadDeadline{};
RevolverWeapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"Revolver SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,RevolverOwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const RevolverEvent& e) noexcept
{
    const unsigned k=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(k);
    switch(e.kind)
    {
    case RevolverEffect::ViewAnimation:Event(k,e.value);break;
    case RevolverEffect::EjectEvent:Event(k,Bits(e.a),e.value,Bits(e.first.x),Bits(e.first.y),Bits(e.first.z));break;
    case RevolverEffect::FireEvent:Event(k,Bits(e.first.x),Bits(e.first.y),Bits(e.first.z),e.value);Check(e.other==0&&e.second==g_vecZero);break;
    case RevolverEffect::Recoil:Event(k,Bits(e.first.x),Bits(e.first.y),Bits(e.first.z));break;
    case RevolverEffect::Shake:Event(k,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case RevolverEffect::ShotState:Event(k,Bits(e.a));break;
    case RevolverEffect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case RevolverEffect::InventoryChanged:Event(k);break;
    case RevolverEffect::ReserveHud:Event(k,e.value);break;
    case RevolverEffect::Gunsmoke:Check(e.value==19&&e.enabled&&unsigned(e.modelString)==unsigned(item.m_pPlayer->pev->viewmodel));Event(k);break;
    case RevolverEffect::FirstDraw:frame.firstDrawn=true;break;
    case RevolverEffect::ReloadStage:Event(k,e.value);if(e.value<2)reloadDeadline=e.a;break;
    case RevolverEffect::AmmoX:Event(k,e.value,e.other);break;
    case RevolverEffect::Cylinder:frame.cylinderAngle=e.a;if(e.enabled)frame.cylinderGate=e.b;break;
    case RevolverEffect::Aim:Event(k,Bits(e.first.x),Bits(e.first.y),Bits(e.first.z),e.value,e.enabled);break;
    case RevolverEffect::EmptyClick:Event(k);break;
    case RevolverEffect::FireSound:Check(e.a==0&&e.b==1&&e.enabled);Event(k);break;
    default:break;
    }
    if(cancel==int(k))current=false;
    return !failEffect;
}

struct Player final:CBasePlayer
{
    Vector GetGunPosition() override{return {2,3,4};}
    int GiveAmmo(int amount,const char* name,int maximum) override
    {
        Check(amount>=0&&!std::strcmp(name,"cof_revolver")&&(maximum==15||maximum==50));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
RevolverWeapon weapon;
PlayerAnimationAdapter animations;
RevolverPrepared prepared;
RevolverBackends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,RevolverWeapon&,RevolverPrepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,RevolverBackends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,RevolverWeapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,RevolverWeapon&,RevolverPrepared&) noexcept{return false;}
bool CancelColdPrepare(void*,RevolverWeapon&,RevolverPrepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=reloadDeadline=0;frame={};frame.bulletDamage=37;frame.meleeDamage=11;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=15;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/revolver/v_revolver.mdl\0models/guest/cof/revolver/p_revolver.mdl\0models/guest/cof/revolver/w_revolver.mdl\0inventoryitems/weapons/weapon_revolver.txt\0weapon_cof_revolver\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="cof_revolver";
    player.pev->health=100;player.pev->modelindex=1;player.m_Activity=ACT_WALK;player.m_IdealActivity=ACT_WALK;
    player.m_fSequenceFinished=true;player.m_flFlinchTime=0;
    player.m_pActiveItem=&weapon;
    g_engfuncs={};g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return model.data();};
    g_engfuncs.pfnSetModel=[](edict_t*,const char*){};
    g_engfuncs.pfnIndexOfEdict=[](const edict_t*){return 19;};
    g_engfuncs.pfnRandomLong=[](int low,int high)
    {if(traceDeploy)deployRandom.emplace_back(low,high);return 2;};
    g_engfuncs.pfnMakeVectors=[](const float* angles)
    {
        Event(30,Bits(angles[0]),Bits(angles[1]),Bits(angles[2]));
        globals.v_forward={1,0,0};globals.v_right={0,1,0};globals.v_up={0,0,1};
    };
    std::string error;
    const auto plan=RecommendedPistolAnimationPlan(Pistol::Revolver,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},Pistol::Revolver,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<34)-1;
    for(unsigned i=0;i<prepared.sequences.duration.size();++i)prepared.sequences.duration[i]=.25f+float(i)*.03125f;
    prepared.ballistics={77,31};prepared.melee={78,true,Recoil,nullptr,OriginalPistolMeleeSamples};
    prepared.aimOffset={1,2,3};prepared.aimZoom=55;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=1;prepared.hostPosition=2;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.cylinderReady=prepared.ejectEventReady=prepared.reloadStagesReady=prepared.ammoXReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/revolver/v_revolver.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    backends={nullptr,Current,OwnerFrame,Effect,RevolverRequiredEffects,&animations,{1,1}};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=15;weapon.m_iClip=5;
    Check(weapon.Bind(player,prepared,backends)==RevolverStatus::Ready);
    Check(weapon.Deploy());events.clear();live=&weapon;
}
}

// Controlled calls into separately instruction-verified modules. They validate
// every integration argument; they are test boundaries, never production stubs.
namespace guest::cof
{
BallisticsStatus CheckPistolBallistics(const PistolBallistics& shot,const BallisticsEffects& fx,const BallisticsCall& call) noexcept
{
    Check(shot.playerHits==PistolPlayerHits::OriginalCooperativeSkipPlayers);
    if(shot.sourceMode!=PistolSource::NormalMuzzle)return BallisticsStatus::UnsupportedSource;
    return fx.customDecalMessage>0&&fx.bubbleModel>0&&call.current&&call.current(call.context)?BallisticsStatus::Ready:BallisticsStatus::MissingEffects;
}
BallisticsResult FirePistolBallistics(CBaseEntity& owner,const PistolBallistics& shot,const BallisticsEffects& fx,const BallisticsCall& call) noexcept
{
    Check(&owner==&player&&CheckPistolBallistics(shot,fx,call)==BallisticsStatus::Ready);
    Event(31,Bits(shot.source.x),Bits(shot.source.y),Bits(shot.source.z),Bits(shot.direction.x),Bits(shot.direction.y),Bits(shot.direction.z),
          Bits(shot.spread.x),Bits(shot.spread.y),Bits(shot.spread.z),Bits(8192),shot.damage,shot.seed);
    return {BallisticsStatus::Fired,{}};
}
BallisticsResult FireCSPistolBallistics(CBaseEntity& owner,const PistolBallistics& shot,
    const BallisticsEffects& fx,const BallisticsCall& call,int penetration) noexcept
{
    Check(penetration==5&&shot.damage==100);
    ++csBallisticsCalls;
    return FirePistolBallistics(owner,shot,fx,call);
}
MeleeStatus CheckPistolWeaponMelee(CBasePlayer& owner,const PistolMeleeRequest& r,const PistolMeleeEffects& fx,const MeleeCall& call) noexcept
{
    Check(&owner==&player&&r.world==MeleeWorld::Multiplayer&&r.damage==frame.meleeDamage);
    Check(ValidPistolMeleeSamples(fx.samples));
    return fx.recoil&&fx.soundsReady&&call.current&&call.current(call.context)?MeleeStatus::Ready:MeleeStatus::MissingEffects;
}
MeleeStatus PistolWeaponMelee(CBasePlayer& owner,const PistolMeleeRequest& r,const PistolMeleeEffects& fx,const MeleeCall& call) noexcept
{Check(CheckPistolWeaponMelee(owner,r,fx,call)==MeleeStatus::Ready);++meleeCalls;return MeleeStatus::Miss;}
}

// Real SDK sequence implementations, extracted at build time and hashed.
#include "sdk-sequence-info.inc"
#include "sdk-sequence-flags.inc"
int CBaseAnimating::GetSequenceFlags(){return ::GetSequenceFlags(GET_MODEL_PTR(edict()),pev);}
void CBaseAnimating::ResetSequenceInfo(){
#include "sdk-reset-sequence.inc"
}
#include "sdk-abort-virtuals.inc"
#include "sdk-world-lifecycle.inc"
// Explicit abort boundaries: this fixture does not claim native target firing,
// engine removal, inventory insertion or world pickup integration acceptance.
void CBaseDelay::SUB_UseTargets(CBaseEntity*,USE_TYPE,float){std::abort();}
void CBaseEntity::SUB_Remove(){std::abort();}
void CCSEntity::FireBullets(int,Vector&,Vector&,Vector&,float,int,int,int,entvars_t*){std::abort();}
void CCSEntity::FireBuckshots(ULONG,Vector&,Vector&,Vector&,float,int,int,entvars_t*){std::abort();}
Vector CCSEntity::FireBullets3(Vector&,Vector&,float,float,int,int,int,float,entvars_t*,bool,int){std::abort();}
AmmoInfo CBasePlayerItem::m_AmmoInfoArray[MAX_AMMO_SLOTS]{};
int gmsgWeapPickup{};
// Controlled protocol boundary: validate the extension identity and record
// every actual SDK pickup notification. No packet transport is claimed.
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==898);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==898);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==898);return 0;}

void InventoryRestoreCases()
{
    const auto prepareOwned=[] {
        Initialize();weapon.m_pEntity=&itemWrapper;itemWrapper.m_pContainingEntity=&weapon;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});weapon.Precache();
        Check(weapon.Bind(player,prepared,backends)==RevolverStatus::Ready);
        weapon.AttachToPlayer(&player);Check(weapon.pev->owner==player.edict());
        Check(weapon.Deploy());globals.time=5;
    };
    prepareOwned();
    const auto profile=weapon.ScenarioGuestProfile();
    Check(goldcraft::scenario::ValidGuestProfile(profile,898));
    Check(!goldcraft::scenario::ValidGuestProfile(profile,897)&&!goldcraft::scenario::ValidGuestProfile(profile,21));
    frame.pressed=1;weapon.PrimaryAttack();Check(weapon.m_iClip==4);
    const auto deadline=weapon.m_flNextPrimaryAttack;
    goldcraft::inventory::Snapshot snapshot;
    Check(weapon.SaveInventorySnapshot(snapshot));weapon.BeforeWeaponDrop();
    Check(weapon.Status()==RevolverStatus::Cancelled&&weapon.RestoreInventoryBinding(player));
    Check(weapon.RestoreInventorySnapshot(snapshot)&&weapon.m_iClip==4&&weapon.m_flNextPrimaryAttack==deadline);
    globals.time=5.001F;events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==4);
    globals.time=deadline+.001F;weapon.PrimaryAttack();Check(weapon.m_iClip==3);
    auto invalid=snapshot;invalid.kind=0;Check(!weapon.RestoreInventorySnapshot(invalid));
    invalid=snapshot;--invalid.bytes;Check(!weapon.RestoreInventorySnapshot(invalid));
    ++player.m_iNumSpawns;Check(!weapon.RestoreInventorySnapshot(snapshot));--player.m_iNumSpawns;
    ++ownerEdict.serialnumber;Check(!weapon.RestoreInventorySnapshot(snapshot));--ownerEdict.serialnumber;
    ++weaponEdict.serialnumber;Check(!weapon.RestoreInventorySnapshot(snapshot));--weaponEdict.serialnumber;
    ++player.pev->modelindex;Check(!weapon.RestoreInventorySnapshot(snapshot));--player.pev->modelindex;

    prepareOwned();weapon.SecondaryAttack();const auto pending=weapon.pev->nextthink;
    Check(pending>globals.time&&weapon.SaveInventorySnapshot(snapshot));weapon.BeforeWeaponDrop();
    Check(weapon.pev->nextthink==0&&weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.pev->nextthink==pending&&weapon.m_pfnThink!=nullptr);
    const auto before=meleeCalls;globals.time=pending+.001F;weapon.Think();
    Check(meleeCalls==before+1&&weapon.m_pfnThink==nullptr);

    prepareOwned();weapon.m_iClip=2;weapon.pev->body=2;weapon.Reload();
    Check(weapon.CylinderState().loading&&weapon.pev->nextthink>globals.time);
    const auto resetTime=weapon.pev->nextthink;Check(weapon.SaveInventorySnapshot(snapshot));
    weapon.BeforeWeaponDrop();Check(weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.pev->nextthink==resetTime&&weapon.CylinderState().loading&&weapon.m_pfnThink);
    globals.time=resetTime;weapon.DispatchThink();const auto insertTime=weapon.pev->nextthink;
    globals.time=insertTime;weapon.DispatchThink();
    Check(weapon.CylinderState().pending&&weapon.CylinderState().commitDeadline>globals.time);
    const auto commit=weapon.CylinderState().commitDeadline;const auto next=weapon.pev->nextthink;
    Check(weapon.SaveInventorySnapshot(snapshot));weapon.BeforeWeaponDrop();
    Check(weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.CylinderState().pending&&weapon.CylinderState().commitDeadline==commit&&weapon.pev->nextthink==next);
    globals.time=commit;weapon.ItemPostFrame();Check(weapon.m_iClip==0&&weapon.CylinderState().pending);
    globals.time=commit+.001F;weapon.ItemPostFrame();Check(weapon.m_iClip==1&&!weapon.CylinderState().pending&&player.m_rgAmmo[1]==14);

    prepareOwned();frame.toggleAim=true;frame.pressed=0x40;player.pev->button=0x40;
    weapon.ItemPostFrame();Check(weapon.ActionState().aiming&&player.pev->fov==55);
    const auto aimDeadline=weapon.m_flNextPrimaryAttack;const auto animation=player.pev->weaponanim;
    Check(weapon.SaveInventorySnapshot(snapshot));weapon.BeforeWeaponDrop();
    deployOrder.clear();traceDeploy=true;
    Check(weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));traceDeploy=false;
    Check(weapon.ActionState().aiming&&player.pev->fov==55&&weapon.m_flNextPrimaryAttack==aimDeadline&&player.pev->weaponanim==animation);
    Check(deployOrder==std::vector<unsigned>({unsigned(RevolverEffect::Aim),unsigned(RevolverEffect::Body),unsigned(RevolverEffect::Aim),unsigned(RevolverEffect::Body)}));
    current=false;Check(!weapon.RestoreInventoryBinding(player)&&!weapon.RestoreInventorySnapshot(snapshot));
}


void DeploySuppressionCases()
{
    for(unsigned armor:{0xc0000000u,0u,0x3f000000u,0x3f800000u,0x40000000u,0x7fc00001u})for(int clip:{0,5})
    {
        Initialize();weapon.pev->armorvalue=Float(armor);weapon.m_iClip=clip;
        deployOrder.clear();deployRandom.clear();traceDeploy=true;
        Check(weapon.Deploy());traceDeploy=false;
        const bool suppressed=armor==0x3f800000u;
        const bool heard=std::find(deployOrder.begin(),deployOrder.end(),unsigned(RevolverEffect::Sleeve))!=deployOrder.end();
        Check(heard!=suppressed&&Bits(weapon.pev->armorvalue)==(suppressed?0u:armor));
        const std::vector<std::pair<int,int>> wanted=suppressed?
            std::vector<std::pair<int,int>>{{-5,5}}:std::vector<std::pair<int,int>>{{-5,5},{0,2}};
        Check(deployRandom==wanted&&weapon.ActionState().clip==clip);
    }
}

void ModelRebindCases()
{
    const auto prepareOwned=[]
    {
        Initialize();weapon.m_pEntity=&itemWrapper;itemWrapper.m_pContainingEntity=&weapon;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});weapon.Precache();
        backends.ownerLife=17;Check(weapon.Bind(player,prepared,backends)==RevolverStatus::Ready);
        weapon.AttachToPlayer(&player);Check(weapon.Deploy());globals.time=10;
    };
    const auto replaceModel=[]
    {
        ++player.pev->modelindex;++backends.playerModelGeneration.modelGeneration;
        const auto plan=RecommendedPistolAnimationPlan(Pistol::Revolver,PistolAnimationRig::Stock);std::string error;
        Check(plan&&animations.Prepare(player,model,backends.playerModelGeneration,Pistol::Revolver,*plan,_stricmp,error));
    };
    // Existing generic retirement demonstrably loses the original deferred slot.
    prepareOwned();weapon.m_iClip=3;weapon.Reload();Check(weapon.pev->nextthink>globals.time);
    weapon.Invalidate();replaceModel();Check(weapon.RestoreInventoryBinding(player)&&weapon.pev->nextthink==0);
    for(int stage=0;stage<4;++stage)
    {
        prepareOwned();
        if(stage==3)weapon.SecondaryAttack();
        else
        {
            weapon.m_iClip=3;weapon.Reload();
            for(int i=0;i<stage;++i){globals.time=weapon.pev->nextthink;weapon.DispatchThink();}
        }
        const auto next=weapon.pev->nextthink;Check(next>globals.time);
        const auto primary=weapon.m_flNextPrimaryAttack,secondary=weapon.m_flNextSecondaryAttack;
        const auto idle=weapon.m_flTimeWeaponIdle,ownerAttack=player.m_flNextAttack;
        const int reserve=player.m_rgAmmo[1],clip=weapon.m_iClip,beforeMelee=meleeCalls;
        const auto cylinder=weapon.CylinderState();
        weapon.OwnerModelChanging();Check(weapon.m_pfnThink==nullptr&&weapon.pev->nextthink==0);
        replaceModel();Check(weapon.RestoreInventoryBinding(player)&&weapon.pev->nextthink==next&&weapon.m_pfnThink!=nullptr);
        Check(Bits(weapon.m_flNextPrimaryAttack)==Bits(primary)&&Bits(weapon.m_flNextSecondaryAttack)==Bits(secondary)&&
            Bits(weapon.m_flTimeWeaponIdle)==Bits(idle)&&Bits(player.m_flNextAttack)==Bits(ownerAttack));
        Check(weapon.m_iClip==clip&&player.m_rgAmmo[1]==reserve&&weapon.CylinderState().pending==cylinder.pending&&
            Bits(weapon.CylinderState().commitDeadline)==Bits(cylinder.commitDeadline));
        Check(weapon.RestoreInventoryBinding(player)&&weapon.pev->nextthink==next);
        globals.time=std::nextafter(next,-INFINITY);weapon.DispatchThink();Check(meleeCalls==beforeMelee);
        globals.time=next;weapon.DispatchThink();
        if(stage==3){Check(meleeCalls==beforeMelee+1&&weapon.m_pfnThink==nullptr);weapon.DispatchThink();Check(meleeCalls==beforeMelee+1);}
        else Check(weapon.CylinderState().loading&&weapon.pev->nextthink>next);
    }
    // Authority changes inside the model boundary are retained, never rolled back.
    prepareOwned();weapon.SecondaryAttack();const float next=weapon.pev->nextthink;
    weapon.OwnerModelChanging();weapon.m_iClip=75;player.m_rgAmmo[1]=500;replaceModel();
    Check(weapon.RestoreInventoryBinding(player)&&weapon.m_iClip==75&&player.m_rgAmmo[1]==500&&weapon.pev->nextthink==next);
    for(int mutation=0;mutation<7;++mutation)
    {
        prepareOwned();weapon.SecondaryAttack();weapon.OwnerModelChanging();replaceModel();
        switch(mutation){case 0:++backends.ownerLife;break;case 1:++player.m_iNumSpawns;break;
        case 2:++ownerEdict.serialnumber;break;case 3:++weaponEdict.serialnumber;break;
        case 4:player.m_pActiveItem=nullptr;break;case 5:player.pev->deadflag=DEAD_DEAD;break;case 6:current=false;break;}
        const int before=meleeCalls;Check(!weapon.RestoreInventoryBinding(player));
        globals.time=30;weapon.DispatchThink();Check(meleeCalls==before&&weapon.m_pfnThink==nullptr);
    }
    prepareOwned();weapon.SecondaryAttack();weapon.OwnerModelChanging();replaceModel();failEffect=true;
    Check(!weapon.RestoreInventoryBinding(player)&&weapon.m_pfnThink==nullptr);
    prepareOwned();weapon.SecondaryAttack();weapon.OwnerModelChanging();weapon.Invalidate();replaceModel();
    Check(weapon.RestoreInventoryBinding(player)&&weapon.pev->nextthink==0&&weapon.m_pfnThink==nullptr);
}
void ExpandedAmmoCases()
{
    for(int clip:{6,25,75,511,65535,std::numeric_limits<int>::max()})
    {
        Initialize();globals.time=10;weapon.m_iClip=clip;player.m_rgAmmo[1]=500;
        frame.firstDrawn=false;events.clear();
        Check(weapon.Deploy()&&weapon.m_iClip==clip&&player.m_rgAmmo[1]==500&&weapon.pev->body==5);
        globals.time=20;frame.pressed=1;weapon.PrimaryAttack();
        const int remaining=clip-1;
        Check(weapon.Status()==RevolverStatus::Applied&&weapon.m_iClip==remaining&&player.m_rgAmmo[1]==500);
        const int shotPhysical=remaining%5;
        Check(weapon.CylinderState().targetAngle==shotPhysical*72);
        for(float angle:{0.0F,72.0F,288.0F,360.0F})
        {
            frame.cylinderAngle=angle;frame.cylinderGate=0;
            for(int step=0;step<32;++step)
            {
                globals.time+=.1F;weapon.AdvanceOwnerFrame();
                Check(weapon.Status()==RevolverStatus::Ready&&frame.cylinderAngle>=0&&frame.cylinderAngle<=360);
            }
            Check(frame.cylinderAngle==float(shotPhysical*72));
        }
        weapon.SetCylinder();
        const int physical=remaining>5?remaining%5:remaining;
        Check(weapon.Status()==RevolverStatus::Applied&&frame.cylinderAngle==float(physical*72));
        Check(frame.cylinderAngle>=0&&frame.cylinderAngle<=360);
        if(remaining>5)
        {
            globals.time=30;events.clear();weapon.Reload();
            Check(weapon.Status()==RevolverStatus::Ineligible&&weapon.m_iClip==remaining&&
                player.m_rgAmmo[1]==500&&events.empty()&&!weapon.CylinderState().loading);
        }
        globals.time=40;weapon.Holster();globals.time=41;
        Check(weapon.Deploy()&&weapon.m_iClip==remaining&&player.m_rgAmmo[1]==500);
    }
    // A delayed original insertion must not consume reserve after an authority
    // skill has already refilled/overfilled the actual clip.
    Initialize();globals.time=10;weapon.m_iClip=3;weapon.Reload();
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();
    Check(weapon.CylinderState().pending);
    weapon.m_iClip=75;player.m_rgAmmo[1]=500;
    globals.time=std::nextafter(weapon.CylinderState().commitDeadline,INFINITY);
    frame.pressed=0;weapon.ItemPostFrame();
    Check(weapon.m_iClip==75&&player.m_rgAmmo[1]==500&&!weapon.CylinderState().pending);
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();
    Check(!weapon.CylinderState().loading&&weapon.m_iClip==75&&player.m_rgAmmo[1]==500);
    globals.time=30;frame.pressed=1;weapon.PrimaryAttack();
    Check(weapon.Status()==RevolverStatus::Applied&&weapon.m_iClip==74);
    // Native corruption checks stay distinct from legitimate high counts.
    for(int mutation=0;mutation<2;++mutation)
    {
        Initialize();globals.time=10;frame.pressed=1;
        if(mutation)player.m_rgAmmo[1]=-1;else weapon.m_iClip=-1;
        events.clear();weapon.PrimaryAttack();
        Check(weapon.Status()==RevolverStatus::InvalidInput&&events.empty());
    }
}
int main(int argc,char** argv)
{
    if(argc!=6)return 1;
    std::ifstream file(argv[3],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
    unsigned rows{};
    while(cases>>std::ws&&cases.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,35> r{};for(auto& v:r)if(!(cases>>std::hex>>v))return 3;
        if(r[0]>10)return 3;
        Initialize();events.clear();
        globals.time=Float(r[1]);weapon.m_iClip=r[2];player.m_rgAmmo[1]=r[3];
        auto& s=const_cast<MagazineActionState&>(weapon.ActionState());
        s.primary=Float(r[4]);s.secondary=Float(r[5]);s.extra=Float(r[6]);s.idle=Float(r[7]);s.ownerAttack=Float(r[8]);s.action=r[9];s.aiming=r[10];s.pendingReload=r[11];
        auto& c=const_cast<RevolverCylinder&>(weapon.CylinderState());
        c.loading=r[12];c.cancel=r[13];c.pending=r[14];c.commitDeadline=Float(r[15]);c.targetAngle=r[16];
        weapon.pev->body=r[17];player.pev->weaponanim=r[18];frame.infiniteAmmo=r[19];frame.pressed=r[20];frame.released=r[21];player.pev->button=r[22];player.pev->waterlevel=r[23];weapon.m_fFireOnEmpty=r[24];frame.originalFlags=r[25];frame.condition3AA=r[26];frame.pendingGate=r[27];frame.pendingGateTime=Float(r[28]);frame.toggleAim=r[29];
        auto& spread=const_cast<SpreadState&>(weapon.Spread());spread.scalar=Float(r[30]);spread.recoveryDeadline=Float(r[31]);spread.vector={.01f,.01f,.01f};frame.firstDrawn=r[32];frame.cylinderAngle=Float(r[33]);frame.cylinderGate=Float(r[34]);
        globals.v_forward={1,2,3};globals.v_up={4,5,6};globals.v_right={7,8,9};
        for(auto* v:{weapon.pev,player.pev}){v->origin={11,12,13};v->view_ofs={1,2,3};v->velocity={4,5,6};v->v_angle={1,2,3};v->punchangle={.1f,.2f,.3f};}
        weapon.pev->startpos.x=9;
        switch(r[0])
        {
        case 0:weapon.PrimaryAttack();break;case 1:weapon.Reload();break;
        case 2:weapon.ReloadInsert();break;case 3:weapon.ResetCylinder();break;
        case 4:weapon.SetCylinder();break;case 5:weapon.ItemPostFrame();break;
        case 6:weapon.WeaponIdle();break;case 7:weapon.Holster();break;
        case 8:weapon.Deploy();break;case 9:weapon.AdvanceOwnerFrame();break;
        case 10:weapon.SecondaryAttack();break;
        }
        std::vector<std::uint32_t> actual{unsigned(weapon.m_iClip),unsigned(player.m_rgAmmo[1]),Bits(s.primary),Bits(s.secondary),Bits(s.extra),Bits(s.idle),Bits(s.ownerAttack),unsigned(s.action),s.aiming,s.pendingReload,c.loading,c.cancel,c.pending,Bits(c.commitDeadline),unsigned(c.targetAngle),unsigned(weapon.pev->body),unsigned(player.pev->weaponanim),unsigned(shotLatch),Bits(shotLatchDeadline),Bits(spread.scalar),Bits(spread.recoveryDeadline),0,Bits(weapon.pev->nextthink),Bits(frame.cylinderAngle),Bits(frame.cylinderGate),Bits(reloadDeadline),Bits(weapon.pev->startpos.x)};
        // Mode9 isolates the original owner-cylinder fragment. AdvanceOwnerFrame
        // also composes the separately instruction-tested spread updater.
        if(r[0]==9){actual[19]=r[30];actual[20]=r[31];}
        actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(expected,line))return 4;std::istringstream values(line);std::vector<unsigned> wanted;unsigned value;
        while(values>>std::hex>>value)wanted.push_back(value);
        if(!values.eof()||wanted.size()<27)return 4;
        if(actual!=wanted)
        {std::cerr<<"Original Revolver mismatch row "<<rows<<" mode "<<r[0]<<" actual/wanted "<<actual.size()<<'/'<<wanted.size()<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 5;}
        ++rows;
    }
    if(!rows)return 6;if(expected>>std::ws&&expected.peek()!=std::char_traits<char>::eof())return 7;
    std::ifstream worldCases(argv[4]),worldExpected(argv[5]);if(!worldCases||!worldExpected)return 1;
    unsigned worldRows{};
    while(worldCases>>std::ws&&worldCases.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,6> row{};for(auto& v:row)if(!(worldCases>>std::hex>>v))return 8;
        Initialize();events.clear();
        weapon.m_pEntity=&itemWrapper;itemWrapper.m_pContainingEntity=&weapon;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetModel=[](edict_t*,const char*){Event(101);};
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float* v){Event(102,Bits(v[0]),Bits(v[1]),Bits(v[2]));};
        g_engfuncs.pfnSetSize=[](edict_t*,const float* a,const float* b){Event(103,Bits(a[0]),Bits(a[1]),Bits(a[2]),Bits(b[0]),Bits(b[1]),Bits(b[2]));};
        weapon.pev->origin={11,12,13};weapon.pev->health=Float(row[1]);weapon.pev->iuser4=row[2];
        globals.time=Float(row[0]);weapon.Spawn();
        Check(weapon.ResourcesReady()&&weapon.Status()==RevolverStatus::Applied&&itemWrapper.m_ItemInfo.iId==898);
        weapon.pev->flags=row[3]?FL_ONGROUND:0;weapon.pev->effects=row[4]?EF_NODRAW:0;
        weapon.pev->angles.x=17;weapon.pev->angles.z=19;
        weapon.WorldFallThink();
        if(row[5]==1)weapon.Drop();else if(row[5]==2)weapon.Kill();
        const unsigned think=row[5]?2:row[3]?0:1;
        std::vector<std::uint32_t> actual{static_cast<unsigned>(weapon.m_iDefaultAmmo),static_cast<unsigned>(weapon.m_iClip),
            static_cast<unsigned>(weapon.pev->movetype),static_cast<unsigned>(weapon.pev->solid),static_cast<unsigned>(weapon.pev->renderfx),
            Bits(weapon.pev->angles.x),Bits(weapon.pev->angles.z),static_cast<unsigned>(weapon.pev->effects),Bits(weapon.pev->nextthink),
            weapon.m_pfnUse!=nullptr,weapon.m_pfnTouch!=nullptr,think,static_cast<unsigned>(weapon.ObjectCaps()&FCAP_IMPULSE_USE)};
        Check((weapon.m_pfnThink!=nullptr)==(think!=0));actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(worldExpected,line))return 9;std::istringstream values(line);std::vector<unsigned> wanted;unsigned value;
        while(values>>std::hex>>value)wanted.push_back(value);
        if(!values.eof())return 9;
        if(actual!=wanted){std::cerr<<"Original world mismatch row "<<worldRows<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 10;}
        ++worldRows;
    }
    if(worldRows!=180)return 11;

    // Host adapter lifecycle and typed backend admission; no transport acceptance.
    // Truly ownerless metadata is available before Prepare/Bind and remains
    // available after generation retirement; it cannot make Spawn usable.
    {
        RevolverWeapon ownerless;ItemInfo info{};
        Check(ownerless.GetItemInfo(&info)&&info.iId==898&&ownerless.m_iId==898&&
            info.iSlot==RevolverHudSlot&&info.iPosition==RevolverHudPosition&&info.pszAmmo2==nullptr&&
            !std::strcmp(info.pszName,"weapon_cof_revolver")&&!std::strcmp(info.pszAmmo1,"cof_revolver"));
        Check(!ownerless.ResourcesReady()&&!ownerless.GetItemInfo(nullptr));
    }
    // All eight samples are mandatory even with soundsReady=true. Reject each
    // missing/empty field and the entire absent table before any world/input
    // effects; never substitute the original resource names as a fallback.
    for(int mutation=0;mutation<17;++mutation)
    {
        Initialize();auto p=prepared;
        if(mutation==16)p.melee.samples={};
        else p.melee.samples[mutation%8]=mutation<8?nullptr:"";
        Check(p.melee.soundsReady&&!ValidPistolMeleeSamples(p.melee.samples));
        prepared=p;weapon.m_pEntity=&itemWrapper;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        weapon.Spawn();
        Check(!weapon.ResourcesReady()&&weapon.Status()==RevolverStatus::MissingBackend&&
            weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr);
        events.clear();Check(weapon.AddToPlayer(&player)==FALSE&&events.empty());
        Check(weapon.Bind(player,p,backends)==RevolverStatus::MissingBackend);
        events.clear();weapon.PrimaryAttack();weapon.SecondaryAttack();
        Check(events.empty()&&weapon.m_pfnThink==nullptr&&!weapon.ResourcesReady());
    }
    for(int mutation=0;mutation<10;++mutation)
    {
        Initialize();weapon.m_pEntity=&itemWrapper;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        switch(mutation){case 0:prepared.worldModelString=0;break;case 1:prepared.inventoryItemString=0;break;
        case 2:prepared.inventoryReady=false;break;case 3:prepared.pickupSoundReady=false;break;
        case 4:prepared.deployPreludeReady=false;break;case 5:prepared.hostSlot=0;break;
        case 6:prepared.hostPosition=0;break;case 7:prepared.sequences.present=0;break;
        case 8:prepared.modelsReady=false;break;case 9:prepared.particleReady=false;break;}
        weapon.Spawn();Check(!weapon.ResourcesReady()&&weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr);
        Check(weapon.AddToPlayer(&player)==FALSE);
    }
    for(auto prepare:{MissingColdPrepare,CancelColdPrepare})
    {
        Initialize();weapon.m_pEntity=&itemWrapper;
        weapon.ConfigureColdHost({nullptr,Current,prepare,ColdOwner,ColdInventory});weapon.Spawn();
        Check(!weapon.ResourcesReady()&&weapon.m_pfnThink==nullptr&&weapon.m_pfnUse==nullptr);
    }
    Initialize();weapon.BeforeWeaponDrop();events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.Status()!=RevolverStatus::Applied);
    // Actual SDK AddToPlayer/AddWeapon composition and real CCSPlayerItem
    // storage, with controlled GiveAmmo and reliable-message boundaries.
    for(int limit:{15,50})for(int health:{-2,0,13})
    {
        Initialize();weapon.m_pEntity=&itemWrapper;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        weapon.pev->health=float(health);weapon.Spawn();player.m_rgAmmo[1]=0;frame.reserveLimit=limit;
        events.clear();Check(weapon.AddToPlayer(&player));
        Check(weapon.m_pPlayer==&player&&weapon.m_iPrimaryAmmoType==1&&weapon.m_iSecondaryAmmoType==-1&&player.pev->weapons==0);
        if(health==-2)Check(weapon.m_iClip==-666&&weapon.m_iDefaultAmmo==5&&player.m_rgAmmo[1]==0);
        else Check(weapon.m_iClip==5&&weapon.m_iDefaultAmmo==0&&player.m_rgAmmo[1]==(health==13?8:0));
        weapon.AttachToPlayer(&player);
        Check(weapon.pev->movetype==MOVETYPE_FOLLOW&&weapon.pev->solid==SOLID_NOT&&weapon.pev->effects==EF_NODRAW&&
            weapon.pev->aiment==player.edict()&&weapon.pev->owner==player.edict()&&weapon.pev->modelindex==0&&int(weapon.pev->model)==0&&
            weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr&&!(weapon.ObjectCaps()&FCAP_IMPULSE_USE));
    }

    for(unsigned kind=0;kind<unsigned(RevolverEffect::Count);++kind)
    {
        Initialize();auto b=backends;b.preparedEffects&=~(1u<<kind);
        Check(weapon.Bind(player,prepared,b)==RevolverStatus::MissingBackend);
        events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==5);
    }
    for(int missing=0;missing<13;++missing)
    {
        Initialize();auto p=prepared;auto b=backends;
        switch(missing){case 0:p.modelsReady=false;break;case 1:p.soundsReady=false;break;case 2:p.particleReady=false;break;
        case 3:p.aimMetadataReady=false;break;case 4:p.melee.recoil=nullptr;break;case 5:b.playerAnimation=nullptr;break;
        case 6:b.ownerFrame=nullptr;break;case 7:b.effect=nullptr;break;case 8:p.world=MeleeWorld::UnsupportedSingleplayer;break;
        case 9:p.cylinderReady=false;break;case 10:p.ejectEventReady=false;break;case 11:p.reloadStagesReady=false;break;case 12:p.ammoXReady=false;break;}
        Check(weapon.Bind(player,p,b)==RevolverStatus::MissingBackend);events.clear();weapon.PrimaryAttack();Check(events.empty());
    }
    // Actual bounded dispatcher: reset, insert, strict pending commit, cancelled
    // reload, and both independent original think clocks surviving one frame.
    Initialize();globals.time=10;weapon.m_iClip=3;weapon.pev->body=3;weapon.Reload();
    Check(weapon.CylinderState().loading&&weapon.ActionState().action==2&&weapon.m_iClip==0&&!weapon.m_fInReload);
    Check(Bits(weapon.pev->nextthink)==Bits(10+.2f));
    globals.time=std::nextafter(weapon.pev->nextthink,-INFINITY);weapon.DispatchThink();Check(weapon.CylinderState().loading&&!weapon.CylinderState().pending);
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(Bits(weapon.pev->nextthink)==Bits(globals.time+1.3f));
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(weapon.CylinderState().pending&&weapon.pev->body==1&&player.pev->weaponanim==5);
    globals.time=weapon.CylinderState().commitDeadline;weapon.ItemPostFrame();Check(weapon.m_iClip==0&&player.m_rgAmmo[1]==15&&weapon.CylinderState().pending);
    globals.time=std::nextafter(globals.time,INFINITY);weapon.ItemPostFrame();Check(weapon.m_iClip==1&&player.m_rgAmmo[1]==14&&!weapon.CylinderState().pending);
    frame.pressed=1;weapon.ItemPostFrame();Check(weapon.CylinderState().cancel);
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(!weapon.CylinderState().loading&&!weapon.CylinderState().cancel&&player.pev->weaponanim==10);
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(frame.cylinderAngle==72&&weapon.CylinderState().targetAngle==72&&weapon.m_pfnThink==nullptr);
    Initialize();globals.time=10;weapon.m_iClip=3;auto& simultaneous=const_cast<RevolverCylinder&>(weapon.CylinderState());simultaneous.loading=true;
    weapon.ResetCylinder();weapon.SecondaryAttack();Check(Bits(weapon.pev->nextthink)==Bits(10+.3f));
    const int oldMelee=meleeCalls;globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(meleeCalls==oldMelee+1&&Bits(weapon.pev->nextthink)==Bits(11.3f));
    globals.time=weapon.pev->nextthink;weapon.DispatchThink();Check(weapon.CylinderState().pending);
    // Every stale owner/weapon/session/model lifetime blocks both clocks.
    for(int mutation=0;mutation<10;++mutation)
    {
        Initialize();globals.time=10;weapon.m_iClip=3;weapon.Reload();const int before=meleeCalls;
        switch(mutation){case 0:++ownerEdict.serialnumber;break;case 1:ownerEdict.pvPrivateData=nullptr;break;
        case 2:++weaponEdict.serialnumber;break;case 3:weaponEdict.pvPrivateData=nullptr;break;
        case 4:player.m_pActiveItem=nullptr;break;case 5:++player.pev->modelindex;break;case 6:player.pev->deadflag=DEAD_DEAD;break;
        case 7:player.pev->health=0;break;case 8:current=false;break;case 9:weapon.BeforeWeaponDrop();break;}
        events.clear();globals.time=20;weapon.DispatchThink();Check(events.empty()&&meleeCalls==before&&weapon.m_pfnThink==nullptr);
    }
    Initialize();globals.time=10;weapon.SecondaryAttack();weapon.Holster();const int beforeHolster=meleeCalls;
    globals.time=11;weapon.DispatchThink();Check(meleeCalls==beforeHolster&&weapon.m_pfnThink==nullptr);
    // A native CS view animation may exceed this model's range before pickup;
    // ownerless admission must not interpret that unrelated model index.
    Initialize();player.pev->weaponanim=300;Check(weapon.Bind(player,prepared,backends)==RevolverStatus::Ready);Check(weapon.Deploy()&&player.pev->weaponanim==1);
    // At each delivered effect, cancellation stops any following callback.
    for(int edge:{6,7,8,9,3,12,11})
    {
        Initialize();globals.time=10;frame.pressed=1;cancel=edge;events.clear();weapon.PrimaryAttack();
        bool seen=false;for(std::size_t at=0;at<events.size();at+=events[at+1]+2){Check(!seen);if(events[at]==unsigned(edge))seen=true;}Check(seen);
    }
    Initialize();globals.time=10;frame.pressed=1;frame.sourceMode=PistolSource::UnsupportedOriginalOverride;
    events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==5);
    Initialize();globals.time=10;frame.pressed=1;weapon.m_iClip=0;weapon.pev->body=0;weapon.m_fFireOnEmpty=0;failEffect=true;
    events.clear();weapon.PrimaryAttack();Check(weapon.Status()==RevolverStatus::MissingBackend&&weapon.ActionState().primary<10&&events.size()==3&&events[0]==unsigned(RevolverEffect::ViewAnimation));
    Initialize();ItemInfo info{};Check(weapon.GetItemInfo(&info)&&info.iId==898&&info.iMaxClip==5&&info.iMaxAmmo1==50&&info.iMaxAmmo2==-1&&weapon.iItemSlot()==PISTOL_SLOT);
    InventoryRestoreCases();DeploySuppressionCases();ExpandedAmmoCases();ModelRebindCases();
    // Execute a complete uninterrupted reload through engine-like think
    // clearing and ordinary weapon frames, rather than one insert/cancel only.
    Initialize();globals.time=10;weapon.m_iClip=2;weapon.pev->body=2;weapon.Reload();
    unsigned inserted{};
    for(unsigned tick=1;tick<=640;++tick)
    {
        globals.time=10+float(tick)/64;
        if(weapon.pev->nextthink>0&&weapon.pev->nextthink<=globals.time)
        {weapon.pev->nextthink=0;weapon.Think();}
        const auto clip=weapon.m_iClip;weapon.ItemPostFrame();
        if(weapon.m_iClip>clip)++inserted;
    }
    Check(inserted==5&&weapon.m_iClip==5&&player.m_rgAmmo[1]==10&&
        !weapon.CylinderState().loading&&!weapon.CylinderState().pending&&
        weapon.ActionState().action==0&&weapon.m_pfnThink==nullptr);
    Initialize();auto hostPrepared=prepared;hostPrepared.csPenetration=5;
    Check(weapon.Bind(player,hostPrepared,backends)==RevolverStatus::Ready&&weapon.Deploy());
    globals.time=12;frame.bulletDamage=100;frame.pressed=1;player.pev->button=IN_ATTACK;
    const auto oldCSCalls=csBallisticsCalls;weapon.PrimaryAttack();
    Check(csBallisticsCalls==oldCSCalls+1&&weapon.m_iClip==4&&weapon.Status()==RevolverStatus::Applied);
    for(int invalid:{-1,33})
    {hostPrepared.csPenetration=invalid;Check(weapon.Prepare(hostPrepared)==RevolverStatus::InvalidInput);}
    std::cout<<rows<<" Revolver original scenarios, "<<worldRows<<" original world scenarios, "<<checks<<" SDK checks, zero failures\n";
}
