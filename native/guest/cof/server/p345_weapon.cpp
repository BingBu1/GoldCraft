// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "p345_weapon.hpp"
#include "../magazine_reload.hpp"
#include "../catalog.hpp"

#include <cmath>
#include <cstring>

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
constexpr unsigned OriginalId = 7;
}

bool P345Weapon::Current(void* context) noexcept
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

bool P345Weapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool P345Weapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void P345Weapon::ConfigureColdHost(P345ColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
P345Status P345Weapon::Prepare(const P345Prepared& p) noexcept
{
    preparedReady_ = false;
    status_ = P345Status::MissingBackend;
    if (!p.modelsReady || !p.soundsReady || !p.particleReady || !p.aimMetadataReady ||
        !p.inventoryReady || !p.pickupSoundReady || !p.deployPreludeReady ||
        p.world != MeleeWorld::Multiplayer || !p.worldModelString || !p.inventoryItemString ||
        !p.viewModelString || !p.playerModelString || p.shellModel <= 0 ||
        !p.melee.recoil || !p.melee.soundsReady || p.melee.shakeMessage <= 0 ||
        p.ballistics.customDecalMessage <= 0 || p.ballistics.bubbleModel <= 0 ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnIndexOfEdict)
        return status_;
    if (!Finite(p.aimOffset) || p.aimZoom < 0 || p.aimZoom > 255 ||
        p.hostSlot != P345HudSlot || p.hostPosition != P345HudPosition)
        return status_ = P345Status::InvalidInput;
    for (int i = 0; i <= 33; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = P345Status::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = P345Status::Ready;
}
void P345Weapon::Precache()
{
    preparedReady_ = false;
    status_ = P345Status::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    P345Prepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

P345Status P345Weapon::Bind(CBasePlayer& owner, const P345Prepared& p,
                          const P345Backends& backends) noexcept
{
    bound_ = selected_ = meleePending_ = false;
    status_ = P345Status::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != P345RequiredEffects || !backends.playerAnimation)
        return status_;
    if (Prepare(p) != P345Status::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = P345Status::InvalidInput;
    status_ = P345Status::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, Pistol::P345,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = P345Status::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias P345 to an unrelated stock CS weapon.
    m_iId = FindPistol(Pistol::P345)->hostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = P345Status::Ready;
}

void P345Weapon::Spawn()
{
    action_ = {};
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 8;
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
    { status_ = P345Status::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = P345Status::InvalidInput; return; }
    m_iId = FindPistol(Pistol::P345)->hostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 8;
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
    SetUse(&P345Weapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&P345Weapon::WorldTouch);
    SetThink(&P345Weapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = P345Status::Applied;
}

int P345Weapon::GetItemInfo(ItemInfo* info)
{
    // Original 1005DB80 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_p345";
    info->pszAmmo1 = "cof_p345";
    info->iMaxAmmo1 = 80;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 8;
    info->iId = m_iId = FindPistol(Pistol::P345)->hostId;
    info->iSlot = P345HudSlot;
    info->iPosition = P345HudPosition;
    info->iWeight = 5;
    return 1;
}

int P345Weapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void P345Weapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void P345Weapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&P345Weapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&P345Weapon::WorldTouch);
}
void P345Weapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void P345Weapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = P345Status::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = P345Status::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        P345Event event{P345Effect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int P345Weapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    P345Backends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != P345Status::Ready) return FALSE;
    P345OwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = P345Status::Ineligible; return FALSE; }
    return TRUE;
}
int P345Weapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == P345Status::Ready || status_ == P345Status::Applied);
}
int P345Weapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int P345Weapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<P345Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    P345OwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = P345Status::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 8 ? amount : 8;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, "cof_p345", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({P345Effect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int P345Weapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<P345Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    P345OwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_p345", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void P345Weapon::AttachToPlayer(CBasePlayer* player)
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
void P345Weapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void P345Weapon::Drop() { RetireWorld(false); }
void P345Weapon::Kill() { RetireWorld(true); }
void P345Weapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = P345Status::UnsupportedState;
}

bool P345Weapon::Check(P345OwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = P345Status::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = P345Status::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = P345Status::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || frame.meleeDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 24 && frame.reserveLimit != 80) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = P345Status::InvalidInput; return false; }
    ReadAmmo();
    if ((action_.clip < 0 && (active || action_.clip != -666)) || action_.clip > 8 ||
        action_.reserve < 0 || action_.reserve > 80)
    { status_ = P345Status::InvalidInput; return false; }
    status_ = P345Status::Ready;
    return true;
}

bool P345Weapon::Emit(P345Event event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l)) return false;
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = P345Status::MissingBackend;
    return delivered;
}
bool P345Weapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, Pistol::P345, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
bool P345Weapon::View(int sequence) noexcept
{
    P345Event event{P345Effect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void P345Weapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void P345Weapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
void P345Weapon::MirrorTimers(bool selected) noexcept
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
BOOL P345Weapon::CanDeploy()
{
    P345OwnerFrame frame{};
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL P345Weapon::Deploy()
{
    P345OwnerFrame frame{};
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_ = true;
    if (action_.clip == -666) { action_.clip = 0; WriteAmmo(); }
    action_.action = 0;
    if (!Emit({P345Effect::DeployPrelude})) return FALSE;
    action_.aiming = false;
    spread_.scalar = 0;
    spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({P345Effect::DeployReset})) return FALSE;
    if (reserveHud_ != action_.reserve)
    {
        P345Event e{P345Effect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return FALSE;
        reserveHud_ = action_.reserve;
    }
    // Original 1011C3FE consumes pitch RNG even when armorvalue suppresses
    // one sleeve. Keep RNG ordering before ammo tabulation/player models.
    Lease sleeveLease = lease_; sleeveLease.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&sleeveLease)) return FALSE;
    if (pev->armorvalue == 0)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&sleeveLease)) return FALSE;
        P345Event event{P345Effect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8f;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({P345Effect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "onehanded", sizeof("onehanded"));
    const int sequence = action_.clip ? 6 : 7;
    if (!View(sequence)) return FALSE;
    Lease l = lease_; l.active = true;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(const_cast<Lease*>(&l))) return FALSE;
    const float deadline = gpGlobals->time + prepared_.sequences.duration[sequence];
    action_.ownerAttack = action_.primary = action_.secondary = action_.idle = deadline;
    MirrorTimers(true);
    status_ = P345Status::Applied;
    return TRUE;
}
void P345Weapon::Holster(int)
{
    P345OwnerFrame frame{};
    if (!Check(frame, false)) return;
    // Safety adaptation: original normal Holster did not clear the player think.
    // A detached CoF weapon may never damage as a subsequently selected CS item.
    meleePending_ = false;
    SetThink(nullptr);
    pev->nextthink = 0;
    if (action_.aiming)
    {
        P345Event e{P345Effect::Aim}; e.a = 350; e.other = 1;
        if (!Emit(e)) return;
        m_pPlayer->pev->fov = 90;
        action_.aiming = false;
    }
    if (!View(action_.clip ? 8 : 9)) return;
    action_.ownerAttack = AddDouble(gpGlobals->time, .5);
    selected_ = false;
    MirrorTimers(false);
    status_ = P345Status::Applied;
}

MagazineActionHost P345Weapon::ActionHost(ActionBridge& bridge) noexcept { return {&bridge, [](void* p) noexcept {
    auto& b = *static_cast<ActionBridge*>(p);
    return Current(&b.lease);
}, MagazineEvent}; }

bool P345Weapon::MagazineEvent(void* context, const guest::cof::MagazineEvent& e) noexcept
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
        self.SetThink(&P345Weapon::DelayedMelee);
        self.pev->nextthink = gpGlobals->time + e.delay;
        return true;
    }
    return false;
}
void P345Weapon::SetResult(MagazineActionResult result) noexcept
{
    status_ = result == MagazineActionResult::Applied || result == MagazineActionResult::Deferred ?
        P345Status::Applied : result == MagazineActionResult::Ineligible ? P345Status::Ineligible :
        result == MagazineActionResult::Cancelled ? P345Status::Cancelled : P345Status::InvalidInput;
}
void P345Weapon::Reload()
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartMagazineReload(Pistol::P345, action_, gpGlobals->time,
                                           prepared_.sequences, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void P345Weapon::SecondaryAttack()
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    if (CheckPistolWeaponMelee(*m_pPlayer, {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer},
        prepared_.melee, {Current, &bridge.lease}) != MeleeStatus::Ready)
    { status_ = P345Status::MissingBackend; return; }
    const auto result = StartPistolMelee(Pistol::P345, action_, gpGlobals->time, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void P345Weapon::DelayedMelee()
{
    P345OwnerFrame frame{}; if (!Check(frame) || !meleePending_) return;
    if (gpGlobals->time < pev->nextthink) { status_ = P345Status::Ineligible; return; }
    meleePending_ = false; SetThink(nullptr); pev->nextthink = 0;
    Lease l = lease_; l.active = true;
    const auto result = PistolWeaponMelee(*m_pPlayer,
        {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer}, prepared_.melee, {Current, &l});
    if (!Current(const_cast<Lease*>(&l))) return;
    status_ = result == MeleeStatus::Hit || result == MeleeStatus::Miss ? P345Status::Applied :
        result == MeleeStatus::Cancelled ? P345Status::Cancelled : P345Status::MissingBackend;
}

bool P345Weapon::SetAim(bool enabled) noexcept
{
    // All ordinary callers set the aim bit before entering the original
    // entry/exit method (F980 toggle, pending reload and hold-aim release).
    action_.aiming = enabled;
    P345Event e{P345Effect::Aim}; e.enabled = enabled;
    e.first = enabled ? prepared_.aimOffset : Vector{};
    e.value = enabled ? prepared_.aimZoom : 0; e.a = 350;
    if (!Emit(e)) return false;
    m_pPlayer->pev->fov = enabled ? float(prepared_.aimZoom) : 90.0f;
    const int sequence = (enabled ? 19 : 23) + (action_.clip == 0);
    if (!View(sequence)) return false;
    action_.aiming = enabled;
    action_.primary = action_.secondary = action_.extra = AddDouble(gpGlobals->time, .3);
    action_.idle = gpGlobals->time + prepared_.sequences.duration[sequence];
    MirrorTimers(true);
    return true;
}

void P345Weapon::PrimaryAttack()
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || !(frame.pressed & 1u) || action_.action)
    { status_ = P345Status::Ineligible; return; }
    if (m_pPlayer->pev->waterlevel == 3 || action_.clip <= 0)
    {
        if (m_pPlayer->pev->waterlevel != 3 && !m_fFireOnEmpty) { Reload(); return; }
        if (now <= emptyDeadline_) { status_ = P345Status::Ineligible; return; }
        if (!View(action_.aiming ? 27 : 16) || !Emit({P345Effect::EmptyClick})) return;
        emptyDeadline_ = AddDouble(now, .5); status_ = P345Status::Applied; return;
    }
    // Full preflight precedes shell, particle, ammo, animation and recoil.
    PistolBallistics shot{{}, {1,0,0}, {spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0}, {0,0,1}, frame.randomSeed, frame.bulletDamage, action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers, frame.sourceMode};
    Lease l = lease_; l.active = true;
    if (CheckPistolBallistics(shot, prepared_.ballistics, {Current, const_cast<Lease*>(&l)}) != BallisticsStatus::Ready)
    { status_ = P345Status::MissingBackend; return; }
    const Vector forward = gpGlobals->v_forward, right = gpGlobals->v_right, up = gpGlobals->v_up;
    P345Event shell{P345Effect::Shell};
    shell.first = {((pev->origin.x + pev->view_ofs.x) + forward.x * 80.0f) + right.x * 20.0f,
                   ((pev->origin.y + pev->view_ofs.y) + forward.y * 80.0f) + right.y * 20.0f,
                   ((pev->origin.z + pev->view_ofs.z) + forward.z * 80.0f) + right.z * 20.0f};
    shell.second = {(up.x*200.0f + right.x*220.0f) + forward.x*40.0f,
                    (up.y*200.0f + right.y*220.0f) + forward.y*40.0f,
                    (up.z*200.0f + right.z*220.0f) + forward.z*40.0f};
    // Original reads weapon pev+54h (velocity.y), not an inferred yaw/z value.
    shell.a = pev->velocity.y; shell.value = prepared_.shellModel; shell.other = 1;
    if (!Emit(shell)) return;
    P345Event smoke{P345Effect::Gunsmoke};
    smoke.value = g_engfuncs.pfnIndexOfEdict(l.weapon);
    if (!Current(&l)) return;
    smoke.enabled = true;
    smoke.modelString = prepared_.viewModelString;
    if (!Emit(smoke)) return;
    if (!frame.infiniteAmmo) --action_.clip;
    WriteAmmo();
    g_engfuncs.pfnMakeVectors(m_pPlayer->pev->v_angle);
    if (!Current(const_cast<Lease*>(&l))) return;
    const Vector gun = m_pPlayer->GetGunPosition();
    if (!Current(const_cast<Lease*>(&l))) return;
    P345Event fire{P345Effect::FireEvent}; fire.first = m_pPlayer->pev->origin;
    fire.second = gun + gpGlobals->v_right * 32.0f;
    fire.value = OriginalId; fire.other = frame.eventOwner2814 != 1;
    if (!Emit(fire) || !Animate(PlayerAction::Fire)) return;
    const int recoil = g_engfuncs.pfnRandomLong(-4,4);
    if (!Current(const_cast<Lease*>(&l))) return;
    P345Event kick{P345Effect::Recoil}; kick.first = {float(recoil),3,3};
    if (!Emit(kick)) return;
    P345Event shake{P345Effect::Shake};
    const bool crouch = (frame.originalFlags & 0x4000u) != 0;
    shake.a = crouch ? 4.0f : 5.0f; shake.b = crouch ? .2f : .35f; shake.c = .15f; shake.value = 2;
    if (!Emit(shake) || !View((action_.aiming ? 25 : 2) + (action_.clip == 0)) ||
        !Animate(PlayerAction::Move)) return;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    const Vector angles = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
    g_engfuncs.pfnMakeVectors(angles);
    if (!Current(const_cast<Lease*>(&l))) return;
    shot.source = m_pPlayer->GetGunPosition();
    if (!Current(const_cast<Lease*>(&l))) return;
    shot.direction = gpGlobals->v_forward; shot.right = gpGlobals->v_right; shot.up = gpGlobals->v_up;
    spread_.scalar += .85f; spread_.recoveryDeadline = AddDouble(now, .75);
    const auto result = FirePistolBallistics(*m_pPlayer, shot, prepared_.ballistics,
                                          {Current, const_cast<Lease*>(&l)});
    if (!Current(const_cast<Lease*>(&l))) return;
    if (result.status != BallisticsStatus::Fired) { status_ = P345Status::Cancelled; return; }
    action_.idle = now + 1.0f;
    // Original100F9100(3) is part of the authenticated recoil/effect backend.
    P345Event shotState{P345Effect::ShotState}; shotState.a = 3;
    if (!Emit(shotState) || !Emit({P345Effect::FireSound})) return;
    P345Event latch{P345Effect::ShotLatch}; latch.value = 1; latch.a = AddDouble(now,.2);
    if (!Emit(latch)) return;
    action_.primary = action_.secondary = AddDouble(now,.2);
    if (action_.clip <= 0) action_.primary = now + 1.0f;
    MirrorTimers(true); status_ = P345Status::Applied;
}

void P345Weapon::ReloadCommitted(void* context, const MagazineReload& state) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    self.action_.clip = state.clip; self.action_.reserve = state.reserve;
    self.WriteAmmo();
    bridge.callbackFailed = !self.Emit({P345Effect::InventoryChanged});
}
void P345Weapon::AdvanceOwnerFrame() noexcept
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

void P345Weapon::ItemPostFrame()
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    Lease l = lease_; l.active = true;
    const float now = gpGlobals->time;
    if (now < action_.ownerAttack) { status_ = P345Status::Ineligible; return; }
    if (action_.action == 2 && !action_.loading) action_.action = 0;
    if (action_.pendingReload && !frame.pendingGate && frame.pendingGateTime <= now &&
        action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action)
    {
        if (action_.aiming) { if (!SetAim(false)) return; }
        else { action_.pendingReload = false; Reload(); if (!Current(&l)) return; }
    }
    if (!action_.action && now > action_.primary && now > action_.secondary && now > action_.extra &&
        !frame.toggleAim && action_.aiming &&
        !((m_pPlayer->pev->button | frame.pressed | frame.released) & 0x40u))
        if (!SetAim(false)) return;
    const float speedSquared = m_pPlayer->pev->velocity.x*m_pPlayer->pev->velocity.x +
                               m_pPlayer->pev->velocity.y*m_pPlayer->pev->velocity.y;
    const float speed = static_cast<float>(std::sqrt(double(speedSquared)));
    if (!action_.loading && action_.action != 2 && now > action_.primary)
    {
        if (frame.condition3AA && !action_.aiming && now > action_.extra)
        {
            if (!action_.action)
            {
                action_.action = 5; const int seq = action_.clip ? 10 : 13;
                if (!View(seq)) return;
                const float duration = prepared_.sequences.duration[seq];
                action_.primary = static_cast<float>(double(now) + double(duration)*.5);
                actionDeadline_ = now + duration;
            }
        }
        else if (action_.action == 5)
        {
            const int seq = action_.clip ? 12 : 15; if (!View(seq)) return;
            action_.primary = action_.idle = now + prepared_.sequences.duration[seq]; action_.action = 0;
        }
        const bool sprint = (frame.originalFlags & 0x40000u) && speed > 0;
        if (sprint && action_.action != 5 && action_.action != 1)
        {
            action_.action = 1; const int seq = action_.clip ? 10 : 13; if (!View(seq)) return;
            const float duration = prepared_.sequences.duration[seq];
            action_.primary = static_cast<float>(double(now) + double(duration)*.5);
            actionDeadline_ = now + duration;
        }
        if (sprint && action_.action == 1 && now > actionDeadline_)
        {
            const int seq = action_.clip ? 11 : 14; if (!View(seq)) return;
            actionDeadline_ = now + prepared_.sequences.duration[seq];
        }
        if (!sprint && action_.action == 1 && now > actionDeadline_)
        {
            const int seq = action_.clip ? 12 : 15; if (!View(seq)) return;
            action_.primary = action_.idle = now + prepared_.sequences.duration[seq]; action_.action = 0;
        }
    }
    if (action_.clip > 0) m_iPlayEmptySound = 1;
    // Common1011D587 invokes P345's separate Fidget1005DA70 before wear,
    // reserve HUD, reload completion and input. It is not WeaponIdle. The
    // chance RNG runs even while aiming; only the second RNG is aim-gated.
    if (action_.primary <= now && action_.secondary <= now && action_.idle <= now &&
        !action_.action && !action_.special)
    {
        const int chance = g_engfuncs.pfnRandomLong(0, 599);
        if (!Current(&l)) return;
        if (chance == 50 && !action_.aiming)
        {
            const int choice = g_engfuncs.pfnRandomLong(0, 2);
            if (!Current(&l)) return;
            if (choice >= 0 && choice <= 2 && !View(28 + choice * 2 + (action_.clip == 0))) return;
        }
    }
    if (now > wearDeadline_) { wearDeadline_ = AddDouble(now,.75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_; // original max hook is empty for P345
    if (reserveHud_ != action_.reserve)
    {
        P345Event e{P345Effect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return; reserveHud_ = action_.reserve;
    }
    if (action_.loading)
    {
        MagazineReload reload{action_.clip,action_.reserve,action_.ownerAttack,true};
        ActionBridge bridge{this, lease_}; bridge.lease.active = true;
        CommitMagazineReload(reload,8,0,now,ReloadCommitted,&bridge);
        if (!Current(&bridge.lease)) return;
        if (bridge.callbackFailed) return;
        action_.clip = reload.clip; action_.reserve = reload.reserve; action_.loading = reload.loading;
        WriteAmmo();
    }
    const unsigned buttons = m_pPlayer->pev->button;
    const bool allowed = frame.activityBlock <= 0 && m_pPlayer->pev->movetype != 5;
    if ((buttons & 0x800u) && frame.activityBlock <= 0 && action_.secondary <= now)
    {
        if (allowed) { if (!Emit({P345Effect::InventoryChanged})) return; SecondaryAttack(); }
    }
    else if ((buttons & 0x40u) && allowed && action_.ownerAttack <= now)
    {
        if (!Emit({P345Effect::InventoryChanged})) return;
        if (action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action &&
            (frame.pressed & 0x40u) && (frame.toggleAim || !action_.aiming))
            if (!SetAim(!action_.aiming)) return;
    }
    else if ((buttons & 1u) && frame.activityBlock <= 0 && action_.primary <= now)
    {
        if (allowed)
        {
            if (!action_.clip) m_fFireOnEmpty = 1;
            if (!Emit({P345Effect::InventoryChanged})) return;
            PrimaryAttack();
        }
    }
    else if ((buttons & 0x2000u) && !action_.loading)
    { if (m_pPlayer->pev->movetype != 5) Reload(); }
    else if (!(buttons & 0x841u)) m_fFireOnEmpty = 0;
    if (!Current(const_cast<Lease*>(&l))) return;
    WriteAmmo(); MirrorTimers(true);
}
void P345Weapon::WeaponIdle()
{
    P345OwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = MagazineIdle(Pistol::P345, action_, gpGlobals->time, prepared_.sequences, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    MirrorTimers(true); SetResult(result);
}
void P345Weapon::Invalidate() noexcept
{
    bound_ = selected_ = meleePending_ = false;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = P345Status::Cancelled;
}
}
