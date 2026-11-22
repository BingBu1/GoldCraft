// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_arsenal.hpp"
#include "guest_cof_melee.hpp"
#include "melee_trace.h"
#include "../scenario/monsters/scenario_monster_pool.h"
#include "guest_cof_pushback.hpp"
#include "weapon_call_guard.h"
#include "weapon_slots.h"
#include "primary_purchase.h"
#include "../custom/action_stamina.h"
#include "../custom/body_stains.h"
#include "native/include/goldcraft/scenario_guest_weapon.hpp"
#include "../../buy_menu.hpp"
#include "native/guest/cof/arsenal.hpp"
#include "native/guest/cof/arsenal_audio.hpp"
#include "native/guest/cof/server/ballistics.hpp"
#include "native/guest/cof/server/effects_dispatch.hpp"
#include "native/guest/cof/server/screen_shake.hpp"
#include "native/guest/cof/spread.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace
{
using namespace guest::cof;
namespace arsenal = guest::cof::arsenal;
using namespace arsenal;

struct Prepared
{
    int shot{}, secondary{}, penetration{};
    string_t view{}, scope{};
    bool ready{}, cooperativeAudio{};
};
std::array<Prepared,Items.size()> prepared;
unsigned short recoilEvent{};
BallisticsEffects impact{};
int shakeMessage{}, shellModel{}, soundMessage{};

Prepared& Resources(Id id) { return prepared[id-Browning]; }
bool Current(void* context) noexcept { return static_cast<CSOLWeaponCall*>(context)->Current(); }
void Recoil(void* context,CBasePlayer& player,const Vector& amount) noexcept
{
    CooperativeViewRecoil(player,amount,recoilEvent,{context,Current});
}

constexpr const char *BrowningSounds[]{Gunshots[0].nativeAsset,
    "guest/cof/weapons/browning/browning_magin.wav","guest/cof/weapons/browning/browning_magout.wav"};
constexpr const char *RifleSounds[]{Gunshots[1].nativeAsset,
    "guest/cof/weapons/rifle/rifle_cock_back.wav","guest/cof/weapons/rifle/rifle_cock_forward.wav",
    "guest/cof/weapons/rifle/rifle_insert.wav","guest/cof/weapons/melee_swing.wav","guest/cof/weapons/melee_hit.wav",
    "guest/cof/player/vocal/breathing/hold_breath.wav","guest/cof/player/vocal/breathing/release_breath.wav"};
constexpr const char *AxeSounds[]{"guest/cof/weapons/axe/axe_swing.wav","guest/cof/weapons/axe/axe_hitbody.wav",
    "guest/cof/weapons/axe/axe_hit.wav"};
constexpr const char *BranchSounds[]{"guest/cof/weapons/branch/branch_swing.wav","guest/cof/weapons/branch/branch_hit.wav",
    "guest/cof/weapons/branch/branch_first_get.wav"};
constexpr const char *NightstickSounds[]{"guest/cof/weapons/nightstick/nightstick_swing.wav",
    "guest/cof/weapons/nightstick/nightstick_hitbody1.wav","guest/cof/weapons/nightstick/nightstick_hitbody2.wav",
    "guest/cof/weapons/nightstick/nightstick_hitwall1.wav","guest/cof/weapons/nightstick/nightstick_hitwall2.wav"};
constexpr const char *PickaxeSounds[]{"guest/cof/weapons/sledgehammer/sledgehammer_swing.wav",
    "guest/cof/weapons/sledgehammer/sledgehammer_grab.wav","guest/cof/weapons/sledgehammer/sledgehammer_hitbody.wav",
    "guest/cof/weapons/sledgehammer/sledgehammer_hit.wav"};
constexpr const char *BladeSounds[]{"guest/cof/weapons/switchblade/switchblade_swing.wav",
    "guest/cof/weapons/switchblade/switchblade_hitbody1.wav","guest/cof/weapons/switchblade/switchblade_hitbody2.wav",
    "guest/cof/weapons/switchblade/switchblade_hitwall1.wav","guest/cof/weapons/switchblade/switchblade_hitwall2.wav",
    "guest/cof/weapons/switchblade/switchblade_draw.wav","guest/cof/weapons/switchblade/switchblade_draw2.wav",
    "guest/cof/weapons/switchblade/switchblade_holster.wav","guest/cof/weapons/switchblade/switchblade_holster2.wav"};
constexpr std::span<const char* const> Sounds[]{BrowningSounds,RifleSounds,AxeSounds,BranchSounds,
    NightstickSounds,PickaxeSounds,BladeSounds};
constexpr const char* Lists[]{"sprites/weapon_cof_browning.txt","sprites/weapon_cof_rifle.txt","sprites/weapon_cof_axe.txt",
    "sprites/weapon_cof_branch.txt","sprites/weapon_cof_nightstick.txt","sprites/weapon_cof_sledgehammer.txt",
    "sprites/weapon_cof_switchblade.txt"};
constexpr const char* Icons[]{"sprites/guest/cof/cbrowning.spr","sprites/guest/cof/crifle.spr","sprites/guest/cof/caxe.spr",
    "sprites/guest/cof/cbranch.spr","sprites/guest/cof/cnightstick.spr","sprites/guest/cof/csledgehammer.spr",
    "sprites/guest/cof/cswitchblade.spr"};
constexpr const char* AmmoIcons[]{"sprites/guest/cof/browning_ammo.spr","sprites/guest/cof/rifle_ammo.spr"};
constexpr char Shell[]="models/guest/cof/common/shell.mdl";
constexpr char RifleShell[]="models/guest/cof/common/556shell.mdl";
int rifleShellModel{};
constexpr char Bubble[]="sprites/guest/cof/bubble.spr";
constexpr char RecoilScript[]="events/guest/cof/viewpunch.sc";

bool SoundExists(const char* name)
{
    char path[MAX_QPATH];
    Q_snprintf(path,sizeof(path),"sound/%s",name);
    return GET_FILE_SIZE(path)>0;
}
void Sound(CBasePlayer& owner,const char* name,int pitch=PITCH_NORM)
{
    EMIT_SOUND_DYN2(owner.edict(),CHAN_WEAPON,name,VOL_NORM,ATTN_NORM,0,pitch);
}
bool Registered(const Definition& def)
{
    const auto* info=CSOL_ItemInfo(def.id);
    return Resources(def.id).ready && info && info->pszName && !Q_strcmp(info->pszName,def.classname);
}
bool LoadDamage(const rapidjson::Value& group,const Definition& def,Prepared& out)
{
    if(!group.IsObject() || !group.HasMember(def.key) || !group[def.key].IsObject())return false;
    const auto& row=group[def.key];
    const auto read=[&](const char* key,int& value,int low,int high) {
        if(!row.HasMember(key) || !row[key].IsInt())return false;
        value=row[key].GetInt();return value>=low && value<=high;
    };
    if(!read("primary",out.shot,0,1000000))return false;
    if(def.id==Branch || def.id==Switchblade || def.id==Rifle)
        if(!read("secondary",out.secondary,0,1000000))return false;
    if (def.id != Rifle) return true;
    if (!row.HasMember("wallLayers")) return read("penetration", out.penetration, 3, 32);
    int layers{};
    if (!read("wallLayers", layers, 2, 31)) return false;
    out.penetration = layers + 1; // FireBullets3 consumes a contact for the target after the walls.
    return true;
}
bool PrepareItem(const Definition& def,const CSOLScenarioSdkScope& scope)
{
    const auto i=def.id-Browning;
    const bool gun=!def.Melee();
    for(const char* path:{def.view,def.player,def.world,def.card,def.art,Lists[i],Icons[i]})
        if(GET_FILE_SIZE(path)<=0 || !scope.Current())return false;
    if(gun && (GET_FILE_SIZE(def.ammoArt)<=0 || GET_FILE_SIZE(AmmoIcons[i])<=0))return false;
    if(def.id==Rifle && (GET_FILE_SIZE(ScopeModel)<=0 || GET_FILE_SIZE(RifleShell)<=0))return false;
    for(auto sound:Sounds[i])if(!SoundExists(sound) || !scope.Current())return false;
    if(def.id==Branch)
        for(auto sound:csol::server::cof::P345MeleeSamples)if(!SoundExists(sound) || !scope.Current())return false;
    if(!scope.Current())return false;
    for(auto path:{def.view,def.player,def.world})if(PRECACHE_MODEL(path)<=0 || !scope.Current())return false;
    if(def.id==Rifle){
        if(PRECACHE_MODEL(ScopeModel)<=0 || !scope.Current())return false;
        rifleShellModel=PRECACHE_MODEL(RifleShell);
        if(!rifleShellModel || !scope.Current())return false;
        Resources(def.id).scope=ALLOC_STRING(ScopeModel);
    }
    for(auto sound:Sounds[i])if(PRECACHE_SOUND(sound)<=0 || !scope.Current())return false;
    if(def.id==Branch && !PrecachePistolMeleeSounds(csol::server::cof::P345MeleeSamples))return false;
    for(auto path:{def.art,Lists[i],Icons[i]}){PRECACHE_GENERIC(path);if(!scope.Current())return false;}
    if(gun){PRECACHE_GENERIC(def.ammoArt);if(!scope.Current())return false;PRECACHE_GENERIC(AmmoIcons[i]);}
    if(!scope.Current())return false;
    if (gun && soundMessage && GET_FILE_SIZE(Gunshots[i].localFile) > 0) {
        if (!scope.Current()) return false;
        PRECACHE_GENERIC(Gunshots[i].localFile);
        if (!scope.Current()) return false;
        Resources(def.id).cooperativeAudio = true;
    }
    Resources(def.id).view=ALLOC_STRING(def.view);
    return scope.Current() && Resources(def.id).view && (def.id!=Rifle || Resources(def.id).scope);
}

class ArsenalWeapon : public CBasePlayerWeapon, public goldcraft::scenario::GuestWeapon
{
public:
    virtual Id Identity() const=0;
    goldcraft::scenario::GuestProfile ScenarioGuestProfile() const noexcept final
    {
        using namespace goldcraft::scenario;
        return {Identity(), GuestMetadata::NoCsoItem, GuestImpulse::WeaponOwnsHitBehavior,
            GuestKill::StandardPlayerProcessing};
    }
    const Definition& Info() const { return *arsenal::Find(Identity()); }
    int iItemSlot() override { return Info().hudSlot+1; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL IsUseable() override { return TRUE; }
    BOOL CanDeploy() override { return m_pPlayer && m_pPlayer->IsAlive() && Registered(Info()); }
    int GetItemInfo(ItemInfo* info) override
    {
        if(!info)return 0;
        const auto& def=Info();
        *info={};
        m_iId=info->iId=def.id;
        info->pszName=def.classname;info->iSlot=def.hudSlot;info->iPosition=def.position;
        info->pszAmmo1=def.ammo;info->iMaxAmmo1=def.reserve;info->iMaxAmmo2=-1;
        info->iMaxClip=def.clip;info->iWeight=def.Melee()?1:5;
        return 1;
    }
    void Spawn() override
    {
        const auto& def=Info();
        m_iId=def.id;
        if(!Registered(def)){UTIL_Remove(this);return;}
        m_iClip=def.Melee()?WEAPON_NOCLIP:0;
        m_iDefaultAmmo=def.Melee()?0:def.clip;
        CBasePlayerWeapon::Spawn();
        SET_MODEL(edict(),def.world);
        FallInit();
    }
    int AddToPlayer(CBasePlayer* player) override
    {
        if(!player || !CSOL_CofArsenalReady(*player,Identity()))return FALSE;
        if(!Info().Melee())return CBasePlayerWeapon::AddToPlayer(player);
        m_iPrimaryAmmoType=0;m_iSecondaryAmmoType=-1;
        return CBasePlayerItem::AddToPlayer(player);
    }
    BOOL Deploy() override
    {
        const auto& def=Info();
        CSOLWeaponCall call(*this);
        const int sequence=DrawSequence();
        deployed=false;
        if(!call.Current() || !DefaultDeploy(const_cast<char*>(def.view),const_cast<char*>(def.player),
            sequence,const_cast<char*>(Extension()),0) || !call.Current())return FALSE;
        life=m_pPlayer->m_iNumSpawns;
        m_pPlayer->m_flNextAttack=DrawDuration();
        Lock(m_pPlayer->m_flNextAttack);
        deployed=true;
        return TRUE;
    }
    void Holster(int skip=0) override
    {
        deployed=false;
        CBasePlayerItem::Holster(skip);
    }
    bool Deployed() const { return deployed && m_pPlayer && m_pPlayer->m_pActiveItem==this && life==m_pPlayer->m_iNumSpawns; }
protected:
    bool BeginFrame()
    {
        if (Deployed()) return true;
        // A survivor can retain the active entity through a native respawn.
        // Re-deploy that entity once so delayed attacks from the old life expire.
        if (deployed && m_pPlayer && m_pPlayer->m_pActiveItem == this &&
            life != m_pPlayer->m_iNumSpawns && m_pPlayer->IsAlive() &&
            pev->owner == m_pPlayer->edict() && m_pPlayer->GetItemById(static_cast<WeaponIdType>(m_iId)) == this)
            Deploy();
        return false;
    }
    virtual int DrawSequence() const=0;
    virtual float DrawDuration() const=0;
    virtual const char* Extension() const=0;
    void Lock(float seconds){m_flNextPrimaryAttack=m_flNextSecondaryAttack=m_flTimeWeaponIdle=seconds;}
    bool deployed{};
    int life{};
};

constexpr float UnitsPerMetre = 39.37f;
struct HandAttack {
    float reach, halfAngleCosine, pushSpeed, stagger;
    bool sector;
};

constexpr HandAttack HandAttackFor(Id id, bool reverse, bool punch)
{
    // Half-angle cosines for total arcs of 60, 180, 45 and 30 degrees.
    switch (id) {
    case Axe: return {2.8f * UnitsPerMetre, .8660254f, 0, 0, true};
    case Branch: return {(punch ? 1.8f : 2.f) * UnitsPerMetre, 0, 500, 1.f, !punch};
    case Nightstick: return {1.5f * UnitsPerMetre, 0, 200, .5f, false};
    case Pickaxe: return {3.f * UnitsPerMetre, 0, 0, 1.5f, true};
    case Switchblade: return {(reverse ? 1.f : 1.5f) * UnitsPerMetre,
        reverse ? .96592583f : .92387953f, 0, 0, true};
    default: return {};
    }
}

void HandContact(CBasePlayer& owner, CBaseEntity& target, const Vector& start, const Vector& direction,
    TraceResult& trace, float damage, int flags, const HandAttack& attack, CSOLWeaponCall& call)
{
    const EHANDLE victim(&target);
    auto* monster = dynamic_cast<CCSOLScenarioMonster*>(&target);
    auto* pool = monster ? monster->Pool() : nullptr;
    const auto lease = pool ? pool->Lease(*monster) : 0;
    const auto current = [&] {
        return call.Current() && victim.Get<CBaseEntity>() == &target &&
            (!pool || (monster->Pool() == pool && pool->Borrowed(*monster) && pool->Lease(*monster) == lease));
    };
    const Vector impactDirection = direction;
    const float health = target.pev->health;
    struct RestoreDamage {
        MULTIDAMAGE saved;
        ~RestoreDamage() { gMultiDamage = saved; }
    } restore{gMultiDamage};
    ClearMultiDamage();
    if (!current()) return;
    CSOLBodyStainShot swing(&owner, start, owner.m_pActiveItem);
    swing.TraceAttack(&target, owner.pev, damage, impactDirection, &trace, flags);
    if (!current()) return;
    ApplyMultiDamage(owner.pev, owner.pev);
    if (attack.stagger && pool && current() && target.pev->health < health && target.pev->health > 0) {
        Vector away = target.Center() - owner.pev->origin;
        if (away.x == 0 && away.y == 0) away = impactDirection;
        monster->ApplyCofMeleeControl(away, attack.pushSpeed, attack.stagger);
    }
}

class HandWeapon : public ArsenalWeapon
{
    enum class Contact { None, Swing, Punch };
    Contact contact{};
    float due{};
    bool reverse{};
    const char* Extension() const override { return "knife"; }
    int DrawSequence() const override
    {
        if(Identity()==Switchblade && reverse)return 6;
        return Identity()==Axe || Identity()==Pickaxe ? 0 : 1;
    }
    float DrawDuration() const override
    {
        if(Identity()==Axe)return 24.F/30;
        if(Identity()==Pickaxe)return 49.F/30;
        return 29.F/30;
    }
    bool PickaxeStamina(bool debit)
    {
        CSOLScenarioSdkScope scope;
        const auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
        return scope.Current() && CSOL_GuestCofPickaxeSwing(runtime, *m_pPlayer, debit);
    }
    void Strike(CSOLWeaponCall& call);
public:
    BOOL Deploy() override
    {
        contact=Contact::None;
        if(!ArsenalWeapon::Deploy())return FALSE;
        if(Identity()==Switchblade)Sound(*m_pPlayer,BladeSounds[reverse?6:5]);
        return TRUE;
    }
    void Holster(int skip=0) override
    {
        contact=Contact::None;
        if(m_pPlayer && Identity()==Switchblade)Sound(*m_pPlayer,BladeSounds[reverse?8:7]);
        ArsenalWeapon::Holster(skip);
    }
    void PrimaryAttack() override
    {
        if(!Deployed() || contact!=Contact::None)return;
        int sequence{};float delay{},primary{},idle{};
        const char* swing=nullptr;
        switch(Identity()){
        case Axe: sequence=2;delay=.26F;primary=idle=29.F/30;swing=AxeSounds[0];break;
        case Branch: sequence=RANDOM_LONG(3,4);delay=.37F;primary=idle=35.F/38;swing=BranchSounds[0];break;
        case Nightstick: sequence=RANDOM_LONG(3,5);delay=.31F;primary=idle=1;break;
        case Pickaxe:
            if(!PickaxeStamina(false))return;
            sequence=1;delay=1;primary=idle=2.66F;break;
        case Switchblade:
            sequence=RANDOM_LONG(0,2)==0 ? (reverse?8:3) : (reverse?18:16);
            delay=.16F;primary=reverse?.9F:.5F;idle=reverse?1.2F:1.F;
            if(!reverse)swing=BladeSounds[0];
            break;
        default:return;
        }
        CSOLWeaponCall call(*this);
        contact=Contact::Swing;due=gpGlobals->time+delay;
        m_flNextPrimaryAttack=primary;m_flTimeWeaponIdle=idle;
        if(Identity()==Switchblade)m_flNextSecondaryAttack=idle;
        SendWeaponAnim(sequence,0);
        if(call.Current())m_pPlayer->SetAnimation(PLAYER_ATTACK1);
        if(swing && call.Current())Sound(*m_pPlayer,swing);
    }
    void SecondaryAttack() override
    {
        if(!Deployed() || contact!=Contact::None || m_flNextPrimaryAttack>0)return;
        if(Identity()==Branch){
            contact=Contact::Punch;due=gpGlobals->time+.3F;Lock(1);
            SendWeaponAnim(8,0);
        }else if(Identity()==Switchblade){
            reverse=!reverse;Lock(1.5F);SendWeaponAnim(reverse?4:9,0);
        }
    }
    void ItemPostFrame() override
    {
        if (!BeginFrame()) return;
        CSOLWeaponCall call(*this);
        if(contact!=Contact::None && gpGlobals->time>=due){
            const auto action=contact;contact=Contact::None;
            if(action==Contact::Punch){
                const PistolMeleeEffects fx{shakeMessage,true,Recoil,&call,csol::server::cof::P345MeleeSamples};
                const auto attack = HandAttackFor(Branch, false, true);
                const int damage = Resources(Branch).secondary;
                struct Punch { CBasePlayer& owner; CSOLWeaponCall& call; HandAttack attack; int damage; }
                    punch{*m_pPlayer, call, attack, damage};
                PistolWeaponMelee(*m_pPlayer, {damage, false, MeleeWorld::Multiplayer, attack.reach,
                    [](void* context, CBaseEntity& target, const Vector& direction, TraceResult& trace) noexcept {
                        auto& hit = *static_cast<Punch*>(context);
                        HandContact(hit.owner, target, hit.owner.GetGunPosition(), direction, trace,
                            float(hit.damage), 0x04001000, hit.attack, hit.call);
                    }, &punch}, fx, {Current, &call});
            }else Strike(call);
            if(!call.Current())return;
        }
        if((m_pPlayer->pev->button&IN_ATTACK2) && m_flNextSecondaryAttack<=0)SecondaryAttack();
        else if((m_pPlayer->pev->button&IN_ATTACK) && m_flNextPrimaryAttack<=0)PrimaryAttack();
        else if(m_flTimeWeaponIdle<=0)WeaponIdle();
    }
    void WeaponIdle() override
    {
        int seq=0;
        if(Identity()==Axe)seq=1;
        if(Identity()==Pickaxe)seq=2;
        if(Identity()==Switchblade && reverse)seq=5;
        if(m_pPlayer->pev->weaponanim!=seq)SendWeaponAnim(seq,0);
    }
};

void HandWeapon::Strike(CSOLWeaponCall& call)
{
    const auto id=Identity();
    auto& owner=*m_pPlayer;
    if(id==Pickaxe && !PickaxeStamina(true))return;
    UTIL_MakeVectors(owner.pev->v_angle);
    const Vector direction=gpGlobals->v_forward, start=owner.GetGunPosition();
    const auto attack = HandAttackFor(id, reverse, false);
    const Vector end = start + direction * attack.reach;
    if (!call.Current()) return;
    TraceResult trace{};
    UTIL_TraceLine(start, end, dont_ignore_monsters, owner.edict(), &trace);
    if (!call.Current()) return;
    if (trace.flFraction >= 1 && id == Nightstick)
        UTIL_TraceHull(start, end, dont_ignore_monsters, head_hull, owner.edict(), &trace);
    if (!call.Current()) return;
    const EHANDLE first(trace.pHit);
    if(id==Nightstick || (id==Switchblade && reverse))Sound(owner,Sounds[id-Browning][0]);
    if(!call.Current())return;
    if(id==Pickaxe)Recoil(&call,owner,Vector(-4,1,8));
    if(id==Nightstick)Recoil(&call,owner,Vector(-3,-8,0));
    if(id==Switchblade)Recoil(&call,owner,reverse?Vector(-4,1,0):Vector(2,-5,0));
    if (!call.Current()) return;
    const int damage = (id == Switchblade && reverse) ? Resources(id).secondary : Resources(id).shot;
    const int flags = id == Pickaxe ? 0x0d001000 : 0x04001000;
    bool contact = trace.flFraction < 1, flesh = false;
    const auto strike = [&](CBaseEntity& target, const Vector& hitDirection, TraceResult& hit) {
        const int kind = target.Classify();
        flesh |= kind != CLASS_NONE && kind != CLASS_MACHINE;
        contact = true;
        if (call.Current()) HandContact(owner, target, start, hitDirection, hit, float(damage), flags, attack, call);
    };
    if (auto* target = first.Get<CBaseEntity>(); contact && target) strike(*target, direction, trace);
    if (attack.sector) {
        const Vector horizontal = Vector(direction.x, direction.y, 0).Normalize();
        int cursor = 0;
        while (call.Current()) {
            edict_t* found = g_engfuncs.pfnFindEntityInSphere(cursor ? INDEXENT(cursor) : nullptr, start, attack.reach);
            if (!call.Current() || FNullEnt(found)) break;
            const int index = ENTINDEX(found);
            if (index <= cursor || index >= gpGlobals->maxEntities) break;
            cursor = index;
            auto* other = CBaseEntity::Instance(found);
            if (!other || other == &owner || found == first.Get() || other->pev->takedamage == DAMAGE_NO ||
                (other->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
            TraceResult hit{};
            const EHANDLE candidate(other);
            const bool touches = CSOL_TraceMeleeTarget(*other, owner, start, direction, horizontal,
                attack.reach, attack.halfAngleCosine,
                [](CBaseEntity& entity, const Vector& origin, float) { return entity.BodyTarget(origin); }, hit);
            if (!call.Current()) return;
            if (!touches || candidate.Get<CBaseEntity>() != other) continue;
            const Vector displacement = hit.vecEndPos - start;
            if (DotProduct(displacement, displacement) > attack.reach * attack.reach) continue;
            strike(*other, displacement.Normalize(), hit);
        }
    }
    if (!contact) return;
    if(!call.Current())return;
    const char* hit{};int pitch=100;
    if(id==Axe || id==Branch){
        hit=id==Axe ? AxeSounds[flesh?1:2] : BranchSounds[1];pitch+=RANDOM_LONG(-10,10);
    }else if(id==Pickaxe)hit=PickaxeSounds[flesh?2:3];
    else hit=Sounds[id-Browning][(flesh?1:3)+RANDOM_LONG(0,1)];
    if(call.Current())Sound(owner,hit,pitch);
}

class Gun : public ArsenalWeapon
{
protected:
    void Eject(bool rifle,CSOLWeaponCall& call)
    {
        const auto& owner=*m_pPlayer;
        UTIL_MakeVectors(owner.pev->v_angle);
        const auto& forward=gpGlobals->v_forward;
        const auto& right=gpGlobals->v_right;
        const auto& up=gpGlobals->v_up;
        auto origin=owner.pev->origin+owner.pev->view_ofs+forward*(rifle?60.F:80.F)+up*20;
        auto velocity=right*200+up*(rifle?60.F:220.F)+forward*40;
        EjectWeaponShell({origin,velocity,owner.pev->angles.y,rifle?rifleShellModel:shellModel,1},{&call,Current});
    }
    bool Fire(float spread,bool scoped,CSOLWeaponCall& call)
    {
        auto& owner=*m_pPlayer;
        const bool rifle=Identity()==Rifle;
        const auto& cfg=Resources(Identity());
        const int kick = rifle ? (scoped ? 4 : 9) : 2;
        const float yaw = static_cast<float>(RANDOM_LONG(-kick,kick));
        const Vector impulse(yaw, rifle ? (scoped ? 3.F : 4.F) : 1.F, rifle ? (scoped ? 2.F : 4.F) : 1.F);
        if (!call.Current()) return false;
        Recoil(&call, owner, impulse);
        if (!call.Current()) return false;
        const bool duck = (owner.pev->flags & FL_DUCKING) != 0;
        ScreenShakeRequest shake{owner.pev->origin, 5.F, .25F, .15F, 2.F};
        if (rifle && scoped && duck) shake = {owner.pev->origin, 2.F, .1F, .1F, 2.F};
        else if (rifle && !scoped && duck) shake = {owner.pev->origin, 5.F, .3F, .2F, 4.F};
        else if (rifle && !scoped) shake = {owner.pev->origin, 7.F, .4F, .3F, 6.F};
        ShakePlayers(shake, shakeMessage, {&call, Current});
        if (!call.Current()) return false;
        UTIL_MakeVectors(owner.pev->v_angle+owner.pev->punchangle);
        PistolBallistics shot{owner.GetGunPosition(),gpGlobals->v_forward,Vector(spread,spread,spread),
            gpGlobals->v_right,gpGlobals->v_up,static_cast<unsigned>(owner.random_seed),cfg.shot,scoped,
            PistolPlayerHits::OriginalCooperativeSkipPlayers,PistolSource::NormalMuzzle};
        const BallisticsCall lease{Current,&call};
        const auto check=rifle ? CheckCSRifleBallistics(shot,lease,cfg.penetration,CSRifleMaterial::Rifle762)
                               : CheckPistolBallistics(shot,impact,lease);
        if(check!=BallisticsStatus::Ready || !call.Current())return false;
        --m_iClip;
        SendWeaponAnim(rifle?3:2,0);
        if(!call.Current())return false;
        owner.SetAnimation(PLAYER_ATTACK1);
        if(!call.Current())return false;
        owner.pev->effects|=EF_MUZZLEFLASH;
        if(!call.Current())return false;
        const auto result=rifle ? FireCSRifleBallistics(owner,shot,impact,lease,cfg.penetration,CSRifleMaterial::Rifle762)
                                : FirePistolBallistics(owner,shot,impact,lease);
        if(result.status!=BallisticsStatus::Fired || !call.Current())return false;
        const auto& sample = Gunshots[Identity() - Browning];
        if (cfg.cooperativeAudio)
            SendPlaySfx(owner, {sample.localKey, {}, 0, 1, true, PlaySfxRules::Cooperative},
                soundMessage, {&call, Current});
        else
            Sound(owner, sample.nativeAsset);
        return call.Current();
    }
    bool HasReserve() const
    {
        return m_iPrimaryAmmoType>0 && m_iPrimaryAmmoType<MAX_AMMO_SLOTS && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]>0;
    }
};

class BrowningWeapon final : public Gun
{
    float buildup{}, decay{}, magazineIn{};
    Id Identity() const override { return Browning; }
    int DrawSequence() const override { return 0; }
    float DrawDuration() const override { return 35.F/30; }
    const char* Extension() const override { return "onehanded"; }
public:
    BOOL Deploy() override { magazineIn=buildup=decay=0;return Gun::Deploy(); }
    void Holster(int skip=0) override { magazineIn=0;Gun::Holster(skip); }
    void PrimaryAttack() override
    {
        if(!Deployed() || magazineIn)return;
        if(m_pPlayer->pev->waterlevel==3 || m_iClip<=0){
            if(m_pPlayer->pev->waterlevel!=3 && HasReserve())Reload();
            else {PlayEmptySound();m_flNextPrimaryAttack=.25F;}
            return;
        }
        const float next=buildup+.75F;
        const float spread=next<1?0.F:next<2?.00873F:next<3?.02618F:next<4?.05234F:.08716F;
        CSOLWeaponCall call(*this);
        if(!Fire(spread,false,call))return;
        buildup=next;
        m_flNextPrimaryAttack=m_iClip>0?.25F:1.F;
        m_flNextSecondaryAttack=m_flTimeWeaponIdle=1.F;
        Eject(false,call);
    }
    void Reload() override
    {
        if(!Deployed() || magazineIn || m_iClip>=13 || !HasReserve() || m_flNextPrimaryAttack>0)return;
        CSOLWeaponCall call(*this);
        m_iClip=0;
        magazineIn=gpGlobals->time+1.F; // sequence 3 is the one-second holster
        Lock(1.F);SendWeaponAnim(3,0);
        if(call.Current())Sound(*m_pPlayer,BrowningSounds[2]);
    }
    void ItemPostFrame() override
    {
        if (!BeginFrame()) return;
        const float now=gpGlobals->time;
        if(now>decay){buildup=std::max(0.F,buildup-.25F);decay=now+.1F;}
        if(magazineIn){
            if(now<magazineIn)return;
            magazineIn=0;
            CSOLWeaponCall call(*this);
            if (m_iClip < Info().clip && HasReserve()) {
                auto& reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
                const int loaded = std::min(Info().clip - m_iClip, reserve);
                m_iClip += loaded;
                reserve -= loaded;
                if (!CSOL_ScenarioSkillReload(*this) || !call.Current()) return;
            }
            Lock(1);SendWeaponAnim(0,0);
            if(call.Current())Sound(*m_pPlayer,BrowningSounds[1]);
            return;
        }
        if((m_pPlayer->m_afButtonPressed&IN_ATTACK) && m_flNextPrimaryAttack<=0)PrimaryAttack();
        else if(m_pPlayer->pev->button&IN_RELOAD)Reload();
        else if(m_flTimeWeaponIdle<=0 && m_pPlayer->pev->weaponanim!=1)SendWeaponAnim(1,0);
    }
};

class HuntingRifle final : public Gun
{
    enum class Action { Ready, Zoom, PostZoom, StartReload, Reload, Insert, Push };
    Action action{};
    float due{}, eject{}, bolt{};
    bool scoped{}, pullingBolt{}, cancelReload{}, breath{};
    float breathAfter{};
    SpreadState spread;
    SpreadTransitions transitions;
    Id Identity() const override { return Rifle; }
    int DrawSequence() const override { return 1; }
    float DrawDuration() const override { return 29.F/30; }
    const char* Extension() const override { return "shotgun"; }
    void Schedule(Action next,float delay){action=next;due=gpGlobals->time+delay;}
    void ZoomOut()
    {
        scoped=false;bolt=0;
        m_pPlayer->m_iFOV=DEFAULT_FOV;m_pPlayer->pev->fov=DEFAULT_FOV;
        m_pPlayer->pev->viewmodel=Resources(Rifle).view;
        if(breath){breath=false;Sound(*m_pPlayer,RifleSounds[7]);}
    }
    void Scope()
    {
        if(action!=Action::Ready || m_flNextPrimaryAttack>0 || m_flNextSecondaryAttack>0)return;
        CSOLWeaponCall call(*this);
        if(scoped){
            ZoomOut();
            if(!call.Current())return;
            SendWeaponAnim(8,0);
        }else{
            scoped=true;Schedule(Action::Zoom,.1F);SendWeaponAnim(7,0);
        }
        if(call.Current())Lock(.25F);
    }
    void BeginReload()
    {
        if(m_iClip>=Info().clip || !HasReserve())return;
        cancelReload=false;Schedule(Action::Reload,.66F);Lock(1);SendWeaponAnim(4,0);
    }
    void Step(CSOLWeaponCall& call)
    {
        const auto previous=action;
        action=Action::Ready;
        switch(previous){
        case Action::Zoom:
            m_pPlayer->m_iFOV=85;m_pPlayer->pev->fov=85;Schedule(Action::PostZoom,.15F);break;
        case Action::PostZoom:
            m_pPlayer->m_iFOV=30;m_pPlayer->pev->fov=30;
            m_pPlayer->pev->viewmodel=Resources(Rifle).scope;break;
        case Action::StartReload: BeginReload();break;
        case Action::Reload:
            if(cancelReload || m_iClip>=Info().clip || !HasReserve()){
                cancelReload=false;Lock(.66F);SendWeaponAnim(6,0);
            }else{
                Schedule(Action::Insert,.3F);Lock(.83F);SendWeaponAnim(5,0);
            }
            break;
        case Action::Insert:
            if(m_iClip<Info().clip && HasReserve()){
                ++m_iClip;--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
                Sound(*m_pPlayer,RifleSounds[3],95+RANDOM_LONG(0,10));
            }
            if(call.Current())Schedule(Action::Reload,.5F);
            break;
        case Action::Push:{
            CSOLScenarioSdkScope scope;
            auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
            struct Context { CSOLWeaponCall& call; CBasePlayer& player; decltype(runtime) map; } ctx{call,*m_pPlayer,runtime};
            csol::server::cof::NativePushbackHost host{{&ctx,[](void* p) noexcept {return static_cast<Context*>(p)->call.Current();}},
                [](void* p,float& amount,bool& infinite) noexcept {auto& c=*static_cast<Context*>(p);
                    return c.call.Current() && CSOL_ReadGuestCofPushbackStamina(c.map,c.player,amount,infinite);},
                [](void* p,float requested) noexcept {auto& c=*static_cast<Context*>(p);
                    return c.call.Current() && CSOL_DebitGuestCofPushbackStamina(c.map,c.player,requested);},
                {RifleSounds[4],RifleSounds[5]},recoilEvent,float(Resources(Rifle).secondary),false};
            csol::server::cof::NativeLongGunPushback(*m_pPlayer,runtime,host);
            break;
        }
        case Action::Ready:break;
        }
    }
public:
    BOOL Deploy() override
    {
        action=Action::Ready;scoped=breath=cancelReload=pullingBolt=false;eject=bolt=breathAfter=0;
        spread={};transitions={};
        return Gun::Deploy();
    }
    void Holster(int skip=0) override
    {
        action=Action::Ready;eject=bolt=0;
        if(m_pPlayer && scoped)ZoomOut();
        Gun::Holster(skip);
    }
    void PrimaryAttack() override
    {
        if(!Deployed() || action!=Action::Ready)return;
        if(m_iClip<=0 || m_pPlayer->pev->waterlevel==3){
            if(m_pPlayer->pev->waterlevel!=3 && HasReserve())Reload();
            else {SendWeaponAnim(18,0);PlayEmptySound();m_flNextPrimaryAttack=.25F;}
            return;
        }
        CSOLWeaponCall call(*this);
        if(!Fire(scoped?.00873F:spread.vector[0],scoped,call))return;
        spread.scalar+=5;spread.recoveryDeadline=gpGlobals->time+1.8F;
        if(scoped){pullingBolt=true;bolt=gpGlobals->time+.66F;}
        eject=gpGlobals->time+.76F;Lock(1.66F);
    }
    void SecondaryAttack() override
    {
        if(!Deployed() || scoped || action!=Action::Ready || m_flNextPrimaryAttack>0)return;
        CSOLScenarioSdkScope scope;
        auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
        float amount{};bool infinite{};
        if(!CSOL_ReadGuestCofPushbackStamina(runtime,*m_pPlayer,amount,infinite) || amount<=0)return;
        Schedule(Action::Push,.23F);Lock(1);SendWeaponAnim(12,0);
    }
    void Reload() override
    {
        if(!Deployed() || action!=Action::Ready || m_flNextPrimaryAttack>0 || m_iClip>=Info().clip || !HasReserve())return;
        if(scoped){
            CSOLWeaponCall call(*this);
            ZoomOut();if(!call.Current())return;
            Schedule(Action::StartReload,14.F/30);Lock(14.F/30);SendWeaponAnim(8,0);
        }else BeginReload();
    }
    void ItemPostFrame() override
    {
        if (!BeginFrame()) return;
        CSOLWeaponCall call(*this);
        const float now=gpGlobals->time;
        const int pressed=m_pPlayer->m_afButtonPressed;
        auto* actor=m_pPlayer->pev;
        const auto flags = static_cast<std::uint32_t>(actor->flags & (FL_ONGROUND | FL_DUCKING));
        UpdateSpread(spread,transitions,{actor->velocity.x,actor->velocity.y,now,flags,11,true});
        if(scoped && !breath && (actor->button&IN_RUN) && now>breathAfter){
            CSOLScenarioSdkScope scope;float amount{};bool infinite{};
            const auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
            if(CSOL_ReadGuestCofPushbackStamina(runtime,*m_pPlayer,amount,infinite) && amount>=1){
                breath=true;breathAfter=now+1;Sound(*m_pPlayer,RifleSounds[6]);
            }
        }
        if(!call.Current())return;
        if(bolt && now>bolt){
            const bool back=pullingBolt;
            pullingBolt=false;bolt=back?now+.33F:0;
            Sound(*m_pPlayer,RifleSounds[back?1:2]);
            if(!call.Current())return;
            Recoil(&call,*m_pPlayer,Vector(0,0,back?15:-15));
        }
        if(!call.Current())return;
        if(eject && now>eject){eject=0;Eject(true,call);}
        if(!call.Current())return;
        if((action==Action::Reload || action==Action::Insert) && (pressed&IN_ATTACK))cancelReload=true;
        if(action!=Action::Ready && now>=due){Step(call);return;}
        if(action!=Action::Ready)return;
        if(pressed&IN_CANCEL)Scope();
        else if((pressed&IN_ATTACK) && m_flNextPrimaryAttack<=0)PrimaryAttack();
        else if((actor->button&IN_ATTACK2) && m_flNextSecondaryAttack<=0)SecondaryAttack();
        else if(actor->button&IN_RELOAD)Reload();
        else if(!scoped && m_flTimeWeaponIdle<=0 && actor->weaponanim!=0)SendWeaponAnim(0,0);
    }
};

template<Id id> class Melee final : public HandWeapon
{
    Id Identity() const override { return id; }
};
using AxeWeapon=Melee<Axe>;
using BranchWeapon=Melee<Branch>;
using NightstickWeapon=Melee<Nightstick>;
using PickaxeWeapon=Melee<Pickaxe>;
using SwitchbladeWeapon=Melee<Switchblade>;
}

LINK_ENTITY_TO_CLASS(weapon_cof_browning,BrowningWeapon,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_rifle,HuntingRifle,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_axe,AxeWeapon,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_branch,BranchWeapon,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_nightstick,NightstickWeapon,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_sledgehammer,PickaxeWeapon,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_switchblade,SwitchbladeWeapon,CCSPlayerWeapon)

void CSOL_PrecacheCofArsenal()
{
    prepared={};impact={};soundMessage=shakeMessage=shellModel=rifleShellModel=recoilEvent=0;
    CSOLScenarioSdkScope scope;
    if(!scope.Scenario() || !scope.Current() || !g_engfuncs.pfnGetFileSize ||
        GET_FILE_SIZE(csol::buy::ArsenalPresentationResource)<=0)return;
    int size{};
    auto* raw=LOAD_FILE_FOR_ME(DamageFile,&size);
    if(!raw)return;
    rapidjson::Document doc;
    if(size>0 && size<=65536)doc.Parse(reinterpret_cast<const char*>(raw),std::size_t(size));
    FREE_FILE(raw);
    if(!scope.Current() || !doc.IsObject())return;
    if(GET_FILE_SIZE(RecoilScript)>0)recoilEvent=PRECACHE_EVENT(1,RecoilScript);
    if(!scope.Current())return;
    if(GET_FILE_SIZE(Shell)>0)shellModel=PRECACHE_MODEL(Shell);
    if(!scope.Current())return;
    if(GET_FILE_SIZE(Bubble)>0)impact.bubbleModel=PRECACHE_MODEL(Bubble);
    if(!scope.Current())return;
    impact.customDecalMessage=REG_USER_MSG(guest::cof::resources::CustomDecalMessage,18);
    if(!scope.Current())return;
    shakeMessage=REG_USER_MSG("ScreenShake",6);
    if (!scope.Current()) return;
    soundMessage=RegisterPlaySfxMessage({&scope, [](void* context) noexcept {
        return static_cast<CSOLScenarioSdkScope*>(context)->Current();
    }});
    for(const auto& def:Items){
        if(!scope.Current())return;
        auto& state=Resources(def.id);
        if(!LoadDamage(doc,def,state) || ((def.id==Rifle || def.id==Branch) && !recoilEvent) ||
           (def.id==Browning && (!shellModel || !impact.bubbleModel || impact.customDecalMessage<64)) ||
           !PrepareItem(def,scope)){
            if(scope.Current())ALERT(at_warning,"[CoF] Could not prepare %s\n",def.classname);
            continue;
        }
        if(!scope.Current())return;
        UTIL_PrecacheOtherWeapon(def.classname);
        if(!scope.Current())return;
        const auto* info=CSOL_ItemInfo(def.id);
        state.ready=info && info->pszName && !Q_strcmp(info->pszName,def.classname);
    }
}
bool CSOL_CofArsenalReady(CBasePlayer& player,int id) noexcept
{
    const auto* def=arsenal::Find(id);
    return def && Registered(*def) && player.m_iTeam==CT && player.IsAlive() &&
        CSOL_CanUseExtendedWeapons(&player) && CSOL_UsesScenarioRules();
}
bool CSOL_IsCofArsenal(const CBasePlayerWeapon& weapon,int id) noexcept
{
    const auto* def=arsenal::Find(id);
    const auto* concrete=dynamic_cast<const ArsenalWeapon*>(&weapon);
    return def && concrete && concrete->Identity()==id && weapon.m_iId==id &&
        weapon.pev && FClassnameIs(weapon.pev,def->classname);
}
bool CSOL_CofArsenalDeployed(const CBasePlayerWeapon& weapon,int id) noexcept
{
    return CSOL_IsCofArsenal(weapon,id) && static_cast<const ArsenalWeapon&>(weapon).Deployed();
}
const char* CSOL_CofArsenalWorldModel(const CBasePlayerItem* item)
{
    if(!item || !arsenal::Find(item->m_iId))return nullptr;
    const auto* gun=dynamic_cast<const CBasePlayerWeapon*>(item);
    return gun && CSOL_IsCofArsenal(*gun,item->m_iId) ? arsenal::Find(item->m_iId)->world : nullptr;
}
bool CSOL_GiveCofArsenalGun(CBasePlayer& player,int id)
{
    const auto* def=arsenal::Find(id);
    if(!def || def->Melee())return false;
    return CSOL_DeliverAdaptedGun(player,{id,def->hudSlot+1,def->classname,CSOL_CofArsenalReady,
        CSOL_IsCofArsenal,CSOL_CofArsenalDeployed,false});
}
