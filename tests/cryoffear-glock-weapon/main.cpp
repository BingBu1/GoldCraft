// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/glock_weapon.hpp"
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
GlockOwnerFrame frame{};
bool current=true;
int cancel=-1, checks{}, meleeCalls{};
bool failEffect{}, observeReload{};
bool traceDeploy{}, transitionMode{};
int transitionRandomFirst=2,transitionRandomSecond=2;
std::vector<unsigned> deployOrder;
std::vector<std::pair<int,int>> deployRandom;
int shotLatch{};
float shotLatchDeadline{};
GlockWeapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"Glock SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,GlockOwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const GlockEvent& e) noexcept
{
    if(transitionMode)
    {
        if(e.kind==GlockEffect::LightState)frame.lightOn=e.enabled;
        else if(e.kind==GlockEffect::ViewAnimation||e.kind==GlockEffect::Body||e.kind==GlockEffect::Tutorial||e.kind==GlockEffect::AttachmentSound)
            Event(unsigned(e.kind),e.value);
        if(cancel==static_cast<int>(e.kind))current=false;
        return !failEffect;
    }
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    switch(e.kind)
    {
    case GlockEffect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case GlockEffect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case GlockEffect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case GlockEffect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case GlockEffect::ViewAnimation:Event(kind,e.value);break;
    case GlockEffect::ShotState:Event(kind,Bits(e.a));break;
    case GlockEffect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case GlockEffect::FirstDraw:frame.firstDrawn=true;break;
    case GlockEffect::LightState:frame.lightOn=e.enabled;break;
    case GlockEffect::InventoryChanged:
        if(observeReload)Check(item.m_iClip==15&&item.m_fInReload&&item.m_pPlayer->m_rgAmmo[item.m_iPrimaryAmmoType]==25);
        break;
    case GlockEffect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==0&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case GlockEffect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case GlockEffect::EmptyClick:case GlockEffect::FireSound:Event(kind);break;
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
#if defined(COF_RUNTIME_P345_FIXTURE)
        if(!std::strcmp(name,"cof_vp70"))
        {
            Check(amount>=0&&(maximum==54||maximum==180));
            const int add=std::min(amount,maximum-m_rgAmmo[4]);if(add<0)return -1;
            m_rgAmmo[4]+=add;Event(104,amount,maximum);return 4;
        }
        if(!std::strcmp(name,"cof_revolver"))
        {
            Check(amount>=0&&(maximum==15||maximum==50));
            const int add=std::min(amount,maximum-m_rgAmmo[3]);if(add<0)return -1;
            m_rgAmmo[3]+=add;Event(104,amount,maximum);return 3;
        }
        if(!std::strcmp(name,"cof_p345"))
        {
            Check(amount>=0&&(maximum==24||maximum==80));
            const int add=std::min(amount,maximum-m_rgAmmo[2]);if(add<0)return -1;
            m_rgAmmo[2]+=add;Event(104,amount,maximum);return 2;
        }
#endif
        Check(amount>=0&&!std::strcmp(name,"cof_glock")&&(maximum==45||maximum==150));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
GlockWeapon weapon;
PlayerAnimationAdapter animations;
GlockPrepared prepared;
GlockBackends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,GlockWeapon&,GlockPrepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,GlockBackends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,GlockWeapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,GlockWeapon&,GlockPrepared&) noexcept{return false;}
bool CancelColdPrepare(void*,GlockWeapon&,GlockPrepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;cancel=-1;failEffect=observeReload=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;frame.meleeDamage=11;
    frame.firstDrawn=true;frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=150;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/glock/v_glock.mdl\0models/guest/cof/glock/p_glock.mdl\0models/guest/cof/glock/w_glock.mdl\0inventoryitems/weapons/weapon_glock.txt\0weapon_cof_glock\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="cof_glock";
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
    const auto plan=RecommendedPistolAnimationPlan(Pistol::Glock,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},Pistol::Glock,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<43)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.melee={78,true,Recoil,nullptr,OriginalPistolMeleeSamples};
    prepared.aimOffset={1,2,3};prepared.aimZoom=55;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=1;prepared.hostPosition=0;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=prepared.taclightSoundsReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/glock/v_glock.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    backends={nullptr,Current,OwnerFrame,Effect,GlockRequiredEffects,&animations,{1,1}};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=40;weapon.m_iClip=15;
    Check(weapon.Bind(player,prepared,backends)==GlockStatus::Ready);
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
BallisticsResult FireCSPistolBallistics(CBaseEntity&,const PistolBallistics&,
    const BallisticsEffects&,const BallisticsCall&,int) noexcept
{std::abort();} // This shared Glock fixture never opts into Revolver's host rule.
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
constexpr bool FixtureGuestId(int id) noexcept
{
#if defined(COF_RUNTIME_P345_FIXTURE)
    return id==896||id==897||id==898||id==899;
#else
    return id==896;
#endif
}
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&FixtureGuestId(id));Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(FixtureGuestId(id)||id==WEAPON_USP);return FixtureGuestId(id);}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(FixtureGuestId(id));return 0;}

void InventoryRestoreCases()
{
    const auto prepareOwned=[] {
        Initialize();
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        weapon.Precache();
        Check(weapon.Bind(player,prepared,backends)==GlockStatus::Ready);
        weapon.AttachToPlayer(&player);Check(weapon.pev->owner==player.edict());
        Check(weapon.Deploy());globals.time=5;
    };
    prepareOwned();frame.pressed=1;weapon.PrimaryAttack();
    Check(weapon.m_iClip==14);
    const float deadline=weapon.m_flNextPrimaryAttack;
    goldcraft::inventory::Snapshot snapshot;
    Check(weapon.SaveInventorySnapshot(snapshot));
    weapon.BeforeWeaponDrop();Check(weapon.Status()==GlockStatus::Cancelled);
    Check(weapon.RestoreInventoryBinding(player));Check(weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.m_iClip==14&&weapon.m_flNextPrimaryAttack==deadline);
    events.clear();globals.time=5.001F;weapon.PrimaryAttack();
    Check(events.empty()&&weapon.m_iClip==14);
    globals.time=deadline+.001F;weapon.PrimaryAttack();Check(weapon.m_iClip==13);
    auto invalid=snapshot;invalid.kind=0;Check(!weapon.RestoreInventorySnapshot(invalid));
    invalid=snapshot;--invalid.bytes;Check(!weapon.RestoreInventorySnapshot(invalid));
    ++ownerEdict.serialnumber;Check(!weapon.RestoreInventorySnapshot(snapshot));--ownerEdict.serialnumber;
    prepareOwned();weapon.SecondaryAttack();
    const float pending=weapon.pev->nextthink;Check(pending>globals.time);
    Check(weapon.SaveInventorySnapshot(snapshot));weapon.BeforeWeaponDrop();Check(weapon.pev->nextthink==0);
    Check(weapon.RestoreInventoryBinding(player)&&weapon.RestoreInventorySnapshot(snapshot));
    Check(weapon.pev->nextthink==pending&&weapon.m_pfnThink!=nullptr);
    const auto before=meleeCalls;globals.time=pending+.001F;weapon.Think();
    Check(meleeCalls==before+1&&weapon.m_pfnThink==nullptr);
}

int main(int argc,char** argv)
{
    if(argc!=8)return 1;
    std::ifstream file(argv[3],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
    unsigned rows{};
    while(cases>>std::ws&&cases.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,12> row{};for(auto& v:row)if(!(cases>>std::hex>>v))return 3;
        Initialize();events.clear();
        globals.time=Float(row[0]);weapon.m_iClip=row[1];player.m_rgAmmo[1]=0;
        auto& state=const_cast<MagazineActionState&>(weapon.ActionState());
        state.primary=Float(row[2]);state.secondary=state.idle=0;state.ownerAttack=0;
        state.action=row[3];state.aiming=row[7]!=0;
        frame.pressed=row[4];player.pev->waterlevel=row[5];weapon.m_fFireOnEmpty=row[6];
        frame.infiniteAmmo=row[8]!=0;frame.originalFlags=row[9];
        auto& spread=const_cast<SpreadState&>(weapon.Spread());spread.scalar=Float(row[10]);spread.recoveryDeadline=Float(row[11]);spread.vector={.01f,.01f,.01f};
        globals.v_forward={1,2,3};globals.v_up={4,5,6};globals.v_right={7,8,9};
        for(auto* v:{weapon.pev,player.pev}){v->origin={11,12,13};v->view_ofs={1,2,3};v->velocity={4,5,6};v->v_angle={1,2,3};v->punchangle={.1f,.2f,.3f};}
        weapon.PrimaryAttack();
        std::vector<std::uint32_t> actual{static_cast<unsigned>(weapon.m_iClip),Bits(state.primary),Bits(state.secondary),Bits(state.idle),0,Bits(spread.scalar),Bits(spread.recoveryDeadline),static_cast<unsigned>(shotLatch),Bits(shotLatchDeadline)};
        // Empty-click deadline is private; its externally visible strict gate is
        // tested below. Original row retains that field, remove before compare.
        actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(expected,line))return 4;std::istringstream values(line);std::vector<unsigned> wanted;unsigned value;
        while(values>>std::hex>>value)wanted.push_back(value);
        if(wanted.size()<7)return 4;wanted[4]=0;
        if(actual!=wanted)
        {std::cerr<<"Original primary mismatch row "<<rows<<" actual/wanted "<<actual.size()<<'/'<<wanted.size()<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 5;}
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
        Check(weapon.ResourcesReady()&&weapon.Status()==GlockStatus::Applied&&itemWrapper.m_ItemInfo.iId==896);
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
        if(actual!=wanted){std::cerr<<"Original world mismatch row "<<worldRows<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 10;}
        ++worldRows;
    }
    if(worldRows!=180)return 11;
    std::ifstream transitionCases(argv[6]),transitionExpected(argv[7]);if(!transitionCases||!transitionExpected)return 12;
    unsigned transitionRows{};
    while(transitionCases>>std::ws&&transitionCases.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,19> row{};for(auto& v:row)if(!(transitionCases>>std::hex>>v))return 13;
        Initialize();
        const unsigned op=row[0];
        if(row[13]||op==1||op==2||op==4)
        {
            globals.time=10;Check(weapon.RequestTaclightAttachment()==GlockStatus::Applied);weapon.ItemPostFrame();
            if(op!=1){globals.time=weapon.pev->nextthink;weapon.MidAttachTaclight();}
            if(op!=1&&op!=2){globals.time=weapon.pev->nextthink;weapon.AttachTaclight();}
            if(op==4){globals.time=20;weapon.WeaponFunction();}
        }
        else if(row[14])Check(weapon.RequestTaclightAttachment()==GlockStatus::Applied);
        globals.time=Float(row[1]);weapon.m_iClip=row[2];player.m_rgAmmo[1]=0;
        auto& s=const_cast<MagazineActionState&>(weapon.ActionState());
        s.action=row[3];s.aiming=row[4]!=0;s.primary=Float(row[8]);s.secondary=Float(row[9]);s.extra=Float(row[10]);s.idle=0;s.ownerAttack=Float(row[11]);
        frame.originalFlags=row[5];player.pev->velocity={Float(row[6]),0,0};frame.condition3AA=row[7]!=0;
        frame.lightOn=row[12]!=0;frame.pendingGate=row[15]!=0;frame.pendingGateTime=Float(row[16]);frame.toggleAim=true;
        weapon.pev->body=0;weapon.pev->nextthink=0;events.clear();transitionMode=true;
        transitionRandomFirst=row[17];transitionRandomSecond=row[18];
        g_engfuncs.pfnRandomLong=[](int,int high){return high==599?transitionRandomFirst:transitionRandomSecond;};
        if(op==0)weapon.ItemPostFrame();else if(op==1)weapon.MidAttachTaclight();else if(op==2)weapon.AttachTaclight();
        else if(op==3)weapon.WeaponFunction();else if(op==4)weapon.ToggleSwitch();else weapon.Holster();
        transitionMode=false;
        std::vector<std::uint32_t> actual{unsigned(s.action),s.aiming,Bits(s.primary),Bits(s.secondary),Bits(s.extra),Bits(s.idle),
            Bits(s.ownerAttack),unsigned(weapon.pev->body),frame.lightOn,weapon.TaclightInstalled()};
        actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(transitionExpected,line))return 14;std::istringstream values(line);std::vector<unsigned> wanted;unsigned value;
        while(values>>std::hex>>value)wanted.push_back(value);
        if(actual!=wanted)
        {std::cerr<<"Original transition mismatch row "<<transitionRows<<" op "<<op<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 15;}
        ++transitionRows;
    }
    if(transitionRows!=105)return 16;
    // Real scheduled stages: no private deadline or installed-bit injection.
    // Before/equal callback checks preserve engine deadline semantics.
    for(int clip:{0,15})
    {
        Initialize();weapon.m_iClip=clip;globals.time=10;transitionMode=true;
        Check(weapon.RequestTaclightAttachment()==GlockStatus::Applied);weapon.ItemPostFrame();
        Check(!weapon.TaclightInstalled()&&Bits(weapon.pev->nextthink)==Bits(11)&&weapon.m_pfnThink!=nullptr);
        events.clear();globals.time=std::nextafter(11.0f,-INFINITY);weapon.MidAttachTaclight();
        Check(events.empty()&&Bits(weapon.pev->nextthink)==Bits(11));
        globals.time=11;weapon.MidAttachTaclight();
        Check((events==std::vector<unsigned>{unsigned(GlockEffect::AttachmentSound),1,1})&&Bits(weapon.pev->nextthink)==Bits(12));
        events.clear();globals.time=std::nextafter(12.0f,-INFINITY);weapon.AttachTaclight();Check(events.empty()&&!weapon.TaclightInstalled());
        globals.time=12;weapon.AttachTaclight();
        Check(weapon.TaclightInstalled()&&weapon.pev->body==1&&weapon.m_pfnThink==nullptr&&weapon.pev->nextthink==0&&
            Bits(weapon.ActionState().primary)==Bits(13));
        globals.time=13;weapon.WeaponFunction();const float switchAt=weapon.pev->nextthink;
        Check(Bits(switchAt)==Bits(13.0f+.3f)&&weapon.m_pfnThink!=nullptr&&!frame.lightOn);
        globals.time=std::nextafter(switchAt,-INFINITY);weapon.ToggleSwitch();Check(!frame.lightOn&&weapon.m_pfnThink!=nullptr);
        globals.time=switchAt;weapon.ToggleSwitch();Check(frame.lightOn&&weapon.m_pfnThink==nullptr&&weapon.pev->nextthink==0);
        globals.time=14;weapon.ItemPostFrame();Check(weapon.pev->body==2);
        weapon.Holster();Check(!frame.lightOn&&weapon.m_pfnThink==nullptr);
        globals.time=20;weapon.ToggleSwitch();Check(!frame.lightOn);
        transitionMode=false;
    }
    for(int edge:{0,1,2})
    {
        Initialize();globals.time=10;transitionMode=true;
        Check(weapon.RequestTaclightAttachment()==GlockStatus::Applied);weapon.ItemPostFrame();
        if(edge==0)weapon.Holster();
        else if(edge==1)++ownerEdict.serialnumber;
        else player.pev->deadflag=DEAD_DEAD;
        globals.time=11;events.clear();weapon.MidAttachTaclight();weapon.AttachTaclight();weapon.ToggleSwitch();
        Check(events.empty()&&!weapon.TaclightInstalled()&&!frame.lightOn);transitionMode=false;
    }
    Initialize();globals.time=10;transitionMode=true;
    Check(weapon.RequestTaclightAttachment()==GlockStatus::Applied);weapon.ItemPostFrame();
    cancel=int(GlockEffect::AttachmentSound);globals.time=11;events.clear();weapon.MidAttachTaclight();
    Check(!current&&!weapon.TaclightInstalled()&&Bits(weapon.pev->nextthink)==Bits(11));
    globals.time=12;weapon.AttachTaclight();Check(!weapon.TaclightInstalled());transitionMode=false;
    // Truly ownerless metadata is available before Prepare/Bind and remains
    // available after generation retirement; it cannot make Spawn usable.
    {
        GlockWeapon ownerless;ItemInfo info{};
        Check(ownerless.GetItemInfo(&info)&&info.iId==896&&ownerless.m_iId==896&&
            info.iSlot==GlockHudSlot&&info.iPosition==GlockHudPosition&&info.pszAmmo2==nullptr&&
            !std::strcmp(info.pszName,"weapon_cof_glock")&&!std::strcmp(info.pszAmmo1,"cof_glock"));
        Check(!ownerless.ResourcesReady()&&!ownerless.GetItemInfo(nullptr));
    }
    // Melee samples are mandatory at the reachable melee operation, while
    // an ownerless world item and ordinary deployment do not consume them.
    for(int mutation=0;mutation<17;++mutation)
    {
        Initialize();auto p=prepared;
        if(mutation==16)p.melee.samples={};
        else p.melee.samples[mutation%8]=mutation<8?nullptr:"";
        Check(p.melee.soundsReady&&!ValidPistolMeleeSamples(p.melee.samples));
        Check(weapon.Bind(player,p,backends)==GlockStatus::Ready&&weapon.Deploy());
        globals.time=10;events.clear();weapon.SecondaryAttack();
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&
            weapon.m_pfnThink==nullptr&&weapon.m_iClip==15);
    }
    for(int mutation=0;mutation<6;++mutation)
    {
        Initialize();weapon.m_pEntity=&itemWrapper;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        switch(mutation){case 0:prepared.worldModelString=0;break;case 1:prepared.inventoryItemString=0;break;
        case 2:prepared.inventoryReady=false;break;case 3:prepared.hostSlot=0;break;
        case 4:prepared.hostPosition=2;break;case 5:prepared.modelsReady=false;break;}
        weapon.Spawn();Check(!weapon.ResourcesReady()&&weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr);
        Check(weapon.AddToPlayer(&player)==FALSE);
    }
    // Conditional operation resources never masquerade as ready: they admit
    // the world item only and reject their actual operation before mutation.
    for(int missing=0;missing<7;++missing)
    {
        Initialize();auto p=prepared;
        switch(missing){case 0:p.particleReady=false;break;case 1:p.aimMetadataReady=false;break;
        case 2:p.taclightSoundsReady=false;break;case 3:p.melee.recoil=nullptr;break;
        case 4:p.deployPreludeReady=false;break;case 5:p.soundsReady=false;break;
        case 6:p.sequences.present=0;break;}
        Check(weapon.Prepare(p)==GlockStatus::Ready);
        Check(weapon.Bind(player,p,backends)==GlockStatus::Ready);
        if(missing==5)weapon.pev->armorvalue=1; // Original suppresses the sleeve sound.
        events.clear();deployOrder.clear();deployRandom.clear();traceDeploy=true;
        const bool deployed=weapon.Deploy()!=FALSE;traceDeploy=false;
        if(missing==4||missing==6)
        {Check(!deployed&&deployOrder.empty()&&deployRandom.empty());continue;}
        Check(deployed);globals.time=10;events.clear();
        if(missing==0||missing==5){frame.pressed=1;weapon.PrimaryAttack();}
        else if(missing==1){frame.pressed=0x40;player.pev->button=0x40;weapon.ItemPostFrame();}
        else if(missing==2)Check(weapon.RequestTaclightAttachment()==GlockStatus::MissingBackend);
        else weapon.SecondaryAttack();
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==15&&
            !weapon.ActionState().aiming&&weapon.m_pfnThink==nullptr);
    }
    Initialize();weapon.m_pEntity=&itemWrapper;
    prepared.particleReady=prepared.aimMetadataReady=prepared.taclightSoundsReady=false;
    prepared.melee={};prepared.ballistics={};prepared.sequences.present=0;
    weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
    g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
    g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
    weapon.Spawn();Check(weapon.ResourcesReady()&&weapon.m_pfnThink!=nullptr&&weapon.m_pfnUse!=nullptr);
    // This world-ready item still has no view sequences or combat backend.
    Check(weapon.AddToPlayer(&player));events.clear();Check(!weapon.Deploy()&&events.empty());
    for(auto prepare:{MissingColdPrepare,CancelColdPrepare})
    {
        Initialize();weapon.m_pEntity=&itemWrapper;
        weapon.ConfigureColdHost({nullptr,Current,prepare,ColdOwner,ColdInventory});weapon.Spawn();
        Check(!weapon.ResourcesReady()&&weapon.m_pfnThink==nullptr&&weapon.m_pfnUse==nullptr);
    }
    Initialize();weapon.BeforeWeaponDrop();events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.Status()!=GlockStatus::Applied);
    // Actual SDK AddToPlayer/AddWeapon composition and real CCSPlayerItem
    // storage, with controlled GiveAmmo and reliable-message boundaries.
    for(int limit:{45,150})for(int health:{-2,0,20})
    {
        Initialize();weapon.m_pEntity=&itemWrapper;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        weapon.pev->health=float(health);weapon.Spawn();player.m_rgAmmo[1]=0;frame.reserveLimit=limit;
        events.clear();Check(weapon.AddToPlayer(&player));
        Check(weapon.m_pPlayer==&player&&weapon.m_iPrimaryAmmoType==1&&weapon.m_iSecondaryAmmoType==-1&&player.pev->weapons==0);
        if(health==-2)Check(weapon.m_iClip==-666&&weapon.m_iDefaultAmmo==15&&player.m_rgAmmo[1]==0);
        else Check(weapon.m_iClip==15&&weapon.m_iDefaultAmmo==0&&player.m_rgAmmo[1]==(health==20?5:0));
        weapon.AttachToPlayer(&player);
        Check(weapon.pev->movetype==MOVETYPE_FOLLOW&&weapon.pev->solid==SOLID_NOT&&weapon.pev->effects==EF_NODRAW&&
            weapon.pev->aiment==player.edict()&&weapon.pev->owner==player.edict()&&weapon.pev->modelindex==0&&int(weapon.pev->model)==0&&
            weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr&&!(weapon.ObjectCaps()&FCAP_IMPULSE_USE));
    }
    // Original DefaultDeploy's observable ordering, loaded/empty sequence and
    // pitch RNG consumption even when armorvalue suppresses the sleeve sound.
    for(int armor:{0,1})for(int clip:{0,15})
    {
        Initialize();weapon.pev->armorvalue=float(armor);weapon.m_iClip=clip;
        player.m_rgAmmo[1]=24;deployOrder.clear();deployRandom.clear();traceDeploy=true;
        Check(weapon.Deploy());traceDeploy=false;
        std::vector<unsigned> expected{unsigned(GlockEffect::DeployPrelude),unsigned(GlockEffect::DeployReset),
            unsigned(GlockEffect::ReserveHud)};
        if(!armor)expected.push_back(unsigned(GlockEffect::Sleeve));
        expected.push_back(unsigned(GlockEffect::InventoryChanged));expected.push_back(unsigned(GlockEffect::ViewAnimation));
        Check(deployOrder==expected&&weapon.pev->armorvalue==0&&
            Bits(weapon.ActionState().primary)==Bits(1)&&weapon.ActionState().clip==clip);
        const std::vector<std::pair<int,int>> expectedRandom=armor?
            std::vector<std::pair<int,int>>{{-5,5}}:std::vector<std::pair<int,int>>{{-5,5},{0,2}};
        Check(deployRandom==expectedRandom);
    }
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();Check(weapon.m_iClip==0&&weapon.m_fInReload&&weapon.ActionState().action==2);
    const float complete=weapon.ActionState().ownerAttack;
    globals.time=std::nextafter(complete,-INFINITY);weapon.ItemPostFrame();Check(weapon.m_iClip==0&&weapon.m_fInReload);
    globals.time=complete;weapon.ItemPostFrame();Check(weapon.m_iClip==15&&player.m_rgAmmo[1]==25&&!weapon.m_fInReload&&weapon.ActionState().action==2);
    weapon.ItemPostFrame();Check(weapon.ActionState().action==0);
    Initialize();globals.time=10;weapon.SecondaryAttack();Check(Bits(weapon.pev->nextthink)==Bits(10+.3f)&&weapon.m_pfnThink!=nullptr);
    const int old=meleeCalls;weapon.DelayedMelee();Check(meleeCalls==old);globals.time=weapon.pev->nextthink;weapon.DelayedMelee();Check(meleeCalls==old+1&&weapon.m_pfnThink==nullptr);
    Initialize();globals.time=10;weapon.SecondaryAttack();weapon.Holster();const int before=meleeCalls;
    globals.time=11;weapon.DelayedMelee();Check(meleeCalls==before&&weapon.m_pfnThink==nullptr);
    Initialize();globals.time=10;weapon.SecondaryAttack();player.pev->deadflag=DEAD_DEAD;weapon.DelayedMelee();Check(meleeCalls==before);
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();globals.time=weapon.ActionState().ownerAttack;observeReload=true;
    weapon.ItemPostFrame();Check(!weapon.m_fInReload);
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();globals.time=weapon.ActionState().ownerAttack;failEffect=true;
    weapon.ItemPostFrame();Check(weapon.m_iClip==15&&weapon.m_fInReload&&weapon.Status()==GlockStatus::MissingBackend);
    for(int mutation=0;mutation<6;++mutation)
    {
        Initialize();globals.time=10;frame.pressed=1;
        switch(mutation){case 0:++ownerEdict.serialnumber;break;case 1:ownerEdict.pvPrivateData=nullptr;break;
        case 2:++weaponEdict.serialnumber;break;case 3:weaponEdict.pvPrivateData=nullptr;break;
        case 4:player.m_pActiveItem=nullptr;break;case 5:++player.pev->modelindex;break;}
        events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==15);
    }
    // Real entry/strict deadline/loop/exit, without writing private action clocks.
    Initialize();globals.time=10;frame.originalFlags=0x40000;player.pev->velocity={10,0,0};weapon.ItemPostFrame();
    Check(weapon.ActionState().action==1&&Bits(weapon.ActionState().primary)==Bits(10.5f));
    events.clear();globals.time=11;weapon.ItemPostFrame();Check(events.empty());
    globals.time=std::nextafter(11.0f,INFINITY);weapon.ItemPostFrame();Check(!events.empty()&&events[2]==12);
    frame.originalFlags=0;globals.time=13;weapon.ItemPostFrame();Check(!weapon.ActionState().action&&Bits(weapon.ActionState().primary)==Bits(static_cast<float>(double(13.0f)+.33)));
    Initialize();globals.time=10;frame.condition3AA=true;weapon.ItemPostFrame();Check(weapon.ActionState().action==5);
    globals.time=11;frame.condition3AA=false;weapon.ItemPostFrame();Check(!weapon.ActionState().action&&Bits(weapon.ActionState().primary)==Bits(12));
    Initialize();globals.time=10;frame.pressed=0x40;player.pev->button=0x40;weapon.ItemPostFrame();Check(weapon.ActionState().aiming);
    globals.time=10.4f;weapon.Reload();Check(weapon.ActionState().pendingReload&&weapon.m_iClip==15);
    globals.time=11;frame.pressed=0;player.pev->button=0;weapon.ItemPostFrame();Check(!weapon.ActionState().aiming&&weapon.ActionState().pendingReload);
    globals.time=12;weapon.ItemPostFrame();Check(!weapon.ActionState().pendingReload&&weapon.m_iClip==15&&!weapon.m_fInReload);
    const auto bit=[](GlockEffect effect){return 1u<<unsigned(effect);};
    const auto effects=[](std::initializer_list<GlockEffect> values)
    {std::uint32_t mask{};for(auto value:values)mask|=1u<<unsigned(value);return mask;};
    // Each loaded shot capability rejects before smoke, ammunition, player
    // animation, spread, damage or RNG. Unrelated attachment capabilities do
    // not block this original branch.
    for(auto missing:{GlockEffect::Gunsmoke,GlockEffect::FireEvent,GlockEffect::Recoil,
        GlockEffect::Shake,GlockEffect::ShotState,GlockEffect::FireSound,GlockEffect::ShotLatch})
    {
        Initialize();auto b=backends;b.preparedEffects&=~bit(missing);
        Check(weapon.Bind(player,prepared,b)==GlockStatus::Ready&&weapon.Deploy());
        globals.time=10;frame.pressed=1;events.clear();const auto spread=weapon.Spread();
        const auto action=weapon.ActionState();weapon.PrimaryAttack();
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==15&&
            Bits(weapon.Spread().scalar)==Bits(spread.scalar)&&Bits(weapon.ActionState().primary)==Bits(action.primary));
        // ItemPostFrame must not emit its earlier InventoryChanged first.
        player.pev->button=1;weapon.ItemPostFrame();
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==15);
    }
    const auto ordinary=GlockRequiredEffects&~effects({GlockEffect::Body,GlockEffect::LightState,
        GlockEffect::Tutorial,GlockEffect::AttachmentSound});
    Initialize();auto ordinaryBackend=backends;ordinaryBackend.preparedEffects=ordinary;
    auto ordinaryPrepared=prepared;ordinaryPrepared.taclightSoundsReady=false;
    Check(weapon.Bind(player,ordinaryPrepared,ordinaryBackend)==GlockStatus::Ready&&weapon.Deploy());
    globals.time=10;frame.pressed=1;events.clear();weapon.PrimaryAttack();
    Check(weapon.Status()==GlockStatus::Applied&&weapon.m_iClip==14&&!events.empty());
    globals.time=11;frame.pressed=0;events.clear();weapon.WeaponIdle();Check(weapon.Status()==GlockStatus::Applied);
    Check(weapon.RequestTaclightAttachment()==GlockStatus::MissingBackend&&!weapon.TaclightInstalled());
    // Conditional deployment capabilities are admitted before selected/body,
    // sentinel normalization, first-draw state, event delivery or sleeve RNG.
    for(auto missing:{GlockEffect::DeployPrelude,GlockEffect::DeployReset,GlockEffect::InventoryChanged,
        GlockEffect::ReserveHud,GlockEffect::Sleeve,GlockEffect::ViewAnimation,GlockEffect::FirstDraw,GlockEffect::Body})
    {
        Initialize();auto b=backends;b.preparedEffects&=~bit(missing);
        Check(weapon.Bind(player,prepared,b)==GlockStatus::Ready);
        if(missing==GlockEffect::ReserveHud)player.m_rgAmmo[1]=39;
        if(missing==GlockEffect::FirstDraw)frame.firstDrawn=false;
        if(missing==GlockEffect::Body)weapon.pev->body=1;
        events.clear();deployOrder.clear();deployRandom.clear();traceDeploy=true;
        Check(!weapon.Deploy());traceDeploy=false;
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&deployOrder.empty()&&deployRandom.empty()&&
            weapon.m_iClip==15&&weapon.pev->body==(missing==GlockEffect::Body?1:0));
    }
    // Same firstDraw/reserve/sleeve capabilities are unnecessary when their
    // exact original branches are absent; no forced-success Emit is inserted.
    Initialize();auto conditional=backends;
    conditional.preparedEffects&=~effects({GlockEffect::FirstDraw,GlockEffect::ReserveHud,GlockEffect::Sleeve});
    Check(weapon.Bind(player,prepared,conditional)==GlockStatus::Ready);weapon.pev->armorvalue=1;
    Check(weapon.Deploy()&&weapon.pev->armorvalue==0);
    // Reload, delayed melee and idle reject missing operation resources before
    // any clip/loading/player-think changes, even though Bind/Deploy succeed.
    for(int operation=0;operation<4;++operation)
    {
        Initialize();auto p=prepared;
        const int sequence=operation==0?5:operation==1?4:operation==2?27:0;
        p.sequences.present&=~(std::uint64_t{1}<<sequence);
        Check(weapon.Bind(player,p,backends)==GlockStatus::Ready&&weapon.Deploy());globals.time=10;
        weapon.m_iClip=operation==0?0:4;events.clear();const int clip=weapon.m_iClip;
        const auto action=weapon.ActionState();
        if(operation<2)weapon.Reload();else if(operation==2)weapon.SecondaryAttack();else weapon.WeaponIdle();
        Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==clip&&
            !weapon.m_fInReload&&weapon.m_pfnThink==nullptr&&Bits(weapon.ActionState().primary)==Bits(action.primary));
    }
    // Every requested attachment dependency is checked before setting pending,
    // so failed requests cannot silently start later through ItemPostFrame.
    for(auto missing:{GlockEffect::AttachmentSound,GlockEffect::Body,GlockEffect::Tutorial})
    {
        Initialize();auto b=backends;b.preparedEffects&=~bit(missing);
        Check(weapon.Bind(player,prepared,b)==GlockStatus::Ready&&weapon.Deploy());globals.time=10;events.clear();
        Check(weapon.RequestTaclightAttachment()==GlockStatus::MissingBackend&&events.empty());
        weapon.ItemPostFrame();Check(!weapon.TaclightInstalled()&&weapon.m_pfnThink==nullptr&&events.empty());
    }
    // The first-draw consumer remains required for an actual first draw; the
    // host advertises its proven MP early-return implementation independently
    // of the tactical Tutorial capability.
    Initialize();auto firstDraw=backends;firstDraw.preparedEffects&=~bit(GlockEffect::Tutorial);
    frame.firstDrawn=false;Check(weapon.Bind(player,prepared,firstDraw)==GlockStatus::Ready&&weapon.Deploy());
    Check(frame.firstDrawn);
    // Duplicate ammunition cannot deduct the item/default ammo or fill clip
    // before discovering an absent pickup sound consumer.
    for(bool missingResource:{false,true})
    {
        Initialize();auto p=prepared;auto b=backends;
        if(missingResource)p.pickupSoundReady=false;else b.preparedEffects&=~bit(GlockEffect::AmmoPickupSound);
        Check(weapon.Bind(player,p,b)==GlockStatus::Ready&&weapon.Deploy());
        player.m_rgpPlayerItems[PISTOL_SLOT]=&weapon;weapon.m_pNext=nullptr;
        weapon.m_iClip=0;weapon.m_iDefaultAmmo=15;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        Check(weapon.Bind(player,p,b)==GlockStatus::Ready);events.clear();const int reserve=player.m_rgAmmo[1];
        Check(!weapon.ExtractAmmo(&weapon)&&events.empty()&&weapon.m_iClip==0&&weapon.m_iDefaultAmmo==15&&
            player.m_rgAmmo[1]==reserve&&weapon.Status()==GlockStatus::MissingBackend);
    }
    // Optional animation adapter is admitted only by actual animated actions.
    Initialize();auto noAnimation=backends;noAnimation.playerAnimation=nullptr;
    Check(weapon.Bind(player,prepared,noAnimation)==GlockStatus::Ready&&weapon.Deploy());
    globals.time=10;frame.pressed=1;events.clear();weapon.PrimaryAttack();
    Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==15);
    weapon.SecondaryAttack();Check(events.empty()&&weapon.m_pfnThink==nullptr);
    weapon.m_iClip=4;weapon.Reload();Check(events.empty()&&weapon.m_iClip==4&&!weapon.m_fInReload);
    // Dry attempts need no loaded-shot effects, but their click branch must be
    // ready before the preceding view animation. The strict deadline remains.
    Initialize();auto dry=backends;dry.preparedEffects=bit(GlockEffect::ViewAnimation)|bit(GlockEffect::DeployPrelude)|
        bit(GlockEffect::DeployReset)|bit(GlockEffect::InventoryChanged)|bit(GlockEffect::Sleeve);
    Check(weapon.Bind(player,prepared,dry)==GlockStatus::Ready&&weapon.Deploy());
    globals.time=10;frame.pressed=1;weapon.m_iClip=0;weapon.m_fFireOnEmpty=1;events.clear();weapon.PrimaryAttack();
    Check(weapon.Status()==GlockStatus::MissingBackend&&events.empty()&&weapon.m_iClip==0);
    // Binding still rejects absent fundamental identity/lifetime interfaces.
    for(int missing=0;missing<5;++missing)
    {
        Initialize();auto p=prepared;auto b=backends;
        switch(missing){case 0:p.modelsReady=false;break;case 1:b.ownerFrame=nullptr;break;
        case 2:b.effect=nullptr;break;case 3:b.current=nullptr;break;case 4:p.world=MeleeWorld::UnsupportedSingleplayer;break;}
        Check(weapon.Bind(player,p,b)==GlockStatus::MissingBackend);events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==15);
    }
    Initialize();globals.time=10;frame.pressed=1;frame.sourceMode=PistolSource::UnsupportedOriginalOverride;
    events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==15);
    for(int edge:{6,7,8,9,3,12,11})
    {
        Initialize();globals.time=10;frame.pressed=1;cancel=edge;events.clear();weapon.PrimaryAttack();
        bool seen=false;for(std::size_t at=0;at<events.size();at+=events[at+1]+2){Check(!seen);if(events[at]==unsigned(edge))seen=true;}Check(seen);
    }
    Initialize();globals.time=10;weapon.m_iClip=0;weapon.m_fFireOnEmpty=1;frame.pressed=1;events.clear();weapon.PrimaryAttack();Check(!events.empty());
    events.clear();globals.time=10.5f;weapon.PrimaryAttack();Check(events.size()==3&&events[0]==3);events.clear();globals.time=11;weapon.PrimaryAttack();Check(events.size()>3);
    // Native Scenario buys can exceed CoF's metadata capacity. That limit is
    // enforced when granting ammo, never by disabling a valid inventory item.
    for(int reserve:{151,500,800,65535,std::numeric_limits<int>::max()})
    {
        Initialize();player.m_rgAmmo[1]=reserve;weapon.m_iClip=14;
        Check(weapon.CanDeploy()&&weapon.Deploy());globals.time=10;
        weapon.Reload();Check(weapon.m_fInReload&&weapon.ActionState().loading&&weapon.m_iClip==0&&player.m_rgAmmo[1]==reserve);
        globals.time=weapon.ActionState().ownerAttack+.01f;weapon.ItemPostFrame();
        Check(weapon.m_iClip==15&&player.m_rgAmmo[1]==reserve-15&&!weapon.m_fInReload);
        globals.time+=5;weapon.Holster();Check(weapon.Status()==GlockStatus::Applied);
        player.m_pActiveItem=nullptr;Check(weapon.CanDeploy());player.m_pActiveItem=&weapon;
        Check(weapon.Deploy()&&weapon.m_iClip==15&&player.m_rgAmmo[1]==reserve-15);
    }
    // Skill grants are authoritative loaded rounds, not corrupt physical-mag
    // counts. They must survive fire, idle, melee, holster and a later reselect.
    for(int clip:{16,45,75,255,65535,std::numeric_limits<int>::max()})
    {
        Initialize();weapon.m_iClip=clip;Check(weapon.CanDeploy()&&weapon.Deploy());
        globals.time=10;frame.pressed=1;weapon.PrimaryAttack();
        Check(weapon.m_iClip==clip-1&&weapon.Status()==GlockStatus::Applied);
        const int remaining=weapon.m_iClip;const int reserve=player.m_rgAmmo[1];
        globals.time+=5;weapon.Reload();
        Check(!weapon.m_fInReload&&weapon.m_iClip==remaining&&player.m_rgAmmo[1]==reserve);
        globals.time+=5;weapon.WeaponIdle();Check(weapon.Status()!=GlockStatus::InvalidInput);
        globals.time+=5;weapon.SecondaryAttack();Check(weapon.Status()==GlockStatus::Applied&&weapon.m_iClip==remaining);
        globals.time+=5;weapon.Holster();Check(weapon.Status()==GlockStatus::Applied);
        player.m_pActiveItem=nullptr;Check(weapon.CanDeploy());player.m_pActiveItem=&weapon;
        Check(weapon.Deploy()&&weapon.m_iClip==remaining);
    }
    Initialize();globals.time=10;weapon.m_iClip=0;player.m_rgAmmo[1]=500;
    weapon.Reload();Check(weapon.m_fInReload);weapon.m_iClip=75;
    globals.time=weapon.ActionState().ownerAttack+.01f;weapon.ItemPostFrame();
    Check(weapon.m_iClip==75&&player.m_rgAmmo[1]==500&&!weapon.m_fInReload);
    Initialize();player.m_rgAmmo[1]=-1;Check(!weapon.CanDeploy()&&weapon.Status()==GlockStatus::InvalidInput);
    Initialize();ItemInfo info{};Check(weapon.GetItemInfo(&info)&&info.iId==896&&weapon.m_iId==896&&info.iMaxClip==15&&info.iMaxAmmo1==150&&info.iMaxAmmo2==-1&&weapon.m_iSecondaryAmmoType==-1&&weapon.iItemSlot()==PISTOL_SLOT);
    InventoryRestoreCases();
    std::cout<<rows<<" original primary cases; "<<worldRows<<" original world cases; "<<transitionRows<<" original transition cases; "<<checks<<" actual SDK checks; 0 failures\n";
}
