// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "inventory_recovery.hpp"
#include "shotgun_weapon.hpp"
#include "player_think.hpp"
#include "../m16_control.hpp" // Shared original1011D560 input dispatcher only.
#include <bit>

#include <cmath>
#include <cstring>
#include <type_traits>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof::server
{
goldcraft::scenario::GuestProfile ShotgunWeapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {ShotgunHostId, GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior, GuestKill::StandardPlayerProcessing};
}

namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
constexpr std::uint64_t InventoryKind = 0x434f463837300001ULL;
}


struct ShotgunWeapon::InventoryState
{
    ShotgunShotState shot;
    SpreadState spread;
    SpreadTransitions transitions;
    float actionDeadline{}, wearDeadline{}, nextThink{}, taskDeadline{};
    float armor{}, primary{}, secondary{}, idle{}, ownerAttack{};
    int body{}, reserveHud{}, wearMaximum{}, wear{}, clip{}, inReload{}, fireOnEmpty{};
    int ownerSerial{}, weaponSerial{}, modelIndex{}, ownerSpawn{};
    std::uintptr_t map{}, ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    WeaponTask task{};
    bool selected{};
    ShotgunStatus status{};
};
bool ShotgunWeapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease = lease_;
    if (!bound_ || !pev || m_pPlayer != lease.player || !Current(const_cast<Lease*>(&lease))) return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState) <= 512);
    InventoryState s{};
    s.shot = shot_;
    s.spread = spread_; s.transitions = transitions_;
    // Scenario skills can update native ammo before another weapon frame.
    // A purchase rollback must preserve that grant, not the last mirror.
    s.shot.action.clip = m_iClip;
    s.shot.action.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
    s.actionDeadline = actionDeadline_; s.wearDeadline = wearDeadline_;
    s.task = task_; s.taskDeadline = taskDeadline_;
    s.body = pev->body; s.armor = pev->armorvalue; s.think = m_pfnThink; s.nextThink = pev->nextthink;
    s.primary = m_flNextPrimaryAttack; s.secondary = m_flNextSecondaryAttack; s.idle = m_flTimeWeaponIdle;
    s.ownerAttack = m_pPlayer->m_flNextAttack; s.clip = m_iClip;
    s.inReload = m_fInReload; s.fireOnEmpty = m_fFireOnEmpty;
    s.reserveHud = reserveHud_; s.wearMaximum = wearMaximum_; s.wear = wear_;
    s.ownerSerial = lease.ownerSerial; s.weaponSerial = lease.weaponSerial; s.modelIndex = lease.modelIndex;
    s.ownerSpawn = m_pPlayer->m_iNumSpawns; s.map = reinterpret_cast<std::uintptr_t>(worldLease_.context);
    s.ownerContext = reinterpret_cast<std::uintptr_t>(lease.context);
    s.selected = selected_; s.status = status_;
    output.kind = InventoryKind; output.bytes = sizeof(s);
    std::memcpy(output.data.data(), &s, sizeof(s)); return true;
}
bool ShotgunWeapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
{
    if(!pev||m_pPlayer!=&player)return false;
    const auto pending=modelRebind_;modelRebind_.reset();
    const auto previous=lease_;
    if(bound_&&Current(const_cast<Lease*>(&previous))&&pev->owner==player.edict())return true;
    const auto world=worldLease_;const auto cold=cold_;const auto prepared=prepared_;
    if(!WorldCurrent(world)||!cold.ownerBackends)return false;
    const auto sameInventory=[&]() noexcept {
        return !pending||(pending->owner==player.edict()&&
            pending->ownerSerial==player.edict()->serialnumber&&pending->weaponSerial==edict()->serialnumber&&
            pending->spawn==player.m_iNumSpawns&&pending->map==worldLease_.context&&
            pending->selected==(player.m_pActiveItem==this));
    };
    if(!sameInventory()){Invalidate();return false;}
    const EHANDLE owner(&player);ShotgunBackends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found||!sameInventory()||
        (pending&&(!backend.ownerLife||pending->life!=backend.ownerLife)))return false;
    if(Bind(player,prepared,backend)!=ShotgunStatus::Ready)return false;
    const auto lease=lease_;AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    if(selected_&&!PublishOwnerState())
    {const auto result=status_;Invalidate();status_=result;return false;}
    if (pending) { task_ = pending->task; taskDeadline_ = pending->deadline; ScheduleThink(); }
    return true;
}
bool ShotgunWeapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
{
    const auto lease = lease_;
    if (input.kind != InventoryKind || input.bytes != sizeof(InventoryState) || !bound_ ||
        !Current(const_cast<Lease*>(&lease)) || m_pPlayer != lease.player) return false;
    InventoryState s{}; std::memcpy(&s, input.data.data(), sizeof(s));
    if (s.ownerSerial != lease.ownerSerial || s.weaponSerial != lease.weaponSerial || s.modelIndex != lease.modelIndex ||
        s.ownerSpawn != m_pPlayer->m_iNumSpawns || s.map != reinterpret_cast<std::uintptr_t>(worldLease_.context) ||
        s.ownerContext != reinterpret_cast<std::uintptr_t>(lease.context) ||
        (s.selected && m_pPlayer->m_pActiveItem != this)) return false;
    shot_ = s.shot; spread_ = s.spread; transitions_ = s.transitions;
    actionDeadline_ = s.actionDeadline; wearDeadline_ = s.wearDeadline;
    task_ = s.task; taskDeadline_ = s.taskDeadline;
    pev->body = s.body; pev->armorvalue = s.armor; m_pfnThink = s.think; pev->nextthink = s.nextThink;
    m_flNextPrimaryAttack = s.primary; m_flNextSecondaryAttack = s.secondary; m_flTimeWeaponIdle = s.idle;
    if (s.selected) m_pPlayer->m_flNextAttack = s.ownerAttack;
    m_iClip = s.clip; m_fInReload = s.inReload; m_fFireOnEmpty = s.fireOnEmpty;
    reserveHud_ = s.reserveHud; wearMaximum_ = s.wearMaximum; wear_ = s.wear;
    selected_ = s.selected; status_ = s.status;
    return !selected_ || PublishOwnerState();
}
bool ShotgunWeapon::PublishOwnerState() noexcept
{
    auto lease = lease_; lease.active = true;
    if (!selected_ || !Current(&lease) || !prepared_.aimMetadataReady) return false;
    ShotgunEvent event{ShotgunEffect::Aim}; event.enabled = shot_.action.aiming;
    event.first = shot_.action.aiming ? prepared_.aimOffset : Vector{};
    event.value = shot_.action.aiming ? prepared_.aimZoom : 0;
    if (!Emit(event)) return false;
    event = {ShotgunEffect::CameraZoom}; event.a = 350;
    if (!Emit(event)) return false;
    lease.player->pev->fov = shot_.action.aiming ? float(prepared_.aimZoom) : 90.F;
    return true;
}


void ShotgunWeapon::OwnerModelChanging() noexcept
{
    ShotgunOwnerFrame frame{};
    const auto dispatch = static_cast<decltype(m_pfnThink)>(&ShotgunWeapon::DispatchThink);
    if (!Check(frame, false) || !ResourcesReady() || !backends_.ownerLife ||
        selected_ != (m_pPlayer->m_pActiveItem == this) ||
        (task_ != WeaponTask::None && m_pfnThink != dispatch) || !std::isfinite(taskDeadline_))
    { Invalidate(); return; }
    const ModelRebind pending{lease_.owner, worldLease_.context, backends_.ownerLife,
        lease_.ownerSerial, lease_.weaponSerial, m_pPlayer->m_iNumSpawns, task_, taskDeadline_, selected_};
    Invalidate(); modelRebind_ = pending;
}

bool ShotgunWeapon::Current(void* context) noexcept
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

bool ShotgunWeapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool ShotgunWeapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void ShotgunWeapon::ConfigureColdHost(ShotgunColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
ShotgunStatus ShotgunWeapon::Prepare(const ShotgunPrepared& p) noexcept
{
    preparedReady_ = false;
    status_ = ShotgunStatus::MissingBackend;
    if (!p.modelsReady || !p.soundsReady || !p.particleReady || !p.aimMetadataReady ||
        !p.inventoryReady || !p.pickupSoundReady || !p.deployPreludeReady ||
        p.world != MeleeWorld::Multiplayer || !p.worldModelString || !p.inventoryItemString ||
        !p.viewModelString || !p.playerModelString || p.shellModel <= 0 ||
        !p.pushbackReady ||
        p.ballistics.customDecalMessage <= 0 || p.ballistics.bubbleModel <= 0 ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnIndexOfEdict)
        return status_;
    if (!Finite(p.aimOffset) || p.aimZoom < 0 || p.aimZoom > 255 ||
        p.hostSlot != ShotgunHudSlot || p.hostPosition != ShotgunHudPosition)
        return status_ = ShotgunStatus::InvalidInput;
    for (int i = 0; i < 20; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = ShotgunStatus::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = ShotgunStatus::Ready;
}
void ShotgunWeapon::Precache()
{
    preparedReady_ = false;
    status_ = ShotgunStatus::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    ShotgunPrepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

ShotgunStatus ShotgunWeapon::Bind(CBasePlayer& owner, const ShotgunPrepared& p,
                          const ShotgunBackends& backends) noexcept
{
    bound_ = selected_ = false;
    modelRebind_.reset();
    status_ = ShotgunStatus::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != ShotgunRequiredEffects || !backends.playerAnimation ||
        !backends.playerThink || !backends.playerThink->PreparedFor(owner, PlayerThinkKind::Pushback,
            backends.playerModelGeneration.mapEpoch, backends.ownerLife))
        return status_;
    if (Prepare(p) != ShotgunStatus::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = ShotgunStatus::InvalidInput;
    status_ = ShotgunStatus::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, LongGun::Shotgun,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = ShotgunStatus::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias Shotgun to an unrelated stock CS weapon.
    m_iId = ShotgunHostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = ShotgunStatus::Ready;
}

void ShotgunWeapon::Spawn()
{
    modelRebind_.reset(); shot_ = {}; spread_ = {}; transitions_ = {};
    actionDeadline_ = wearDeadline_ = taskDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0; task_ = WeaponTask::None;
    m_iDefaultAmmo = 5; m_iSecondaryAmmoType = -1; m_iClip = 0;
    m_fInReload = m_fFireOnEmpty = 0;
    bound_ = selected_ = worldUse_ = false;
    if (!pev) return;
    SetThink(nullptr); SetTouch(nullptr); SetUse(nullptr); pev->nextthink = 0;
    Precache(); const auto l = worldLease_;
    if (!WorldCurrent(l)) return;
    if (!preparedReady_ || !m_pEntity || !g_engfuncs.pfnSetOrigin || !g_engfuncs.pfnSetSize)
    { status_ = ShotgunStatus::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.F)
    { status_ = ShotgunStatus::InvalidInput; return; }
    m_iId = ShotgunHostId; m_pPlayer = nullptr; m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 5;
    m_iClip = pev->health == -2 ? -666 : 0; pev->renderfx = 99;
    SET_MODEL(edict(), STRING(prepared_.worldModelString));
    if (!WorldCurrent(l)) return;
    pev->movetype = MOVETYPE_TOSS; pev->solid = SOLID_BBOX;
    UTIL_SetOrigin(pev, pev->origin); if (!WorldCurrent(l)) return;
    UTIL_SetSize(pev, g_vecZero, g_vecZero); if (!WorldCurrent(l)) return;
    worldUse_ = true; SetUse(&ShotgunWeapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&ShotgunWeapon::WorldTouch);
    SetThink(&ShotgunWeapon::WorldFallThink); pev->nextthink = gpGlobals->time + .1F;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = ShotgunStatus::Applied;
}

int ShotgunWeapon::GetItemInfo(ItemInfo* info)
{
    if (!info) return 0;
    *info = {}; info->pszName = "weapon_cof_shotgun"; info->pszAmmo1 = "cof_shotgun";
    info->iMaxAmmo1 = 50; info->iMaxAmmo2 = -1; info->iMaxClip = 5;
    info->iId = m_iId = ShotgunHostId; info->iSlot = ShotgunHudSlot;
    info->iPosition = ShotgunHudPosition; info->iWeight = 20; return 1;
}

int ShotgunWeapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void ShotgunWeapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void ShotgunWeapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&ShotgunWeapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&ShotgunWeapon::WorldTouch);
}
void ShotgunWeapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void ShotgunWeapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = ShotgunStatus::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = ShotgunStatus::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        ShotgunEvent event{ShotgunEffect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int ShotgunWeapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    ShotgunBackends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != ShotgunStatus::Ready) return FALSE;
    ShotgunOwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = ShotgunStatus::Ineligible; return FALSE; }
    return TRUE;
}
int ShotgunWeapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == ShotgunStatus::Ready || status_ == ShotgunStatus::Applied);
}
int ShotgunWeapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int ShotgunWeapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<ShotgunWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    ShotgunOwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = ShotgunStatus::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 5 ? amount : 5;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, "cof_shotgun", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({ShotgunEffect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int ShotgunWeapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<ShotgunWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    ShotgunOwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_shotgun", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void ShotgunWeapon::AttachToPlayer(CBasePlayer* player)
{
    const auto l = lease_;
    if (!bound_ || !player || player != m_pPlayer || !Current(const_cast<Lease*>(&l))) return;
    // Native attach clears fall Think; ordinary CoF reload/pump tasks remain
    // weapon-owned across inventory/model reattachment in the same life.
    CBasePlayerItem::AttachToPlayer(player);
    if (!Current(const_cast<Lease*>(&l))) return;
    SetUse(nullptr); worldUse_ = false; ScheduleThink();
}
void ShotgunWeapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void ShotgunWeapon::Drop() { RetireWorld(false); }
void ShotgunWeapon::Kill() { RetireWorld(true); }
void ShotgunWeapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = ShotgunStatus::UnsupportedState;
}

bool ShotgunWeapon::Check(ShotgunOwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = ShotgunStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = ShotgunStatus::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || shot_.action.special || shot_.action.action < 0 ||
        (shot_.action.action > 2 && shot_.action.action != 5))
    { status_ = ShotgunStatus::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 15 && frame.reserveLimit != 50) || (frame.stamina && !std::isfinite(*frame.stamina)) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = ShotgunStatus::InvalidInput; return false; }
    ReadAmmo();
    // Carry metadata limits pickups, not trusted scenario ammo already owned.
    if ((shot_.action.clip < 0 && (active || shot_.action.clip != -666)) ||
        shot_.action.reserve < 0)
    { status_ = ShotgunStatus::InvalidInput; return false; }
    status_ = ShotgunStatus::Ready;
    return true;
}

bool ShotgunWeapon::Emit(ShotgunEvent event) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    WriteAmmo(); MirrorTimers(selected_, false);
    const bool delivered = backends_.effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    ReadAmmo();
    if (!delivered) status_ = ShotgunStatus::MissingBackend;
    return delivered;
}
bool ShotgunWeapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, LongGun::Shotgun, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
bool ShotgunWeapon::View(int sequence) noexcept
{
    Lease lease = lease_; lease.active = selected_;
    if (!Current(&lease)) return false;
    m_pPlayer->pev->weaponanim = sequence; // original1011F5D0, before transport
    ShotgunEvent event{ShotgunEffect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void ShotgunWeapon::ReadAmmo() noexcept
{
    shot_.action.clip = m_iClip;
    shot_.action.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void ShotgunWeapon::WriteAmmo() noexcept
{
    m_iClip = shot_.action.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = shot_.action.reserve;
    // Per-shell reload never enters CS's magazine-reload completion path.
    m_fInReload = FALSE;
}
void ShotgunWeapon::MirrorTimers(bool selected, bool ownerGate) noexcept
{
    // UseDecrement=false keeps weapon clocks absolute. ReGameDLL's player gate
    // is nevertheless a CLIENT_WEAPONS countdown; this selected adapter owns
    // its absolute ownerAttack gate. Holster restores a native countdown lock.
    m_flNextPrimaryAttack = shot_.action.primary;
    m_flNextSecondaryAttack = shot_.action.secondary;
    m_flTimeWeaponIdle = shot_.action.idle;
    if (!ownerGate) return;
#ifdef CLIENT_WEAPONS
    m_pPlayer->m_flNextAttack = selected ? 0.0f : shot_.action.ownerAttack - gpGlobals->time;
#else
    m_pPlayer->m_flNextAttack = selected ? gpGlobals->time : shot_.action.ownerAttack;
#endif
}
BOOL ShotgunWeapon::CanDeploy()
{
    ShotgunOwnerFrame frame{};
    Lease binding=lease_;
    if (!bound_ || !Current(&binding)) RecoverOwnedInventoryBinding(*this);
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL ShotgunWeapon::Deploy()
{
    ShotgunOwnerFrame frame{};
    Lease binding=lease_;
    if (!bound_ || !Current(&binding)) RecoverOwnedInventoryBinding(*this);
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_ = true;
    if (shot_.action.clip == -666) { shot_.action.clip = 0; WriteAmmo(); }
    shot_.action.action = 0;
    if (!Emit({ShotgunEffect::DeployPrelude})) return FALSE;
    shot_.action.aiming = false;
    spread_.scalar = 0; spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({ShotgunEffect::DeployReset})) return FALSE;
    if (reserveHud_ != shot_.action.reserve)
    {
        ShotgunEvent event{ShotgunEffect::ReserveHud}; event.value = shot_.action.reserve;
        if (!Emit(event)) return FALSE;
        reserveHud_ = shot_.action.reserve;
    }
    Lease lease = lease_; lease.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&lease)) return FALSE;
    if (pev->armorvalue != 1.F)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&lease)) return FALSE;
        ShotgunEvent event{ShotgunEffect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8F;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({ShotgunEffect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "shotgun", sizeof("shotgun"));
    if (!View(6)) return FALSE;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(&lease)) return FALSE;
    float duration{};
    if (!CurrentDuration(duration)) { status_ = ShotgunStatus::InvalidInput; return FALSE; }
    auto& a = shot_.action;
    a.ownerAttack = a.primary = a.secondary = a.idle = duration + gpGlobals->time;
    MirrorTimers(true); status_ = ShotgunStatus::Applied; return TRUE;
}
void ShotgunWeapon::Holster(int)
{
    ShotgunOwnerFrame frame{}; if (!Check(frame, false)) return;
    // Ordinary1006CD40 retains ADS, reloadStage and the weapon Think task.
    if (!View(7)) return;
    shot_.action.ownerAttack = AddDouble(gpGlobals->time, .5);
    selected_ = false; MirrorTimers(false); status_ = ShotgunStatus::Applied;
}

ShotgunControlHost ShotgunWeapon::ControlHost(ActionBridge& bridge) noexcept
{
    return {&bridge, [](void* p) noexcept { return Current(&static_cast<ActionBridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; }, ControlEvent};
}

bool ShotgunWeapon::ControlEvent(void* context, const ShotgunControlEvent& event) noexcept
{
    auto& b = *static_cast<ActionBridge*>(context); auto& self = *b.item;
    self.WriteAmmo(); self.MirrorTimers(self.selected_, false);
    switch (event.kind)
    {
    case ShotgunControlEventKind::ViewAnimation: return self.View(event.sequence);
    case ShotgunControlEventKind::ScheduleShellInsert:
        self.task_ = WeaponTask::Insert; self.taskDeadline_ = gpGlobals->time + event.delay;
        self.ScheduleThink(); return true;
    case ShotgunControlEventKind::SchedulePushback:
        return self.backends_.playerThink->Schedule(*self.m_pPlayer, PlayerThinkKind::Pushback,
            event.delay, self.backends_.playerModelGeneration.mapEpoch, self.backends_.ownerLife, ShotgunHostId);
    case ShotgunControlEventKind::PlayerWeaponMelee: return self.Animate(PlayerAction::WeaponMelee);
    default: break;
    }
    int stage;
    switch (event.kind)
    {
    case ShotgunControlEventKind::PlayerReloadStart: stage = 0; break;
    case ShotgunControlEventKind::PlayerReloadInsert: stage = 1; break;
    case ShotgunControlEventKind::PlayerReloadEndEmpty: stage = 2; break;
    case ShotgunControlEventKind::PlayerReloadEndLoaded: stage = 3; break;
    case ShotgunControlEventKind::PlayerReloadEndAbort: stage = 4; break;
    default: return false;
    }
    if (!self.Animate(PlayerAction::Reload)) return false;
    ShotgunEvent output{ShotgunEffect::ReloadStage}; output.value = stage;
    if (stage < 2) output.a = gpGlobals->time + 1.F;
    return self.Emit(output);
}
void ShotgunWeapon::SetResult(ShotgunControlResult result) noexcept
{
    status_ = result == ShotgunControlResult::Applied || result == ShotgunControlResult::Deferred ? ShotgunStatus::Applied :
        result == ShotgunControlResult::Ineligible ? ShotgunStatus::Ineligible :
        result == ShotgunControlResult::Cancelled ? ShotgunStatus::Cancelled :
        result == ShotgunControlResult::InvalidInput || result == ShotgunControlResult::MissingSequence ?
        ShotgunStatus::InvalidInput : result == ShotgunControlResult::UnsupportedState ?
        ShotgunStatus::UnsupportedState : ShotgunStatus::MissingBackend;
}
void ShotgunWeapon::Reload()
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return;
    GuardExpandedReload();
    if (shot_.action.clip >= 5) { status_ = ShotgunStatus::Ineligible; return; }
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartShotgunReload(shot_.action, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void ShotgunWeapon::SecondaryAttack()
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return;
    if (!frame.stamina) { status_ = ShotgunStatus::Ineligible; return; }
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartShotgunPushback(shot_.action, *frame.stamina, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}

bool ShotgunWeapon::CurrentDuration(float& duration) noexcept
{
    const int sequence = m_pPlayer->pev->weaponanim;
    if (sequence >= 20) { duration = 0; return true; }
    if (sequence < 0 || !(prepared_.sequences.present & (std::uint64_t{1} << sequence))) return false;
    duration = prepared_.sequences.duration[sequence];
    return true;
}
ShotgunAimHost ShotgunWeapon::AimHost(ActionBridge& bridge) noexcept
{
    return {&bridge,
        [](void* p) noexcept { return Current(&static_cast<ActionBridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, const ShotgunAimEvent& value) noexcept
        {
            auto& self = *static_cast<ActionBridge*>(p)->item;
            ShotgunEvent event{ShotgunEffect::Aim};
            event.first = {value.offset[0], value.offset[1], value.offset[2]};
            event.enabled = value.enabled; event.value = value.value;
            switch (value.kind)
            {
            case ShotgunAimEventKind::Presentation: return self.Emit(event);
            case ShotgunAimEventKind::CameraZoom:
                event = {ShotgunEffect::CameraZoom}; event.a = value.amount; return self.Emit(event);
            case ShotgunAimEventKind::PlayerFov: self.m_pPlayer->pev->fov = value.amount; return true;
            case ShotgunAimEventKind::ViewAnimation: return self.View(value.value);
            }
            return false;
        },
        [](void* p, float& duration) noexcept { return static_cast<ActionBridge*>(p)->item->CurrentDuration(duration); },
        [](void* p, float) noexcept
        { return Current(&static_cast<ActionBridge*>(p)->lease); }};
}
bool ShotgunWeapon::SetAim(bool enabled, bool input) noexcept
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return false;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const ShotgunAimMetadata metadata{{prepared_.aimOffset.x, prepared_.aimOffset.y, prepared_.aimOffset.z}, prepared_.aimZoom};
    const auto host = AimHost(bridge);
    const auto result = input ? ToggleShotgunAim(shot_.action, (frame.pressed & 0x40U) != 0, frame.toggleAim, metadata, host) :
        ApplyShotgunAim(shot_.action, enabled, metadata, host);
    if (!Current(&bridge.lease)) return false;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
    return result == ShotgunControlResult::Applied || result == ShotgunControlResult::Ineligible;
}

ShotgunShotFrame ShotgunWeapon::ShotFrame(const ShotgunOwnerFrame& owner) const noexcept
{
    const auto v = [](const Vector& p) noexcept -> ShotgunVector { return {p.x, p.y, p.z}; };
    ShotgunShotFrame f{}; f.damage = float(owner.bulletDamage);
    f.waterLevel = m_pPlayer->pev->waterlevel; f.shellModel = prepared_.shellModel;
    f.viewModel = prepared_.viewModelString; f.eventOwner2814 = owner.eventOwner2814;
    f.randomSeed = owner.randomSeed; f.attackPressed = (owner.pressed & 1U) != 0; f.infiniteAmmo = owner.infiniteAmmo;
    f.weaponOrigin = v(pev->origin); f.weaponViewOffset = v(pev->view_ofs); f.weaponVelocityY = pev->velocity.y;
    f.ownerOrigin = v(m_pPlayer->pev->origin); f.viewAngles = v(m_pPlayer->pev->v_angle);
    f.initialBasis = {v(gpGlobals->v_forward), v(gpGlobals->v_right), v(gpGlobals->v_up)};
    return f;
}
ShotgunShotHost ShotgunWeapon::ShotHost(ActionBridge& bridge) noexcept
{
    return {&bridge,
        [](void* p) noexcept { return Current(&static_cast<ActionBridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, const ShotgunShotEvent& value) noexcept
        {
            auto& self = *static_cast<ActionBridge*>(p)->item;
            const auto v = [](const ShotgunVector& a) noexcept -> Vector { return {a[0], a[1], a[2]}; };
            // Original installs pump Think before the first smoke consumer.
            if (value.kind == ShotgunShotEffect::Gunsmoke && self.shot_.pumpPending)
            {
                self.task_ = WeaponTask::Pump; self.taskDeadline_ = self.shot_.pumpDeadline;
                self.shot_.pumpPending = false; self.ScheduleThink();
                self.m_pPlayer->m_iWeaponVolume = self.shot_.weaponVolume;
                self.m_pPlayer->m_iWeaponFlash = self.shot_.weaponFlash;
            }
            ShotgunEvent e{ShotgunEffect::EmptyClick}; e.first = v(value.first); e.second = v(value.second);
            e.a = value.a; e.b = value.b; e.c = value.c; e.value = value.value; e.other = value.other;
            switch (value.kind)
            {
            case ShotgunShotEffect::Shell: e.kind = ShotgunEffect::Shell; break;
            case ShotgunShotEffect::Gunsmoke:
                e.kind = ShotgunEffect::Gunsmoke; e.enabled = true; e.modelString = value.other; e.other = 1; break;
            case ShotgunShotEffect::FireEvent: e.kind = ShotgunEffect::FireEvent; break;
            case ShotgunShotEffect::PlayerFire: return self.Animate(PlayerAction::Fire);
            case ShotgunShotEffect::Shake: e.kind = ShotgunEffect::Shake; break;
            case ShotgunShotEffect::ViewAnimation: return self.View(value.value);
            case ShotgunShotEffect::MuzzleFlash: self.m_pPlayer->pev->effects |= EF_MUZZLEFLASH; return true;
            case ShotgunShotEffect::Recoil: e.kind = ShotgunEffect::Recoil; break;
            case ShotgunShotEffect::ShotState: e.kind = ShotgunEffect::ShotState; break;
            case ShotgunShotEffect::AmmoWarning: e.kind = ShotgunEffect::AmmoWarning; break;
            case ShotgunShotEffect::EmptyClick: break;
            }
            return self.Emit(e);
        },
        [](void* p, const ShotgunVector& angles, ShotgunBasis& basis) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); b.item->WriteAmmo();
            g_engfuncs.pfnMakeVectors(Vector{angles[0], angles[1], angles[2]});
            if (!Current(&b.lease)) return false;
            const auto v = [](const Vector& a) noexcept -> ShotgunVector { return {a.x, a.y, a.z}; };
            basis = {v(gpGlobals->v_forward), v(gpGlobals->v_right), v(gpGlobals->v_up)}; return true;
        },
        [](void* p, ShotgunVector& out) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); b.item->WriteAmmo();
            const auto v = b.item->m_pPlayer->GetGunPosition();
            out = {v.x, v.y, v.z}; return Current(&b.lease);
        },
        [](void* p, float, ShotgunVector& out) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); const auto& v = gpGlobals->v_forward;
            out = {v.x, v.y, v.z}; return Current(&b.lease);
        },
        [](void* p, int low, int high, int& out) noexcept
        { auto& b = *static_cast<ActionBridge*>(p); out = g_engfuncs.pfnRandomLong(low, high); return Current(&b.lease); },
        [](void* p, const ShotgunBullet& bullet) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); b.item->WriteAmmo(); ShotgunOwnerFrame f{};
            if (!b.item->backends_.ownerFrame(b.item->backends_.context, f) || !Current(&b.lease)) return false;
            const auto v = [](const ShotgunVector& a) noexcept -> Vector { return {a[0], a[1], a[2]}; };
            PistolBallistics shot{v(bullet.source), v(bullet.direction), v(bullet.spread), gpGlobals->v_right, gpGlobals->v_up,
                bullet.seed, bullet.damage, b.item->shot_.action.aiming,
                PistolPlayerHits::OriginalCooperativeSkipPlayers, f.sourceMode};
            const auto result = FireShotgunBallistics(*b.item->m_pPlayer, shot, b.item->prepared_.ballistics, {Current, &b.lease});
            if (!Current(&b.lease)) return false;
            b.item->ReadAmmo(); return result.status == BallisticsStatus::Fired;
        },
        [](void* p, ShotgunVector& out) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); const auto v = b.item->m_pPlayer->EyePosition();
            out = {v.x, v.y, v.z}; return Current(&b.lease);
        },
        [](void* p, const ShotgunVector& position) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); ShotgunEvent e{ShotgunEffect::FireSound};
            e.first = {position[0], position[1], position[2]}; e.value = 0; e.other = 1; e.enabled = true;
            return b.item->Emit(e);
        }};
}
void ShotgunWeapon::PrimaryAttack()
{
    ShotgunOwnerFrame owner{}; if (!Check(owner)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    PistolBallistics check{{}, {1,0,0}, {}, {0,1,0}, {0,0,1}, owner.randomSeed, owner.bulletDamage,
        shot_.action.aiming, PistolPlayerHits::OriginalCooperativeSkipPlayers, owner.sourceMode};
    if (!shot_.action.action && !shot_.action.special && shot_.action.clip > 0 && m_pPlayer->pev->waterlevel != 3 &&
        CheckPistolBallistics(check, prepared_.ballistics, {Current, &bridge.lease}) != BallisticsStatus::Ready)
    { status_ = ShotgunStatus::MissingBackend; return; }
    auto request = ShotFrame(owner); request.weaponEntity = g_engfuncs.pfnIndexOfEdict(edict());
    if (!Current(&bridge.lease)) return;
    const auto result = ShotgunPrimaryAttack(shot_, request, ShotHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true);
    status_ = result == ShotgunShotResult::Fired || result == ShotgunShotResult::Empty ? ShotgunStatus::Applied :
        result == ShotgunShotResult::Ineligible ? ShotgunStatus::Ineligible :
        result == ShotgunShotResult::Cancelled ? ShotgunStatus::Cancelled :
        result == ShotgunShotResult::InvalidInput ? ShotgunStatus::InvalidInput : ShotgunStatus::MissingBackend;
}


void ShotgunWeapon::AdvanceOwnerFrame() noexcept
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return;
    // Player100EF7B2 updates every active firearm, including the 870. This is
    // its HUD scalar; ShotFrame keeps the original fixed eight-pellet cone.
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, 4, true});
}

void ShotgunWeapon::GuardExpandedReload() noexcept
{
    auto& a = shot_.action;
    if (!a.reloadStage || (a.clip < 5 && a.reserve > 0)) return;
    // Host-only skill grants must not enter the original clip!=5 insert loop.
    if (a.clip > 5 || a.reserve <= 0) a.reloadAbort = 1;
}
bool ShotgunWeapon::Fidget() noexcept
{
    const auto& a = shot_.action; const float now = gpGlobals->time;
    if (a.primary > now || a.secondary > now || a.idle > now || a.action || a.special) return true;
    auto lease = lease_; lease.active = true;
    const int chance = g_engfuncs.pfnRandomLong(0, 599);
    if (!Current(&lease)) return false;
    if (chance != 50 || shot_.action.aiming) return true;
    const int choice = g_engfuncs.pfnRandomLong(0, 2);
    if (!Current(&lease)) return false;
    return choice < 0 || choice > 2 || View(11 + choice);
}
bool ShotgunWeapon::CommonFrame() noexcept
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return false;
    Lease lease = lease_; lease.active = true;
    if (shot_.action.clip > 0) m_iPlayEmptySound = 1;
    if (!Fidget()) return false;
    if (gpGlobals->time > wearDeadline_)
    { wearDeadline_ = AddDouble(gpGlobals->time, .75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_;
    ReadAmmo();
    if (reserveHud_ != shot_.action.reserve)
    {
        ShotgunEvent event{ShotgunEffect::ReserveHud}; event.value = shot_.action.reserve;
        if (!Emit(event)) return false;
        reserveHud_ = shot_.action.reserve;
    }
    MagazineActionState input{};
    const auto refresh = [&]() noexcept
    {
        const auto& a = shot_.action;
        input.clip = a.clip; input.reserve = a.reserve; input.action = a.action; input.special = a.special;
        input.primary = a.primary; input.secondary = a.secondary; input.extra = a.extra;
        input.idle = a.idle; input.ownerAttack = a.ownerAttack; input.aiming = a.aiming;
    };
    refresh();
    struct InputBridge
    {
        ShotgunWeapon& item; Lease lease; MagazineActionState& input;
        void Refresh() noexcept
        {
            const auto& a = item.shot_.action;
            input.clip = a.clip; input.reserve = a.reserve; input.action = a.action; input.special = a.special;
            input.primary = a.primary; input.secondary = a.secondary; input.extra = a.extra;
            input.idle = a.idle; input.ownerAttack = a.ownerAttack; input.aiming = a.aiming;
        }
    } bridge{*this, lease, input};
    const M16InputHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<InputBridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, M16InputOwner& output) noexcept
        {
            auto& b = *static_cast<InputBridge*>(p); ShotgunOwnerFrame owner{};
            if (!b.item.backends_.ownerFrame(b.item.backends_.context, owner) || !Current(&b.lease)) return false;
            b.item.ReadAmmo(); b.Refresh();
            output = {static_cast<std::uint32_t>(b.item.m_pPlayer->pev->button), owner.activityBlock,
                b.item.m_pPlayer->pev->movetype}; return true;
        },
        [](void* p, M16InputEvent event) noexcept
        {
            auto& b = *static_cast<InputBridge*>(p);
            switch (event)
            {
            case M16InputEvent::InventoryChanged:
                if (!b.item.Emit({ShotgunEffect::InventoryChanged})) return false;
                b.Refresh(); return true;
            case M16InputEvent::Primary: b.item.PrimaryAttack(); break;
            case M16InputEvent::Secondary: b.item.SecondaryAttack(); break;
            case M16InputEvent::Reload: b.item.Reload(); break;
            case M16InputEvent::Aim: { const bool ok = b.item.SetAim(false, true); b.Refresh(); return ok; }
            }
            if (!Current(&b.lease)) return false;
            b.Refresh();
            return b.item.status_ == ShotgunStatus::Applied || b.item.status_ == ShotgunStatus::Ineligible;
        }};
    const auto result = DispatchM16FrameInput(input, m_fFireOnEmpty, host);
    if (!Current(&lease)) return false;
    status_ = result == M16ControlResult::Applied ? ShotgunStatus::Applied :
        result == M16ControlResult::Cancelled ? ShotgunStatus::Cancelled :
        result == M16ControlResult::InvalidInput ? ShotgunStatus::InvalidInput : ShotgunStatus::MissingBackend;
    return result == M16ControlResult::Applied;
}
void ShotgunWeapon::ItemPostFrame()
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return;
    if (gpGlobals->time < shot_.action.ownerAttack) { status_ = ShotgunStatus::Ineligible; return; }
    GuardExpandedReload();
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const ShotgunFrameHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<ActionBridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, ShotgunFrameOwner& out) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p); ShotgunOwnerFrame f{};
            if (!b.item->backends_.ownerFrame(b.item->backends_.context, f) || !Current(&b.lease)) return false;
            b.item->ReadAmmo(); b.item->GuardExpandedReload();
            const auto& v = b.item->m_pPlayer->pev->velocity;
            out = {v.x, v.y, f.pendingGateTime, f.originalFlags, f.pressed, f.condition3AA, f.pendingGate}; return true;
        },
        [](void* p, ShotgunFrameEvent event, int sequence) noexcept
        {
            auto& b = *static_cast<ActionBridge*>(p);
            switch (event)
            {
            case ShotgunFrameEvent::ViewAnimation: return b.item->View(sequence);
            case ShotgunFrameEvent::ExitAim: return b.item->SetAim(false);
            case ShotgunFrameEvent::Reload:
                b.item->Reload(); return Current(&b.lease) &&
                    (b.item->status_ == ShotgunStatus::Applied || b.item->status_ == ShotgunStatus::Ineligible);
            case ShotgunFrameEvent::CommonFrame: return b.item->CommonFrame();
            }
            return false;
        },
        [](void* p, float& duration) noexcept { return static_cast<ActionBridge*>(p)->item->CurrentDuration(duration); },
        ControlEvent};
    const auto result = AdvanceShotgunDerivedFrame(shot_, actionDeadline_, host);
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void ShotgunWeapon::WeaponIdle()
{
    ShotgunOwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = ShotgunIdle(shot_.action, AimHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void ShotgunWeapon::ScheduleThink() noexcept
{
    if (task_ == WeaponTask::None) { SetThink(nullptr); pev->nextthink = 0; return; }
    SetThink(&ShotgunWeapon::DispatchThink); pev->nextthink = taskDeadline_;
}
void ShotgunWeapon::DispatchThink()
{
    ShotgunOwnerFrame frame{}; if (!Check(frame, false)) return;
    if (task_ == WeaponTask::None || gpGlobals->time < taskDeadline_) return;
    const auto task = task_; task_ = WeaponTask::None; ScheduleThink();
    if (task == WeaponTask::Insert)
    {
        auto& a = shot_.action;
        if (a.clip >= 5 || a.reserve <= 0) { a.reloadStage = 1; a.reloadAbort = 1; }
        else InsertShotgunShell(a);
        WriteAmmo(); status_ = ShotgunStatus::Applied;
    }
    else
    {
        ActionBridge bridge{this, lease_}; // Pump survives an ordinary holster.
        const auto result = ShotgunPumpShell(ShotFrame(frame), ShotHost(bridge));
        if (!Current(&bridge.lease)) return;
        status_ = result == ShotgunShotResult::Fired ? ShotgunStatus::Applied :
            result == ShotgunShotResult::InvalidInput ? ShotgunStatus::InvalidInput : ShotgunStatus::MissingBackend;
    }
}
void ShotgunWeapon::Invalidate() noexcept
{
    modelRebind_.reset(); bound_ = selected_ = false;
    task_ = WeaponTask::None; shot_.pumpPending = false;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = ShotgunStatus::Cancelled;
}
}
