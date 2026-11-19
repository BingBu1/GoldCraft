// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/m16_weapon.hpp"
#include "native/guest/cof/server/player_think.hpp"
#include "native/guest/cof/catalog.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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
M16OwnerFrame frame{};
bool current=true;
int cancel=-1, checks{}, meleeCalls{};
bool failEffect{}, observeReload{};
bool traceDeploy{}, mutateMovement{}, mutateInput{};
std::vector<unsigned> deployOrder;
std::vector<std::pair<int,int>> deployRandom;
bool fidgetMode{};
unsigned fidgetChance{},fidgetChoice{};
std::vector<unsigned> fidgetOrder;
int shotLatch{};
int rifleShots{},riflePenetration{};
float shotLatchDeadline{};
M16Weapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"M16 SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,M16OwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const M16Event& e) noexcept
{
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    if(fidgetMode)fidgetOrder.push_back(kind);
    switch(e.kind)
    {
    case M16Effect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case M16Effect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case M16Effect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case M16Effect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case M16Effect::ViewAnimation:
        Check(item.m_pPlayer->pev->weaponanim==e.value);
        Event(kind,e.value);
        if(mutateMovement&&e.value==14)
        {
            globals.time+=.375F;frame.originalFlags=0;item.m_pPlayer->pev->weaponanim=17;
        }
        break;
    case M16Effect::ShotState:Event(kind,Bits(e.a));break;
    case M16Effect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case M16Effect::Aim:Event(21,e.enabled);break;
    case M16Effect::InventoryChanged:
        if(observeReload)Check(item.m_iClip==20&&item.m_fInReload&&item.m_pPlayer->m_rgAmmo[item.m_iPrimaryAmmoType]==20);
        if(fidgetMode)Event(kind);
        break;
    case M16Effect::ReserveHud:
        if(fidgetMode)Event(kind,e.value);
        if(mutateInput)
        {
            mutateInput=false;globals.time+=1.F;item.m_pPlayer->pev->button=1;frame.pressed=1;
        }
        break;
    case M16Effect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==0&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case M16Effect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case M16Effect::FireSound:
        Check(e.value==0&&e.other==1&&e.enabled);
        Check(e.first==item.m_pPlayer->EyePosition());Event(kind);break;
    case M16Effect::EmptyClick:Event(kind);break;
    default:break;
    }
    if(cancel==static_cast<int>(kind))current=false;
    return !failEffect;
}
struct Player final:CBasePlayer
{
    Vector GetGunPosition() override{return {2,3,4};}
    int GiveAmmo(int amount,const char* name,int maximum) override
    {
        Check(amount>=0&&!std::strcmp(name,"556Nato")&&(maximum==60||maximum==200));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
M16Weapon weapon;
PlayerThinkSlot playerThink;
PlayerThinkKind lastPlayerThinkKind{};
std::uint64_t taskLife=17;
bool TaskCurrent(void*,CBasePlayer& owner,std::uint64_t map,std::uint64_t life) noexcept
{return current&&&owner==&player&&map==1&&life==taskLife;}
// Controlled target/damage boundary, separate from the player scheduler.
bool TaskDispatch(void*,CBasePlayer& owner,PlayerThinkKind kind) noexcept
{
    Check(&owner==&player);lastPlayerThinkKind=kind;
    if(frame.stamina>0){++meleeCalls;Event(110,unsigned(kind));}
    return true;
}
PlayerAnimationAdapter animations;
M16Prepared prepared;
M16Backends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,M16Weapon&,M16Prepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,M16Backends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,M16Weapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,M16Weapon&,M16Prepared&) noexcept{return false;}
bool CancelColdPrepare(void*,M16Weapon&,M16Prepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;mutateMovement=mutateInput=false;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=200;frame.stamina=100;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/m16/v_m16.mdl\0models/guest/cof/m16/p_m16.mdl\0models/guest/cof/m16/w_m16.mdl\0inventoryitems/weapons/weapon_m16.txt\0weapon_cof_m16\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="556Nato";
    player.has_disconnected=false;player.m_iNumSpawns=0;
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
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::M16,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},LongGun::M16,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<38)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.pushbackReady=true;
    prepared.aimOffset={0,-4.25F,2};prepared.aimZoom=50;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=0;prepared.hostPosition=2;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/m16/v_m16.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    taskLife=17;Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    backends={nullptr,Current,OwnerFrame,Effect,M16RequiredEffects,&animations,{1,1},17,&playerThink};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=40;weapon.m_iClip=20;
    Check(weapon.Bind(player,prepared,backends)==M16Status::Ready);
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
BallisticsResult FireCSRifleBallistics(CBaseEntity& owner,const PistolBallistics& shot,const BallisticsEffects& fx,
    const BallisticsCall& call,int penetration,CSRifleMaterial material) noexcept
{
    Check(material==CSRifleMaterial::Rifle556&&penetration==riflePenetration);
    ++rifleShots;
    return FirePistolBallistics(owner,shot,fx,call);
}

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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==902);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==902);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==902);return 0;}


namespace
{
MagazineActionState& State() { return const_cast<MagazineActionState&>(weapon.ActionState()); }
void Ready(float now=10)
{
    globals.time=now;auto& a=State();a.primary=a.secondary=a.extra=a.idle=a.ownerAttack=0;
}
void Prime()
{
    Initialize();weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
    weapon.Precache();backends.ownerLife=17;
    Check(weapon.Bind(player,prepared,backends)==M16Status::Ready);Check(weapon.Deploy());Ready();
}
bool SawView(int sequence)
{
    for(std::size_t at=0;at<events.size();at+=events[at+1]+2)
        if(events[at]==unsigned(M16Effect::ViewAnimation)&&events[at+1]==1&&events[at+2]==unsigned(sequence))return true;
    return false;
}
void Drain()
{
    unsigned count{};
    while(weapon.m_pfnThink)
    {
        Check(++count<=4);globals.time=weapon.pev->nextthink;weapon.DispatchThink();
    }
}
}

int main(int argc,char** argv)
{
    if(argc!=2)return 1;
    std::ifstream file(argv[1],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    Prime();weapon.AttachToPlayer(&player);frame.pressed=1;weapon.PrimaryAttack();
    const int survivorClip=weapon.m_iClip;goldcraft::inventory::Snapshot survivorSnapshot{};
    Check(weapon.SaveInventorySnapshot(survivorSnapshot));weapon.Invalidate();
    playerThink.Invalidate();++player.m_iNumSpawns;++taskLife;backends.ownerLife=taskLife;
    Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    Check(weapon.RestoreInventoryBinding(player)&&weapon.m_iClip==survivorClip&&!weapon.m_pfnThink);
    Check(!weapon.RestoreInventorySnapshot(survivorSnapshot));
    Check(weapon.Deploy());Ready(20);weapon.PrimaryAttack();Check(weapon.m_iClip==survivorClip-1);
    {
        M16Weapon fresh;ItemInfo info{};
        Check(fresh.GetItemInfo(&info)&&info.iId==902&&info.iMaxClip==20&&info.iMaxAmmo1==200&&
            info.iSlot==0&&info.iPosition==2&&!std::strcmp(info.pszName,"weapon_cof_m16")&&
            !std::strcmp(info.pszAmmo1,"556Nato")&&fresh.iItemSlot()==PRIMARY_WEAPON_SLOT&&!fresh.IsPistol());
        Check(!fresh.ResourcesReady()&&!fresh.GetItemInfo(nullptr));
    }
    // Actual SDK callback changes clock, sprint flag and current sequence.
    // The derived frame must use the post-consumer sequence duration and time.
    Initialize();prepared.sequences.duration[17]=1.75F;
    Check(weapon.Bind(player,prepared,backends)==M16Status::Ready);Check(weapon.Deploy());Ready();
    frame.originalFlags=0x40000;player.pev->velocity={3,4,0};mutateMovement=true;
    events.clear();weapon.ItemPostFrame();
    Check(SawView(14)&&player.pev->weaponanim==17&&State().action==1);
    Check(Bits(State().primary)==Bits(float(10.375+1.75*.5))&&State().secondary==State().primary);
    Check(State().idle==10.375F+1.75F*.5F);
    globals.time=12.126F;events.clear();weapon.ItemPostFrame();
    Check(SawView(16)&&State().action==0&&Bits(State().primary)==Bits(13.126F));
    // Pending aim exit cannot be delayed behind the ordinary hold-release lock.
    Initialize();Ready();State().aiming=State().pendingReload=true;frame.toggleAim=false;
    events.clear();weapon.ItemPostFrame();
    Check(!State().aiming&&State().pendingReload&&SawView(20));
    // A real pre-input HUD consumer advances time and changes the held key.
    // An entry-time snapshot must not suppress the now-ready primary attack.
    Initialize();Ready();State().primary=State().secondary=10.5F;
    player.m_rgAmmo[1]=41;mutateInput=true;events.clear();weapon.ItemPostFrame();
    Check(globals.time==11.F&&weapon.m_iClip==19&&weapon.BurstPending()&&SawView(9));
    // Original fidget selects its mode branch before the second RNG callback.
    for(bool burst:{false,true})
    {
        Initialize();Ready();if(!burst){weapon.ToggleFireMode();Ready(12);}
        g_engfuncs.pfnRandomLong=[](int low,int high)
        {
            Check(low==0);if(high==599)return 50;
            Check(high==2);weapon.ToggleFireMode();return 1;
        };
        events.clear();weapon.ItemPostFrame();
        Check(weapon.BurstMode()!=burst&&SawView(burst?30:27));
    }
    // M16 requires an attack edge to start its group; scheduled shots ignore
    // the long group deadline and remain tied to this inventory/life.
    Initialize();Ready();frame.pressed=0;events.clear();weapon.PrimaryAttack();
    Check(weapon.m_iClip==20&&events.empty());
    frame.pressed=1;weapon.PrimaryAttack();
    Check(weapon.m_iClip==19&&State().burstShots==1&&weapon.BurstPending()&&weapon.m_pfnThink);
    Check(Bits(State().primary)==Bits(10.75F)&&Bits(State().idle)==Bits(float(10.0+.66)));
    const float due=weapon.BurstDeadline();
    Check(Bits(due)==Bits(10+.05F)&&Bits(weapon.pev->nextthink)==Bits(due));
    globals.time=std::nextafter(due,-INFINITY);weapon.ScheduledBurst();Check(weapon.m_iClip==19);
    globals.time=due;weapon.ScheduledBurst();Check(weapon.m_iClip==18&&State().burstShots==2);
    const float last=weapon.BurstDeadline();State().aiming=true;
    events.clear();weapon.Holster();Check(SawView(12)&&State().aiming);
    player.m_pActiveItem=nullptr;player.m_flNextAttack=7.25F;globals.time=last;weapon.DispatchThink();
    Check(weapon.m_iClip==17&&State().burstShots==3&&!weapon.BurstPending()&&!weapon.m_pfnThink&&player.m_flNextAttack==7.25F);
    for(int mutation=0;mutation<4;++mutation)
    {
        Initialize();Ready();frame.pressed=1;weapon.PrimaryAttack();const auto clip=weapon.m_iClip;
        const auto due=weapon.BurstDeadline();
        if(mutation==0)++ownerEdict.serialnumber;else if(mutation==1)player.pev->deadflag=DEAD_DEAD;
        else if(mutation==2)weapon.BeforeWeaponDrop();else ++weaponEdict.serialnumber;
        events.clear();globals.time=due;weapon.DispatchThink();Check(events.empty()&&weapon.m_iClip==clip);
    }
    Initialize();Ready();events.clear();weapon.ToggleFireMode();
    Check(!weapon.BurstMode()&&SawView(13)&&Bits(State().primary)==Bits(11.F));
    Ready(12);events.clear();frame.pressed=1;weapon.PrimaryAttack();
    Check(weapon.m_iClip==19&&!weapon.BurstPending()&&!weapon.m_pfnThink&&SawView(2)&&State().burstShots==0);
    Check(Bits(State().primary)==Bits(float(12.0+.1)));
    Ready(14);State().aiming=true;events.clear();weapon.ToggleFireMode();
    Check(weapon.BurstMode()&&SawView(32));
    Ready(16);events.clear();weapon.ToggleFireMode();Check(!weapon.BurstMode()&&SawView(33));
    // Original owner task survives holster/drop; native CS Think is untouched.
    for(int beforeSwitch:{0,1,2})
    {
        Initialize();Ready();const int before=meleeCalls;events.clear();
        player.pev->nextthink=61;weapon.SecondaryAttack();
        const float due=playerThink.Deadline();
        Check(SawView(22)&&Bits(due)==Bits(10+.23F)&&!weapon.m_pfnThink&&player.pev->nextthink==61);
        Check(playerThink.Run(0,std::nextafter(due,-INFINITY))==PlayerThinkResult::Deferred&&meleeCalls==before);
        if(beforeSwitch){weapon.Holster();player.m_pActiveItem=nullptr;}
        if(beforeSwitch==2)weapon.BeforeWeaponDrop();
        Check(playerThink.Run(0,due)==PlayerThinkResult::Dispatched&&meleeCalls==before+1);
        Check(!playerThink.Deadline()&&playerThink.InternalDeadline()==due&&lastPlayerThinkKind==PlayerThinkKind::Pushback);
        Check(playerThink.Run(0,due)==PlayerThinkResult::Deferred&&meleeCalls==before+1&&player.pev->nextthink==61);
    }
    for(float stamina:{0.F,-1.F})
    {
        Initialize();Ready();frame.stamina=stamina;events.clear();weapon.SecondaryAttack();
        Check(events.empty()&&!playerThink.Deadline()&&!weapon.m_pfnThink&&weapon.Status()==M16Status::Ineligible);
    }
    Initialize();Ready();frame.stamina.reset();events.clear();weapon.SecondaryAttack();
    Check(events.empty()&&!playerThink.Deadline()&&weapon.Status()==M16Status::Ineligible);
    frame.pressed=1;weapon.PrimaryAttack();Drain();
    Check(weapon.m_iClip==17&&State().burstShots==3);
    for(int order:{0,1})
    {
        Initialize();Ready();frame.pressed=1;const int before=meleeCalls;
        if(order==0){weapon.PrimaryAttack();Ready(11);weapon.SecondaryAttack();}
        else {weapon.SecondaryAttack();Ready(11);weapon.PrimaryAttack();}
        const float due=playerThink.Deadline();Drain();
        Check(weapon.m_iClip==17&&State().burstShots==3&&meleeCalls==before);
        Check(playerThink.Run(0,due)==PlayerThinkResult::Dispatched&&meleeCalls==before+1);
    }
    for(int mutation=0;mutation<6;++mutation)
    {
        Initialize();Ready();weapon.SecondaryAttack();const float due=playerThink.Deadline();const int before=meleeCalls;
        if(mutation==0)++ownerEdict.serialnumber;else if(mutation==1)++player.m_iNumSpawns;
        else if(mutation==2)player.pev->deadflag=DEAD_DEAD;else if(mutation==3)player.has_disconnected=true;
        else if(mutation==4)current=false;else player.pev->health=0;
        Check(playerThink.Run(0,due)==PlayerThinkResult::Cancelled&&meleeCalls==before&&playerThink.Deadline()==0);
        current=true;player.pev->deadflag=DEAD_NO;player.pev->health=100;player.has_disconnected=false;
        Check(playerThink.Run(0,due)==PlayerThinkResult::Cancelled&&meleeCalls==before);
    }
    for(bool burst:{false,true})for(int clip:{0,4})
    {
        Initialize();Ready();if(!burst){weapon.ToggleFireMode();Ready(12);}
        weapon.m_iClip=clip;events.clear();weapon.Reload();
        Check(weapon.m_iClip==0&&weapon.m_fInReload&&SawView((burst?10:3)+!clip));
        const auto done=State().ownerAttack;
        globals.time=std::nextafter(done,-INFINITY);weapon.ItemPostFrame();Check(weapon.m_iClip==0&&weapon.m_fInReload);
        globals.time=done;weapon.ItemPostFrame();
        Check(weapon.m_iClip==20&&player.m_rgAmmo[1]==20&&!weapon.m_fInReload);
    }
    for(int limit:{60,200})for(int amount=1;amount<=41;++amount)
    {
        Initialize();weapon.m_pEntity=&itemWrapper;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        weapon.pev->health=float(amount);weapon.Spawn();player.m_rgAmmo[1]=0;frame.reserveLimit=limit;
        Check(weapon.ResourcesReady()&&weapon.AddToPlayer(&player));
        Check(weapon.m_iClip==std::min(amount,20)&&player.m_rgAmmo[1]==std::max(amount-20,0));
        weapon.AttachToPlayer(&player);
        Check(weapon.pev->owner==player.edict()&&weapon.pev->movetype==MOVETYPE_FOLLOW&&weapon.m_pfnThink==nullptr);
    }
    for(int clip:{75,511,10000})
    {
        Initialize();Ready();frame.pressed=1;weapon.m_iClip=clip;player.m_rgAmmo[1]=65535;
        weapon.PrimaryAttack();Drain();Check(weapon.m_iClip==clip-3&&player.m_rgAmmo[1]==65535);
        Ready(12);weapon.Reload();Check(weapon.m_iClip==clip-3&&!weapon.m_fInReload);
        weapon.Holster();Ready(13);Check(weapon.Deploy()&&weapon.m_iClip==clip-3);
    }
    // Inventory snapshots and actual model rebinding preserve both task
    // deadlines, fire mode and legitimate large ammo under the same owner life.
    for(bool active:{false,true})
    {
        Prime();frame.pressed=1;weapon.m_iClip=75;weapon.PrimaryAttack();
        const float due=weapon.BurstDeadline();Ready(11);weapon.SecondaryAttack();
        const float playerDue=playerThink.Deadline();const int before=meleeCalls;
        if(!active){weapon.Holster();player.m_pActiveItem=nullptr;}
        weapon.OwnerModelChanging();player.pev->modelindex=2;
        std::string error;const auto plan=RecommendedLongGunAnimationPlan(LongGun::M16,PistolAnimationRig::Stock);
        backends.playerModelGeneration={1,2};
        Check(animations.Prepare(player,model,{1,2},LongGun::M16,*plan,_stricmp,error));
        Check(weapon.RestoreInventoryBinding(player)&&weapon.BurstPending()&&weapon.BurstDeadline()==due&&weapon.m_iClip==74);
        Drain();Check(weapon.m_iClip==72&&!weapon.BurstPending());
        Check(playerThink.Deadline()==playerDue&&playerThink.Run(0,playerDue)==PlayerThinkResult::Dispatched&&meleeCalls==before+1);
    }
    Prime();Ready();weapon.ToggleFireMode();Ready(12);weapon.m_iClip=75;
    goldcraft::inventory::Snapshot saved;
    Check(weapon.SaveInventorySnapshot(saved));weapon.m_iClip=5;
    Check(weapon.RestoreInventorySnapshot(saved)&&weapon.m_iClip==75&&!weapon.BurstMode());
    Check(playerThink.Schedule(player,PlayerThinkKind::WeaponMelee,.5F,1,17));
    const float replacement=playerThink.Deadline();
    Check(weapon.RestoreInventorySnapshot(saved)&&playerThink.Deadline()==replacement);
    Check(playerThink.Run(0,replacement)==PlayerThinkResult::Dispatched&&lastPlayerThinkKind==PlayerThinkKind::WeaponMelee);
    ++ownerEdict.serialnumber;Check(!weapon.RestoreInventorySnapshot(saved));
    for(bool burst:{false,true})for(bool aimed:{false,true})for(int choice=0;choice<3;++choice)
    {
        Initialize();Ready();if(!burst){weapon.ToggleFireMode();Ready(12);}
        State().aiming=aimed;weapon.m_iClip=0;frame.toggleAim=true;fidgetChance=50;fidgetChoice=choice;
        unsigned rngCalls{};static unsigned* calls;calls=&rngCalls;
        g_engfuncs.pfnRandomLong=[](int low,int high){Check(low==0);++*calls;return int(high==599?fidgetChance:fidgetChoice);};
        events.clear();weapon.ItemPostFrame();
        Check(rngCalls==unsigned(aimed?1:2));
        Check(aimed?!SawView((burst?29:26)+choice):SawView((burst?29:26)+choice));
    }
    Initialize();Ready();frame.pressed=1;failEffect=true;weapon.PrimaryAttack();
    Check(weapon.Status()==M16Status::MissingBackend&&weapon.m_iClip==20);
    failEffect=false;Ready(12);weapon.PrimaryAttack();Drain();Check(weapon.m_iClip==17);
    Initialize();auto bad=prepared;bad.pushbackReady=false;
    Check(weapon.Bind(player,bad,backends)==M16Status::Ready);
    Initialize();auto noTask=backends;noTask.playerThink=nullptr;
    Check(weapon.Bind(player,prepared,noTask)==M16Status::Ready);
    Initialize();auto wrongLife=backends;++wrongLife.ownerLife;
    Check(weapon.Bind(player,prepared,wrongLife)==M16Status::MissingBackend);
    auto wrongMap=backends;++wrongMap.playerModelGeneration.mapEpoch;
    Check(weapon.Bind(player,prepared,wrongMap)==M16Status::MissingBackend);
    for(int kind=0;kind<static_cast<int>(M16Effect::Count);++kind)
    {
        Initialize();auto b=backends;b.preparedEffects&=~(1U<<kind);
        Check(weapon.Bind(player,prepared,b)==M16Status::MissingBackend);
    }
    for(int count:{3,5,32})
    {
        Initialize();prepared.csPenetration=count;rifleShots=0;riflePenetration=count;
        Check(weapon.Bind(player,prepared,backends)==M16Status::Ready);
        Check(weapon.Deploy());
        Ready();frame.pressed=1;weapon.PrimaryAttack();Drain();
        Check(rifleShots==3&&weapon.m_iClip==17);
        Ready(20);weapon.Reload();Ready(30);weapon.ItemPostFrame();
        Check(weapon.m_iClip==20&&!weapon.m_fInReload);
        weapon.Holster();Ready(31);Check(weapon.Deploy());
        Ready(35);frame.pressed=1;weapon.PrimaryAttack();Drain();Check(rifleShots==6&&weapon.m_iClip==17);
    }
    for(int count:{-1,1,2,33})
    {
        Initialize();prepared.csPenetration=count;Check(weapon.Prepare(prepared)==M16Status::InvalidInput);
    }
    std::cout<<checks<<" actual M16 SDK checks; 0 failures; pushback target/audio and transport are controlled boundaries\n";
    return 0;
}
