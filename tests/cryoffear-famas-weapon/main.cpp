// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/famas_weapon.hpp"
#include "native/guest/cof/server/player_think.hpp"
#include "native/guest/cof/catalog.hpp"
#include "native/csol/server/custom/weapon_mastery.h"
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
// This fixture exercises base capacities and timings for a player without a mastery bonus.
float CSOL_MasteryReloadTime(const CBasePlayerWeapon&, float duration) { return duration; }
void CSOL_MasteryScaleGuestShot(const CBasePlayerWeapon&, float, float&, float*, float*) {}


namespace
{
using namespace guest::cof;
using namespace guest::cof::server;
std::vector<std::uint32_t> events;
std::vector<std::byte> model;
FamasOwnerFrame frame{};
bool current=true;
bool ownerFrameReady=true, firstDrawSeen=false;
MagazineSequenceTimes viewTimes;
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
FamasWeapon* live{};
std::uint32_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
float Float(std::uint32_t x){return std::bit_cast<float>(x);}
void CheckAt(bool value,int line){++checks;if(!value){std::cerr<<"Famas SDK check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
template<class... T>void Event(unsigned kind,T... values)
{events.insert(events.end(),{kind,sizeof...(T),static_cast<unsigned>(values)...});}
bool Current(void*) noexcept{return current;}
constexpr int RifleAmmoSlot = 4;
constexpr int BoxAmmoSlot = 3;
bool OwnerFrame(void*,FamasOwnerFrame& out) noexcept{out=frame;return ownerFrameReady;}
void Recoil(void*,CBasePlayer&,const Vector&) noexcept{++meleeCalls;}
bool Effect(void*,CBasePlayer&,CBasePlayerWeapon& item,const FamasEvent& e) noexcept
{
    const auto vector=[](const Vector& v){return std::array{Bits(v.x),Bits(v.y),Bits(v.z)};};
    const auto a=vector(e.first),b=vector(e.second);
    const auto kind=static_cast<unsigned>(e.kind);
    if(traceDeploy)deployOrder.push_back(kind);
    if(fidgetMode)fidgetOrder.push_back(kind);
    switch(e.kind)
    {
    case FamasEffect::Shell:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],Bits(e.a),e.value,e.other);break;
    case FamasEffect::FireEvent:Event(kind,a[0],a[1],a[2],b[0],b[1],b[2],e.value,e.other);break;
    case FamasEffect::Recoil:Event(kind,a[0],a[1],a[2]);break;
    case FamasEffect::Shake:Event(kind,Bits(e.a),Bits(e.b),Bits(e.c),Bits(float(e.value)));break;
    case FamasEffect::ViewAnimation:
        Check(item.m_pPlayer->pev->weaponanim==e.value);
        Event(kind,e.value);
        if(mutateMovement&&e.value==34)
        {
            globals.time+=.375F;frame.originalFlags=0;item.m_pPlayer->pev->weaponanim=37;
        }
        break;
    case FamasEffect::ShotState:Event(kind,Bits(e.a));break;
    case FamasEffect::ShotLatch:shotLatch=e.value;shotLatchDeadline=e.a;break;
    case FamasEffect::Aim:Event(21,e.enabled);break;
    case FamasEffect::InventoryChanged:
        if(observeReload)std::abort();
        if(fidgetMode)Event(kind);
        break;
    case FamasEffect::ReserveHud:
        if(fidgetMode)Event(kind,e.value);
        if(mutateInput)
        {
            mutateInput=false;globals.time+=1.F;item.m_pPlayer->pev->button=1;frame.pressed=1;
        }
        break;
    case FamasEffect::Gunsmoke:
        Check(e.value==19&&e.enabled&&e.other==1&&
            static_cast<unsigned>(e.modelString)==static_cast<unsigned>(item.m_pPlayer->pev->viewmodel));
        Event(kind);break;
    case FamasEffect::Sleeve:
        if(traceDeploy)Check(e.value==2&&e.other==102&&e.a==1&&e.b==.8f);
        break;
    case FamasEffect::FireSound:
        Check(e.value==0&&e.other==1&&e.enabled);
        Check(e.first==item.m_pPlayer->EyePosition());Event(kind);break;
    case FamasEffect::CameraZoom:
        Check(e.a==350&&e.value==0&&e.other==0&&!e.enabled&&e.first==Vector{}&&e.second==Vector{});
        Event(kind,Bits(e.a));break;
    case FamasEffect::EmptyClick:Event(kind);break;
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
        const int slot = GetAmmoIndex(name);
        Check(amount >= 0 && slot == RifleAmmoSlot && (maximum == 90 || maximum == 150));
        const int add = std::min(amount, maximum - m_rgAmmo[slot]);
        if (add < 0) return -1;
        m_rgAmmo[slot] += add;
        Event(104, amount, maximum);
        return slot;
    }
};
edict_t ownerEdict{},weaponEdict{};
Player player;
FamasWeapon weapon;
PlayerThinkSlot playerThink;
PlayerThinkKind lastPlayerThinkKind{};
std::uint64_t taskLife=17;
bool TaskCurrent(void*,CBasePlayer& owner,std::uint64_t map,std::uint64_t life) noexcept
{return current&&&owner==&player&&map==1&&life==taskLife;}
// Controlled target/damage boundary, separate from the player scheduler.
bool TaskDispatch(void*,CBasePlayer& owner,PlayerThinkKind kind) noexcept
{
    Check(&owner==&player);lastPlayerThinkKind=kind;
    if(frame.stamina.value_or(0)>0){++meleeCalls;Event(110,unsigned(kind));}
    return true;
}
PlayerAnimationAdapter animations;
FamasPrepared prepared;
FamasBackends backends;
CCSPlayerItem itemWrapper;
bool NativeRecoil(void*,CBasePlayer& p,const Vector& v) noexcept
{Check(&p==&player);p.pev->punchangle+=v;Event(8,Bits(v.x),Bits(v.y),Bits(v.z));return true;}
bool FirstEquip(void*,bool& first) noexcept
{first=!firstDrawSeen;firstDrawSeen=true;return true;}
bool ColdPrepare(void*,FamasWeapon&,FamasPrepared& p) noexcept{Event(100);p=prepared;return true;}
bool ColdOwner(void*,CBasePlayer&,FamasBackends& b) noexcept{b=backends;return true;}
bool ColdInventory(void*,CBasePlayer&,FamasWeapon&,int) noexcept{std::abort();}
bool MissingColdPrepare(void*,FamasWeapon&,FamasPrepared&) noexcept{return false;}
bool CancelColdPrepare(void*,FamasWeapon&,FamasPrepared&) noexcept{current=false;return true;}
bool extendedAllowed=true;

void Initialize()
{
    events.clear();current=true;ownerFrameReady=true;firstDrawSeen=false;mutateMovement=mutateInput=false;cancel=-1;failEffect=observeReload=fidgetMode=false;shotLatch=0;shotLatchDeadline=0;frame={};frame.bulletDamage=37;
    frame.randomSeed=17;frame.sourceMode=PistolSource::NormalMuzzle;
    frame.reserveLimit=150;frame.stamina=100;
    globals={};globals.maxClients=2;globals.time=0;
    static char strings[]="\0models/guest/cof/famas/v_famas.mdl\0models/guest/cof/famas/p_famas.mdl\0models/guest/cof/famas/w_famas.mdl\0inventoryitems/weapons/weapon_famas.txt\0weapon_cof_famas\0";
    globals.pStringBase=strings;
    ownerEdict={};weaponEdict={};ownerEdict.serialnumber=2;weaponEdict.serialnumber=3;
    player.pev=&ownerEdict.v;ownerEdict.v.pContainingEntity=&ownerEdict;ownerEdict.pvPrivateData=&player;
    weapon.pev=&weaponEdict.v;weaponEdict.v.pContainingEntity=&weaponEdict;weaponEdict.pvPrivateData=&weapon;
    weapon.ConfigureColdHost({});
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
    CBasePlayerItem::m_AmmoInfoArray[RifleAmmoSlot].pszName = "556Nato";
    CBasePlayerItem::m_AmmoInfoArray[BoxAmmoSlot].pszName = "556NatoBox";
    player.has_disconnected=false;player.m_iNumSpawns=0;
    player.pev->health=100;player.pev->modelindex=1;player.m_Activity=ACT_WALK;player.m_IdealActivity=ACT_WALK;
    player.m_fSequenceFinished=true;player.m_flFlinchTime=0;
    player.m_pActiveItem=&weapon;
    g_engfuncs={};g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return model.data();};
    g_engfuncs.pfnSetModel=[](edict_t*,const char*){};
    g_engfuncs.pfnIndexOfEdict=[](const edict_t*){return 19;};
    g_engfuncs.pfnRandomLong=[](int low,int high)
    {
        if(traceDeploy)deployRandom.emplace_back(low,high);
        if(fidgetMode)
        {
            const int value=high==599?int(fidgetChance):int(fidgetChoice);
            Event(91,low,high,value);return value;
        }
        return 2;
    };
    g_engfuncs.pfnMakeVectors=[](const float* angles)
    {
        Event(30,Bits(angles[0]),Bits(angles[1]),Bits(angles[2]));
        globals.v_forward={1,0,0};globals.v_right={0,1,0};globals.v_up={0,0,1};
    };
    std::string error;
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::Rifle,PistolAnimationRig::Stock);
    Check(plan.has_value()&&animations.Prepare(player,model,{1,1},LongGun::Rifle,*plan,_stricmp,error));
    prepared={};prepared.sequences.present=(std::uint64_t{1}<<24)-1;
    prepared.sequences.duration=viewTimes.duration;
    prepared.ballistics={77,31};
    prepared.aimOffset={0,-4.25F,2};prepared.aimZoom=50;prepared.viewModelString=1;
    prepared.playerModelString=32;prepared.shellModel=31;prepared.worldModelString=64;prepared.inventoryItemString=96;prepared.hostSlot=0;prepared.hostPosition=5;
    prepared.modelsReady=prepared.aimMetadataReady=true;
    prepared.inventoryReady=true;
    prepared.viewModelString=int(std::strstr(strings+1,"models/guest/cof/famas/v_famas.mdl")-strings);
    // Walk the actual engine string table; each resource is separately NUL terminated.
    prepared.playerModelString=prepared.viewModelString+int(std::strlen(strings+prepared.viewModelString))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(strings+prepared.worldModelString))+1;
    weapon.pev->classname=prepared.inventoryItemString+int(std::strlen(strings+prepared.inventoryItemString))+1;
    prepared.world=MeleeWorld::Multiplayer;
    taskLife=17;Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    backends={};backends.current=Current;backends.ownerFrame=OwnerFrame;backends.effect=Effect;
    backends.recoil=NativeRecoil;backends.firstEquip=FirstEquip;backends.playerAnimation=&animations;
    backends.playerModelGeneration={1,1};backends.ownerLife=17;backends.playerThink=&playerThink;
    weapon.Spawn();weapon.m_iPrimaryAmmoType=RifleAmmoSlot;player.m_rgAmmo[RifleAmmoSlot]=150;weapon.m_iClip=30;
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready);
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
    Check(shot.aiming==weapon.ActionState().aiming);
    Event(31,Bits(shot.source.x),Bits(shot.source.y),Bits(shot.source.z),Bits(shot.direction.x),Bits(shot.direction.y),Bits(shot.direction.z),
          Bits(shot.spread.x),Bits(shot.spread.y),Bits(shot.spread.z),Bits(8192),shot.damage,shot.seed);
    return {BallisticsStatus::Fired,{}};
}
BallisticsResult FireCSRifleBallistics(CBaseEntity& owner,const PistolBallistics& shot,const BallisticsEffects& fx,
    const BallisticsCall& call,int penetration,CSRifleMaterial material) noexcept
{
    static_cast<void>(fx);
    Check(material==CSRifleMaterial::Rifle556&&penetration==riflePenetration);
    Check(&owner==&player&&CheckCSRifleBallistics(shot,call,penetration,material)==BallisticsStatus::Ready);
    Check(shot.aiming==weapon.ActionState().aiming);
    ++rifleShots;
    Event(31,Bits(shot.source.x),Bits(shot.source.y),Bits(shot.source.z),Bits(shot.direction.x),Bits(shot.direction.y),Bits(shot.direction.z),
          Bits(shot.spread.x),Bits(shot.spread.y),Bits(shot.spread.z),Bits(8192),shot.damage,shot.seed);
    return {BallisticsStatus::Fired,{}};
}
BallisticsStatus CheckCSRifleBallistics(const PistolBallistics& shot,const BallisticsCall& call,
    int penetration,CSRifleMaterial material) noexcept
{
    Check(shot.playerHits==PistolPlayerHits::OriginalCooperativeSkipPlayers&&
        material==CSRifleMaterial::Rifle556&&penetration>=3);
    return shot.sourceMode==PistolSource::NormalMuzzle&&shot.damage>=0&&call.current&&call.current(call.context)?
        BallisticsStatus::Ready:BallisticsStatus::InvalidInput;
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
bool CSOL_SendWeaponPickup(CBasePlayer* p,int id){Check(p==&player&&id==905);Event(105,id);return true;}
bool CSOL_ExtendedWeapon(int id){Check(id==905);return true;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* p){Check(p==&player);return extendedAllowed;}
unsigned CSOL_LegacyWeaponMask(int id){Check(id==905);return 0;}


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
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready);Check(weapon.Deploy());Ready();
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&weapon;weapon.m_pNext=nullptr;
}
bool SawView(int sequence)
{
    for(std::size_t at=0;at<events.size();at+=events[at+1]+2)
        if(events[at]==unsigned(FamasEffect::ViewAnimation)&&events[at+1]==1&&events[at+2]==unsigned(sequence))return true;
    return false;
}

// Sample the native owner frame and held-attack dispatcher. The two bullet
// consumers above expose the exact cone passed to the seeded ray calculation.
void WriteHandlingSamples(std::ostream& out)
{
    struct Pose { const char* name; unsigned flags; float speed; };
    constexpr Pose poses[]{{"standing",0x200,0}, {"walking",0x200,180},
        {"crouching",0x4200,0}, {"crouch-walk",0x4200,75}, {"airborne",0,180}};
    out << "pose,aim,material,shot,tick,cone,raw,pitch,yaw,roll,seed,clip,damage\n";
    for (auto pose : poses) for (bool aim : {false,true}) for (int material : {0,3})
    {
        Initialize();
        riflePenetration=prepared.csPenetration=material;
        backends.recoil=[](void*,CBasePlayer&,const Vector& impulse) noexcept {
            Event(8,Bits(impulse.x),Bits(impulse.y),Bits(impulse.z));return true;
        };
        Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready && weapon.Deploy());
        Ready(8);
        player.pev->velocity={pose.speed,0,0};
        frame.originalFlags=pose.flags;
        frame.toggleAim=true;
        if (aim)
        {
            player.pev->button=frame.pressed=0x40;
            weapon.ItemPostFrame();
            Check(weapon.ActionState().aiming);
        }
        frame.pressed=0;
        g_engfuncs.pfnRandomLong=[](int low,int high) {
            return low==-2 && high==2 ? int(frame.randomSeed%5)-2 : low;
        };
        int shots=0;
        for (int tick=-100; tick<=300 && shots<30; ++tick)
        {
            globals.time=10.F+float(tick)*.01F;
            player.pev->button=tick>=0 ? IN_ATTACK : 0;
            frame.randomSeed=53+37*shots;
            events.clear();
            weapon.AdvanceOwnerFrame();
            weapon.ItemPostFrame();
            std::array<float,3> impulse{};
            bool recoilSeen=false;
            for (std::size_t at=0; at<events.size(); at+=events[at+1]+2)
            {
                if (events[at]==8 && events[at+1]==3)
                {
                    for (unsigned i=0;i<3;++i) impulse[i]=Float(events[at+2+i]);
                    recoilSeen=true;
                }
                if (events[at]!=31) continue;
                Check(events[at+1]==12 && recoilSeen && weapon.ActionState().aiming==aim);
                const auto* ray=events.data()+at+2;
                Check(ray[6]==ray[7] && ray[6]==ray[8] && ray[11]==frame.randomSeed);
                Check(++shots==30-weapon.m_iClip && ray[10]==unsigned(frame.bulletDamage));
                const float rawCone=weapon.Spread().vector[0];
                Check(ray[6]==Bits((pose.flags & FL_ONGROUND) ? rawCone*.75F : rawCone));
                const float viewScale=aim ? .5F : 1.F;
                Check(impulse[0]==float(int(frame.randomSeed%5)-2)*viewScale &&
                    impulse[1]==.5F*viewScale && impulse[2]==.25F*viewScale);
                out << pose.name << ',' << aim << ',' << material << ',' << shots << ',' << tick
                    << ',' << ray[6] << ',' << Bits(weapon.Spread().vector[0])
                    << ',' << impulse[0] << ',' << impulse[1] << ',' << impulse[2]
                    << ',' << ray[11] << ',' << weapon.m_iClip << ',' << ray[10] << '\n';
            }
        }
        Check(shots==30);
        // Recovery is strict at the original deadline, with one decay step
        // even after a long pause. The cone is selected before that decay.
        player.pev->button=0;
        const auto fired=weapon.Spread();
        Check(fired.recoveryDeadline==float(double(globals.time)+.25));
        globals.time=fired.recoveryDeadline;
        weapon.AdvanceOwnerFrame();
        const auto atDeadline=weapon.Spread();
        globals.time=std::nextafter(globals.time,INFINITY);
        weapon.AdvanceOwnerFrame();
        Check(weapon.Spread().recoveryDeadline==float(double(globals.time)+.1));
        Check(weapon.Spread().vector==atDeadline.vector && weapon.Spread().scalar==atDeadline.scalar-1.F);
    }
}

}


int main(int argc,char** argv)
{
    if(argc!=5)return 1;
    std::ifstream file(argv[1],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream viewFile(argv[2],std::ios::binary);if(!viewFile)return 1;
    std::vector<char> view{std::istreambuf_iterator<char>(viewFile),{}};
    if(view.size()<sizeof(studiohdr_t))return 1;
    const auto* header=reinterpret_cast<const studiohdr_t*>(view.data());
    if(header->numseq!=24||header->seqindex<0||
        std::size_t(header->seqindex)+24*sizeof(mstudioseqdesc_t)>view.size())return 1;
    const auto* sequence=reinterpret_cast<const mstudioseqdesc_t*>(view.data()+header->seqindex);
    viewTimes.present=(std::uint64_t{1}<<24)-1;
    for(unsigned i=0;i<24;++i)
    {
        if(sequence[i].fps<=0||sequence[i].numframes<1)return 1;
        viewTimes.duration[i]=float(sequence[i].numframes-1)/sequence[i].fps;
    }
    if (!std::strcmp(argv[3],"--handling"))
    {
        std::ofstream out(argv[4]);
        if (!out) return 1;
        WriteHandlingSamples(out);
        return out.flush() ? 0 : 1;
    }
    {
        FamasWeapon fresh;ItemInfo info{};
        Check(fresh.GetItemInfo(&info)&&info.iId==905&&info.iMaxClip==35&&info.iMaxAmmo1==150&&
            info.iSlot==0&&info.iPosition==5&&!std::strcmp(info.pszName,"weapon_cof_famas")&&
            !std::strcmp(info.pszAmmo1,"556Nato")&&fresh.iItemSlot()==PRIMARY_WEAPON_SLOT);
        Check(!fresh.GetItemInfo(nullptr)&&!fresh.IsPistol()&&!fresh.UseDecrement());
    }
    // New and dropped duplicates take different SDK paths into the same reserve.
    for (int limit : {90, 150})
    {
        Prime();
        frame.reserveLimit = limit;
        player.m_rgAmmo[RifleAmmoSlot] = limit - 12;
        player.m_rgAmmo[BoxAmmoSlot] = 81;
        FamasWeapon pickup;
        edict_t entity{};
        entity.v.pContainingEntity = &entity;
        entity.pvPrivateData = &pickup;
        pickup.pev = &entity.v;
        pickup.ConfigureColdHost({nullptr, Current, ColdPrepare, ColdOwner, ColdInventory});
        pickup.Precache();
        weapon.m_iClip = 0;
        pickup.m_iDefaultAmmo = 40;
        Check(pickup.AddDuplicate(&weapon) && pickup.m_iDefaultAmmo == 0);
        Check(weapon.m_iClip == 35 && weapon.m_iPrimaryAmmoType == RifleAmmoSlot);
        Check(player.m_rgAmmo[RifleAmmoSlot] == limit - 7 && player.m_rgAmmo[BoxAmmoSlot] == 81);
        pickup.m_iClip = 10;
        Check(pickup.AddDuplicate(&weapon) && player.m_rgAmmo[RifleAmmoSlot] == limit);
        Check(weapon.m_iClip == 35 && player.m_rgAmmo[BoxAmmoSlot] == 81);
    }
    // Continuous automatic fire ignores the previous secondary cooldown and
    // does not require a new pressed edge for each round.
    Initialize();Ready();frame.pressed=0;weapon.PrimaryAttack();
    Check(weapon.m_iClip==29&&SawView(4)&&State().secondary==11);
    Ready(10.07F);weapon.PrimaryAttack();Check(weapon.m_iClip==28);
    Initialize();Ready();player.pev->button=1;
    for(int i=0;i<20;++i){globals.time=10.F+.07F*i;weapon.ItemPostFrame();}
    Check(weapon.m_iClip==10&&!weapon.m_pfnThink);
    // Required recoil is applied before fetching the actual shot angle.
    Initialize();Ready();player.pev->v_angle={1,2,3};player.pev->punchangle={0,0,0};
    events.clear();weapon.PrimaryAttack();
    Check(player.pev->punchangle==Vector(2,.5F,.25F)&&weapon.m_iClip==29);
    bool postRecoil=false;
    for(std::size_t at=0;at<events.size();at+=events[at+1]+2)
        if(events[at]==30&&events[at+1]==3&&events[at+2]==Bits(3)&&
            events[at+3]==Bits(2.5F)&&events[at+4]==Bits(3.25F))postRecoil=true;
    Check(postRecoil);
    // ID23 refills without reserve consumption or the ordinary observer.
    for(int reserve:{0,150,32000})for(int clip:{0,5,30,34})
    {
        Initialize();Ready();weapon.m_iClip=clip;player.m_rgAmmo[RifleAmmoSlot]=reserve;
        events.clear();weapon.Reload();
        const float duration=viewTimes.duration[clip?5:6];
        Check(SawView(clip?5:6)&&weapon.m_iClip==0&&State().loading&&State().action==2&&
            Bits(State().ownerAttack)==Bits(10.F+duration));
        globals.time=std::nextafter(State().ownerAttack,INFINITY);events.clear();weapon.ItemPostFrame();
        Check(weapon.m_iClip==35&&player.m_rgAmmo[RifleAmmoSlot]==reserve&&!State().loading);
        globals.time+=.01F;weapon.ItemPostFrame();Check(State().action==0);
    }
    Initialize();Ready();weapon.m_iClip=75;weapon.Reload();
    Check(weapon.m_iClip==75&&!State().loading);
    State().loading=true;State().action=2;State().ownerAttack=0;weapon.ItemPostFrame();
    Check(weapon.m_iClip==75&&!State().loading&&player.m_rgAmmo[RifleAmmoSlot]==150);
    Ready(20);weapon.ItemPostFrame();weapon.PrimaryAttack();Check(weapon.m_iClip==74);
    // Presentation delivery is optional. It cannot veto the input dispatcher,
    // mechanical reload completion, deployment or subsequent weapon use.
    for(bool absent:{false,true})
    {
        Initialize();if(absent)backends.effect=nullptr;else failEffect=true;
        Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready);
        Check(weapon.Deploy());Ready();player.pev->button=1;
        weapon.ItemPostFrame();Check(weapon.m_iClip==29&&weapon.CanDeploy());
        Check(weapon.Status()==FamasStatus::AppliedWithoutPresentation);
        Ready(12);player.pev->button=0;weapon.Reload();
        Check(State().loading&&weapon.m_iClip==0);
        globals.time=std::nextafter(State().ownerAttack,INFINITY);weapon.ItemPostFrame();
        Check(!State().loading&&weapon.m_iClip==35&&weapon.CanDeploy());
        weapon.Holster();Check(SawView(3)||absent);
        Check(weapon.Deploy());Ready(20);weapon.PrimaryAttack();Check(weapon.m_iClip==34);
    }
    Initialize();backends.firstEquip=nullptr;backends.playerAnimation=nullptr;backends.playerThink=nullptr;
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready&&weapon.Deploy());
    Ready();weapon.SecondaryAttack();Check(weapon.Status()==FamasStatus::Ineligible);
    weapon.PrimaryAttack();Check(weapon.m_iClip==29);
    // Missing timing does not synthesize a long lock, and only the action
    // requiring that timing is unavailable until the resource is repaired.
    Initialize();prepared.sequences.present&=~((1ULL<<1)|(1ULL<<2));
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready);
    Ready();Check(weapon.Deploy()&&weapon.CanDeploy());
    Check(State().primary==0&&State().ownerAttack==0);
    weapon.PrimaryAttack();Check(weapon.m_iClip==29);
    Initialize();prepared.sequences.present&=~(1ULL<<5);
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready&&weapon.Deploy());Ready();
    weapon.m_iClip=5;weapon.Reload();Check(weapon.m_iClip==5&&!State().loading);
    weapon.PrimaryAttack();Check(weapon.m_iClip==4);
    Initialize();prepared.sequences.duration[0]=NAN;prepared.aimOffset.x=NAN;
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready&&weapon.Deploy());Ready();
    weapon.WeaponIdle();Check(weapon.Status()==FamasStatus::AppliedWithoutPresentation);
    weapon.PrimaryAttack();Check(weapon.m_iClip==29);
    // A temporary per-use authority failure is retried on the next use.
    Initialize();Ready();ownerFrameReady=false;Check(weapon.CanDeploy());
    weapon.PrimaryAttack();Check(weapon.m_iClip==30);
    ownerFrameReady=true;weapon.PrimaryAttack();Check(weapon.m_iClip==29);
    // Same-map retained inventory may bind a newly authenticated life. A
    // snapshot or deferred task from the previous life is never replayed.
    Prime();weapon.AttachToPlayer(&player);Ready();weapon.PrimaryAttack();
    const int survivor=weapon.m_iClip;goldcraft::inventory::Snapshot old{};
    Check(weapon.SaveInventorySnapshot(old));weapon.Invalidate();
    playerThink.Invalidate();++player.m_iNumSpawns;++taskLife;backends.ownerLife=taskLife;
    Check(playerThink.Bind(player,{nullptr,TaskCurrent,TaskDispatch,1,taskLife,3}));
    Check(weapon.CanDeploy()&&weapon.m_iClip==survivor);
    Check(!weapon.RestoreInventorySnapshot(old));Check(weapon.Deploy());Ready(30);
    weapon.PrimaryAttack();Check(weapon.m_iClip==survivor-1);
    Prime();weapon.AttachToPlayer(&player);weapon.Invalidate();
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;Check(!weapon.CanDeploy());
    // Only a current-life model rebind may preserve the selected weapon state.
    Prime();weapon.AttachToPlayer(&player);Ready();weapon.Reload();
    goldcraft::inventory::Snapshot saved{};Check(weapon.SaveInventorySnapshot(saved));
    weapon.OwnerModelChanging();Check(weapon.RestoreInventoryBinding(player));
    Check(weapon.RestoreInventorySnapshot(saved));
    weapon.WeaponReloadCompleted(123);Check(weapon.m_fInReload==State().loading);
    // Fixed original aim/holster/idle sequences, independent of clip. Idle has
    // only its original idle/action gates, rather than copied G43 fire gates.
    for(int clip:{0,30,75})
    {
        Initialize();Ready();weapon.m_iClip=clip;frame.toggleAim=true;
        frame.pressed=0x40;player.pev->button=0x40;events.clear();weapon.ItemPostFrame();
        Check(State().aiming&&SawView(15)&&player.pev->fov==50);
        Ready(12);weapon.ItemPostFrame();Check(!State().aiming&&SawView(18)&&player.pev->fov==90);
        Ready(15);State().aiming=true;weapon.Holster();
        Check(SawView(3)&&State().aiming&&State().ownerAttack==15.5F&&player.pev->fov==90);
        Check(weapon.Deploy());Ready(20);State().primary=State().secondary=100;
        events.clear();weapon.WeaponIdle();Check(SawView(0));
    }
    Initialize();Ready();prepared.csPenetration=3;prepared.ballistics={};riflePenetration=3;rifleShots=0;
    Check(weapon.Bind(player,prepared,backends)==FamasStatus::Ready&&weapon.Deploy());Ready();
    weapon.PrimaryAttack();Check(rifleShots==1&&weapon.m_iClip==29);
    // Original Fidget instruction playback, including RNG before its aim gate.
    std::ifstream cases(argv[3]),golden(argv[4]);if(!cases||!golden)return 1;
    std::string caseLine,goldenLine;unsigned scenes=0;
    while(std::getline(cases,caseLine))
    {
        if(!std::getline(golden,goldenLine)||caseLine.empty()||goldenLine.empty())return 2;
        std::array<std::uint32_t,10> row{};std::istringstream fields(caseLine);fields>>std::hex;
        for(auto& v:row)if(!(fields>>v))return 2;
        std::string extra;if(fields>>extra)return 2;
        std::vector<std::uint32_t> expected;std::istringstream values(goldenLine);values>>std::hex;
        std::uint32_t value;while(values>>value)expected.push_back(value);
        if(!values.eof()||expected.size()<7)return 2;
        Initialize();globals.time=Float(row[0]);weapon.m_iClip=int(row[1]);State().clip=int(row[1]);
        State().primary=Float(row[2]);State().secondary=Float(row[3]);State().idle=Float(row[4]);
        State().action=int(row[5]);State().special=int(row[6]);State().aiming=row[7]!=0;
        fidgetChance=row[8];fidgetChoice=row[9];events.clear();fidgetMode=true;weapon.Fidget();fidgetMode=false;
        std::vector<std::uint32_t> actual{unsigned(weapon.m_iClip),Bits(State().primary),Bits(State().secondary),
            Bits(State().idle),unsigned(State().action),unsigned(State().special),unsigned(State().aiming)};
        actual.insert(actual.end(),events.begin(),events.end());
        if(actual!=expected){std::cerr<<"FAMAS original Fidget mismatch "<<scenes<<'\n';return 2;}
        ++scenes;
    }
    if(!scenes||std::getline(golden,goldenLine))return 2;
    std::cout<<"FAMAS SDK "<<checks<<" checks; original Fidget "<<scenes<<" scenes passed\n";
    return 0;
}
