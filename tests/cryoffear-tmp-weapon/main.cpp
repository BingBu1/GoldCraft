// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/tmp_weapon.hpp"
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
TMPOwnerFrame frame{};
bool current=true;
int cancel=-1, checks{}, meleeCalls{}, automaticFires{}, automaticStops{}, automaticTails{};
bool failEffect{}, observeReload{}, transitionMode{};
bool traceDeploy{};
std::vector<unsigned> deployOrder;
std::vector<std::pair<int,int>> deployRandom;
bool fidgetMode{};
unsigned fidgetChance{},fidgetChoice{};
std::vector<unsigned> fidgetOrder;
int shotLatch{};
float shotLatchDeadline{};
TMPWeapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"TMP SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,TMPOwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const TMPEvent& e) noexcept
{
    if(transitionMode)
    {
        if(e.kind==TMPEffect::Aim)Event(21,e.enabled);
        else if(e.kind==TMPEffect::ViewAnimation)Event(3,e.value);
        return true;
    }
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    if(fidgetMode)fidgetOrder.push_back(kind);
    switch(e.kind)
    {
    case TMPEffect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case TMPEffect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case TMPEffect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case TMPEffect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case TMPEffect::ViewAnimation:Event(kind,e.value);break;
    case TMPEffect::ShotState:Event(kind,Bits(e.a));break;
    case TMPEffect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case TMPEffect::InventoryChanged:
        if(observeReload)Check(item.m_iClip==30&&item.m_fInReload&&item.m_pPlayer->m_rgAmmo[item.m_iPrimaryAmmoType]==10);
        if(fidgetMode)Event(kind);
        break;
    case TMPEffect::ReserveHud:if(fidgetMode)Event(kind,e.value);break;
    case TMPEffect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==0&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case TMPEffect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case TMPEffect::FirstDraw:frame.firstDrawn=true;break;
    case TMPEffect::KillLocalParticles:Check(e.enabled);Event(kind,1u);break;
    case TMPEffect::AutomaticSound:
        if(e.sound.flags==32)++automaticStops;
        else if(e.sound.sample==AutomaticSoundSample::Tail)++automaticTails;
        else ++automaticFires;
        Event(kind,unsigned(e.sound.sample),unsigned(e.sound.channel),unsigned(e.sound.flags),unsigned(e.sound.pitch),Bits(e.sound.volume),Bits(e.sound.attenuation));break;
    case TMPEffect::EmptyClick:Event(kind);break;
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
        Check(amount>=0&&!std::strcmp(name,"cof_tmp")&&(maximum==90||maximum==300));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
TMPWeapon weapon;
PlayerAnimationAdapter animations;
TMPPrepared prepared;
TMPBackends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,TMPWeapon&,TMPPrepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,TMPBackends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,TMPWeapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,TMPWeapon&,TMPPrepared&) noexcept{return false;}
bool CancelColdPrepare(void*,TMPWeapon&,TMPPrepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;cancel=-1;failEffect=observeReload=fidgetMode=transitionMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;frame.meleeDamage=11;
    frame.firstDrawn=true;frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    automaticFires=automaticStops=automaticTails=0;
    frame.reserveLimit=300;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/tmp/v_tmp.mdl\0models/guest/cof/tmp/p_tmp.mdl\0models/guest/cof/tmp/w_tmp.mdl\0inventoryitems/weapons/weapon_tmp.txt\0weapon_cof_tmp\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="cof_tmp";
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
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::TMP,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},LongGun::TMP,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<16)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.melee={78,true,Recoil,nullptr,OriginalPistolMeleeSamples};
    prepared.aimOffset={1,2,3};prepared.aimZoom=55;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=0;prepared.hostPosition=0;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.automaticFireReady=prepared.automaticTailReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/tmp/v_tmp.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    backends={nullptr,Current,OwnerFrame,Effect,TMPRequiredEffects,&animations,{1,1},17};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=40;weapon.m_iClip=30;
    Check(weapon.Bind(player,prepared,backends)==TMPStatus::Ready);
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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==900);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==900);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==900);return 0;}

int main(int argc,char** argv)
{
    if(argc!=8)return 1;
    std::ifstream file(argv[3],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
    unsigned rows{};
    while(cases>>std::ws&&cases.peek()!=std::char_traits<char>::eof())
    {
        std::array<unsigned,12> row{};for(auto& value:row)if(!(cases>>std::hex>>value))return 3;
        Initialize();events.clear();globals.time=Float(row[0]);weapon.m_iClip=int(row[1]);player.m_rgAmmo[1]=0;
        auto& state=const_cast<MagazineActionState&>(weapon.ActionState());
        state.primary=Float(row[2]);state.secondary=state.idle=state.ownerAttack=0;
        state.action=int(row[3]);state.aiming=row[7]!=0;
        frame.pressed=row[4];player.pev->waterlevel=int(row[5]);weapon.m_fFireOnEmpty=int(row[6]);
        frame.infiniteAmmo=row[8]!=0;frame.originalFlags=row[9];
        auto& spread=const_cast<SpreadState&>(weapon.Spread());spread.scalar=Float(row[10]);spread.recoveryDeadline=Float(row[11]);spread.vector={.01F,.01F,.01F};
        globals.v_forward={1,2,3};globals.v_up={4,5,6};globals.v_right={7,8,9};
        for(auto* value:{weapon.pev,player.pev})
        {value->origin={11,12,13};value->view_ofs={1,2,3};value->velocity={4,5,6};value->v_angle={1,2,3};value->punchangle={.1F,.2F,.3F};}
        weapon.PrimaryAttack();
        std::vector<unsigned> actual{unsigned(weapon.m_iClip),Bits(state.primary),Bits(state.secondary),Bits(state.idle),0,
            Bits(spread.scalar),Bits(spread.recoveryDeadline),unsigned(shotLatch),Bits(shotLatchDeadline)};
        actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(expected,line))return 4;
        std::istringstream input(line);std::vector<unsigned> wanted;unsigned word;while(input>>std::hex>>word)wanted.push_back(word);
        if(wanted.size()<9||!input.eof())return 4;
        wanted[4]=0; // Private click deadline: public strict-gate cases below.
        if(actual!=wanted)
        {
            std::cerr<<"TMP original Primary mismatch row "<<rows<<" actual/wanted "<<actual.size()<<'/'<<wanted.size()<<'\n';
            for(const auto value:actual)std::cerr<<std::hex<<value<<' ';std::cerr<<'\n';
            for(const auto value:wanted)std::cerr<<std::hex<<value<<' ';std::cerr<<'\n';return 5;
        }
        ++rows;
    }
    if(!rows)return 6;if(expected>>std::ws&&expected.peek()!=std::char_traits<char>::eof())return 7;
    std::ifstream worldCases(argv[4]),worldExpected(argv[5]);if(!worldCases||!worldExpected)return 8;
    unsigned worldRows{};
    while(worldCases>>std::ws&&worldCases.peek()!=std::char_traits<char>::eof())
    {
        std::array<unsigned,6> row{};for(auto& value:row)if(!(worldCases>>std::hex>>value))return 9;
        Initialize();events.clear();weapon.m_pEntity=&itemWrapper;itemWrapper.m_pContainingEntity=&weapon;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetModel=[](edict_t*,const char*){Event(101);};
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float* value){Event(102,Bits(value[0]),Bits(value[1]),Bits(value[2]));};
        g_engfuncs.pfnSetSize=[](edict_t*,const float* a,const float* b){Event(103,Bits(a[0]),Bits(a[1]),Bits(a[2]),Bits(b[0]),Bits(b[1]),Bits(b[2]));};
        weapon.pev->origin={11,12,13};weapon.pev->health=Float(row[1]);weapon.pev->iuser4=int(row[2]);globals.time=Float(row[0]);
        weapon.Spawn();Check(weapon.ResourcesReady()&&weapon.Status()==TMPStatus::Applied&&itemWrapper.m_ItemInfo.iId==900);
        weapon.pev->flags=row[3]?FL_ONGROUND:0;weapon.pev->effects=row[4]?EF_NODRAW:0;
        weapon.pev->angles.x=17;weapon.pev->angles.z=19;weapon.WorldFallThink();
        if(row[5]==1)weapon.Drop();else if(row[5]==2)weapon.Kill();
        const unsigned think=row[5]?2:row[3]?0:1;
        std::vector<unsigned> actual{unsigned(weapon.m_iDefaultAmmo),unsigned(weapon.m_iClip),unsigned(weapon.pev->movetype),
            unsigned(weapon.pev->solid),unsigned(weapon.pev->renderfx),Bits(weapon.pev->angles.x),Bits(weapon.pev->angles.z),
            unsigned(weapon.pev->effects),Bits(weapon.pev->nextthink),weapon.m_pfnUse!=nullptr,weapon.m_pfnTouch!=nullptr,think,
            unsigned(weapon.ObjectCaps()&FCAP_IMPULSE_USE)};
        Check((weapon.m_pfnThink!=nullptr)==(think!=0));actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(worldExpected,line))return 10;std::istringstream input(line);
        std::vector<unsigned> wanted;unsigned word;while(input>>std::hex>>word)wanted.push_back(word);
        if(!input.eof()||actual!=wanted){std::cerr<<"TMP original world mismatch row "<<worldRows<<'\n';return 11;}
        ++worldRows;
    }
    Check(worldRows==180);if(worldExpected>>std::ws&&worldExpected.peek()!=std::char_traits<char>::eof())return 12;
    std::ifstream transitionCases(argv[6]),transitionExpected(argv[7]);if(!transitionCases||!transitionExpected)return 13;
    unsigned transitionRows{};
    while(transitionCases>>std::ws&&transitionCases.peek()!=std::char_traits<char>::eof())
    {
        std::array<unsigned,15> row{};for(auto& word:row)if(!(transitionCases>>std::hex>>word))return 14;
        Initialize();const unsigned op=row[0];globals.time=Float(row[1]);weapon.m_iClip=int(row[2]);player.m_rgAmmo[1]=0;
        auto& state=const_cast<MagazineActionState&>(weapon.ActionState());
        state.action=int(row[3]);state.aiming=row[4]!=0;state.primary=Float(row[8]);state.secondary=Float(row[9]);
        state.extra=Float(row[10]);state.idle=0;state.ownerAttack=Float(row[11]);state.pendingReload=row[12]!=0;
        frame.originalFlags=row[5];player.pev->velocity={Float(row[6]),0,0};frame.condition3AA=row[7]!=0;
        frame.pendingGate=row[13]!=0;frame.pendingGateTime=Float(row[14]);frame.toggleAim=true;
        if(op==1||op==2){frame.pressed=0x40;player.pev->button=0x40;}
        events.clear();transitionMode=true;
        if(op==3)weapon.Holster();else weapon.ItemPostFrame();
        transitionMode=false;
        std::vector<unsigned> actual{unsigned(state.action),state.aiming,Bits(state.primary),Bits(state.secondary),Bits(state.extra),
            Bits(state.idle),Bits(state.ownerAttack),state.pendingReload};actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(transitionExpected,line))return 15;std::istringstream input(line);
        std::vector<unsigned> wanted;unsigned word;while(input>>std::hex>>word)wanted.push_back(word);
        if(!input.eof()||actual!=wanted)
        {
            std::cerr<<"TMP original transition mismatch row "<<transitionRows<<" op "<<op<<'\n';
            for(auto value:actual)std::cerr<<std::hex<<value<<' ';std::cerr<<'\n';for(auto value:wanted)std::cerr<<std::hex<<value<<' ';std::cerr<<'\n';return 16;
        }
        ++transitionRows;
    }
    Check(transitionRows==90);if(transitionExpected>>std::ws&&transitionExpected.peek()!=std::char_traits<char>::eof())return 17;
    const auto owned=[] {
        Initialize();weapon.m_pEntity=&itemWrapper;itemWrapper.m_pContainingEntity=&weapon;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});weapon.Precache();
        Check(weapon.Bind(player,prepared,backends)==TMPStatus::Ready);weapon.AttachToPlayer(&player);
        Check(weapon.pev->owner==player.edict()&&weapon.Deploy());globals.time=10;
    };
    const auto rebindModel=[] {
        ++player.pev->modelindex;
        const PlayerModelGeneration generation{1,unsigned(player.pev->modelindex)};
        const auto plan=RecommendedLongGunAnimationPlan(LongGun::TMP,PistolAnimationRig::Stock);
        std::string error;Check(plan&&animations.Prepare(player,model,generation,LongGun::TMP,*plan,_stricmp,error));
        backends.playerModelGeneration=generation;
    };
    owned();weapon.m_iClip=20;weapon.Reload();const float reloadDue=weapon.ActionState().ownerAttack;
    Check(weapon.m_fInReload&&!weapon.m_iClip);
    weapon.OwnerModelChanging();rebindModel();const auto ammoBefore=player.m_rgAmmo[1];
    g_engfuncs.pfnRandomLong=[](int,int){std::abort();return 0;};
    Check(weapon.RestoreInventoryBinding(player)&&weapon.ActionState().ownerAttack==reloadDue&&
        weapon.m_fInReload&&!weapon.m_iClip&&player.m_rgAmmo[1]==ammoBefore);
    globals.time=reloadDue;weapon.ItemPostFrame();Check(weapon.m_iClip==30&&!weapon.m_fInReload);
    owned();const int meleeBefore=meleeCalls;weapon.SecondaryAttack();const float due=weapon.pev->nextthink;
    weapon.OwnerModelChanging();Check(!weapon.m_pfnThink);rebindModel();
    Check(weapon.RestoreInventoryBinding(player)&&weapon.m_pfnThink&&weapon.pev->nextthink==due);
    globals.time=due;weapon.DelayedMelee();Check(meleeCalls==meleeBefore+1&&!weapon.m_pfnThink);
    owned();weapon.SecondaryAttack();weapon.OwnerModelChanging();rebindModel();++backends.ownerLife;
    Check(!weapon.RestoreInventoryBinding(player)&&!weapon.m_pfnThink);
    owned();weapon.PrimaryAttack();goldcraft::inventory::Snapshot snapshot;
    Check(weapon.SaveInventorySnapshot(snapshot));const auto gate=weapon.ActionState().primary;
    weapon.Holster();Check(weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.m_iClip==29&&weapon.ActionState().primary==gate);
    auto wrong=snapshot;++wrong.kind;Check(!weapon.RestoreInventorySnapshot(wrong)&&weapon.m_iClip==29);
    wrong=snapshot;--wrong.bytes;Check(!weapon.RestoreInventorySnapshot(wrong)&&weapon.m_iClip==29);
    for(unsigned bit=0;bit<unsigned(TMPEffect::Count);++bit)
    {
        if(!(TMPRequiredEffects&(1u<<bit)))continue;
        Initialize();auto missing=backends;missing.preparedEffects&=~(1u<<bit);
        Check(weapon.Bind(player,prepared,missing)==TMPStatus::MissingBackend);
        events.clear();weapon.PrimaryAttack();Check(events.empty());
    }
    for(unsigned sequence=0;sequence<16;++sequence)
    {
        Initialize();auto missing=prepared;missing.sequences.present&=~(std::uint64_t{1}<<sequence);
        Check(weapon.Prepare(missing)==TMPStatus::InvalidInput&&!weapon.ResourcesReady());
    }
    ItemInfo info{};Check(weapon.GetItemInfo(&info)&&info.iId==900&&info.iMaxClip==30&&info.iMaxAmmo1==300&&
        weapon.iItemSlot()==PRIMARY_WEAPON_SLOT&&!weapon.IsPistol());
    Initialize();globals.time=10;frame.pressed=0;weapon.PrimaryAttack();Check(weapon.m_iClip==29);
    globals.time=weapon.m_flNextPrimaryAttack;weapon.PrimaryAttack();Check(weapon.m_iClip==28);
    Check(automaticFires==1&&!automaticStops&&!automaticTails);
    globals.time+=2;player.pev->button=0;weapon.ItemPostFrame();
    Check(automaticFires==1&&automaticStops==1&&automaticTails==1);
    Initialize();globals.time=10;weapon.m_iClip=75;weapon.PrimaryAttack();Check(weapon.m_iClip==74);
    weapon.Reload();Check(weapon.m_iClip==74&&!weapon.m_fInReload);
    Initialize();globals.time=10;weapon.m_iClip=20;weapon.Reload();Check(!weapon.m_iClip&&weapon.m_fInReload);
    globals.time=weapon.ActionState().ownerAttack;weapon.ItemPostFrame();Check(weapon.m_iClip==30&&!weapon.m_fInReload);
    Initialize();globals.time=10;const int beforeHolsterMelee=meleeCalls;
    weapon.SecondaryAttack();Check(weapon.m_pfnThink&&Bits(weapon.pev->nextthink)==Bits(10.F+.3F));
    weapon.Holster();Check(!weapon.m_pfnThink);weapon.DelayedMelee();Check(meleeCalls==beforeHolsterMelee);
    Initialize();globals.time=10;frame.pressed=0x40;player.pev->button=0x40;weapon.ItemPostFrame();
    Check(weapon.ActionState().aiming&&player.pev->fov==55);
    std::cout<<"TMP actual SDK core: "<<rows<<" original Primary / "<<worldRows<<" world / "<<transitionRows
        <<" transitions; "<<checks<<" checks, zero failures; external consumers controlled\n";
}
