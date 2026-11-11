// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/p345_weapon.hpp"
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
P345OwnerFrame frame{};
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
float shotLatchDeadline{};
P345Weapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"P345 SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
bool OwnerFrame(void*,P345OwnerFrame& out) noexcept{out=frame;return true;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const P345Event& e) noexcept
{
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    if(fidgetMode)fidgetOrder.push_back(kind);
    switch(e.kind)
    {
    case P345Effect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case P345Effect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case P345Effect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case P345Effect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case P345Effect::ViewAnimation:Event(kind,e.value);break;
    case P345Effect::ShotState:Event(kind,Bits(e.a));break;
    case P345Effect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case P345Effect::InventoryChanged:
        if(observeReload)Check(item.m_iClip==8&&item.m_fInReload&&item.m_pPlayer->m_rgAmmo[item.m_iPrimaryAmmoType]==32);
        if(fidgetMode)Event(kind);
        break;
    case P345Effect::ReserveHud:if(fidgetMode)Event(kind,e.value);break;
    case P345Effect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==0&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case P345Effect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case P345Effect::EmptyClick:case P345Effect::FireSound:Event(kind);break;
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
        Check(amount>=0&&!std::strcmp(name,"cof_p345")&&(maximum==24||maximum==80));
        const int add=std::min(amount,maximum-m_rgAmmo[1]);if(add<0)return -1;
        m_rgAmmo[1]+=add;Event(104,amount,maximum);return 1;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
P345Weapon weapon;
PlayerAnimationAdapter animations;
P345Prepared prepared;
P345Backends backends;
CCSPlayerItem itemWrapper;
bool ColdPrepare(void*,P345Weapon&,P345Prepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,P345Backends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,P345Weapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,P345Weapon&,P345Prepared&) noexcept{return false;}
bool CancelColdPrepare(void*,P345Weapon&,P345Prepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;frame.meleeDamage=11;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=80;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/p345/v_p345.mdl\0models/guest/cof/p345/p_p345.mdl\0models/guest/cof/p345/w_p345.mdl\0inventoryitems/weapons/weapon_p345.txt\0weapon_cof_p345\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[1].pszName="cof_p345";
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
    const auto plan=RecommendedPistolAnimationPlan(Pistol::P345,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},Pistol::P345,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<34)-1;
    prepared.sequences.duration.fill(1);
    prepared.ballistics={77,31};prepared.melee={78,true,Recoil,nullptr};
    prepared.aimOffset={1,2,3};prepared.aimZoom=55;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=1;prepared.hostPosition=1;
    prepared.modelsReady=prepared.soundsReady=prepared.particleReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=prepared.pickupSoundReady=prepared.deployPreludeReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/p345/v_p345.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    backends={nullptr,Current,OwnerFrame,Effect,P345RequiredEffects,&animations,{1,1}};
    weapon.Spawn();weapon.m_iPrimaryAmmoType=1;player.m_rgAmmo[1]=40;weapon.m_iClip=8;
    Check(weapon.Bind(player,prepared,backends)==P345Status::Ready);
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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==897);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==897);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==897);return 0;}

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
        Check(weapon.ResourcesReady()&&weapon.Status()==P345Status::Applied&&itemWrapper.m_ItemInfo.iId==897);
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
    std::ifstream fidgetCases(argv[6]),fidgetExpected(argv[7]);if(!fidgetCases||!fidgetExpected)return 12;
    unsigned fidgetRows{};
    while(fidgetCases>>std::ws&&fidgetCases.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,10> row{};for(auto& v:row)if(!(fidgetCases>>std::hex>>v))return 13;
        Initialize();globals.time=Float(row[0]);weapon.m_iClip=row[1];
        auto& s=const_cast<MagazineActionState&>(weapon.ActionState());
        s.primary=Float(row[2]);s.secondary=Float(row[3]);s.idle=Float(row[4]);
        s.action=row[5];s.special=row[6];s.aiming=row[7]!=0;s.ownerAttack=0;
        frame.toggleAim=true;fidgetChance=row[8];fidgetChoice=row[9];
        g_engfuncs.pfnRandomLong=[](int low,int high)
        {const auto value=high==599?fidgetChance:fidgetChoice;Event(91,low,high,value);return int(value);};
        events.clear();fidgetMode=true;weapon.ItemPostFrame();fidgetMode=false;
        std::vector<std::uint32_t> actual{unsigned(weapon.m_iClip),Bits(s.primary),Bits(s.secondary),Bits(s.idle),
            unsigned(s.action),unsigned(s.special),s.aiming};
        actual.insert(actual.end(),events.begin(),events.end());
        std::string line;if(!std::getline(fidgetExpected,line))return 14;std::istringstream values(line);std::vector<unsigned> wanted;unsigned value;
        while(values>>std::hex>>value)wanted.push_back(value);
        if(actual!=wanted)
        {std::cerr<<"Original Fidget mismatch row "<<fidgetRows<<'\n';for(auto v:actual)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';for(auto v:wanted)std::cerr<<std::hex<<v<<' ';std::cerr<<'\n';return 15;}
        ++fidgetRows;
    }
    if(fidgetRows!=279)return 16;
    if(fidgetExpected>>std::ws&&fidgetExpected.peek()!=std::char_traits<char>::eof())return 17;
    const auto prepareFidget=[]
    {
        Initialize();globals.time=10;frame.toggleAim=true;
        auto& s=const_cast<MagazineActionState&>(weapon.ActionState());
        s.ownerAttack=s.primary=s.secondary=s.extra=s.idle=0;
        weapon.m_iPlayEmptySound=0;events.clear();fidgetOrder.clear();fidgetMode=true;
    };
    // The original common caller restores the empty-click flag before Fidget,
    // then visits wear, reserve HUD, reload completion and input. Wear is
    // internal/unobservable here; its original PC order is in the oracle.
    // The actual SDK callback observes restored empty state and pre-reload ammo.
    for(unsigned buttons:{0u,1u,0x40u,0x800u,0x2000u})
    {
        prepareFidget();weapon.m_iClip=4;player.m_rgAmmo[1]=39;player.pev->button=buttons;frame.pressed=buttons;
        g_engfuncs.pfnRandomLong=[](int,int high)
        {Check(weapon.m_iPlayEmptySound==1);fidgetOrder.push_back(high==599?91:92);return high==599?50:0;};
        weapon.ItemPostFrame();fidgetMode=false;
        Check(fidgetOrder.size()>=4&&fidgetOrder[0]==91&&fidgetOrder[1]==92&&
            fidgetOrder[2]==unsigned(P345Effect::ViewAnimation)&&fidgetOrder[3]==unsigned(P345Effect::ReserveHud));
        if(buttons!=0)Check(fidgetOrder.size()>4&&fidgetOrder[4]==unsigned(
            buttons==0x2000?P345Effect::ViewAnimation:P345Effect::InventoryChanged));
    }
    prepareFidget();weapon.m_iClip=1;player.m_rgAmmo[1]=39;observeReload=true;
    {
        auto& s=const_cast<MagazineActionState&>(weapon.ActionState());s.loading=true;weapon.m_fInReload=1;
        g_engfuncs.pfnRandomLong=[](int,int high)
        {Check(weapon.m_iPlayEmptySound==1&&weapon.m_iClip==1&&weapon.m_fInReload&&player.m_rgAmmo[1]==39);
            fidgetOrder.push_back(high==599?91:92);return high==599?50:0;};
        weapon.ItemPostFrame();fidgetMode=false;
        Check((fidgetOrder==std::vector<unsigned>{91,92,unsigned(P345Effect::ViewAnimation),
            unsigned(P345Effect::ReserveHud),unsigned(P345Effect::InventoryChanged)}));
        Check(!weapon.m_fInReload&&weapon.m_iClip==8&&player.m_rgAmmo[1]==32);
    }
    // Cancelling either RNG or the Fidget view callback must stop before HUD,
    // reload and attack. A delivery failure is equally terminal for this frame.
    for(int edge:{1,2,3,4})
    {
        prepareFidget();player.m_rgAmmo[1]=39;frame.pressed=1;player.pev->button=1;
        if(edge==3)cancel=int(P345Effect::ViewAnimation);
        if(edge==4)failEffect=true;
        fidgetChance=edge;fidgetChoice=0;
        g_engfuncs.pfnRandomLong=[](int,int high)
        {fidgetOrder.push_back(high==599?91:92);if(fidgetChance==(high==599?1u:2u))current=false;return high==599?50:0;};
        weapon.ItemPostFrame();fidgetMode=false;
        Check((fidgetOrder==(edge==1?std::vector<unsigned>{91}:edge==2?std::vector<unsigned>{91,92}:
            std::vector<unsigned>{91,92,unsigned(P345Effect::ViewAnimation)})));
        Check(weapon.m_iPlayEmptySound==1&&weapon.m_iClip==8&&player.m_rgAmmo[1]==39);
    }
    // Truly ownerless metadata is available before Prepare/Bind and remains
    // available after generation retirement; it cannot make Spawn usable.
    {
        P345Weapon ownerless;ItemInfo info{};
        Check(ownerless.GetItemInfo(&info)&&info.iId==897&&ownerless.m_iId==897&&
            info.iSlot==P345HudSlot&&info.iPosition==P345HudPosition&&info.pszAmmo2==nullptr&&
            !std::strcmp(info.pszName,"weapon_cof_p345")&&!std::strcmp(info.pszAmmo1,"cof_p345"));
        Check(!ownerless.ResourcesReady()&&!ownerless.GetItemInfo(nullptr));
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
        case 6:prepared.hostPosition=2;break;case 7:prepared.sequences.present=0;break;
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
    Initialize();weapon.BeforeWeaponDrop();events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.Status()!=P345Status::Applied);
    // Actual SDK AddToPlayer/AddWeapon composition and real CCSPlayerItem
    // storage, with controlled GiveAmmo and reliable-message boundaries.
    for(int limit:{24,80})for(int health:{-2,0,13})
    {
        Initialize();weapon.m_pEntity=&itemWrapper;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};
        g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
        weapon.pev->health=float(health);weapon.Spawn();player.m_rgAmmo[1]=0;frame.reserveLimit=limit;
        events.clear();Check(weapon.AddToPlayer(&player));
        Check(weapon.m_pPlayer==&player&&weapon.m_iPrimaryAmmoType==1&&weapon.m_iSecondaryAmmoType==-1&&player.pev->weapons==0);
        if(health==-2)Check(weapon.m_iClip==-666&&weapon.m_iDefaultAmmo==8&&player.m_rgAmmo[1]==0);
        else Check(weapon.m_iClip==8&&weapon.m_iDefaultAmmo==0&&player.m_rgAmmo[1]==(health==13?5:0));
        weapon.AttachToPlayer(&player);
        Check(weapon.pev->movetype==MOVETYPE_FOLLOW&&weapon.pev->solid==SOLID_NOT&&weapon.pev->effects==EF_NODRAW&&
            weapon.pev->aiment==player.edict()&&weapon.pev->owner==player.edict()&&weapon.pev->modelindex==0&&int(weapon.pev->model)==0&&
            weapon.m_pfnThink==nullptr&&weapon.m_pfnTouch==nullptr&&weapon.m_pfnUse==nullptr&&!(weapon.ObjectCaps()&FCAP_IMPULSE_USE));
    }
    // Original DefaultDeploy's observable ordering, loaded/empty sequence and
    // pitch RNG consumption even when armorvalue suppresses the sleeve sound.
    for(int armor:{0,1})for(int clip:{0,8})
    {
        Initialize();weapon.pev->armorvalue=float(armor);weapon.m_iClip=clip;
        player.m_rgAmmo[1]=24;deployOrder.clear();deployRandom.clear();traceDeploy=true;
        Check(weapon.Deploy());traceDeploy=false;
        std::vector<unsigned> expected{unsigned(P345Effect::DeployPrelude),unsigned(P345Effect::DeployReset),
            unsigned(P345Effect::ReserveHud)};
        if(!armor)expected.push_back(unsigned(P345Effect::Sleeve));
        expected.push_back(unsigned(P345Effect::InventoryChanged));expected.push_back(unsigned(P345Effect::ViewAnimation));
        Check(deployOrder==expected&&weapon.pev->armorvalue==0&&
            Bits(weapon.ActionState().primary)==Bits(1)&&weapon.ActionState().clip==clip);
        const std::vector<std::pair<int,int>> expectedRandom=armor?
            std::vector<std::pair<int,int>>{{-5,5}}:std::vector<std::pair<int,int>>{{-5,5},{0,2}};
        Check(deployRandom==expectedRandom);
    }
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();Check(weapon.m_iClip==0&&weapon.m_fInReload&&weapon.ActionState().action==2);
    const float complete=weapon.ActionState().ownerAttack;
    globals.time=std::nextafter(complete,-INFINITY);weapon.ItemPostFrame();Check(weapon.m_iClip==0&&weapon.m_fInReload);
    globals.time=complete;weapon.ItemPostFrame();Check(weapon.m_iClip==8&&player.m_rgAmmo[1]==32&&!weapon.m_fInReload&&weapon.ActionState().action==2);
    weapon.ItemPostFrame();Check(weapon.ActionState().action==0);
    Initialize();globals.time=10;weapon.SecondaryAttack();Check(Bits(weapon.pev->nextthink)==Bits(10+.3f)&&weapon.m_pfnThink!=nullptr);
    const int old=meleeCalls;weapon.DelayedMelee();Check(meleeCalls==old);globals.time=weapon.pev->nextthink;weapon.DelayedMelee();Check(meleeCalls==old+1&&weapon.m_pfnThink==nullptr);
    Initialize();globals.time=10;weapon.SecondaryAttack();weapon.Holster();const int before=meleeCalls;
    globals.time=11;weapon.DelayedMelee();Check(meleeCalls==before&&weapon.m_pfnThink==nullptr);
    Initialize();globals.time=10;weapon.SecondaryAttack();player.pev->deadflag=DEAD_DEAD;weapon.DelayedMelee();Check(meleeCalls==before);
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();globals.time=weapon.ActionState().ownerAttack;observeReload=true;
    weapon.ItemPostFrame();Check(!weapon.m_fInReload);
    Initialize();globals.time=10;weapon.m_iClip=4;weapon.Reload();globals.time=weapon.ActionState().ownerAttack;failEffect=true;
    weapon.ItemPostFrame();Check(weapon.m_iClip==8&&weapon.m_fInReload&&weapon.Status()==P345Status::MissingBackend);
    for(int mutation=0;mutation<6;++mutation)
    {
        Initialize();globals.time=10;frame.pressed=1;
        switch(mutation){case 0:++ownerEdict.serialnumber;break;case 1:ownerEdict.pvPrivateData=nullptr;break;
        case 2:++weaponEdict.serialnumber;break;case 3:weaponEdict.pvPrivateData=nullptr;break;
        case 4:player.m_pActiveItem=nullptr;break;case 5:++player.pev->modelindex;break;}
        events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==8);
    }
    // Real entry/strict deadline/loop/exit, without writing private action clocks.
    Initialize();globals.time=10;frame.originalFlags=0x40000;player.pev->velocity={10,0,0};weapon.ItemPostFrame();
    Check(weapon.ActionState().action==1&&Bits(weapon.ActionState().primary)==Bits(10.5f));
    events.clear();globals.time=11;weapon.ItemPostFrame();Check(events.empty());
    globals.time=std::nextafter(11.0f,INFINITY);weapon.ItemPostFrame();Check(!events.empty()&&events[2]==11);
    frame.originalFlags=0;globals.time=13;weapon.ItemPostFrame();Check(!weapon.ActionState().action&&Bits(weapon.ActionState().primary)==Bits(14));
    Initialize();globals.time=10;frame.condition3AA=true;weapon.ItemPostFrame();Check(weapon.ActionState().action==5);
    globals.time=11;frame.condition3AA=false;weapon.ItemPostFrame();Check(!weapon.ActionState().action&&Bits(weapon.ActionState().primary)==Bits(12));
    Initialize();globals.time=10;frame.pressed=0x40;player.pev->button=0x40;weapon.ItemPostFrame();Check(weapon.ActionState().aiming);
    globals.time=10.4f;weapon.Reload();Check(weapon.ActionState().pendingReload&&weapon.m_iClip==8);
    globals.time=11;frame.pressed=0;player.pev->button=0;weapon.ItemPostFrame();Check(!weapon.ActionState().aiming&&weapon.ActionState().pendingReload);
    globals.time=12;weapon.ItemPostFrame();Check(!weapon.ActionState().pendingReload&&weapon.m_iClip==8&&!weapon.m_fInReload);
    for(int kind=0;kind<static_cast<int>(P345Effect::Count);++kind)
    {
        Initialize();auto bad=backends;bad.preparedEffects&=~(1u<<kind);
        Check(weapon.Bind(player,prepared,bad)==P345Status::MissingBackend);events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==8);
    }
    for(int missing=0;missing<9;++missing)
    {
        Initialize();auto p=prepared;auto b=backends;
        switch(missing){case 0:p.modelsReady=false;break;case 1:p.soundsReady=false;break;case 2:p.particleReady=false;break;
        case 3:p.aimMetadataReady=false;break;case 4:p.melee.recoil=nullptr;break;case 5:b.playerAnimation=nullptr;break;
        case 6:b.ownerFrame=nullptr;break;case 7:b.effect=nullptr;break;case 8:p.world=MeleeWorld::UnsupportedSingleplayer;break;}
        Check(weapon.Bind(player,p,b)==P345Status::MissingBackend);events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==8);
    }
    Initialize();globals.time=10;frame.pressed=1;frame.sourceMode=PistolSource::UnsupportedOriginalOverride;
    events.clear();weapon.PrimaryAttack();Check(events.empty()&&weapon.m_iClip==8);
    for(int edge:{5,6,7,8,9,3,12,11})
    {
        Initialize();globals.time=10;frame.pressed=1;cancel=edge;events.clear();weapon.PrimaryAttack();
        bool seen=false;for(std::size_t at=0;at<events.size();at+=events[at+1]+2){Check(!seen);if(events[at]==unsigned(edge))seen=true;}Check(seen);
    }
    Initialize();globals.time=10;weapon.m_iClip=0;weapon.m_fFireOnEmpty=1;frame.pressed=1;events.clear();weapon.PrimaryAttack();Check(!events.empty());
    events.clear();globals.time=10.5f;weapon.PrimaryAttack();Check(events.empty());globals.time=std::nextafter(10.5f,INFINITY);weapon.PrimaryAttack();Check(!events.empty());
    Initialize();ItemInfo info{};Check(weapon.GetItemInfo(&info)&&info.iId==897&&weapon.m_iId==897&&info.iMaxClip==8&&info.iMaxAmmo1==80&&info.iMaxAmmo2==-1&&weapon.m_iSecondaryAmmoType==-1&&weapon.iItemSlot()==PISTOL_SLOT);
    std::cout<<rows<<" original primary cases; "<<worldRows<<" original world cases; "<<fidgetRows<<" original Fidget cases; "<<checks<<" actual SDK checks; 0 failures\n";
}
