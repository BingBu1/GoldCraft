// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "inventory_recovery.hpp"
#include "automatic_weapon.hpp"
#include "../mp5_primary.hpp"
#include "../magazine_reload.hpp"
#include "../catalog.hpp"

#include <cmath>
#include <cstring>
#include <type_traits>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof::server
{
namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
}

template<LongGun Weapon>
goldcraft::scenario::GuestProfile AutomaticWeapon<Weapon>::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {HostId,GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior,GuestKill::StandardPlayerProcessing};
}
template<LongGun Weapon>
struct AutomaticWeapon<Weapon>::InventoryState
{
    MagazineActionState action;
    SpreadState spread;
    SpreadTransitions transitions;
    guest::cof::AutomaticSoundState sound;
    float actionDeadline{},emptyDeadline{},wearDeadline{},nextThink{};
    float primary{},secondary{},idle{},ownerAttack{};
    int reserveHud{},wearMaximum{},wear{},clip{},inReload{},fireOnEmpty{};
    int ownerSerial{},weaponSerial{},modelIndex{},ownerSpawn{};
    std::uintptr_t map{},ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    bool selected{},meleePending{};
    TMPStatus status{};
};
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease=lease_;
    if(!bound_||!pev||m_pPlayer!=lease.player||!Current(const_cast<Lease*>(&lease)))return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState)<=512);
    InventoryState saved{};
    saved.action=action_;saved.spread=spread_;saved.transitions=transitions_;saved.sound=sound_;
    saved.actionDeadline=actionDeadline_;saved.emptyDeadline=emptyDeadline_;saved.wearDeadline=wearDeadline_;
    saved.nextThink=pev->nextthink;saved.think=m_pfnThink;
    saved.primary=m_flNextPrimaryAttack;saved.secondary=m_flNextSecondaryAttack;saved.idle=m_flTimeWeaponIdle;
    saved.ownerAttack=m_pPlayer->m_flNextAttack;saved.clip=m_iClip;
    saved.inReload=m_fInReload;saved.fireOnEmpty=m_fFireOnEmpty;
    saved.reserveHud=reserveHud_;saved.wearMaximum=wearMaximum_;saved.wear=wear_;
    saved.ownerSerial=lease.ownerSerial;saved.weaponSerial=lease.weaponSerial;saved.modelIndex=lease.modelIndex;
    saved.ownerSpawn=m_pPlayer->m_iNumSpawns;
    saved.map=reinterpret_cast<std::uintptr_t>(worldLease_.context);
    saved.ownerContext=reinterpret_cast<std::uintptr_t>(lease.context);
    saved.selected=selected_;saved.meleePending=meleePending_;saved.status=status_;
    output.kind=InventoryKind;output.bytes=sizeof(saved);
    std::memcpy(output.data.data(),&saved,sizeof(saved));return true;
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::RestoreInventoryBinding(CBasePlayer& player) noexcept
{
    if(!pev||m_pPlayer!=&player)return false;
    const auto pending=modelRebind_;modelRebind_.reset();const auto previous=lease_;
    if(bound_&&Current(const_cast<Lease*>(&previous))&&pev->owner==player.edict())return true;
    const auto world=worldLease_;const auto cold=cold_;const auto prepared=prepared_;
    if(!WorldCurrent(world)||!cold.ownerBackends)return false;
    const auto same=[&]() noexcept {
        return !pending||(pending->owner==player.edict()&&pending->ownerSerial==player.edict()->serialnumber&&
            pending->weaponSerial==edict()->serialnumber&&pending->spawn==player.m_iNumSpawns&&
            pending->map==worldLease_.context&&pending->selected==(player.m_pActiveItem==this));
    };
    if(!same()){Invalidate();return false;}
    const EHANDLE owner(&player);TMPBackends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found||!same()||
        (pending&&(!backend.ownerLife||pending->life!=backend.ownerLife)))return false;
    if(Bind(player,prepared,backend)!=TMPStatus::Ready)return false;
    const auto lease=lease_;AttachToPlayer(&player);if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    if(selected_&&!PublishOwnerAim()){const auto result=status_;Invalidate();status_=result;return false;}
    if(pending&&pending->meleePending)
    {meleePending_=true;SetThink(&AutomaticWeapon<Weapon>::DelayedMelee);pev->nextthink=pending->nextThink;}
    return true;
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
{
    const auto lease=lease_;
    if(input.kind!=InventoryKind||input.bytes!=sizeof(InventoryState)||!bound_||
       !Current(const_cast<Lease*>(&lease))||m_pPlayer!=lease.player)return false;
    InventoryState saved{};std::memcpy(&saved,input.data.data(),sizeof(saved));
    if(saved.ownerSerial!=lease.ownerSerial||saved.weaponSerial!=lease.weaponSerial||saved.modelIndex!=lease.modelIndex||
       saved.ownerSpawn!=m_pPlayer->m_iNumSpawns||saved.map!=reinterpret_cast<std::uintptr_t>(worldLease_.context)||
       saved.ownerContext!=reinterpret_cast<std::uintptr_t>(lease.context)||
       (saved.selected&&m_pPlayer->m_pActiveItem!=this))return false;
    action_=saved.action;spread_=saved.spread;transitions_=saved.transitions;sound_=saved.sound;
    actionDeadline_=saved.actionDeadline;emptyDeadline_=saved.emptyDeadline;wearDeadline_=saved.wearDeadline;
    m_pfnThink=saved.think;pev->nextthink=saved.nextThink;
    m_flNextPrimaryAttack=saved.primary;m_flNextSecondaryAttack=saved.secondary;m_flTimeWeaponIdle=saved.idle;
    if(saved.selected)m_pPlayer->m_flNextAttack=saved.ownerAttack;
    m_iClip=saved.clip;m_fInReload=saved.inReload;m_fFireOnEmpty=saved.fireOnEmpty;
    reserveHud_=saved.reserveHud;wearMaximum_=saved.wearMaximum;wear_=saved.wear;
    selected_=saved.selected;meleePending_=saved.meleePending;status_=saved.status;
    return !selected_||PublishOwnerAim();
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::PublishOwnerAim() noexcept
{
    auto lease=lease_;lease.active=true;
    if(!selected_||!Current(&lease)||!prepared_.aimMetadataReady)return false;
    // Host rollback publishes the existing proved aim state. It must not call
    // SetAim, which starts a new animation and changes the original deadlines.
    TMPEvent event{TMPEffect::Aim};event.enabled=action_.aiming;
    event.first=action_.aiming?prepared_.aimOffset:Vector{};
    event.value=action_.aiming?prepared_.aimZoom:0;event.a=350;
    if(!Emit(event))return false;
    lease.player->pev->fov=action_.aiming?float(prepared_.aimZoom):90.0F;
    return true;
}


template<LongGun Weapon>
void AutomaticWeapon<Weapon>::OwnerModelChanging() noexcept
{
    TMPOwnerFrame frame{};
    if(!Check(frame,false)||!ResourcesReady()||!backends_.ownerLife||
        selected_!=(m_pPlayer->m_pActiveItem==this)||!std::isfinite(pev->nextthink)||
        (meleePending_&&m_pfnThink!=static_cast<decltype(m_pfnThink)>(&AutomaticWeapon<Weapon>::DelayedMelee)))
    {Invalidate();return;}
    const ModelRebind pending{lease_.owner,worldLease_.context,backends_.ownerLife,
        lease_.ownerSerial,lease_.weaponSerial,m_pPlayer->m_iNumSpawns,pev->nextthink,selected_,meleePending_};
    Invalidate();modelRebind_=pending;
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::Current(void* context) noexcept
{
    const auto& l = *static_cast<const Lease*>(context);
    // Edicts remain engine-owned while private objects may disappear/recycle.
    return l.current && l.current(l.context) && l.owner && l.weapon &&
        !l.owner->free && !l.weapon->free && l.owner->serialnumber == l.ownerSerial &&
        l.weapon->serialnumber == l.weaponSerial && l.owner->pvPrivateData == l.player &&
        l.weapon->pvPrivateData == l.item && l.player->pev == &l.owner->v &&
        l.player->pev->modelindex == l.modelIndex &&
        l.player->pev->deadflag == DEAD_NO && l.player->pev->health > 0 &&
        (!l.active || (l.player->m_pActiveItem == l.item && l.item->selected_));
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::ConfigureColdHost(typename AutomaticWeapon<Weapon>::ColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
template<LongGun Weapon>
TMPStatus AutomaticWeapon<Weapon>::Prepare(const TMPPrepared& p) noexcept
{
    preparedReady_ = false;
    status_ = TMPStatus::MissingBackend;
    if (!p.modelsReady || !p.soundsReady || !p.particleReady || !p.aimMetadataReady ||
        !p.inventoryReady || !p.pickupSoundReady || !p.deployPreludeReady || !p.automaticFireReady || !p.automaticTailReady ||
        p.world != MeleeWorld::Multiplayer || !p.worldModelString || !p.inventoryItemString ||
        !p.viewModelString || !p.playerModelString || p.shellModel <= 0 ||
        !p.melee.recoil || !p.melee.soundsReady || !ValidPistolMeleeSamples(p.melee.samples) ||
        p.melee.shakeMessage <= 0 ||
        p.ballistics.customDecalMessage <= 0 || p.ballistics.bubbleModel <= 0 ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnIndexOfEdict)
        return status_;
    if (!Finite(p.aimOffset) || p.aimZoom < 0 || p.aimZoom > 255 ||
        p.hostSlot != HudSlot || p.hostPosition != HudPosition)
        return status_ = TMPStatus::InvalidInput;
    for (int i = 0; i < SequenceCount; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = TMPStatus::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = TMPStatus::Ready;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Precache()
{
    preparedReady_ = false;
    status_ = TMPStatus::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    TMPPrepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

template<LongGun Weapon>
TMPStatus AutomaticWeapon<Weapon>::Bind(CBasePlayer& owner, const TMPPrepared& p,
                          const TMPBackends& backends) noexcept
{
    Invalidate();
    status_ = TMPStatus::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != RequiredEffects || !backends.playerAnimation)
        return status_;
    if (Prepare(p) != TMPStatus::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = TMPStatus::InvalidInput;
    status_ = TMPStatus::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, Weapon,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = TMPStatus::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias TMP to an unrelated stock CS weapon.
    m_iId = HostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = TMPStatus::Ready;
}

template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Spawn()
{
    action_ = {};sound_ = {};modelRebind_.reset();
    spread_ = {};spread_.scalar = 0;
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = DefaultAmmo;
    m_iSecondaryAmmoType = -1;
    m_iClip = 0;
    m_fInReload = m_fFireOnEmpty = 0;
    bound_ = selected_ = meleePending_ = false;
    worldUse_ = false;
    if (!pev) return;
    SetThink(nullptr); SetTouch(nullptr); SetUse(nullptr);
    pev->nextthink = 0;
    Precache();
    const auto l = worldLease_;
    if (!WorldCurrent(l)) return;
    // Actual ReGameDLL API factory must supply the CCSPlayerWeapon wrapper.
    // A static ItemInfo query remains valid even when this gate rejects Spawn.
    if (!preparedReady_ || !m_pEntity || !g_engfuncs.pfnSetOrigin || !g_engfuncs.pfnSetSize)
    { status_ = TMPStatus::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = TMPStatus::InvalidInput; return; }
    m_iId = HostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : DefaultAmmo;
    m_iClip = pev->health == -2 ? -666 : 0;
    pev->renderfx = 99;
    SET_MODEL(edict(), STRING(prepared_.worldModelString));
    if (!WorldCurrent(l)) return;
    pev->movetype = MOVETYPE_TOSS;
    pev->solid = SOLID_BBOX;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    UTIL_SetSize(pev, g_vecZero, g_vecZero);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&AutomaticWeapon<Weapon>::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&AutomaticWeapon<Weapon>::WorldTouch);
    SetThink(&AutomaticWeapon<Weapon>::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = TMPStatus::Applied;
}

template<LongGun Weapon>
int AutomaticWeapon<Weapon>::GetItemInfo(ItemInfo* info)
{
    // Original 1007E220 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = Weapon == LongGun::TMP ? "weapon_cof_tmp" : "weapon_cof_mp5";
    info->pszAmmo1 = Weapon == LongGun::TMP ? "cof_tmp" : "cof_mp5";
    info->iMaxAmmo1 = 300;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 30;
    info->iId = m_iId = HostId;
    info->iSlot = HudSlot;
    info->iPosition = HudPosition;
    info->iWeight = 5;
    return 1;
}

template<LongGun Weapon>
int AutomaticWeapon<Weapon>::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&AutomaticWeapon<Weapon>::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&AutomaticWeapon<Weapon>::WorldTouch);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WorldTouch(CBaseEntity* other)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !host.inventoryPickup || !WorldCurrent(l) || !other || !other->pev) return;
    auto* otherEdict = other->edict();
    const int otherSerial = otherEdict->serialnumber;
    const bool isPlayer = other->IsPlayer() != FALSE;
    if (!WorldCurrent(l) || otherEdict->free || otherEdict->serialnumber != otherSerial ||
        otherEdict->pvPrivateData != other || !isPlayer) return;
    auto* player = static_cast<CBasePlayer*>(other);
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const auto current = [&]() {
        return WorldCurrent(l) && !owner->free && owner->serialnumber == serial &&
            owner->pvPrivateData == player && player->pev->deadflag == DEAD_NO && player->pev->health > 0;
    };
    if (!current() || !g_pGameRules) return;
    const bool canHave = g_pGameRules->CanHavePlayerItem(player, this) != FALSE;
    if (!current() || !canHave) return;
    if (pev->iuser4 == 1) pev->iuser4 = 0;
    const bool inventory = host.inventoryPickup(host.context, *player, *this, prepared_.inventoryItemString);
    if (!current()) return;
    if (!inventory) { status_ = TMPStatus::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = TMPStatus::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        TMPEvent event{TMPEffect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
template<LongGun Weapon>
int AutomaticWeapon<Weapon>::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    TMPBackends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != TMPStatus::Ready) return FALSE;
    TMPOwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = TMPStatus::Ineligible; return FALSE; }
    return TRUE;
}
template<LongGun Weapon>
int AutomaticWeapon<Weapon>::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == TMPStatus::Ready || status_ == TMPStatus::Applied);
}
template<LongGun Weapon>
int AutomaticWeapon<Weapon>::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
template<LongGun Weapon>
int AutomaticWeapon<Weapon>::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<AutomaticWeapon<Weapon>*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    TMPOwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = TMPStatus::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 30 ? amount : 30;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, Weapon == LongGun::TMP ? "cof_tmp" : "cof_mp5", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({TMPEffect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
template<LongGun Weapon>
int AutomaticWeapon<Weapon>::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<AutomaticWeapon<Weapon>*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    TMPOwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, Weapon == LongGun::TMP ? "cof_tmp" : "cof_mp5", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::AttachToPlayer(CBasePlayer* player)
{
    const auto l = lease_;
    if (!bound_ || !player || player != m_pPlayer || !Current(const_cast<Lease*>(&l))) return;
    // ReGameDLL clears model/modelindex and cancels the fall think. The latter
    // safely replaces original Attach's cleared think with a pending +.1 clock.
    CBasePlayerItem::AttachToPlayer(player);
    if (!Current(const_cast<Lease*>(&l))) return;
    SetUse(nullptr);
    worldUse_ = false;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Drop() { RetireWorld(false); }
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Kill() { RetireWorld(true); }
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = TMPStatus::UnsupportedState;
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::Check(TMPOwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = TMPStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = TMPStatus::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = TMPStatus::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || frame.meleeDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 90 && frame.reserveLimit != 300) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = TMPStatus::InvalidInput; return false; }
    ReadAmmo();
    if ((action_.clip < 0 && (active || action_.clip != -666)) ||
        action_.reserve < 0)
    { status_ = TMPStatus::InvalidInput; return false; }
    status_ = TMPStatus::Ready;
    return true;
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::Emit(TMPEvent event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l)) return false;
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = TMPStatus::MissingBackend;
    return delivered;
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, Weapon, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::View(int sequence) noexcept
{
    TMPEvent event{TMPEffect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::AutomaticSoundEvent(void* context,const guest::cof::AutomaticSoundEvent& value) noexcept
{
    auto& self=*static_cast<AutomaticWeapon<Weapon>*>(context);
    TMPEvent event{TMPEffect::AutomaticSound};event.sound=value;return self.Emit(event);
}
template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::AutomaticSound(bool start,const TMPOwnerFrame& frame) noexcept
{
    const AutomaticSoundFrame state{action_.clip,action_.action,action_.loading,true,
        bool(m_pPlayer->pev->button&1u),true,frame.quietAudio,
        prepared_.automaticFireReady,prepared_.automaticTailReady};
    struct Call{AutomaticWeapon<Weapon>& item;Lease lease;};Call call{*this,lease_};call.lease.active=true;
    const AutomaticSoundHost host{&call,[](void* p) noexcept{return Current(&static_cast<Call*>(p)->lease);},
        [](void* p,const guest::cof::AutomaticSoundEvent& event) noexcept{return AutomaticSoundEvent(&static_cast<Call*>(p)->item,event);}};
    const auto result=start?StartAutomaticNativeSound(sound_,state,host):AdvanceAutomaticNativeSound(sound_,state,host);
    if(result==AutomaticSoundResult::Unchanged||result==AutomaticSoundResult::Started||result==AutomaticSoundResult::Stopped)return true;
    status_=result==AutomaticSoundResult::Cancelled?TMPStatus::Cancelled:TMPStatus::MissingBackend;return false;
}

template<LongGun Weapon>
void AutomaticWeapon<Weapon>::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::MirrorTimers(bool selected) noexcept
{
    // UseDecrement=false keeps weapon clocks absolute. ReGameDLL's player gate
    // is nevertheless a CLIENT_WEAPONS countdown; this selected adapter owns
    // its absolute ownerAttack gate. Holster restores a native countdown lock.
    m_flNextPrimaryAttack = action_.primary;
    m_flNextSecondaryAttack = action_.secondary;
    m_flTimeWeaponIdle = action_.idle;
#ifdef CLIENT_WEAPONS
    m_pPlayer->m_flNextAttack = selected ? 0.0f : action_.ownerAttack - gpGlobals->time;
#else
    m_pPlayer->m_flNextAttack = selected ? gpGlobals->time : action_.ownerAttack;
#endif
}
template<LongGun Weapon>
BOOL AutomaticWeapon<Weapon>::CanDeploy()
{
    TMPOwnerFrame frame{};
    Lease binding=lease_;
    if (!bound_ || !Current(&binding)) RecoverOwnedInventoryBinding(*this);
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
template<LongGun Weapon>
BOOL AutomaticWeapon<Weapon>::Deploy()
{
    TMPOwnerFrame frame{};
    Lease binding=lease_;
    if (!bound_ || !Current(&binding)) RecoverOwnedInventoryBinding(*this);
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_=true;
    if(action_.clip==-666){action_.clip=0;WriteAmmo();}
    action_.action=0;
    if constexpr (Weapon == LongGun::TMP)
        if(!frame.firstDrawn&&!Emit({TMPEffect::FirstDraw}))return FALSE;
    if(!Emit({TMPEffect::DeployPrelude}))return FALSE;
    action_.aiming=false;spread_.scalar=0;spread_.recoveryDeadline=gpGlobals->time;
    if(!Emit({TMPEffect::DeployReset}))return FALSE;
    if(reserveHud_!=action_.reserve)
    {TMPEvent event{TMPEffect::ReserveHud};event.value=action_.reserve;if(!Emit(event))return FALSE;reserveHud_=action_.reserve;}
    Lease lease=lease_;lease.active=true;
    const int pitch=g_engfuncs.pfnRandomLong(-5,5)+100;if(!Current(&lease))return FALSE;
    if(pev->armorvalue!=1.F)
    {
        const int sample=g_engfuncs.pfnRandomLong(0,2);if(!Current(&lease))return FALSE;
        TMPEvent event{TMPEffect::Sleeve};event.value=sample;event.other=pitch;event.a=1;event.b=.8F;
        if(!Emit(event))return FALSE;
    }
    else pev->armorvalue=0;
    if(!Emit({TMPEffect::InventoryChanged}))return FALSE;
    m_pPlayer->pev->viewmodel=prepared_.viewModelString;m_pPlayer->pev->weaponmodel=prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention,"onehanded",sizeof("onehanded"));
    const int sequence=Weapon == LongGun::MP5 ? 0 : frame.firstDrawn?1:2;if(!View(sequence))return FALSE;
    SET_MODEL(edict(),STRING(prepared_.viewModelString));if(!Current(&lease))return FALSE;
    const float deadline=gpGlobals->time+prepared_.sequences.duration[sequence];
    action_.ownerAttack=action_.primary=action_.secondary=action_.idle=deadline;
    MirrorTimers(true);status_=TMPStatus::Applied;return TRUE;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Holster(int)
{
    TMPOwnerFrame frame{};if(!Check(frame,false))return;
    // Host ownership protection: a deferred melee cannot hit as the next CS gun.
    meleePending_=false;SetThink(nullptr);pev->nextthink=0;
    if(action_.aiming)
    {TMPEvent event{TMPEffect::Aim};event.a=350;event.other=1;if(!Emit(event))return;m_pPlayer->pev->fov=90;action_.aiming=false;}
    if(!View(Weapon == LongGun::MP5 ? 19 : 3))return;
    if constexpr (Weapon == LongGun::MP5) action_.ownerAttack=gpGlobals->time+1.F;
    else action_.ownerAttack=AddDouble(gpGlobals->time,.5);
    selected_=false;MirrorTimers(false);status_=TMPStatus::Applied;
}

template<LongGun Weapon>
MagazineActionHost AutomaticWeapon<Weapon>::ActionHost(ActionBridge& bridge) noexcept { return {&bridge, [](void* p) noexcept {
    auto& b = *static_cast<ActionBridge*>(p);
    return Current(&b.lease);
}, MagazineEvent}; }

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::MagazineEvent(void* context, const guest::cof::MagazineEvent& e) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    // Changes made by the action helper precede the corresponding callback.
    self.WriteAmmo();
    self.MirrorTimers(true);
    switch (e.kind)
    {
    case MagazineEventKind::ViewAnimation: return self.View(e.argument);
    case MagazineEventKind::PlayerReloadLoaded:
    case MagazineEventKind::PlayerReloadEmpty: return self.Animate(PlayerAction::Reload);
    case MagazineEventKind::PlayerMelee: return self.Animate(PlayerAction::WeaponMelee);
    case MagazineEventKind::SchedulePlayerMelee:
        self.meleePending_ = true;
        self.SetThink(&AutomaticWeapon<Weapon>::DelayedMelee);
        self.pev->nextthink = gpGlobals->time + e.delay;
        return true;
    }
    return false;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::SetResult(MagazineActionResult result) noexcept
{
    status_ = result == MagazineActionResult::Applied || result == MagazineActionResult::Deferred ?
        TMPStatus::Applied : result == MagazineActionResult::Ineligible ? TMPStatus::Ineligible :
        result == MagazineActionResult::Cancelled ? TMPStatus::Cancelled : TMPStatus::InvalidInput;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Reload()
{
    TMPOwnerFrame frame{};if(!Check(frame))return;
    ActionBridge bridge{this,lease_};bridge.lease.active=true;
    const auto result=StartAutomaticMagazineReload(Weapon,action_,gpGlobals->time,prepared_.sequences,ActionHost(bridge));
    if(!Current(&bridge.lease))return;
    WriteAmmo();MirrorTimers(true);SetResult(result);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::SecondaryAttack()
{
    TMPOwnerFrame frame{};if(!Check(frame))return;
    const float now=gpGlobals->time;
    if(action_.primary>now||action_.secondary>now||action_.action||action_.special||action_.aiming)
    {status_=TMPStatus::Ineligible;return;}
    Lease lease=lease_;lease.active=true;
    if(CheckPistolWeaponMelee(*m_pPlayer,{frame.meleeDamage,frame.quietAudio,MeleeWorld::Multiplayer},
        prepared_.melee,{Current,&lease})!=MeleeStatus::Ready)
    {status_=TMPStatus::MissingBackend;return;}
    if constexpr (Weapon == LongGun::MP5)
    {
        ActionBridge bridge{this,lease};
        const auto result=StartMP5WeaponMelee(action_,now,ActionHost(bridge));
        if(!Current(&lease))return;
        WriteAmmo();MirrorTimers(true);SetResult(result);return;
    }
    meleePending_=true;SetThink(&AutomaticWeapon<Weapon>::DelayedMelee);pev->nextthink=now+.3F;
    if(!Animate(PlayerAction::WeaponMelee)||!View(14))return;
    action_.primary=action_.secondary=action_.idle=now+1.F;MirrorTimers(true);status_=TMPStatus::Applied;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::DelayedMelee()
{
    TMPOwnerFrame frame{}; if (!Check(frame) || !meleePending_) return;
    if (gpGlobals->time < pev->nextthink) { status_ = TMPStatus::Ineligible; return; }
    meleePending_ = false; SetThink(nullptr); pev->nextthink = 0;
    Lease l = lease_; l.active = true;
    const auto result = PistolWeaponMelee(*m_pPlayer,
        {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer}, prepared_.melee, {Current, &l});
    if (!Current(const_cast<Lease*>(&l))) return;
    status_ = result == MeleeStatus::Hit || result == MeleeStatus::Miss ? TMPStatus::Applied :
        result == MeleeStatus::Cancelled ? TMPStatus::Cancelled : TMPStatus::MissingBackend;
}

template<LongGun Weapon>
bool AutomaticWeapon<Weapon>::SetAim(bool enabled) noexcept
{
    action_.aiming=enabled;
    TMPEvent event{TMPEffect::Aim};event.enabled=enabled;event.first=enabled?prepared_.aimOffset:Vector{};
    event.value=enabled?prepared_.aimZoom:0;event.a=350;if(!Emit(event))return false;
    m_pPlayer->pev->fov=enabled?float(prepared_.aimZoom):90.F;
    const int sequence=Weapon == LongGun::MP5 ? (enabled?8:11) : (enabled?10:12);
    if(!View(sequence))return false;
    action_.primary=action_.secondary=action_.extra=AddDouble(gpGlobals->time,.3);
    action_.idle=gpGlobals->time+prepared_.sequences.duration[sequence];MirrorTimers(true);return true;
}

template<LongGun Weapon>
void AutomaticWeapon<Weapon>::PrimaryAttack()
{
    TMPOwnerFrame frame{};if(!Check(frame))return;
    const float now=gpGlobals->time;
    if constexpr (Weapon == LongGun::MP5)
    {
        Lease lease=lease_;lease.active=true;
        PistolBallistics shot{{},{1,0,0},{spread_.vector[0],spread_.vector[1],spread_.vector[2]},
            {0,1,0},{0,0,1},frame.randomSeed,frame.bulletDamage,action_.aiming,
            PistolPlayerHits::OriginalCooperativeSkipPlayers,frame.sourceMode};
        if(action_.primary<=now&&!action_.action&&!action_.special&&action_.clip>0&&
            m_pPlayer->pev->waterlevel!=3&&
            CheckPistolBallistics(shot,prepared_.ballistics,{Current,&lease})!=BallisticsStatus::Ready)
        {status_=TMPStatus::MissingBackend;return;}
        MP5PrimaryState state{action_,spread_,emptyDeadline_};
        struct Bridge
        {
            AutomaticWeapon& item;MP5PrimaryState& state;const TMPOwnerFrame& frame;Lease lease;PistolBallistics shot;
            void Sync() noexcept
            {
                item.action_=state.action;item.spread_=state.spread;item.emptyDeadline_=state.emptyDeadline;
                item.WriteAmmo();item.MirrorTimers(true);
            }
            static Vector VectorOf(const MP5Vector& v) noexcept{return {v[0],v[1],v[2]};}
            static MP5Vector ArrayOf(const Vector& v) noexcept{return {v.x,v.y,v.z};}
        } bridge{*this,state,frame,lease,shot};
        MP5PrimaryFrame request{};
        request.now=now;request.damage=float(frame.bulletDamage);
        request.waterLevel=m_pPlayer->pev->waterlevel;request.shellModel=prepared_.shellModel;
        request.weaponEntity=g_engfuncs.pfnIndexOfEdict(lease.weapon);
        if(!Current(&lease))return;
        request.viewModel=prepared_.viewModelString;request.ownerFlags=frame.originalFlags;
        request.randomSeed=frame.randomSeed;request.eventOwner2814=frame.eventOwner2814;
        request.fireOnEmpty=m_fFireOnEmpty!=0;request.infiniteAmmo=frame.infiniteAmmo;
        request.weaponOrigin=Bridge::ArrayOf(pev->origin);request.weaponViewOffset=Bridge::ArrayOf(pev->view_ofs);
        request.ownerOrigin=Bridge::ArrayOf(m_pPlayer->pev->origin);request.viewAngles=Bridge::ArrayOf(m_pPlayer->pev->v_angle);
        request.weaponVelocityY=pev->velocity.y;
        request.initialBasis={Bridge::ArrayOf(gpGlobals->v_forward),Bridge::ArrayOf(gpGlobals->v_right),Bridge::ArrayOf(gpGlobals->v_up)};
        const MP5PrimaryHost host{&bridge,
            [](void* p) noexcept{return Current(&static_cast<Bridge*>(p)->lease);},
            [](void* p,const MP5PrimaryEvent& value) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();
                TMPEvent event{TMPEffect::EmptyClick};event.first=Bridge::VectorOf(value.first);
                event.second=Bridge::VectorOf(value.second);event.a=value.a;event.b=value.b;event.c=value.c;
                event.value=value.value;event.other=value.other;
                switch(value.kind)
                {
                case MP5PrimaryEffect::Shell:event.kind=TMPEffect::Shell;break;
                case MP5PrimaryEffect::KillLocalParticles:event.kind=TMPEffect::KillLocalParticles;event.enabled=true;break;
                case MP5PrimaryEffect::Gunsmoke:
                    event.kind=TMPEffect::Gunsmoke;event.enabled=true;event.modelString=value.other;event.other=0;break;
                case MP5PrimaryEffect::FireEvent:event.kind=TMPEffect::FireEvent;break;
                case MP5PrimaryEffect::PlayerFire:return b.item.Animate(PlayerAction::Fire);
                case MP5PrimaryEffect::Recoil:event.kind=TMPEffect::Recoil;break;
                case MP5PrimaryEffect::Shake:event.kind=TMPEffect::Shake;break;
                case MP5PrimaryEffect::ViewAnimation:return b.item.View(value.value);
                case MP5PrimaryEffect::PlayerMove:return b.item.Animate(PlayerAction::Move);
                case MP5PrimaryEffect::MuzzleFlash:b.item.m_pPlayer->pev->effects|=EF_MUZZLEFLASH;return true;
                case MP5PrimaryEffect::ShotState:event.kind=TMPEffect::ShotState;break;
                case MP5PrimaryEffect::ShotLatch:event.kind=TMPEffect::ShotLatch;break;
                case MP5PrimaryEffect::EmptyClick:break;
                }
                return b.item.Emit(event);
            },
            [](void* p) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();b.item.Reload();
                if(!Current(&b.lease))return false;
                b.state.action=b.item.action_;b.state.spread=b.item.spread_;
                return b.item.status_==TMPStatus::Applied||b.item.status_==TMPStatus::Ineligible;
            },
            [](void* p,const MP5Vector& angles,MP5Basis& out) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();const Vector value=Bridge::VectorOf(angles);
                g_engfuncs.pfnMakeVectors(value);if(!Current(&b.lease))return false;
                out={Bridge::ArrayOf(gpGlobals->v_forward),Bridge::ArrayOf(gpGlobals->v_right),Bridge::ArrayOf(gpGlobals->v_up)};return true;
            },
            [](void* p,MP5Vector& out) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::ArrayOf(b.item.m_pPlayer->GetGunPosition());return Current(&b.lease);
            },
            [](void* p,MP5Vector& out) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();
                out=Bridge::ArrayOf(b.item.m_pPlayer->pev->v_angle+b.item.m_pPlayer->pev->punchangle);return true;
            },
            [](void* p,float,MP5Vector& out) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::ArrayOf(gpGlobals->v_forward);return true;
            },
            [](void* p,int low,int high,int& out) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();out=g_engfuncs.pfnRandomLong(low,high);return Current(&b.lease);
            },
            [](void* p,const MP5PrimaryBullet& bullet) noexcept
            {
                auto& b=*static_cast<Bridge*>(p);b.Sync();b.shot.source=Bridge::VectorOf(bullet.source);
                b.shot.direction=Bridge::VectorOf(bullet.direction);b.shot.spread=Bridge::VectorOf(bullet.spread);
                b.shot.right=gpGlobals->v_right;b.shot.up=gpGlobals->v_up;b.shot.damage=bullet.damage;b.shot.seed=bullet.seed;
                const auto result=FirePistolBallistics(*b.item.m_pPlayer,b.shot,b.item.prepared_.ballistics,{Current,&b.lease});
                if(!Current(&b.lease))return false;
                // Authoritative damage hooks may grant ammo. Original BB07
                // reads the current clip after damage, not a pre-hit copy.
                b.item.ReadAmmo();b.state.action.clip=b.item.action_.clip;b.state.action.reserve=b.item.action_.reserve;
                return result.status==BallisticsStatus::Fired;
            },
            [](void* p) noexcept{auto& b=*static_cast<Bridge*>(p);b.Sync();return b.item.AutomaticSound(true,b.frame);}};
        const auto result=MP5PrimaryAttack(state,request,host);
        if(!Current(&lease))return;
        bridge.Sync();
        status_=result==MP5PrimaryResult::Fired||result==MP5PrimaryResult::Empty||result==MP5PrimaryResult::ReloadRequested?
            TMPStatus::Applied:result==MP5PrimaryResult::Ineligible?TMPStatus::Ineligible:
            result==MP5PrimaryResult::InvalidInput?TMPStatus::InvalidInput:
            result==MP5PrimaryResult::Cancelled?TMPStatus::Cancelled:TMPStatus::MissingBackend;
        return;
    }
    if(action_.primary>now||action_.action||action_.special){status_=TMPStatus::Ineligible;return;}
    if(m_pPlayer->pev->waterlevel==3||action_.clip<=0)
    {
        if(m_pPlayer->pev->waterlevel!=3&&!m_fFireOnEmpty){Reload();return;}
        if(now<=emptyDeadline_){status_=TMPStatus::Ineligible;return;}
        if(!Emit({TMPEffect::EmptyClick}))return;
        emptyDeadline_=AddDouble(now,.25);status_=TMPStatus::Applied;return;
    }
    PistolBallistics shot{{},{1,0,0},{spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0},{0,0,1},frame.randomSeed,frame.bulletDamage,action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers,frame.sourceMode};
    Lease lease=lease_;lease.active=true;
    if(CheckPistolBallistics(shot,prepared_.ballistics,{Current,&lease})!=BallisticsStatus::Ready)
    {status_=TMPStatus::MissingBackend;return;}
    const Vector forward=gpGlobals->v_forward,right=gpGlobals->v_right,up=gpGlobals->v_up;
    TMPEvent shell{TMPEffect::Shell};
    shell.first={((pev->origin.x+pev->view_ofs.x)+forward.x*80.F)+right.x*20.F,
                 ((pev->origin.y+pev->view_ofs.y)+forward.y*80.F)+right.y*20.F,
                 ((pev->origin.z+pev->view_ofs.z)+forward.z*80.F)+right.z*20.F};
    shell.second={(up.x*200.F+right.x*220.F)+forward.x*40.F,
                  (up.y*200.F+right.y*220.F)+forward.y*40.F,
                  (up.z*200.F+right.z*220.F)+forward.z*40.F};
    shell.a=pev->velocity.y;shell.value=prepared_.shellModel;shell.other=1;if(!Emit(shell))return;
    TMPEvent kill{TMPEffect::KillLocalParticles};kill.enabled=true;if(!Emit(kill))return;
    TMPEvent smoke{TMPEffect::Gunsmoke};smoke.value=g_engfuncs.pfnIndexOfEdict(lease.weapon);
    if(!Current(&lease))return;
    smoke.enabled=true;smoke.modelString=prepared_.viewModelString;if(!Emit(smoke))return;
    if(!frame.infiniteAmmo)--action_.clip;WriteAmmo();
    g_engfuncs.pfnMakeVectors(m_pPlayer->pev->v_angle);if(!Current(&lease))return;
    const Vector gun=m_pPlayer->GetGunPosition();if(!Current(&lease))return;
    TMPEvent fire{TMPEffect::FireEvent};fire.first=m_pPlayer->pev->origin;fire.second=gun+gpGlobals->v_right*32.F;
    fire.value=OriginalId;fire.other=frame.eventOwner2814!=1;
    if(!Emit(fire)||!Animate(PlayerAction::Fire))return;
    const int recoil=g_engfuncs.pfnRandomLong(-2,2);if(!Current(&lease))return;
    TMPEvent kick{TMPEffect::Recoil};kick.first={float(recoil),1,1};if(!Emit(kick))return;
    TMPEvent shake{TMPEffect::Shake};const bool crouch=(frame.originalFlags&0x4000u)!=0;
    shake.a=crouch?2.F:5.F;shake.b=crouch?.1F:.25F;shake.c=crouch?.1F:.15F;shake.value=2;
    if(!Emit(shake)||!View(action_.aiming?13:4)||!Animate(PlayerAction::Move))return;
    m_pPlayer->pev->effects|=EF_MUZZLEFLASH;
    const Vector angles=m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle;g_engfuncs.pfnMakeVectors(angles);
    if(!Current(&lease))return;
    shot.source=m_pPlayer->GetGunPosition();if(!Current(&lease))return;
    shot.direction=gpGlobals->v_forward;shot.right=gpGlobals->v_right;shot.up=gpGlobals->v_up;
    spread_.scalar+=.25F;spread_.recoveryDeadline=AddDouble(now,.25);
    const auto result=FirePistolBallistics(*m_pPlayer,shot,prepared_.ballistics,{Current,&lease});
    if(!Current(&lease))return;
    if(result.status!=BallisticsStatus::Fired){status_=TMPStatus::Cancelled;return;}
    action_.idle=now+1.F;TMPEvent shotState{TMPEffect::ShotState};shotState.a=1;
    if(!Emit(shotState)||!AutomaticSound(true,frame))return;
    TMPEvent latch{TMPEffect::ShotLatch};latch.value=1;latch.a=AddDouble(now,.2);if(!Emit(latch))return;
    action_.primary=AddDouble(now,.07);action_.secondary=now+1.F;
    if(action_.clip<=0)action_.primary=now+1.F;
    MirrorTimers(true);status_=TMPStatus::Applied;
}

template<LongGun Weapon>
void AutomaticWeapon<Weapon>::ReloadCommitted(void* context, const MagazineReload& state) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    self.action_.clip = state.clip; self.action_.reserve = state.reserve;
    self.WriteAmmo();
    bridge.callbackFailed = !self.Emit({TMPEffect::InventoryChanged});
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::AdvanceOwnerFrame() noexcept
{
    TMPOwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

template<LongGun Weapon>
void AutomaticWeapon<Weapon>::ItemPostFrame()
{
    TMPOwnerFrame frame{};if(!Check(frame))return;
    Lease lease=lease_;lease.active=true;const float now=gpGlobals->time;
    if(now<action_.ownerAttack){status_=TMPStatus::Ineligible;return;}
    if(action_.action==2&&!action_.loading)action_.action=0;
    if(!action_.action&&!action_.special&&now>action_.primary&&now>action_.secondary&&now>action_.extra&&
        !frame.toggleAim&&action_.aiming&&!((m_pPlayer->pev->button|frame.pressed|frame.released)&0x40u))
        if(!SetAim(false))return;
    if(action_.pendingReload&&!frame.pendingGate&&frame.pendingGateTime<=now&&
        action_.primary<=now&&action_.secondary<=now&&action_.extra<=now&&!action_.action&&!action_.special)
    {
        if(action_.aiming){if(!SetAim(false))return;}
        else{action_.pendingReload=false;Reload();if(!Current(&lease))return;}
    }
    const float squared=m_pPlayer->pev->velocity.x*m_pPlayer->pev->velocity.x+
                        m_pPlayer->pev->velocity.y*m_pPlayer->pev->velocity.y;
    const float speed=static_cast<float>(std::sqrt(double(squared)));
    constexpr int jump=Weapon == LongGun::MP5 ? 16 : 7;
    constexpr int land=Weapon == LongGun::MP5 ? 17 : 9;
    constexpr int sprintTo=Weapon == LongGun::MP5 ? 13 : 7;
    constexpr int sprintLoop=Weapon == LongGun::MP5 ? 14 : 8;
    constexpr int sprintEnd=Weapon == LongGun::MP5 ? 15 : 9;
    if(!action_.loading&&!action_.special&&action_.action!=2&&now>action_.primary)
    {
        if(frame.condition3AA&&!action_.aiming&&now>action_.extra)
        {
            if(!action_.action)
            {
                action_.action=5;if(!View(jump))return;
                const float duration=prepared_.sequences.duration[jump];
                action_.primary=static_cast<float>(double(now)+double(duration)*.5);actionDeadline_=now+duration;
            }
        }
        else if(action_.action==5)
        {
            if(!View(land))return;
            action_.primary=action_.idle=now+prepared_.sequences.duration[land];action_.action=0;
        }
        const bool sprint=(frame.originalFlags&0x40000u)&&speed>0;
        if(sprint&&action_.action!=5&&action_.action!=1)
        {
            action_.action=1;if(!View(sprintTo))return;const float duration=prepared_.sequences.duration[sprintTo];
            action_.primary=static_cast<float>(double(now)+double(duration)*.5);actionDeadline_=now+duration;
        }
        if(sprint&&action_.action==1&&now>actionDeadline_)
        {if(!View(sprintLoop))return;actionDeadline_=now+prepared_.sequences.duration[sprintLoop];}
        if(!sprint&&action_.action==1&&now>actionDeadline_)
        {
            if(!View(sprintEnd))return;
            action_.primary=AddDouble(now,.33);action_.idle=AddDouble(now,.35);action_.action=0;
        }
    }
    if(action_.clip>0)m_iPlayEmptySound=1;
    // TMP's real Fidget slot is1001E060 (empty); no pistol idle RNG here.
    if(!AutomaticSound(false,frame))return;
    if(now>wearDeadline_){wearDeadline_=AddDouble(now,.75);if(wear_>0)--wear_;}
    if(wear_<0)wear_=0;if(wear_>wearMaximum_)wear_=wearMaximum_;
    if(reserveHud_!=action_.reserve)
    {TMPEvent event{TMPEffect::ReserveHud};event.value=action_.reserve;if(!Emit(event))return;reserveHud_=action_.reserve;}
    if(action_.loading)
    {
        MagazineReload reload{action_.clip,action_.reserve,action_.ownerAttack,true};
        ActionBridge bridge{this,lease_};bridge.lease.active=true;
        CommitMagazineReload(reload,30,0,now,ReloadCommitted,&bridge);
        if(!Current(&bridge.lease)||bridge.callbackFailed)return;
        action_.clip=reload.clip;action_.reserve=reload.reserve;action_.loading=reload.loading;WriteAmmo();
    }
    const unsigned buttons=m_pPlayer->pev->button;
    const bool allowed=frame.activityBlock<=0&&m_pPlayer->pev->movetype!=5;
    if((buttons&0x800u)&&frame.activityBlock<=0&&action_.secondary<=now)
    {if(allowed){if(!Emit({TMPEffect::InventoryChanged}))return;SecondaryAttack();}}
    else if((buttons&0x40u)&&allowed&&action_.ownerAttack<=now)
    {
        if(!Emit({TMPEffect::InventoryChanged}))return;
        if(action_.primary<=now&&action_.secondary<=now&&action_.extra<=now&&!action_.action&&!action_.special&&
            (frame.pressed&0x40u)&&(frame.toggleAim||!action_.aiming))
            if(!SetAim(!action_.aiming))return;
    }
    else if((buttons&1u)&&frame.activityBlock<=0&&action_.primary<=now)
    {
        if(allowed)
        {if(!action_.clip)m_fFireOnEmpty=1;if(!Emit({TMPEffect::InventoryChanged}))return;PrimaryAttack();}
    }
    else if((buttons&0x2000u)&&!action_.loading)
    {if(m_pPlayer->pev->movetype!=5)Reload();}
    else if(!(buttons&0x841u))m_fFireOnEmpty=0;
    if(!Current(&lease))return;
    WriteAmmo();MirrorTimers(true);
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::WeaponIdle()
{
    TMPOwnerFrame frame{};if(!Check(frame))return;
    if constexpr (Weapon == LongGun::MP5)
    {
        ActionBridge bridge{this,lease_};bridge.lease.active=true;
        const auto result=MP5MagazineIdle(action_,gpGlobals->time,m_pPlayer->pev->weaponanim,
            SequenceCount,prepared_.sequences,ActionHost(bridge));
        if(!Current(&bridge.lease))return;
        WriteAmmo();MirrorTimers(true);SetResult(result);return;
    }
    if(action_.idle>gpGlobals->time||action_.action){status_=TMPStatus::Ineligible;return;}
    const int sequence=action_.aiming?11:0;if(!View(sequence))return;
    action_.idle=gpGlobals->time+prepared_.sequences.duration[sequence];MirrorTimers(true);status_=TMPStatus::Applied;
}
template<LongGun Weapon>
void AutomaticWeapon<Weapon>::Invalidate() noexcept
{
    bound_ = selected_ = meleePending_ = false;
    modelRebind_.reset();
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = TMPStatus::Cancelled;
}
}
