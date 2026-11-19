// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/shotgun_weapon.hpp"
#include "native/guest/cof/server/player_think.hpp"
#include "native/guest/cof/catalog.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <sstream>
#include <vector>

namespace
{
bool measureAllocations{};
std::size_t allocations{};
}
void* operator new(std::size_t bytes)
{
    if (measureAllocations) ++allocations;
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

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
ShotgunOwnerFrame frame{};
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
int pelletTransactions{}, shellCalls{};
Vector lastShellOrigin{}, lastShellVelocity{};
std::vector<int> reloadPhases;
bool recording = true;
float shotLatchDeadline{};
ShotgunWeapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"Shotgun SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{if(recording)events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,ShotgunOwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*, CBasePlayer&, CBasePlayerWeapon& item, const ShotgunEvent& e) noexcept
{
    const auto kind = static_cast<unsigned>(e.kind);
    if (traceDeploy) deployOrder.push_back(kind);
    switch (e.kind)
    {
    case ShotgunEffect::ViewAnimation:
        Check(item.m_pPlayer->pev->weaponanim == e.value); Event(kind, e.value); break;
    case ShotgunEffect::Shell:
        ++shellCalls; lastShellOrigin = e.first; lastShellVelocity = e.second;
        Check(e.value == 31 && e.other == 2); Event(kind); break;
    case ShotgunEffect::Gunsmoke:
        Check(e.value == 19 && e.enabled && e.other == 1 && unsigned(e.modelString) == unsigned(item.m_pPlayer->pev->viewmodel));
        Check(item.m_pfnThink && Bits(item.pev->nextthink) == Bits(float(double(globals.time) + .45)));
        Event(kind); break;
    case ShotgunEffect::FireEvent: Check(e.value == 4); Event(kind); break;
    case ShotgunEffect::FireSound:
        Check(e.value == 0 && e.other == 1 && e.enabled && e.first == item.m_pPlayer->EyePosition());
        Event(kind); break;
    case ShotgunEffect::CameraZoom: Check(e.a == 350); Event(kind); break;
    case ShotgunEffect::ReloadStage:
        Check(e.value >= 0 && e.value <= 4);
        if (e.value < 2) Check(e.a == globals.time + 1.F);
        reloadPhases.push_back(e.value); Event(kind, e.value); break;
    case ShotgunEffect::ReserveHud:
        if (mutateInput) { mutateInput = false; globals.time += 1.F; item.m_pPlayer->pev->button = 1; frame.pressed = 1; }
        Event(kind, e.value); break;
    default: Event(kind); break;
    }
    if (cancel == static_cast<int>(kind)) current = false;
    return !failEffect;
}
struct Player final:CBasePlayer
{
    Vector GetGunPosition() override{return {2,3,4};}
    int GiveAmmo(int amount,const char* name,int maximum) override
    {
        Check(amount>=0&&!std::strcmp(name,"buckshot")&&(maximum==15||maximum==50));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
ShotgunWeapon weapon;
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
ShotgunPrepared prepared;
ShotgunBackends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,ShotgunWeapon&,ShotgunPrepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,ShotgunBackends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,ShotgunWeapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,ShotgunWeapon&,ShotgunPrepared&) noexcept{return false;}
bool CancelColdPrepare(void*,ShotgunWeapon&,ShotgunPrepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();reloadPhases.clear();pelletTransactions=shellCalls=0;recording=true;current=true;mutateMovement=mutateInput=false;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=50;frame.stamina=100;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/shotgun/v_shotgun.mdl\0models/guest/cof/shotgun/p_shotgun.mdl\0models/guest/cof/shotgun/w_shotgun.mdl\0inventoryitems/weapons/weapon_shotgun.txt\0weapon_cof_shotgun\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="buckshot";
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
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::Shotgun,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},LongGun::Shotgun,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<20)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.pushbackReady=true;
    prepared.aimOffset={0,-4.25F,2};prepared.aimZoom=50;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=0;prepared.hostPosition=4;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/shotgun/v_shotgun.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    taskLife=17;Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    backends={nullptr,Current,OwnerFrame,Effect,ShotgunRequiredEffects,&animations,{1,1},17,&playerThink};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=50;weapon.m_iClip=5;
    Check(weapon.Bind(player,prepared,backends)==ShotgunStatus::Ready);
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
BallisticsResult FireShotgunBallistics(CBaseEntity& owner, const PistolBallistics& shot,
    const BallisticsEffects& fx, const BallisticsCall& call) noexcept
{
    Check(&owner == &player && CheckPistolBallistics(shot, fx, call) == BallisticsStatus::Ready);
    Check(shot.seed == frame.randomSeed && shot.damage == frame.bulletDamage);
    Check(shot.source == Vector(2,3,4) && shot.direction == globals.v_forward);
    Check(Bits(shot.spread.x) == 0x3D8EDE55 && shot.spread.x == shot.spread.y && shot.spread.y == shot.spread.z);
    ++pelletTransactions; Event(31); return {BallisticsStatus::Fired, {}};
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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==904);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==904);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==904);return 0;}


namespace
{
ShotgunActionState& State() { return const_cast<ShotgunActionState&>(weapon.ActionState()); }
void Ready(float now=10)
{
    globals.time=now;auto& a=State();a.primary=a.secondary=a.extra=a.idle=a.ownerAttack=0;
}
void Prime()
{
    Initialize();weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
    weapon.Precache();backends.ownerLife=17;
    Check(weapon.Bind(player,prepared,backends)==ShotgunStatus::Ready);Check(weapon.Deploy());Ready();
}
bool SawView(int sequence)
{
    for(std::size_t at=0;at<events.size();at+=events[at+1]+2)
        if(events[at]==unsigned(ShotgunEffect::ViewAnimation)&&events[at+1]==1&&events[at+2]==unsigned(sequence))return true;
    return false;
}
std::array<std::uint32_t,5> SpreadBits(const SpreadState& spread)
{return {Bits(spread.scalar),Bits(spread.recoveryDeadline),Bits(spread.vector[0]),Bits(spread.vector[1]),Bits(spread.vector[2])};}

}

int main(int argc, char** argv)
{
    if (argc != 5) return 1;
    std::ifstream file(argv[1], std::ios::binary); if (!file) return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
    model.resize(bytes.size()); std::memcpy(model.data(), bytes.data(), bytes.size());
    Prime();weapon.AttachToPlayer(&player);frame.pressed=1;weapon.PrimaryAttack();
    const int survivorClip=weapon.m_iClip;goldcraft::inventory::Snapshot survivorSnapshot{};
    Check(weapon.SaveInventorySnapshot(survivorSnapshot));weapon.Invalidate();
    playerThink.Invalidate();++player.m_iNumSpawns;++taskLife;backends.ownerLife=taskLife;
    Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    Check(weapon.RestoreInventoryBinding(player)&&weapon.m_iClip==survivorClip&&!weapon.m_pfnThink);
    Check(!weapon.RestoreInventorySnapshot(survivorSnapshot));
    Check(weapon.Deploy());Ready(20);weapon.PrimaryAttack();Check(weapon.m_iClip==survivorClip-1);
    {
        ShotgunWeapon fresh; ItemInfo info{};
        Check(fresh.GetItemInfo(&info) && info.iId == ShotgunHostId && info.iMaxClip == 5 && info.iMaxAmmo1 == 50 &&
            info.iWeight == 20 && info.iSlot == 0 && info.iPosition == 4 && !std::strcmp(info.pszName,"weapon_cof_shotgun") &&
            !std::strcmp(info.pszAmmo1,"buckshot") && fresh.iItemSlot() == PRIMARY_WEAPON_SLOT && !fresh.IsPistol());
        Check(!fresh.ResourcesReady() && !fresh.GetItemInfo(nullptr) && !fresh.UseDecrement());
    }
    // Cold admission rejects missing effects and incomplete original sequence closure.
    Initialize(); auto bad = prepared; bad.sequences.present &= ~(std::uint64_t{1} << 18);
    Check(weapon.Prepare(bad) == ShotgunStatus::InvalidInput);
    bad = prepared; bad.pushbackReady = false; Check(weapon.Prepare(bad) == ShotgunStatus::Ready);
    Initialize(); auto missing = backends; missing.preparedEffects &= ~(1u << unsigned(ShotgunEffect::ReloadStage));
    Check(weapon.Bind(player, prepared, missing) == ShotgunStatus::MissingBackend);
    // Deploy and ordinary holster use shotgun animations and preserve the ADS state.
    Prime(); Check(SawView(6) && !std::strcmp(player.m_szAnimExtention,"shotgun"));
    State().aiming = true; player.pev->fov = 50; events.clear(); weapon.Holster();
    Check(SawView(7) && State().aiming && player.pev->fov == 50 && State().ownerAttack == 10.5F);
    Check(weapon.Deploy()); Check(!State().aiming && SawView(6));
    // The original player updater drives HUD spread independently of the fixed
    // shotgun pellet cone. DefaultDeploy resets only scalar and recovery time.
    Prime(); Check(weapon.Spread().scalar==0&&weapon.Spread().recoveryDeadline==0);
    frame.originalFlags=0x200;player.pev->velocity={200,0,0};weapon.AdvanceOwnerFrame();
    Check(weapon.Spread().scalar==3.5F&&weapon.Spread().recoveryDeadline==float(double(globals.time)+.1));
    const auto moving=SpreadBits(weapon.Spread());frame.pressed=1;weapon.PrimaryAttack();
    Check(pelletTransactions==1&&SpreadBits(weapon.Spread())==moving);
    weapon.Holster();player.m_pActiveItem=nullptr;globals.time=11;weapon.AdvanceOwnerFrame();
    Check(SpreadBits(weapon.Spread())==moving);player.m_pActiveItem=&weapon;Check(weapon.Deploy());
    Check(weapon.Spread().scalar==0&&weapon.Spread().recoveryDeadline==11);
    Check(weapon.Spread().vector[0]==Float(moving[2]));
    // Rollback and an authenticated model change retain the live spread state.
    Prime();frame.originalFlags=0x200;player.pev->velocity={200,0,0};weapon.AdvanceOwnerFrame();
    goldcraft::inventory::Snapshot spreadSnapshot{};Check(weapon.SaveInventorySnapshot(spreadSnapshot));
    const auto savedSpread=SpreadBits(weapon.Spread());frame.originalFlags=0;globals.time=11;weapon.AdvanceOwnerFrame();
    Check(SpreadBits(weapon.Spread())!=savedSpread&&weapon.RestoreInventorySnapshot(spreadSnapshot));
    Check(SpreadBits(weapon.Spread())==savedSpread);
    weapon.OwnerModelChanging();++player.pev->modelindex;
    {std::string error;auto plan=RecommendedLongGunAnimationPlan(LongGun::Shotgun,PistolAnimationRig::Stock);
     Check(plan&&animations.Prepare(player,model,{1,2},LongGun::Shotgun,*plan,_stricmp,error));}
    backends.playerModelGeneration={1,2};Check(weapon.RestoreInventoryBinding(player));
    Check(SpreadBits(weapon.Spread())==savedSpread);player.pev->health=0;weapon.AdvanceOwnerFrame();
    Check(SpreadBits(weapon.Spread())==savedSpread);
    // Primary needs an attack edge; pump is installed before smoke and uses the later geometry.
    Prime(); frame.pressed = 0; events.clear(); weapon.PrimaryAttack(); Check(weapon.m_iClip == 5 && events.empty());
    frame.pressed = 1; weapon.PrimaryAttack();
    Check(weapon.m_iClip == 4 && pelletTransactions == 1 && SawView(1) && weapon.m_pfnThink);
    Check(Bits(weapon.pev->nextthink) == Bits(float(10.0 + .45)) && State().primary == 11);
    weapon.pev->origin = {10,20,30}; weapon.pev->view_ofs = {1,2,3};
    globals.v_forward = {2,3,5}; globals.v_right = {7,11,13}; globals.v_up = {17,19,23};
    globals.time = weapon.pev->nextthink; weapon.DispatchThink();
    Check(shellCalls == 1 && !weapon.m_pfnThink && weapon.pev->nextthink == 0);
    Check(lastShellOrigin == Vector((11+2*45)+7*30,(22+3*45)+11*30,(33+5*45)+13*30));
    Check(lastShellVelocity == Vector((7*60+17*200)+2*40,(11*60+19*200)+3*40,(13*60+23*200)+5*40));
    weapon.DispatchThink(); Check(shellCalls == 1);
    // Pump and insert are weapon-owned and remain live after switching away.
    Prime(); frame.pressed = 1; weapon.PrimaryAttack(); const auto pumpAt = weapon.pev->nextthink;
    weapon.Holster(); player.m_pActiveItem = nullptr;
    player.m_iWeaponVolume = 123; player.m_iWeaponFlash = 456;
    globals.time = pumpAt; weapon.DispatchThink();
    Check(shellCalls == 1 && !weapon.m_pfnThink);
    Check(player.m_iWeaponVolume == 123 && player.m_iWeaponFlash == 456);
    // Empty-start reload fills exactly five, one native Think transaction per shell.
    Prime(); weapon.m_iClip = 0; weapon.Reload(); Check(State().reloadStage == 1 && SawView(2) && !weapon.m_fInReload);
    for (int n = 0; n != 5; ++n)
    {
        Ready(float(12+n*2)); weapon.ItemPostFrame();
        Check(State().reloadStage == 2 && weapon.m_pfnThink && SawView(3));
        const auto due = weapon.pev->nextthink; globals.time = due-.01F; weapon.DispatchThink(); Check(weapon.m_iClip == n);
        globals.time = due; weapon.DispatchThink();
        Check(weapon.m_iClip == n+1 && player.m_rgAmmo[1] == 49-n && State().reloadStage == 1 && !weapon.m_fInReload);
    }
    Ready(24); weapon.ItemPostFrame(); Check(!State().reloadStage && !State().action && SawView(5) && reloadPhases.back() == 2);
    // Loaded natural finish differs from attack-aborted finish.
    Prime(); weapon.m_iClip = 4; weapon.Reload(); Ready(12); weapon.ItemPostFrame();
    globals.time = weapon.pev->nextthink; weapon.DispatchThink(); Ready(14); weapon.ItemPostFrame();
    Check(weapon.m_iClip == 5 && SawView(4) && reloadPhases.back() == 3);
    Prime(); weapon.m_iClip = 1; weapon.Reload(); Ready(12); frame.pressed = 1; weapon.ItemPostFrame();
    Check(weapon.m_iClip == 1 && !State().reloadStage && reloadPhases.back() == 4 && SawView(4));
    // Trusted skill grants never truncate owned ammunition or run the clip!=5 loop.
    for (int amount : {5,6,75,512,2048})
    {
        Prime(); weapon.m_iClip = amount; const int reserve = player.m_rgAmmo[1]; weapon.Reload();
        Check(weapon.Status() == ShotgunStatus::Ineligible && weapon.m_iClip == amount && !State().reloadStage);
        frame.pressed = 1; weapon.PrimaryAttack(); Check(weapon.m_iClip == amount-1 && pelletTransactions == 1);
        Check(player.m_rgAmmo[1] == reserve);
        Prime(); weapon.m_iClip = 1; weapon.Reload(); Ready(12); weapon.ItemPostFrame();
        Check(State().reloadStage == 2); weapon.m_iClip = amount;
        globals.time = weapon.pev->nextthink; weapon.DispatchThink();
        Check(weapon.m_iClip == amount && player.m_rgAmmo[1] == 50);
        Ready(14); weapon.ItemPostFrame(); Check(!State().reloadStage && weapon.m_iClip == amount);
    }
    Prime(); weapon.m_iClip = 1; weapon.Reload(); Ready(12); weapon.ItemPostFrame();
    player.m_rgAmmo[1] = 0; globals.time = weapon.pev->nextthink; weapon.DispatchThink();
    Check(player.m_rgAmmo[1] == 0 && weapon.m_iClip == 1); Ready(14); weapon.ItemPostFrame(); Check(!State().reloadStage);
    // The shell transaction is retained over holster/deploy and authenticated model rebind.
    for (bool pump : {false,true})
    {
        Prime();
        if (pump) { frame.pressed = 1; weapon.PrimaryAttack(); }
        else { weapon.m_iClip = 1; weapon.Reload(); Ready(12); weapon.ItemPostFrame(); }
        const auto due = weapon.pev->nextthink; const int clip = weapon.m_iClip;
        weapon.OwnerModelChanging(); ++player.pev->modelindex;
        std::string error; auto plan = RecommendedLongGunAnimationPlan(LongGun::Shotgun,PistolAnimationRig::Stock);
        Check(plan && animations.Prepare(player,model,{1,2},LongGun::Shotgun,*plan,_stricmp,error));
        backends.playerModelGeneration = {1,2};
        Check(weapon.RestoreInventoryBinding(player) && weapon.m_pfnThink && Bits(weapon.pev->nextthink) == Bits(due));
        globals.time = due; weapon.DispatchThink(); Check(!weapon.m_pfnThink);
        Check(pump ? (shellCalls == 1 && weapon.m_iClip == clip) : (weapon.m_iClip == clip+1 && player.m_rgAmmo[1] == 49));
    }
    Prime(); weapon.m_iClip = 1; weapon.Reload(); Ready(12); weapon.ItemPostFrame();
    const auto insertAt = weapon.pev->nextthink; weapon.Holster(); player.m_pActiveItem = nullptr;
    globals.time = insertAt; weapon.DispatchThink(); Check(weapon.m_iClip == 2 && player.m_rgAmmo[1] == 49);
    player.m_pActiveItem = &weapon; Check(weapon.Deploy() && State().reloadStage == 1);
    // Inventory snapshots preserve a pending insert, but cannot cross spawn/map/serial identity.
    Prime(); weapon.m_iClip = 1; weapon.Reload(); Ready(12); weapon.ItemPostFrame();
    goldcraft::inventory::Snapshot snapshot{}; Check(weapon.SaveInventorySnapshot(snapshot));
    const auto savedDue = weapon.pev->nextthink; globals.time = savedDue; weapon.DispatchThink();
    Check(weapon.m_iClip == 2); Check(weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.m_iClip == 1 && weapon.m_pfnThink && weapon.pev->nextthink == savedDue);
    ++player.m_iNumSpawns; Check(!weapon.RestoreInventorySnapshot(snapshot));
    // A skill grant may arrive between the last mirror refresh and purchase rollback.
    Prime(); weapon.m_iClip = 75; player.m_rgAmmo[1] = 2000;
    Check(weapon.SaveInventorySnapshot(snapshot));
    weapon.m_iClip = 1; player.m_rgAmmo[1] = 1;
    Check(weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.m_iClip == 75 && player.m_rgAmmo[1] == 2000);
    // Original pending ADS reload uses exit first and starts the transaction on a later frame.
    Prime(); weapon.m_iClip = 1; State().aiming = true; weapon.Reload(); Check(State().pendingReload);
    Ready(12); weapon.ItemPostFrame(); Check(!State().aiming && State().pendingReload && SawView(17));
    Ready(14); weapon.ItemPostFrame(); Check(!State().pendingReload && State().reloadStage == 1 && SawView(2));
    // Shared player pushback survives ordinary holster, and missing stamina only refuses this action.
    Prime(); weapon.SecondaryAttack(); Check((playerThink.Deadline() > 0) && SawView(18));
    const auto taskDue = playerThink.Deadline(); weapon.Holster(); player.m_pActiveItem = nullptr;
    Check(playerThink.Run(0, double(globals.time)) == PlayerThinkResult::Deferred); globals.time = taskDue;
    Check(playerThink.Run(0, double(globals.time)) == PlayerThinkResult::Dispatched && meleeCalls > 0);
    Prime(); frame.stamina.reset(); weapon.SecondaryAttack(); Check(weapon.Status() == ShotgunStatus::Ineligible);
    frame.pressed = 1; weapon.PrimaryAttack(); Check(pelletTransactions == 1);
    // Readiness, death and entity reuse cannot dispatch pending shell work.
    for (int mode = 0; mode != 3; ++mode)
    {
        Prime(); frame.pressed = 1; weapon.PrimaryAttack(); const auto due = weapon.pev->nextthink;
        if (!mode) player.pev->health = 0; else if (mode == 1) ++weaponEdict.serialnumber; else current = false;
        globals.time = due; weapon.DispatchThink(); Check(shellCalls == 0);
    }
    for (auto kind : {ShotgunEffect::Gunsmoke,ShotgunEffect::FireEvent,ShotgunEffect::FireSound,ShotgunEffect::Recoil})
    {
        Prime(); frame.pressed = 1; cancel = int(kind); weapon.PrimaryAttack();
        Check(weapon.Status() == ShotgunStatus::Cancelled || !current);
    }
    Prime(); frame.pressed = 1; failEffect = true; weapon.PrimaryAttack(); Check(weapon.Status() == ShotgunStatus::MissingBackend);
    Prime(); g_engfuncs.pfnRandomLong = [](int, int) { return 1; };
    weapon.ItemPostFrame(); recording = false; allocations = 0; measureAllocations = true;
    for (unsigned n = 0; n != 10000; ++n) { weapon.AdvanceOwnerFrame(); weapon.ItemPostFrame(); }
    measureAllocations = false; Check(allocations == 0); recording = true;
    // Complete original fidget bytecode results, including the RNG calls while aiming.
    std::ifstream rows(argv[2]), answers(argv[3]); if (!rows || !answers) return 1;
    unsigned rowCount{}; std::string row, answer;
    while (std::getline(rows,row))
    {
        if (!std::getline(answers,answer)) return 2;
        std::istringstream fields(row); unsigned chance, choice, aim, gate; if (!(fields>>chance>>choice>>aim>>gate)) return 2;
        Prime(); Ready(); State().aiming = aim != 0; if (gate) State().idle = globals.time + 1;
        fidgetChance = chance; fidgetChoice = choice; deployRandom.clear();
        g_engfuncs.pfnRandomLong = [](int low,int high) { deployRandom.emplace_back(low,high); return high==599 ? int(fidgetChance) : int(fidgetChoice); };
        events.clear(); weapon.ItemPostFrame();
        std::vector<unsigned> actual;
        for (auto [low,high] : deployRandom) { actual.push_back(unsigned(low)); actual.push_back(unsigned(high)); }
        for (int seq = 11; seq <= 13; ++seq) if (SawView(seq)) actual.push_back(unsigned(seq));
        std::istringstream expected(answer); unsigned value; std::vector<unsigned> wanted;
        while (expected>>value) wanted.push_back(value);
        Check(actual == wanted); ++rowCount;
    }
    Check(rowCount != 0 && !std::getline(answers,answer));
    // Sequential original x86 output checks the actual weapon entry point,
    // including transitions across movement, crouch, sprint, air and absence.
    Prime();std::ifstream spreadRows(argv[4]);if(!spreadRows)return 2;unsigned spreadCount{};
    while(std::getline(spreadRows,row))
    {
        std::istringstream fields(row);std::array<std::uint32_t,26> values{};
        for(auto& value:values)if(!(fields>>std::hex>>value))return 2;
        std::string extra;if(fields>>extra)return 2;
        Check(values[14]==4);
        const auto initial=SpreadBits(weapon.Spread());for(unsigned i=0;i<5;++i)Check(initial[i]==values[i]);
        player.pev->velocity={Float(values[10]),Float(values[11]),0};globals.time=Float(values[12]);
        frame.originalFlags=values[13];current=values[15]!=0;weapon.AdvanceOwnerFrame();current=true;
        const auto actual=SpreadBits(weapon.Spread());for(unsigned i=0;i<5;++i)Check(actual[i]==values[i+16]);
        ++spreadCount;
    }
    Check(spreadCount==640);
    std::cout<<"870 actual SDK lifecycle "<<checks<<" checks; original fidget "<<rowCount
        <<" cases; original player-frame spread "<<spreadCount<<" cases passed\n";
}
