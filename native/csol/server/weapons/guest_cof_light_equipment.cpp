// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_light_equipment.hpp"
#include "weapon_call_guard.h"
#include "weapon_slots.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/human_kick_damage.h"
#include "../../buy_menu.hpp"
#include "../../../guest/cof/light_equipment.hpp"
#include <algorithm>

namespace
{
namespace light = guest::cof::light;
constexpr char BurnSound[] = "guest/cof/flare/flare_burn.wav";
constexpr char IgniteSound[] = "guest/cof/flare/flare_ignite.wav";
constexpr float FlareBurnRadius = 96.0f;
constexpr float FlareBurnInterval = 1.0f;
constexpr const char* Models[]{light::Lantern.view, light::Lantern.player, light::Lantern.world,
    light::Flare.view, light::Flare.player, light::Flare.world, light::FlareLitPlayer};
constexpr const char* Sounds[]{BurnSound, IgniteSound, "guest/cof/item_get.wav", "guest/cof/mobile/punch_swing.wav",
    "guest/cof/flare/flare_movement1.wav", "guest/cof/flare/flare_movement2.wav",
    "guest/cof/flare/flare_movement3.wav", "guest/cof/flare/flare_strike1.wav", "guest/cof/flare/flare_strike2.wav"};
constexpr const char* Downloads[]{"sprites/weapon_cof_lantern.txt", "sprites/weapon_cof_flare.txt",
    "sprites/guest/cof/clantern.spr", "sprites/guest/cof/cflare.spr", "sprites/guest/cof/flare_ammo.spr",
    "aurora/guest/cof/signal.aur", "aurora/guest/cof/blacksmoke.aur", "aurora/guest/cof/flare_sparks.aur",
    "aurora/guest/cof/flare_glow.aur", "aurora/guest/cof/flare_extinguish.aur", "aurora/guest/cof/flare_sparks_short.aur",
    "sprites/guest/cof/fire.spr", "sprites/guest/cof/blacksmoke.spr", "sprites/guest/cof/yelflare2.spr",
    "sprites/guest/cof/muzzleflash.spr", "resource/csol/hud/v1/cof_lantern_weapon.tga",
    "resource/csol/hud/v1/cof_flare_weapon.tga", "resource/csol/hud/v1/cof_flare_ammo.tga"};
string_t litPlayerModel{};
int lanternPlayerIndex{}, lanternViewIndex{};
struct FlareDraw { EHANDLE player; int spawn{}; };
std::array<FlareDraw,33> firstDraws;

bool Registered(const light::Equipment& gear)
{
    const auto* info=CSOL_ItemInfo(gear.id);
    return info && info->pszName && !Q_strcmp(info->pszName,gear.classname);
}

class LightItem : public CBasePlayerWeapon
{
public:
    virtual const light::Equipment& Gear() const = 0;
    int iItemSlot() override { return Gear().hudSlot+1; }
    int GetItemInfo(ItemInfo* info) override
    {
        if(!info)return 0;
        const auto& gear=Gear();
        *info={};
        info->pszName=gear.classname;
        info->iId=m_iId=gear.id;
        info->iSlot=gear.hudSlot;
        info->iPosition=gear.id==light::Flare.id?3:0;
        info->iMaxAmmo1=info->iMaxAmmo2=-1;
        info->iMaxClip=gear.id==light::Flare.id?light::MaximumFlares:WEAPON_NOCLIP;
        info->iWeight=gear.id==light::Flare.id?1:5;
        return 1;
    }
    void Spawn() override
    {
        const auto& gear=Gear();
        m_iId=gear.id;
        if(!Registered(gear)){UTIL_Remove(this);return;}
        m_iClip=gear.id==light::Flare.id?1:WEAPON_NOCLIP;
        m_iDefaultAmmo=0;
        CBasePlayerWeapon::Spawn();
        SET_MODEL(edict(),gear.world);
        FallInit();
    }
    int AddToPlayer(CBasePlayer* player) override
    {
        if(!player || !Registered(Gear()) || !CSOL_CanUseExtendedWeapons(player))return FALSE;
        // Native ammo names start at 1. Index 0 is the reserved empty entry.
        // The unit count travels with this item through the native inventory and weaponbox.
        m_iPrimaryAmmoType=0;
        m_iSecondaryAmmoType=-1;
        return CBasePlayerItem::AddToPlayer(player);
    }
    BOOL UseDecrement() override { return TRUE; }
    BOOL IsUseable() override { return TRUE; }
    BOOL CanDeploy() override { return m_pPlayer && m_pPlayer->IsAlive() && IsUseable(); }

protected:
    void Delay(float seconds) { m_flNextPrimaryAttack=m_flNextSecondaryAttack=m_flTimeWeaponIdle=seconds; }
    bool Draw(int sequence,float duration,const char* playerModel,const char* animation)
    {
        CSOLWeaponCall call(*this);
        if(!call.Current() || !DefaultDeploy(const_cast<char*>(Gear().view),const_cast<char*>(playerModel),
            sequence,const_cast<char*>(animation),0) || !call.Current())return false;
        Delay(duration);
        m_pPlayer->m_flNextAttack=duration;
        return true;
    }
};

class Lantern final : public LightItem
{
public:
    const light::Equipment& Gear() const override { return light::Lantern; }
    void AttachToPlayer(CBasePlayer* player) override
    {
        LightItem::AttachToPlayer(player);
        life_ = player->m_iNumSpawns;
        activated_ = lit_ = player->m_pActiveItem == this;
    }
    BOOL Deploy() override
    {
        if (!Draw(0,34.f/30.f,Gear().player,"knife")) return FALSE;
        if (!activated_ || life_ != m_pPlayer->m_iNumSpawns)
        {
            life_ = m_pPlayer->m_iNumSpawns;
            activated_ = lit_ = true;
        }
        return TRUE;
    }
    bool Lit(const CBasePlayer& player) const
    {
        return lit_ && life_ == player.m_iNumSpawns;
    }
    void Toggle(CBasePlayer& player)
    {
        lit_ = !Lit(player);
        activated_ = true;
        life_ = player.m_iNumSpawns;
    }
    void Holster(int skip=0) override
    {
        if(!m_pPlayer)return;
        CSOLWeaponCall call(*this);
        SendWeaponAnim(4,0);
        if(!call.Current(false))return;
        m_pPlayer->m_flNextAttack=1.f;
        CBasePlayerItem::Holster(skip);
    }
    void WeaponIdle() override
    {
        if(m_flTimeWeaponIdle>0)return;
        m_flTimeWeaponIdle=99.f/20.f;
        SendWeaponAnim(2,0);
    }
    // There is no native magazine or primary attack for the lantern.
    void ItemPostFrame() override { WeaponIdle(); }
private:
    int life_{};
    bool activated_{}, lit_{};
};

// The slot is normally empty or holds one item. Use the native inventory as
// authority so drops, removal and slot reuse cannot leave a player light behind.
Lantern* CarriedLantern(CBasePlayer& player)
{
    for (auto* item = player.m_rgpPlayerItems[C4_SLOT]; item; item = item->m_pNext)
    {
        if (item->m_iId == light::Lantern.id && item->m_pPlayer == &player &&
            item->pev->owner == player.edict() && !(item->pev->flags & FL_KILLME))
            return dynamic_cast<Lantern*>(item);
    }
    return nullptr;
}

enum class FlareAction { Idle, Igniting, Throwing, Thrown, Discarding, Redrawing };
class Flare final : public LightItem
{
public:
    const light::Equipment& Gear() const override { return light::Flare; }
    BOOL IsUseable() override { return m_iClip>0; }
    BOOL Deploy() override;
    void Holster(int skip=0) override;
    void PrimaryAttack() override;
    void WeaponIdle() override;
    void ItemPostFrame() override;
    void NextAction();
    void WorldThink();
    bool ThrowEvent();
    int AddDuplicate(CBasePlayerItem* previous) override;
    void MoveStockTo(Flare& held);
    int AddToPlayer(CBasePlayer* player) override;
    bool Burning() const { return burnUntil_>0; }
private:
    FlareAction action_{};
    float burnUntil_{},soundAt_{},throwAt_{},finishAt_{};
    float nextBurnPulse_{};
    EHANDLE thrower_;
    std::uint64_t throwerGeneration_{}, throwerLife_{}, burnMap_{}, burnRound_{};
    bool spent_{},busy_{};
    void Schedule(FlareAction action,float seconds);
    void StopBurn();
    void RetireOrDraw(float after);
    void StartThrow(bool switching);
    void BurnNearbyMonsters();
};

LINK_ENTITY_TO_CLASS(weapon_cof_lantern,Lantern,CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_cof_flare,Flare,CCSPlayerWeapon)

void Flare::Schedule(FlareAction action,float seconds)
{
    action_=action;
    SetThink(&Flare::NextAction);
    pev->nextthink=gpGlobals->time+seconds;
}

int Flare::AddToPlayer(CBasePlayer* player)
{
    if(spent_ || m_iClip<=0)return FALSE;
    if(!LightItem::AddToPlayer(player))return FALSE;
    thrower_ = nullptr;
    nextBurnPulse_ = 0;
    busy_=false;
    action_=FlareAction::Idle;
    return TRUE;
}

void Flare::MoveStockTo(Flare& held)
{
    // A burning unit retains its original deadline in its own entity.
    if(spent_ || Burning() || held.m_iClip<0 || held.m_iClip>=light::MaximumFlares)return;
    const int moved=std::min(m_iClip,light::MaximumFlares-held.m_iClip);
    held.m_iClip+=moved;
    m_iClip-=moved;
}
int Flare::AddDuplicate(CBasePlayerItem* previous)
{
    auto* held=dynamic_cast<Flare*>(previous);
    if(!held || held==this)return FALSE;
    MoveStockTo(*held);
    return m_iClip==0;
}

BOOL Flare::Deploy()
{
    if(!m_pPlayer || !IsUseable())return FALSE;
    const int index=m_pPlayer->entindex();
    if(index<1 || index>=int(firstDraws.size()))return FALSE;
    auto& first=firstDraws[index];
    const bool seen=first.player.Get<CBasePlayer>()==m_pPlayer && first.spawn==m_pPlayer->m_iNumSpawns;
    const int sequence=Burning()?1:seen?0:31;
    const float duration=(sequence==31?44.f:sequence==1?29.f:24.f)/30.f;
    if(!Draw(sequence,duration,Burning()?light::FlareLitPlayer:Gear().player,"grenade"))return FALSE;
    first={EHANDLE(m_pPlayer),m_pPlayer->m_iNumSpawns};
    if(Burning()){
        EMIT_SOUND(m_pPlayer->edict(),CHAN_WEAPON,BurnSound,.3f,ATTN_NORM);
        soundAt_=gpGlobals->time+6.9f;
    }
    return TRUE;
}

void Flare::StopBurn()
{
    burnUntil_=0;
    if(m_pPlayer)STOP_SOUND(m_pPlayer->edict(),CHAN_WEAPON,BurnSound);
}

void Flare::StartThrow(bool switching)
{
    if(action_!=FlareAction::Idle || !Burning())return;
    throwAt_=gpGlobals->time+15.f/30.f;
    finishAt_=gpGlobals->time+.9f;
    Delay(switching?1.f:1.5f);
    if(switching)m_pPlayer->m_flNextAttack=1.f;
    Schedule(FlareAction::Throwing,15.f/30.f);
    SendWeaponAnim(7,0);
}

void Flare::Holster(int skip)
{
    if(!m_pPlayer)return;
    CSOLWeaponCall call(*this);
    if(Burning())StartThrow(true);
    else{
        // Switching before the ignition callback cancels the pending spark.
        if(action_==FlareAction::Igniting){action_=FlareAction::Idle;SetThink(nullptr);}
        if(action_==FlareAction::Idle){m_pPlayer->m_flNextAttack=.5f;SendWeaponAnim(6,0);}
    }
    if(call.Current(false))CBasePlayerItem::Holster(skip);
}

void Flare::PrimaryAttack()
{
    if(action_!=FlareAction::Idle || !m_pPlayer || m_iClip<1)return;
    if(Burning()){StartThrow(false);return;}
    Delay(1.5f);
    Schedule(FlareAction::Igniting,.86f);
    SendWeaponAnim(3,0);
}

void Flare::WeaponIdle()
{
    if(m_flTimeWeaponIdle>0 || action_!=FlareAction::Idle)return;
    const int idle=Burning()?5:2;
    if(m_pPlayer->pev->weaponanim!=idle)SendWeaponAnim(idle,0);
}

void Flare::RetireOrDraw(float after)
{
    if(m_iClip<=0){
        action_=FlareAction::Idle;SetThink(nullptr);
        if(m_pPlayer->m_pActiveItem==this){
            CSOLWeaponCall call(*this);
            RetireWeapon();
            if(!call.Current(false))return;
        }
        if(m_pPlayer->RemovePlayerItem(this))Kill();
        return;
    }
    if(m_pPlayer->m_pActiveItem==this){
        m_pPlayer->m_flNextAttack=1.5f;
        Delay(1.5f);
        Schedule(FlareAction::Redrawing,after);
    }else{action_=FlareAction::Idle;SetThink(nullptr);}
}

bool Flare::ThrowEvent()
{
    if(action_!=FlareAction::Throwing || busy_ || !Burning() || m_iClip<=0 ||
        gpGlobals->time<throwAt_ || !m_pPlayer)return false;
    CSOLWeaponCall call(*this);
    if(!call.Current(false) || !m_pPlayer->IsAlive())return false;
    CSOLScenarioSdkScope scope;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    if (!runtime || !runtime->session || !runtime->players || !scope.Current()) return false;
    const auto thrower = runtime->players->Find(EHANDLE(m_pPlayer));
    busy_=true;
    const auto& vars=*m_pPlayer->pev;
    UTIL_MakeVectors(vars.v_angle+vars.punchangle);
    const Vector eye=m_pPlayer->EyePosition();
    TraceResult hit{};
    UTIL_TraceLine(eye,eye+gpGlobals->v_forward*10.f,dont_ignore_monsters,m_pPlayer->edict(),&hit);
    if(!call.Current(false))return false;
    UTIL_MakeVectors(vars.v_angle);
    const Vector direction=gpGlobals->v_forward;
    const Vector velocity=(direction*300.f+vars.velocity)+direction*100.f;
    auto* created=CREATE_NAMED_ENTITY(MAKE_STRING(light::Flare.classname));
    if(!call.Current(false))return false;
    if(FNullEnt(created)){busy_=false;return false;}
    const EHANDLE fresh(created);
    created->v.origin=hit.vecEndPos;
    created->v.angles=vars.v_angle;
    created->v.spawnflags|=SF_NORESPAWN;
    const int spawned=DispatchSpawn(created);
    auto* dropped=dynamic_cast<Flare*>(fresh.Get<CBaseEntity>());
    if(!call.Current(false) || !dropped || spawned<0 || (created->v.flags&FL_KILLME)){
        if(dropped && !(dropped->pev->flags&FL_KILLME) && !dropped->m_pPlayer)UTIL_Remove(dropped);
        if(call.Current(false))busy_=false;
        return false;
    }
    dropped->burnUntil_=burnUntil_;
    dropped->thrower_ = m_pPlayer;
    dropped->throwerGeneration_ = thrower ? thrower->generation : 0;
    dropped->throwerLife_ = thrower ? thrower->skillLifeSerial : 0;
    dropped->burnMap_ = runtime->Generation();
    dropped->burnRound_ = runtime->session->Map().RoundGeneration();
    dropped->soundAt_=gpGlobals->time;
    dropped->pev->body=1;
    dropped->pev->velocity=velocity;
    dropped->SetTouch(nullptr);
    dropped->SetThink(&Flare::WorldThink);
    dropped->pev->nextthink=gpGlobals->time;
    --m_iClip;
    Schedule(FlareAction::Thrown,std::max(0.f,finishAt_-gpGlobals->time));
    busy_=false;
    StopBurn();
    if(call.Current() && m_pPlayer->pev->weaponmodel==litPlayerModel)
        m_pPlayer->pev->weaponmodel=MAKE_STRING(light::Flare.player);
    if(call.Current(false))EMIT_SOUND(m_pPlayer->edict(),CHAN_AUTO,RANDOM_LONG(0,1)?
        "guest/cof/flare/flare_movement2.wav":"guest/cof/flare/flare_movement1.wav",1.f,ATTN_NORM);
    return true;
}

void Flare::NextAction()
{
    if(!m_pPlayer || !m_pPlayer->IsAlive() || m_pPlayer->has_disconnected ||
       pev->owner!=m_pPlayer->edict()){SetThink(nullptr);action_=FlareAction::Idle;return;}
    const float now=gpGlobals->time;
    switch(action_){
    case FlareAction::Igniting:{
        CSOLScenarioSdkScope scope;
        const auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
        const auto row=runtime && runtime->session?runtime->session->Difficulty().SelectedRow():0;
        action_=FlareAction::Idle;SetThink(nullptr);
        if(!scope.Current() || row<1 || m_pPlayer->m_pActiveItem!=this || m_iClip<1)return;
        constexpr float times[]{120,90,60,30};
        burnUntil_=now+times[std::min<int>(row,4)-1];soundAt_=now+6.9f;
        m_pPlayer->pev->weaponmodel=litPlayerModel;
        CSOLWeaponCall call(*this);
        EMIT_SOUND(m_pPlayer->edict(),CHAN_AUTO,IgniteSound,1.f,ATTN_NORM);
        if(call.Current())EMIT_SOUND(m_pPlayer->edict(),CHAN_WEAPON,BurnSound,.3f,ATTN_NORM);
        return;}
    case FlareAction::Throwing:{
        const CSOLWeaponCall call(*this);
        if(!ThrowEvent() && call.Current(false) && !busy_){
            // Entity creation can fail. Keep the burning unit and its deadline.
            action_=FlareAction::Idle;SetThink(nullptr);
        }
        return;}
    case FlareAction::Thrown:RetireOrDraw(.5f);return;
    case FlareAction::Discarding:RetireOrDraw(0.f);return;
    case FlareAction::Redrawing:
        action_=FlareAction::Idle;SetThink(nullptr);
        if(m_pPlayer->m_pActiveItem==this && m_iClip>0){
            Delay(.86f);m_pPlayer->m_flNextAttack=.86f;SendWeaponAnim(0,0);
        }
        return;
    default:SetThink(nullptr);return;
    }
}

void Flare::ItemPostFrame()
{
    if(action_==FlareAction::Idle && Burning()){
        const float now=gpGlobals->time;
        if(now>burnUntil_){
            --m_iClip;
            m_pPlayer->pev->weaponmodel=MAKE_STRING(light::Flare.player);
            Schedule(FlareAction::Discarding,1.f);
            Delay(1.5f);m_pPlayer->m_flNextAttack=1.5f;
            CSOLWeaponCall call(*this);
            StopBurn();
            if(call.Current())SendWeaponAnim(16,0);
            return;
        }
        if(now>soundAt_){
            soundAt_=now+6.9f;
            CSOLWeaponCall call(*this);
            EMIT_SOUND(m_pPlayer->edict(),CHAN_WEAPON,BurnSound,.3f,ATTN_NORM);
            if(!call.Current())return;
        }
    }
    if(action_!=FlareAction::Idle)return;
    CBasePlayerWeapon::ItemPostFrame();
}

void Flare::BurnNearbyMonsters()
{
    CSOLScenarioSdkScope scope;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    const auto session = runtime ? runtime->session : nullptr;
    if (!session || !runtime->players || !runtime->players->Active() || !scope.Current() ||
        session->State() != CSOLScenarioSessionState::Active || runtime->Generation() != burnMap_ ||
        session->Map().RoundGeneration() != burnRound_) return;

    const EHANDLE self(this);
    const auto round = burnRound_;
    const auto current = [&] {
        return scope.Current() && runtime->session == session && session->Map().RoundGeneration() == round &&
            self.Get<CBaseEntity>() == this && !(pev->flags & FL_KILLME) && !m_pPlayer && !pev->owner &&
            (pev->flags & FL_ONGROUND) && gpGlobals->time < burnUntil_;
    };
    if (!current()) return;
    auto& pool = session->Monsters();
    if (!pool.ActiveCount() || pool.Busy()) return;
    const Vector origin = pev->origin;
    const auto inRange = [&](CCSOLScenarioMonster* target) {
        if (!target || !target->Type() || target->pev->deadflag != DEAD_NO || target->pev->health <= 0 ||
            target->pev->takedamage == DAMAGE_NO || (target->pev->effects & EF_NODRAW) ||
            (target->pev->flags & (FL_KILLME | FL_DORMANT))) return false;
        const auto delta = target->Center() - origin;
        return DotProduct(delta, delta) <= FlareBurnRadius * FlareBurnRadius;
    };
    // A death can recycle another pool slot before its turn in this pulse.
    struct Candidate { std::size_t slot; std::uint64_t lease; };
    std::array<Candidate, CCSOLMonsterPool::Capacity> candidates;
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < pool.Capacity; ++slot)
    {
        auto* target = pool.At(slot);
        if (inRange(target) && pool.Borrowed(*target))
            candidates[count++] = {slot, pool.Lease(*target)};
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!current()) return;
        const auto [slot, lease] = candidates[i];
        auto* target = pool.At(slot);
        if (!inRange(target) || !pool.Borrowed(*target) || pool.Lease(*target) != lease) continue;
        TraceResult trace{};
        UTIL_TraceLine(origin + Vector(0, 0, 1), target->Center(), ignore_monsters, edict(), &trace);
        if (!current()) return;
        if (trace.fStartSolid || trace.fAllSolid || trace.flFraction < 1.0f ||
            pool.At(slot) != target || !pool.Borrowed(*target) || pool.Lease(*target) != lease) continue;
        auto* player = thrower_.Get<CBasePlayer>();
        const auto state = player ? runtime->players->Find(thrower_) : nullptr;
        if (!state || state->generation != throwerGeneration_ || state->skillLifeSerial != throwerLife_ ||
            player->has_disconnected || (player->pev->flags & (FL_KILLME | FL_DORMANT))) player = nullptr;
        CSOL_GuestCofFlareMonsterDamage(*target, *this, player, runtime,
            [](const void* p) { return (*static_cast<const decltype(current)*>(p))(); }, &current);
    }
}

void Flare::WorldThink()
{
    if(m_pPlayer || !Burning())return;
    const float now=gpGlobals->time;
    if(now>burnUntil_){
        burnUntil_=0;spent_=true;m_iClip=0;pev->body=0;
        nextBurnPulse_ = 0;
        thrower_ = nullptr;
        SetThink(nullptr);SetTouch(nullptr);
        STOP_SOUND(edict(),CHAN_WEAPON,BurnSound);
        return;
    }
    pev->nextthink=now+.1f;
    const bool landed = (pev->flags & FL_ONGROUND) && pev->solid != SOLID_TRIGGER;
    if (landed || now > soundAt_) {
        CSOLScenarioSdkScope scope;
        const EHANDLE self(this);
        const auto worldItem = [&] {
            return scope.Current() && self.Get<CBaseEntity>() == this && !m_pPlayer && !pev->owner &&
                !(pev->flags & FL_KILLME);
        };
        if (landed) {
            pev->angles.x=pev->angles.z=0;
            pev->solid=SOLID_TRIGGER;
            // GoldSrc keeps solid and touch-only entities in separate area lists.
            // Match CBasePlayerItem::Materialize without replacing the burn think.
            UTIL_SetOrigin(pev,pev->origin);
            if (!worldItem()) return;
            SetTouch(&CBasePlayerItem::DefaultTouch);
        }
        if (now > soundAt_) {
            soundAt_=now+6.9f;
            EMIT_SOUND(edict(),CHAN_WEAPON,BurnSound,.3f,ATTN_NORM);
            if (!worldItem()) return;
        }
    }
    if (!(pev->flags & FL_ONGROUND) || now >= burnUntil_) return;
    if (!nextBurnPulse_) nextBurnPulse_ = now + FlareBurnInterval;
    if (now < nextBurnPulse_) return;
    nextBurnPulse_ += FlareBurnInterval;
    if (nextBurnPulse_ <= now) nextBurnPulse_ = now + FlareBurnInterval;
    BurnNearbyMonsters();
}

LightItem* Owned(CBasePlayer& player,int id)
{
    auto* item=dynamic_cast<LightItem*>(player.GetItemById(static_cast<WeaponIdType>(id)));
    return item && !(item->pev->flags&FL_KILLME) && item->m_pPlayer==&player &&
        item->pev->owner==player.edict()?item:nullptr;
}
} // namespace

const char* CSOL_LightEquipmentWorldModel(const CBasePlayerItem* item)
{
    if(!item || !light::Find(item->m_iId))return nullptr;
    const auto* lightItem=dynamic_cast<const LightItem*>(item);
    return lightItem?lightItem->Gear().world:nullptr;
}

int CSOL_FlareStock(const CBasePlayerItem* item)
{
    if(!item || item->m_iId!=light::Flare.id)return -1;
    const auto* flare=dynamic_cast<const Flare*>(item);
    return flare?flare->m_iClip:-1;
}

bool CSOL_SelectOwnedLantern(CBasePlayer& player)
{
    if(!Owned(player,light::Lantern.id))return false;
    player.SelectItem(light::Lantern.classname);
    return true;
}

bool CSOL_ToggleOwnedLantern(CBasePlayer& player)
{
    if (!lanternPlayerIndex || player.has_disconnected || player.m_iTeam != CT || !player.IsAlive()) return false;
    auto* lantern = CarriedLantern(player);
    if (!lantern) return false;
    lantern->Toggle(player);
    if (player.FlashlightIsOn()) player.FlashlightTurnOff();
    return true;
}

int CSOL_LanternPlayerState(const edict_s& actor, int weaponModel, bool firstPerson)
{
    const int heldIndex = firstPerson ? lanternViewIndex : lanternPlayerIndex;
    if (!heldIndex || actor.v.deadflag != DEAD_NO || actor.v.health <= 0 || !actor.pvPrivateData ||
        (actor.v.flags & (FL_DORMANT | FL_SPECTATOR | FL_KILLME))) return 0;
    auto& player = *static_cast<CBasePlayer*>(actor.pvPrivateData);
    const auto* lantern = CarriedLantern(player);
    if (!lantern || player.has_disconnected || player.m_iTeam != CT) return 0;
    return lantern->Lit(player) != (weaponModel == heldIndex) ? light::LanternStateBit : 0;
}

bool CSOL_TryFlareThrowEvent(CBasePlayer& player)
{
    auto* flare=dynamic_cast<Flare*>(player.m_pActiveItem);
    return flare && flare->ThrowEvent();
}

bool CSOL_CanReceiveLightEquipment(CBasePlayer& player,int id)
{
    const auto* gear=light::Find(id);
    if(!gear || !Registered(*gear) || !player.IsAlive() || player.has_disconnected || player.m_iTeam!=CT)return false;
    auto* existing=player.GetItemById(static_cast<WeaponIdType>(id));
    if(!existing)return true;
    const int stock=CSOL_FlareStock(existing);
    return stock>=0 && stock<light::MaximumFlares && existing->m_pPlayer==&player &&
        existing->pev->owner==player.edict() && !(existing->pev->flags&FL_KILLME);
}

bool CSOL_GiveLightEquipment(CBasePlayer& player,int id)
{
    const auto* gear=light::Find(id);
    if(!gear || !CSOL_CanReceiveLightEquipment(player,id))return false;
    if(auto* held=Owned(player,id)){
        if(held->m_iId!=light::Flare.id || held->m_iClip>=light::MaximumFlares)return false;
        ++held->m_iClip;
        return true;
    }
    CSOLScenarioSdkScope scope;
    const EHANDLE buyer(&player);
    const int life=player.m_iNumSpawns;
    const auto current=[&]{return scope.Current() && buyer.Get<CBasePlayer>()==&player && player.IsAlive() &&
        !player.has_disconnected && player.m_iNumSpawns==life;};
    if(!scope.Scenario() || !current())return false;
    auto* created=CREATE_NAMED_ENTITY(MAKE_STRING(gear->classname));
    if(!current() || FNullEnt(created))return false;
    const EHANDLE fresh(created);
    created->v.origin=player.pev->origin;
    created->v.spawnflags|=SF_NORESPAWN;
    const int spawned=DispatchSpawn(created);
    auto* item=fresh.Get<LightItem>();
    if(spawned>=0 && current() && item && !(item->pev->flags&FL_KILLME))
        DispatchTouch(created,player.edict());
    if(!scope.Current())return false;
    item=dynamic_cast<LightItem*>(fresh.Get<CBaseEntity>());
    if(item && current() && Owned(player,id)==item)return true;
    if(item && current() && player.GetItemById(static_cast<WeaponIdType>(id))==item)
        player.RemovePlayerItem(item);
    if(scope.Current() && (item=dynamic_cast<LightItem*>(fresh.Get<CBaseEntity>())) &&
       !(item->pev->flags&FL_KILLME) && !item->pev->owner &&
       (!item->m_pPlayer || (current() && item->m_pPlayer==&player && player.GetItemById(static_cast<WeaponIdType>(id))!=item)))
        UTIL_Remove(item);
    return false;
}

CSOLBoxPickup CSOL_PickupBoxLight(CWeaponBox& box,CBasePlayer& player,CBasePlayerItem& item)
{
    if(!CSOL_CanReceiveLightEquipment(player,item.m_iId))return CSOLBoxPickup::Keep;
    if(auto* held=Owned(player,item.m_iId)){
        auto* source=dynamic_cast<Flare*>(&item);
        auto* target=dynamic_cast<Flare*>(held);
        if(!source || !target)return CSOLBoxPickup::Keep;
        source->MoveStockTo(*target);
        if(source->m_iClip>0)return CSOLBoxPickup::Keep;
        source->Kill();
        return CSOLBoxPickup::Taken;
    }
    CSOLScenarioSdkScope scope;
    const EHANDLE buyer(&player),source(&item),container(&box);
    const int life=player.m_iNumSpawns;
    const bool added=player.AddPlayerItem(&item)!=FALSE;
    const auto current=[&]{return scope.Current() && buyer.Get<CBasePlayer>()==&player && player.IsAlive() &&
        player.m_iNumSpawns==life && source.Get<CBaseEntity>()==&item && container.Get<CBaseEntity>()==&box &&
        !(box.pev->flags&FL_KILLME) && !(item.pev->flags&FL_KILLME);};
    if(!current())return CSOLBoxPickup::Cancelled;
    if(!added || item.m_pPlayer!=&player)return CSOLBoxPickup::Keep;
    item.AttachToPlayer(&player);
    return current() && Owned(player,item.m_iId)==&item?CSOLBoxPickup::Taken:CSOLBoxPickup::Cancelled;
}

void CSOL_PrecacheLightEquipment()
{
    firstDraws={};litPlayerModel=0;lanternPlayerIndex=lanternViewIndex=0;
    CSOLScenarioSdkScope scope;
    if(!scope.Scenario() || !scope.Current() || !g_engfuncs.pfnGetFileSize ||
       GET_FILE_SIZE(csol::buy::EquipmentPresentationResource)<=0)return;
    const auto exists=[](const char* path){
        if(GET_FILE_SIZE(path)>0)return true;
        ALERT(at_error,"[CoF] light equipment missing %s\n",path);
        return false;
    };
    for(auto path:Models)if(!exists(path) || !scope.Current())return;
    for(auto path:Downloads)if(!exists(path) || !scope.Current())return;
    for(auto path:Sounds){
        char full[MAX_QPATH];Q_snprintf(full,sizeof(full),"sound/%s",path);
        if(!exists(full) || !scope.Current())return;
    }
    for(auto path:Models)if(PRECACHE_MODEL(path)<=0 || !scope.Current())return;
    for(auto path:Sounds)if(PRECACHE_SOUND(path)<=0 || !scope.Current())return;
    for(auto path:Downloads){PRECACHE_GENERIC(path);if(!scope.Current())return;}
    litPlayerModel=ALLOC_STRING(light::FlareLitPlayer);
    if(!litPlayerModel || !scope.Current())return;
    lanternPlayerIndex=MODEL_INDEX(light::Lantern.player);
    if(!scope.Current())return;
    lanternViewIndex=MODEL_INDEX(light::Lantern.view);
    if(!scope.Current())return;
    UTIL_PrecacheOtherWeapon(light::Lantern.classname);
    if(scope.Current())UTIL_PrecacheOtherWeapon(light::Flare.classname);
}
