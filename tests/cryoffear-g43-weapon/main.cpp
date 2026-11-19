// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/g43_weapon.hpp"
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
G43OwnerFrame frame{};
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
G43Weapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"G43 SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,G43OwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const G43Event& e) noexcept
{
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    if(fidgetMode)fidgetOrder.push_back(kind);
    switch(e.kind)
    {
    case G43Effect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case G43Effect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case G43Effect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case G43Effect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case G43Effect::ViewAnimation:
        Check(item.m_pPlayer->pev->weaponanim==e.value);
        Event(kind,e.value);
        if(mutateMovement&&e.value==34)
        {
            globals.time+=.375F;frame.originalFlags=0;item.m_pPlayer->pev->weaponanim=37;
        }
        break;
    case G43Effect::ShotState:Event(kind,Bits(e.a));break;
    case G43Effect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case G43Effect::Aim:Event(21,e.enabled);break;
    case G43Effect::InventoryChanged:
        if(observeReload)Check(item.m_iClip==10&&item.m_fInReload&&item.m_pPlayer->m_rgAmmo[item.m_iPrimaryAmmoType]==30);
        if(fidgetMode)Event(kind);
        break;
    case G43Effect::ReserveHud:
        if(fidgetMode)Event(kind,e.value);
        if(mutateInput)
        {
            mutateInput=false;globals.time+=1.F;item.m_pPlayer->pev->button=1;frame.pressed=1;
        }
        break;
    case G43Effect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==1&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case G43Effect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case G43Effect::FireSound:
        Check(e.value==0&&e.other==1&&e.enabled);
        Check(e.first==item.m_pPlayer->EyePosition());Event(kind);break;
    case G43Effect::CameraZoom:
        Check(e.a==350&&e.value==0&&e.other==0&&!e.enabled&&e.first==Vector{}&&e.second==Vector{});
        Event(kind,Bits(e.a));break;
    case G43Effect::EmptyClick:Event(kind);break;
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
        Check(amount>=0&&!std::strcmp(name,"762Nato")&&(maximum==30||maximum==40));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
G43Weapon weapon;
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
G43Prepared prepared;
G43Backends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,G43Weapon&,G43Prepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,G43Backends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,G43Weapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,G43Weapon&,G43Prepared&) noexcept{return false;}
bool CancelColdPrepare(void*,G43Weapon&,G43Prepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;mutateMovement=mutateInput=false;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=40;frame.stamina=100;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/g43/v_g43.mdl\0models/guest/cof/g43/p_g43.mdl\0models/guest/cof/g43/w_g43.mdl\0inventoryitems/weapons/weapon_g43.txt\0weapon_cof_g43\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="762Nato";
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
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::Rifle,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},LongGun::Rifle,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<38)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.pushbackReady=true;
    prepared.aimOffset={0,-4.25F,2};prepared.aimZoom=50;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=0;prepared.hostPosition=3;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/g43/v_g43.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    taskLife=17;Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    backends={nullptr,Current,OwnerFrame,Effect,G43RequiredEffects,&animations,{1,1},17,&playerThink};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=40;weapon.m_iClip=10;
    Check(weapon.Bind(player,prepared,backends)==G43Status::Ready);
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
    Check(material==CSRifleMaterial::Rifle762&&penetration==riflePenetration);
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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==903);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==903);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==903);return 0;}


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
    Check(weapon.Bind(player,prepared,backends)==G43Status::Ready);Check(weapon.Deploy());Ready();
}
bool SawView(int sequence)
{
    for(std::size_t at=0;at<events.size();at+=events[at+1]+2)
        if(events[at]==unsigned(G43Effect::ViewAnimation)&&events[at+1]==1&&events[at+2]==unsigned(sequence))return true;
    return false;
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
        G43Weapon fresh;ItemInfo info{};
        Check(fresh.GetItemInfo(&info)&&info.iId==903&&info.iMaxClip==10&&info.iMaxAmmo1==40&&
            info.iSlot==0&&info.iPosition==3&&!std::strcmp(info.pszName,"weapon_cof_g43")&&
            !std::strcmp(info.pszAmmo1,"762Nato")&&fresh.iItemSlot()==PRIMARY_WEAPON_SLOT&&!fresh.IsPistol());
        Check(!fresh.ResourcesReady()&&!fresh.GetItemInfo(nullptr)&&!fresh.UseDecrement());
    }
    // G43 has an empty fidget. Its ordinary common frame never calls Idle.
    Initialize();Ready();g_engfuncs.pfnRandomLong=[](int,int)->int{std::abort();};
    frame.toggleAim=true;events.clear();weapon.ItemPostFrame();Check(events.empty());
    // An attack edge starts exactly one shot, with no M16 burst/scheduler.
    Initialize();Ready();frame.pressed=0;events.clear();weapon.PrimaryAttack();
    Check(weapon.m_iClip==10&&events.empty());
    frame.pressed=1;weapon.PrimaryAttack();
    Check(weapon.m_iClip==9&&!weapon.m_pfnThink&&SawView(6)&&shotLatch==1);
    Check(Bits(State().primary)==Bits(float(10.0+.9))&&Bits(State().secondary)==Bits(float(10.0+.9)));
    Check(Bits(weapon.Spread().scalar)==Bits(2.4F)&&State().idle==11&&Bits(shotLatchDeadline)==Bits(float(10.0+.2)));
    Ready(12);frame.pressed=0;events.clear();weapon.PrimaryAttack();Check(weapon.m_iClip==9&&events.empty());
    for(bool aimed:{false,true})for(int clip:{0,1})
    {
        Initialize();Ready();frame.pressed=1;weapon.m_iClip=clip;State().aiming=aimed;weapon.m_fFireOnEmpty=1;
        events.clear();weapon.PrimaryAttack();
        Check(SawView(clip?(aimed?11:3):(aimed?11:2))&&weapon.m_iClip==0&&!weapon.m_pfnThink);
    }
    Initialize();Ready();frame.pressed=1;player.pev->waterlevel=3;events.clear();weapon.PrimaryAttack();
    Check(SawView(2)&&weapon.m_iClip==10);
    // Original aim presentation and forward reset are distinct consumers.
    for(int clip:{0,10,75})
    {
        Initialize();Ready();weapon.m_iClip=clip;player.pev->button=0x40;frame.pressed=0x40;frame.toggleAim=true;
        events.clear();weapon.ItemPostFrame();
        Check(State().aiming&&player.pev->fov==50&&SawView(clip?20:21));
        Check(Bits(State().primary)==Bits(float(10.0+.3))&&State().extra==State().primary&&State().idle==11);
        Ready(12);events.clear();weapon.ItemPostFrame();
        Check(!State().aiming&&player.pev->fov==90&&SawView(clip?16:17));
    }
    Initialize();Ready();State().aiming=true;frame.toggleAim=false;
    State().primary=State().secondary=State().extra=10;events.clear();weapon.ItemPostFrame();Check(State().aiming);
    globals.time=std::nextafter(10.F,INFINITY);weapon.ItemPostFrame();Check(!State().aiming&&SawView(16));
    // Idle is explicit, not synthesized by ordinary common PostFrame.
    for(bool aimed:{false,true})for(int clip:{0,4})
    {
        Initialize();Ready();State().aiming=aimed;weapon.m_iClip=clip;frame.toggleAim=true;
        events.clear();weapon.WeaponIdle();
        Check(SawView(aimed?(clip?18:19):(clip?9:10))&&State().idle==11&&State().primary==0);
        events.clear();weapon.WeaponIdle();Check(events.empty());
    }
    // Holster exits aim without playing the ordinary exit-aim view or .3 lock.
    for(bool aimed:{false,true})for(int clip:{0,4})
    {
        Initialize();Ready();State().aiming=aimed;weapon.m_iClip=clip;events.clear();weapon.Holster();
        Check(SawView(clip?7:8)&&!SawView(16)&&!SawView(17)&&!State().aiming);
        Check(Bits(State().ownerAttack)==Bits(float(10.0+1.05))&&
            Bits(player.m_flNextAttack)==Bits(State().ownerAttack-10.F));
    }
    for(int clip:{-666,0,10})
    {
        Initialize();Ready();weapon.m_iClip=clip;events.clear();Check(weapon.Deploy());
        Check(SawView(clip>0?0:1)&&weapon.m_iClip==std::max(0,clip));
    }
    // Current animation/duration and post-consumer clock remain authoritative.
    Initialize();prepared.sequences.duration[37]=1.75F;
    Check(weapon.Bind(player,prepared,backends)==G43Status::Ready);Check(weapon.Deploy());Ready();
    frame.originalFlags=0x40000;player.pev->velocity={3,4,0};mutateMovement=true;
    events.clear();weapon.ItemPostFrame();
    Check(SawView(34)&&player.pev->weaponanim==37&&State().action==1);
    Check(Bits(State().primary)==Bits(float(10.375+1.75*.5))&&State().secondary==0&&State().idle==0);
    globals.time=12.126F;events.clear();weapon.ItemPostFrame();
    Check(SawView(32)&&State().action==0&&Bits(State().primary)==Bits(13.126F)&&State().secondary==0);
    Initialize();Ready();weapon.m_iClip=4;State().aiming=State().pendingReload=true;frame.toggleAim=true;
    events.clear();weapon.ItemPostFrame();Check(!State().aiming&&State().pendingReload&&SawView(16));
    Ready(12);events.clear();weapon.ItemPostFrame();Check(!State().pendingReload&&weapon.m_fInReload&&SawView(29));
    // Pre-input reserve HUD may change the clock/input; do not reuse entry data.
    Initialize();Ready();State().primary=State().secondary=10.5F;player.m_rgAmmo[1]=41;mutateInput=true;
    events.clear();weapon.ItemPostFrame();Check(globals.time==11.F&&weapon.m_iClip==9&&SawView(6)&&!weapon.m_pfnThink);
    for(int clip:{0,4})
    {
        Initialize();Ready();weapon.m_iClip=clip;events.clear();weapon.Reload();
        Check(weapon.m_iClip==0&&weapon.m_fInReload&&SawView(clip?29:28));
        const auto done=State().ownerAttack;
        globals.time=std::nextafter(done,-INFINITY);weapon.ItemPostFrame();Check(weapon.m_iClip==0&&weapon.m_fInReload);
        globals.time=done;observeReload=true;weapon.ItemPostFrame();observeReload=false;
        Check(weapon.m_iClip==10&&player.m_rgAmmo[1]==30&&!weapon.m_fInReload);
    }
    for(int limit:{30,40})for(int amount=1;amount<=41;++amount)
    {
        Initialize();weapon.m_pEntity=&itemWrapper;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        weapon.pev->health=float(amount);weapon.Spawn();player.m_rgAmmo[1]=0;frame.reserveLimit=limit;
        Check(weapon.ResourcesReady()&&weapon.AddToPlayer(&player));
        Check(weapon.m_iClip==std::min(amount,10)&&player.m_rgAmmo[1]==std::min(std::max(amount-10,0),limit));
        weapon.AttachToPlayer(&player);Check(weapon.pev->owner==player.edict()&&weapon.pev->movetype==MOVETYPE_FOLLOW&&!weapon.m_pfnThink);
    }
    for(int clip:{75,511,10000})
    {
        Initialize();Ready();frame.pressed=1;weapon.m_iClip=clip;player.m_rgAmmo[1]=65535;weapon.PrimaryAttack();
        Check(weapon.m_iClip==clip-1&&player.m_rgAmmo[1]==65535);
        Ready(12);weapon.Reload();Check(weapon.m_iClip==clip-1&&!weapon.m_fInReload);
        weapon.Holster();Ready(13);Check(weapon.Deploy()&&weapon.m_iClip==clip-1);
    }
    // The player owns pushback. Switching/drop never cancel it or clear native Think.
    for(int beforeSwitch:{0,1,2})for(int clip:{0,10})
    {
        Initialize();Ready();weapon.m_iClip=clip;const int before=meleeCalls;player.pev->nextthink=61;
        events.clear();weapon.SecondaryAttack();const float due=playerThink.Deadline();
        Check(SawView(clip?26:27)&&Bits(due)==Bits(10+.23F)&&!weapon.m_pfnThink&&player.pev->nextthink==61);
        Check(playerThink.Run(0,std::nextafter(due,-INFINITY))==PlayerThinkResult::Deferred&&meleeCalls==before);
        if(beforeSwitch){weapon.Holster();player.m_pActiveItem=nullptr;}
        if(beforeSwitch==2)weapon.BeforeWeaponDrop();
        Check(playerThink.Run(0,due)==PlayerThinkResult::Dispatched&&meleeCalls==before+1&&lastPlayerThinkKind==PlayerThinkKind::Pushback);
        Check(!playerThink.Deadline()&&playerThink.Run(0,due)==PlayerThinkResult::Deferred&&meleeCalls==before+1&&player.pev->nextthink==61);
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
    for(float stamina:{0.F,-1.F})
    {
        Initialize();Ready();frame.stamina=stamina;events.clear();weapon.SecondaryAttack();
        Check(events.empty()&&!playerThink.Deadline()&&weapon.Status()==G43Status::Ineligible);
    }
    Initialize();Ready();frame.stamina.reset();events.clear();weapon.SecondaryAttack();
    Check(events.empty()&&!playerThink.Deadline()&&weapon.Status()==G43Status::Ineligible);
    frame.pressed=1;weapon.PrimaryAttack();Check(weapon.m_iClip==9);
    Ready(12);weapon.Reload();Check(weapon.m_fInReload);
    for(bool active:{false,true})
    {
        Prime();frame.pressed=1;weapon.m_iClip=75;weapon.PrimaryAttack();Ready(11);weapon.SecondaryAttack();
        const float playerDue=playerThink.Deadline();const int before=meleeCalls;
        if(!active){weapon.Holster();player.m_pActiveItem=nullptr;}
        weapon.OwnerModelChanging();player.pev->modelindex=2;
        std::string error;const auto plan=RecommendedLongGunAnimationPlan(LongGun::Rifle,PistolAnimationRig::Stock);
        backends.playerModelGeneration={1,2};Check(animations.Prepare(player,model,{1,2},LongGun::Rifle,*plan,_stricmp,error));
        Check(weapon.RestoreInventoryBinding(player)&&weapon.m_iClip==74&&!weapon.m_pfnThink);
        Check(playerThink.Deadline()==playerDue&&playerThink.Run(0,playerDue)==PlayerThinkResult::Dispatched&&meleeCalls==before+1);
    }
    for(bool aimed:{false,true})
    {
        Prime();State().aiming=aimed;goldcraft::inventory::Snapshot view;
        Check(weapon.SaveInventorySnapshot(view));events.clear();
        Check(weapon.RestoreInventorySnapshot(view)&&State().aiming==aimed&&
            player.pev->fov==(aimed?float(prepared.aimZoom):90.F));
    }
    Prime();weapon.m_iClip=75;goldcraft::inventory::Snapshot saved;Check(weapon.SaveInventorySnapshot(saved));weapon.m_iClip=5;
    Check(weapon.RestoreInventorySnapshot(saved)&&weapon.m_iClip==75);
    Check(playerThink.Schedule(player,PlayerThinkKind::WeaponMelee,.5F,1,17));const float replacement=playerThink.Deadline();
    Check(weapon.RestoreInventorySnapshot(saved)&&playerThink.Deadline()==replacement);
    Check(playerThink.Run(0,replacement)==PlayerThinkResult::Dispatched&&lastPlayerThinkKind==PlayerThinkKind::WeaponMelee);
    ++ownerEdict.serialnumber;Check(!weapon.RestoreInventorySnapshot(saved));
    Initialize();Ready();frame.pressed=1;failEffect=true;weapon.PrimaryAttack();
    Check(weapon.Status()==G43Status::MissingBackend&&weapon.m_iClip==10);
    failEffect=false;Ready(12);weapon.PrimaryAttack();Check(weapon.m_iClip==9);
    // A failed or retired effect never causes a stale follow-on bullet/aim.
    for(auto stop:{G43Effect::Shell,G43Effect::Recoil,G43Effect::ViewAnimation})
    {
        Initialize();Ready();frame.pressed=1;cancel=int(stop);events.clear();weapon.PrimaryAttack();
        Check(!current);
        for(std::size_t at=0;at<events.size();at+=events[at+1]+2)Check(events[at]!=31);
    }
    Initialize();Ready();player.pev->button=0x40;frame.pressed=0x40;cancel=int(G43Effect::CameraZoom);
    events.clear();weapon.ItemPostFrame();Check(!current&&!SawView(20));
    Initialize();auto noTask=backends;noTask.playerThink=nullptr;Check(weapon.Bind(player,prepared,noTask)==G43Status::Ready);
    Initialize();auto noReady=prepared;noReady.pushbackReady=false;Check(weapon.Bind(player,noReady,backends)==G43Status::Ready);
    Initialize();auto invalid=prepared;invalid.sequences.duration[37]=NAN;Check(weapon.Bind(player,invalid,backends)==G43Status::InvalidInput);
    Initialize();auto wrongLife=backends;++wrongLife.ownerLife;Check(weapon.Bind(player,prepared,wrongLife)==G43Status::MissingBackend);
    for(int kind=0;kind<static_cast<int>(G43Effect::Count);++kind)
    {Initialize();auto b=backends;b.preparedEffects&=~(1U<<kind);Check(weapon.Bind(player,prepared,b)==G43Status::MissingBackend);}
    for(int count:{3,5,32})
    {
        Initialize();prepared.csPenetration=count;rifleShots=0;riflePenetration=count;
        Check(weapon.Bind(player,prepared,backends)==G43Status::Ready);
        Check(weapon.Deploy());
        Ready();frame.pressed=1;weapon.PrimaryAttack();Check(rifleShots==1&&weapon.m_iClip==9);
        Ready(20);weapon.Reload();Ready(30);weapon.ItemPostFrame();
        Check(weapon.m_iClip==10&&!weapon.m_fInReload);
        weapon.Holster();Ready(31);Check(weapon.Deploy());
        Ready(35);frame.pressed=1;weapon.PrimaryAttack();Check(rifleShots==2&&weapon.m_iClip==9);
    }
    for(int count:{-1,1,2,33})
    {
        Initialize();prepared.csPenetration=count;Check(weapon.Prepare(prepared)==G43Status::InvalidInput);
    }
    std::cout<<checks<<" actual G43 SDK checks; 0 failures; target/audio/transport are controlled boundaries\n";
}
