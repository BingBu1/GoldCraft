// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "revolver_weapon.hpp"
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
constexpr unsigned OriginalId = 21;
constexpr std::uint64_t InventoryKind = 0x434f465245564f4cULL;
}

goldcraft::scenario::GuestProfile RevolverWeapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {FindPistol(Pistol::Revolver)->hostId,GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior,GuestKill::StandardPlayerProcessing};
}
struct RevolverWeapon::InventoryState
{
    MagazineActionState action;
    RevolverCylinder cylinder;
    CylinderThink cylinderThink{};
    SpreadState spread;
    SpreadTransitions transitions;
    float actionDeadline{},emptyDeadline{},wearDeadline{},nextThink{};
    float cylinderDeadline{},meleeDeadline{},armor{};
    int body{};
    float primary{},secondary{},idle{},ownerAttack{};
    int reserveHud{},wearMaximum{},wear{},clip{},inReload{},fireOnEmpty{};
    int ownerSerial{},weaponSerial{},modelIndex{},ownerSpawn{};
    std::uintptr_t map{},ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    bool selected{},meleePending{};
    RevolverStatus status{};
};
bool RevolverWeapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease=lease_;
    if(!bound_||!pev||m_pPlayer!=lease.player||!Current(const_cast<Lease*>(&lease)))return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState)<=512);
    InventoryState saved{};
    saved.action=action_;saved.spread=spread_;saved.transitions=transitions_;
    saved.cylinder=cylinder_;saved.cylinderThink=cylinderThink_;
    saved.cylinderDeadline=cylinderDeadline_;saved.meleeDeadline=meleeDeadline_;
    saved.body=pev->body;saved.armor=pev->armorvalue;
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
bool RevolverWeapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
{
    if(!pev||m_pPlayer!=&player)return false;
    const auto previous=lease_;
    if(bound_&&Current(const_cast<Lease*>(&previous))&&pev->owner==player.edict())return true;
    const auto world=worldLease_;const auto cold=cold_;const auto prepared=prepared_;
    if(!WorldCurrent(world)||!cold.ownerBackends)return false;
    const EHANDLE owner(&player);RevolverBackends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found)return false;
    if(Bind(player,prepared,backend)!=RevolverStatus::Ready)return false;
    const auto lease=lease_;
    AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    return !selected_||PublishOwnerState();
}
bool RevolverWeapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
{
    const auto lease=lease_;
    if(input.kind!=InventoryKind||input.bytes!=sizeof(InventoryState)||!bound_||
       !Current(const_cast<Lease*>(&lease))||m_pPlayer!=lease.player)return false;
    InventoryState saved{};std::memcpy(&saved,input.data.data(),sizeof(saved));
    if(saved.ownerSerial!=lease.ownerSerial||saved.weaponSerial!=lease.weaponSerial||saved.modelIndex!=lease.modelIndex||
       saved.ownerSpawn!=m_pPlayer->m_iNumSpawns||saved.map!=reinterpret_cast<std::uintptr_t>(worldLease_.context)||
       saved.ownerContext!=reinterpret_cast<std::uintptr_t>(lease.context)||
       (saved.selected&&m_pPlayer->m_pActiveItem!=this))return false;
    action_=saved.action;spread_=saved.spread;transitions_=saved.transitions;
    cylinder_=saved.cylinder;cylinderThink_=saved.cylinderThink;
    cylinderDeadline_=saved.cylinderDeadline;meleeDeadline_=saved.meleeDeadline;
    pev->body=saved.body;pev->armorvalue=saved.armor;
    actionDeadline_=saved.actionDeadline;emptyDeadline_=saved.emptyDeadline;wearDeadline_=saved.wearDeadline;
    m_pfnThink=saved.think;pev->nextthink=saved.nextThink;
    m_flNextPrimaryAttack=saved.primary;m_flNextSecondaryAttack=saved.secondary;m_flTimeWeaponIdle=saved.idle;
    if(saved.selected)m_pPlayer->m_flNextAttack=saved.ownerAttack;
    m_iClip=saved.clip;m_fInReload=saved.inReload;m_fFireOnEmpty=saved.fireOnEmpty;
    reserveHud_=saved.reserveHud;wearMaximum_=saved.wearMaximum;wear_=saved.wear;
    selected_=saved.selected;meleePending_=saved.meleePending;status_=saved.status;
    return !selected_||PublishOwnerState();
}
bool RevolverWeapon::PublishOwnerState() noexcept
{
    auto lease=lease_;lease.active=true;
    if(!selected_||!Current(&lease)||!prepared_.aimMetadataReady)return false;
    // Host rollback publishes the existing proved aim state. It must not call
    // SetAim, which starts a new animation and changes the original deadlines.
    RevolverEvent event{RevolverEffect::Aim};event.enabled=action_.aiming;
    event.first=action_.aiming?prepared_.aimOffset:Vector{};
    event.value=action_.aiming?prepared_.aimZoom:0;event.a=350;
    if(!Emit(event))return false;
    lease.player->pev->fov=action_.aiming?float(prepared_.aimZoom):90.0F;
    return Body(pev->body);
}

bool RevolverWeapon::Current(void* context) noexcept
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

bool RevolverWeapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool RevolverWeapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void RevolverWeapon::ConfigureColdHost(RevolverColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
RevolverStatus RevolverWeapon::Prepare(const RevolverPrepared& p) noexcept
{
    preparedReady_ = false;
    status_ = RevolverStatus::MissingBackend;
    if (!p.modelsReady || !p.soundsReady || !p.particleReady || !p.aimMetadataReady ||
        !p.inventoryReady || !p.pickupSoundReady || !p.deployPreludeReady ||
        !p.cylinderReady || !p.ejectEventReady || !p.reloadStagesReady || !p.ammoXReady ||
        p.world != MeleeWorld::Multiplayer || !p.worldModelString || !p.inventoryItemString ||
        !p.viewModelString || !p.playerModelString || p.shellModel <= 0 ||
        !p.melee.recoil || !p.melee.soundsReady || !ValidPistolMeleeSamples(p.melee.samples) ||
        p.melee.shakeMessage <= 0 ||
        p.ballistics.customDecalMessage <= 0 || p.ballistics.bubbleModel <= 0 ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnIndexOfEdict)
        return status_;
    if (!Finite(p.aimOffset) || p.aimZoom < 0 || p.aimZoom > 255 ||
        p.hostSlot != RevolverHudSlot || p.hostPosition != RevolverHudPosition)
        return status_ = RevolverStatus::InvalidInput;
    for (int i = 0; i <= 20; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = RevolverStatus::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = RevolverStatus::Ready;
}
void RevolverWeapon::Precache()
{
    preparedReady_ = false;
    status_ = RevolverStatus::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    RevolverPrepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

RevolverStatus RevolverWeapon::Bind(CBasePlayer& owner, const RevolverPrepared& p,
                          const RevolverBackends& backends) noexcept
{
    bound_ = selected_ = meleePending_ = false;
    status_ = RevolverStatus::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != RevolverRequiredEffects || !backends.playerAnimation)
        return status_;
    if (Prepare(p) != RevolverStatus::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = RevolverStatus::InvalidInput;
    status_ = RevolverStatus::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, Pistol::Revolver,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = RevolverStatus::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias Revolver to an unrelated stock CS weapon.
    m_iId = FindPistol(Pistol::Revolver)->hostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = RevolverStatus::Ready;
}

void RevolverWeapon::Spawn()
{
    action_ = {};
    cylinder_ = {}; cylinderThink_ = CylinderThink::None;
    cylinderDeadline_ = meleeDeadline_ = 0;
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 5;
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
    { status_ = RevolverStatus::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = RevolverStatus::InvalidInput; return; }
    m_iId = FindPistol(Pistol::Revolver)->hostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 5;
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
    SetUse(&RevolverWeapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&RevolverWeapon::WorldTouch);
    SetThink(&RevolverWeapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = RevolverStatus::Applied;
}

int RevolverWeapon::GetItemInfo(ItemInfo* info)
{
    // Original 10063C90 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_revolver";
    info->pszAmmo1 = "cof_revolver";
    info->iMaxAmmo1 = 50;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 5;
    info->iId = m_iId = FindPistol(Pistol::Revolver)->hostId;
    info->iSlot = RevolverHudSlot;
    info->iPosition = RevolverHudPosition;
    info->iWeight = 5;
    return 1;
}

int RevolverWeapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void RevolverWeapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void RevolverWeapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&RevolverWeapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&RevolverWeapon::WorldTouch);
}
void RevolverWeapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void RevolverWeapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = RevolverStatus::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = RevolverStatus::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        RevolverEvent event{RevolverEffect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int RevolverWeapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    RevolverBackends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != RevolverStatus::Ready) return FALSE;
    RevolverOwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = RevolverStatus::Ineligible; return FALSE; }
    return TRUE;
}
int RevolverWeapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == RevolverStatus::Ready || status_ == RevolverStatus::Applied);
}
int RevolverWeapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int RevolverWeapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<RevolverWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    RevolverOwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = RevolverStatus::UnsupportedState; return FALSE; }
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
    const int ammo = owner.player->GiveAmmo(amount, "cof_revolver", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({RevolverEffect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int RevolverWeapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<RevolverWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    RevolverOwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_revolver", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void RevolverWeapon::AttachToPlayer(CBasePlayer* player)
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
void RevolverWeapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void RevolverWeapon::Drop() { RetireWorld(false); }
void RevolverWeapon::Kill() { RetireWorld(true); }
void RevolverWeapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = RevolverStatus::UnsupportedState;
}

bool RevolverWeapon::Check(RevolverOwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = RevolverStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = RevolverStatus::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = RevolverStatus::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        !std::isfinite(frame.cylinderAngle) || !std::isfinite(frame.cylinderGate) ||
        frame.cylinderAngle < 0 || frame.cylinderAngle > 360 ||
        frame.bulletDamage < 0 || frame.meleeDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 15 && frame.reserveLimit != 50) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = RevolverStatus::InvalidInput; return false; }
    ReadAmmo();
    if ((action_.clip < 0 && (active || action_.clip != -666)) || action_.clip > 5 ||
        action_.reserve < 0 || action_.reserve > 50 || pev->body < 0 || pev->body > 5 ||
        (active && (m_pPlayer->pev->weaponanim < 0 || m_pPlayer->pev->weaponanim > 20)))
    { status_ = RevolverStatus::InvalidInput; return false; }
    status_ = RevolverStatus::Ready;
    return true;
}

bool RevolverWeapon::Emit(RevolverEvent event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l)) return false;
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = RevolverStatus::MissingBackend;
    return delivered;
}
bool RevolverWeapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, Pistol::Revolver, action, gpGlobals->time);
    if (!Current(&l)) { status_ = RevolverStatus::Cancelled; return false; }
    if (result == PlayerAnimationResult::Rejected) { status_ = RevolverStatus::MissingBackend; return false; }
    return true;
}
bool RevolverWeapon::View(int sequence) noexcept
{
    // Original1011F5D0 stores owner view weaponanim, never weapon sequence.
    m_pPlayer->pev->weaponanim = sequence;
    RevolverEvent event{RevolverEffect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void RevolverWeapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void RevolverWeapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = 0;
}
void RevolverWeapon::MirrorTimers(bool selected) noexcept
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
BOOL RevolverWeapon::CanDeploy()
{
    RevolverOwnerFrame frame{};
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL RevolverWeapon::Deploy()
{
    RevolverOwnerFrame frame{};
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_ = true;
    action_.action = 0;
    cylinder_.loading = cylinder_.cancel = false;
    if (action_.clip == -666) { action_.clip = 0; WriteAmmo(); }
    if (!frame.firstDrawn)
    {
        if (!Emit({RevolverEffect::FirstDraw}) || !Body(action_.clip)) return FALSE;
    }
    if (!Emit({RevolverEffect::DeployPrelude})) return FALSE;
    action_.aiming = false;
    spread_.scalar = 0;
    spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({RevolverEffect::DeployReset})) return FALSE;
    if (reserveHud_ != action_.reserve)
    {
        RevolverEvent e{RevolverEffect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return FALSE;
        reserveHud_ = action_.reserve;
    }
    Lease l = lease_; l.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&l)) return FALSE;
    if (pev->armorvalue != 1.0F)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&l)) return FALSE;
        RevolverEvent event{RevolverEffect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8f;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({RevolverEffect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "onehanded", sizeof("onehanded"));
    if (!View(1)) return FALSE;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(&l)) return FALSE;
    const float deadline = gpGlobals->time + prepared_.sequences.duration[1];
    action_.ownerAttack = action_.primary = action_.secondary = action_.idle = deadline;
    MirrorTimers(true);
    status_ = RevolverStatus::Applied;
    return TRUE;
}
void RevolverWeapon::Holster(int)
{
    RevolverOwnerFrame frame{};
    if (!Check(frame, false)) return;
    if (!View(2)) return;
    action_.ownerAttack = AddDouble(gpGlobals->time, .5);
    cylinderThink_ = CylinderThink::None;
    // Host safety adaptation: original melee used a separate player think;
    // detached weapons must not damage as a subsequently selected CS item.
    meleePending_ = false;
    SetThink(nullptr); pev->nextthink = 0;
    selected_ = false;
    MirrorTimers(false);
    status_ = RevolverStatus::Applied;
}





void RevolverWeapon::Reload()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    const int remaining = 5 - action_.clip;
    if (action_.reserve <= 0 || action_.primary > now || action_.action == 1 ||
        action_.action > 2 || (!(remaining >= action_.reserve) && remaining == 0) ||
        cylinder_.loading || action_.pendingReload)
    { status_ = RevolverStatus::Ineligible; return; }
    if (action_.aiming) { action_.pendingReload = true; status_ = RevolverStatus::Applied; return; }
    action_.loading = false; m_fInReload = 0; action_.action = 2;
    for (int i = 1; i <= pev->body; ++i)
    {
        RevolverEvent event{RevolverEffect::EjectEvent};
        event.a = static_cast<float>(double(i)*.05 + 1.2);
        event.value = action_.clip; event.first = m_pPlayer->pev->origin;
        if (!Emit(event)) return;
    }
    action_.clip = 0; WriteAmmo();
    if (!View(4) || !ReloadAnimation(0)) return;
    ScheduleCylinder(CylinderThink::Reset, .2f);
    cylinder_.loading = true;
    status_ = RevolverStatus::Applied;
}
void RevolverWeapon::SecondaryAttack()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || action_.secondary > now || action_.action || action_.aiming)
    { status_ = RevolverStatus::Ineligible; return; }
    Lease l = lease_; l.active = true;
    if (CheckPistolWeaponMelee(*m_pPlayer, {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer},
        prepared_.melee, {Current, &l}) != MeleeStatus::Ready)
    { status_ = RevolverStatus::MissingBackend; return; }
    meleePending_ = true; meleeDeadline_ = now + .3f; ScheduleThink();
    if (!Animate(PlayerAction::WeaponMelee) || !View(20)) return;
    action_.primary = action_.secondary = action_.idle = now + 1.0f;
    MirrorTimers(true); status_ = RevolverStatus::Applied;
}
void RevolverWeapon::DelayedMelee()
{
    RevolverOwnerFrame frame{}; if (!Check(frame) || !meleePending_) return;
    if (gpGlobals->time < meleeDeadline_) { status_ = RevolverStatus::Ineligible; return; }
    meleePending_ = false; ScheduleThink();
    Lease l = lease_; l.active = true;
    const auto result = PistolWeaponMelee(*m_pPlayer,
        {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer}, prepared_.melee, {Current, &l});
    if (!Current(&l)) return;
    status_ = result == MeleeStatus::Hit || result == MeleeStatus::Miss ? RevolverStatus::Applied :
        result == MeleeStatus::Cancelled ? RevolverStatus::Cancelled : RevolverStatus::MissingBackend;
}

bool RevolverWeapon::SetAim(bool enabled) noexcept
{
    action_.aiming = enabled;
    RevolverEvent e{RevolverEffect::Aim}; e.enabled = enabled;
    e.first = enabled ? prepared_.aimOffset : Vector{};
    e.value = enabled ? prepared_.aimZoom : 0; e.a = 350;
    if (!Emit(e)) return false;
    m_pPlayer->pev->fov = enabled ? float(prepared_.aimZoom) : 90.0f;
    if (!View(enabled ? 15 : 17)) return false;
    action_.primary = action_.secondary = action_.extra = action_.idle = AddDouble(gpGlobals->time, .3);
    MirrorTimers(true);
    return true;
}

void RevolverWeapon::PrimaryAttack()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || cylinder_.loading || !(frame.pressed & 1u) || action_.action)
    { status_ = RevolverStatus::Ineligible; return; }
    if (m_pPlayer->pev->waterlevel == 3)
    {
        if (now > emptyDeadline_)
        {
            if (!Emit({RevolverEffect::EmptyClick})) return;
            emptyDeadline_ = now + 1.0f;
        }
        action_.primary = action_.secondary = now + 1.0f;
        if (!View(action_.aiming ? 19 : 11)) return;
        MirrorTimers(true); status_ = RevolverStatus::Applied; return;
    }
    if (action_.clip <= 0)
    {
        if (!m_fFireOnEmpty)
        {
            Reload();
            if (status_ != RevolverStatus::Applied && status_ != RevolverStatus::Ineligible) return;
        }
        else
        {
            if (!View(action_.aiming ? 19 : 11)) return;
            if (now > emptyDeadline_)
            {
                if (!Emit({RevolverEffect::EmptyClick})) return;
                emptyDeadline_ = now + 1.0f;
            }
        }
        Lease l = lease_; l.active = true;
        if (!Current(&l)) return;
        action_.primary = action_.secondary = now + 1.0f;
        MirrorTimers(true); status_ = RevolverStatus::Applied; return;
    }
    PistolBallistics shot{{}, {1,0,0}, {spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0}, {0,0,1}, frame.randomSeed, frame.bulletDamage, action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers, frame.sourceMode};
    Lease l = lease_; l.active = true;
    if (CheckPistolBallistics(shot, prepared_.ballistics, {Current, &l}) != BallisticsStatus::Ready)
    { status_ = RevolverStatus::MissingBackend; return; }
    pev->startpos.x = 0;
    RevolverEvent smoke{RevolverEffect::Gunsmoke};
    smoke.value = g_engfuncs.pfnIndexOfEdict(l.weapon);
    if (!Current(&l)) return;
    smoke.enabled = true; smoke.modelString = prepared_.viewModelString;
    if (!Emit(smoke)) return;
    if (!frame.infiniteAmmo)
    {
        --action_.clip;
        cylinder_.targetAngle = action_.clip * 72;
    }
    WriteAmmo();
    // Original direct event: no Glock gun-position/right-vector payload.
    RevolverEvent fire{RevolverEffect::FireEvent}; fire.first = m_pPlayer->pev->origin;
    fire.value = OriginalId;
    if (!Emit(fire)) return;
    const int recoil = g_engfuncs.pfnRandomLong(-2,2);
    if (!Current(&l)) return;
    RevolverEvent kick{RevolverEffect::Recoil}; kick.first = {float(recoil),1,1};
    if (!Emit(kick)) return;
    RevolverEvent shake{RevolverEffect::Shake};
    const bool crouch = (frame.originalFlags & 0x4000u) != 0;
    shake.a = crouch ? 2.0f : 5.0f; shake.b = crouch ? .1f : .25f;
    shake.c = crouch ? .1f : .15f; shake.value = 2;
    if (!Emit(shake) || !View(action_.aiming ? 18 : 3) ||
        !Animate(PlayerAction::Fire) || !Animate(PlayerAction::Move)) return;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    const Vector angles = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
    g_engfuncs.pfnMakeVectors(angles);
    if (!Current(&l)) return;
    shot.source = m_pPlayer->GetGunPosition();
    if (!Current(&l)) return;
    // Original100DFEA0 only copies the global forward vector; no autoaim query.
    shot.direction = gpGlobals->v_forward; shot.right = gpGlobals->v_right; shot.up = gpGlobals->v_up;
    spread_.scalar += 3.15f; spread_.recoveryDeadline = AddDouble(now, .35);
    const auto result = FirePistolBallistics(*m_pPlayer, shot, prepared_.ballistics, {Current, &l});
    if (!Current(&l)) return;
    if (result.status != BallisticsStatus::Fired) { status_ = RevolverStatus::Cancelled; return; }
    action_.idle = now + 1.0f;
    RevolverEvent shotState{RevolverEffect::ShotState}; shotState.a = 4;
    RevolverEvent sound{RevolverEffect::FireSound}; sound.a = 0; sound.b = 1; sound.enabled = true;
    if (!Emit(shotState) || !Emit(sound)) return;
    RevolverEvent latch{RevolverEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(now,.2);
    if (!Emit(latch)) return;
    action_.primary = action_.secondary = AddDouble(now,.65);
    if (action_.clip <= 0) action_.primary = now + 1.0f;
    MirrorTimers(true); status_ = RevolverStatus::Applied;
}


void RevolverWeapon::AdvanceOwnerFrame() noexcept
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (now > frame.cylinderGate)
    {
        float angle = frame.cylinderAngle;
        if (angle != float(cylinder_.targetAngle))
        {
            angle -= 12.0f;
            if (angle < 0) angle += 360.0f;
            if (angle > 360) angle -= 360.0f;
        }
        RevolverEvent event{RevolverEffect::Cylinder};
        event.a = angle; event.b = AddDouble(now,.05);
        event.value = cylinder_.targetAngle; event.enabled = true;
        if (!Emit(event)) return;
    }
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        now, frame.originalFlags, OriginalId, true});
}

void RevolverWeapon::ItemPostFrame()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    Lease l = lease_; l.active = true;
    const float now = gpGlobals->time;
    if (now < action_.ownerAttack) { status_ = RevolverStatus::Ineligible; return; }
    if (cylinder_.loading && !cylinder_.cancel && (frame.pressed & 1u)) cylinder_.cancel = true;
    if (!action_.action && now > action_.primary && now > action_.secondary && now > action_.extra &&
        !frame.toggleAim && action_.aiming &&
        !((m_pPlayer->pev->button | frame.pressed | frame.released) & 0x40u))
        if (!SetAim(false)) return;
    if (action_.pendingReload && !frame.pendingGate && frame.pendingGateTime <= now &&
        action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action)
    {
        if (action_.aiming) { if (!SetAim(false)) return; }
        else { action_.pendingReload = false; Reload(); if (!Current(&l)) return; }
    }
    const float speedSquared = m_pPlayer->pev->velocity.x*m_pPlayer->pev->velocity.x +
                               m_pPlayer->pev->velocity.y*m_pPlayer->pev->velocity.y;
    const float speed = static_cast<float>(std::sqrt(double(speedSquared)));
    if (!action_.loading && action_.action != 2 && now > action_.primary)
    {
        if (frame.condition3AA && !action_.aiming && now > action_.extra)
        {
            if (!action_.action)
            {
                action_.action = 5;
                if (!View(12)) return;
                const float duration = prepared_.sequences.duration[12];
                action_.primary = static_cast<float>(double(now) + double(duration)*.5);
                actionDeadline_ = now + duration;
            }
        }
        else if (action_.action == 5)
        {
            // Original100647D0 queries the OLD current view before sending14.
            const float duration = prepared_.sequences.duration[m_pPlayer->pev->weaponanim];
            if (!View(14)) return;
            action_.primary = action_.idle = now + duration; action_.action = 0;
        }
        const bool sprint = (frame.originalFlags & 0x40000u) && speed > 0;
        if (sprint && action_.action != 5 && action_.action != 1)
        {
            action_.action = 1; if (!View(12)) return;
            const float duration = prepared_.sequences.duration[12];
            action_.primary = now + duration; actionDeadline_ = now + duration;
        }
        if (sprint && action_.action == 1 && now > actionDeadline_)
        {
            if (!View(13)) return;
            actionDeadline_ = now + prepared_.sequences.duration[13];
        }
        if (!sprint && action_.action == 1 && now > actionDeadline_)
        {
            if (!View(14)) return;
            action_.primary = action_.idle = now + prepared_.sequences.duration[14]; action_.action = 0;
        }
    }
    if (cylinder_.pending && now > cylinder_.commitDeadline)
    {
        cylinder_.pending = false;
        if (action_.reserve != 0 && action_.clip < 5)
        {
            ++action_.clip; --action_.reserve; WriteAmmo();
            if (!Emit({RevolverEffect::InventoryChanged})) return;
            RevolverEvent event{RevolverEffect::AmmoX}; event.value = m_iPrimaryAmmoType;
            event.other = action_.reserve < 0 ? 0 : action_.reserve > 254 ? 254 : action_.reserve;
            if (!Emit(event)) return;
        }
    }
    if (action_.clip > 0) m_iPlayEmptySound = 1;
    // Original Revolver Fidget1001E060 is retn0; it consumes no RNG.
    if (now > wearDeadline_) { wearDeadline_ = AddDouble(now,.75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_;
    if (reserveHud_ != action_.reserve)
    {
        RevolverEvent e{RevolverEffect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return; reserveHud_ = action_.reserve;
    }
    const unsigned buttons = m_pPlayer->pev->button;
    const bool allowed = frame.activityBlock <= 0 && m_pPlayer->pev->movetype != 5;
    if ((buttons & 0x800u) && frame.activityBlock <= 0 && action_.secondary <= now)
    {
        if (allowed) { if (!Emit({RevolverEffect::InventoryChanged})) return; SecondaryAttack(); }
    }
    else if ((buttons & 0x40u) && allowed && action_.ownerAttack <= now)
    {
        if (!Emit({RevolverEffect::InventoryChanged})) return;
        if (action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action &&
            !cylinder_.loading && (frame.pressed & 0x40u) && (frame.toggleAim || !action_.aiming))
            if (!SetAim(!action_.aiming)) return;
    }
    else if ((buttons & 1u) && frame.activityBlock <= 0 && action_.primary <= now)
    {
        if (allowed)
        {
            if (!action_.clip) m_fFireOnEmpty = 1;
            if (!Emit({RevolverEffect::InventoryChanged})) return;
            PrimaryAttack();
        }
    }
    else if ((buttons & 0x2000u) && !action_.loading)
    { if (m_pPlayer->pev->movetype != 5) Reload(); }
    else if (!(buttons & 0x841u)) m_fFireOnEmpty = 0;
    if (!Current(&l)) return;
    WriteAmmo(); MirrorTimers(true);
}
void RevolverWeapon::WeaponIdle()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.idle > now || action_.action)
    { status_ = RevolverStatus::Ineligible; return; }
    const int sequence = action_.aiming ? 16 : 0;
    if (!View(sequence)) return;
    action_.idle = now + prepared_.sequences.duration[sequence];
    MirrorTimers(true); status_ = RevolverStatus::Applied;
}
void RevolverWeapon::Invalidate() noexcept
{
    bound_ = selected_ = meleePending_ = false;
    cylinderThink_ = CylinderThink::None;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = RevolverStatus::Cancelled;
}
bool RevolverWeapon::Body(int value) noexcept
{
    pev->body = value;
    RevolverEvent event{RevolverEffect::Body}; event.value = value;
    return Emit(event);
}
bool RevolverWeapon::ReloadAnimation(int stage) noexcept
{
    if (!Animate(PlayerAction::Reload)) return false;
    RevolverEvent event{RevolverEffect::ReloadStage}; event.value = stage;
    if (stage == 0) event.a = gpGlobals->time + 2.0f;
    else if (stage == 1) event.a = gpGlobals->time + 1.0f;
    return Emit(event);
}
void RevolverWeapon::ScheduleThink() noexcept
{
    const bool cylinder = cylinderThink_ != CylinderThink::None;
    if (!cylinder && !meleePending_)
    { SetThink(nullptr); pev->nextthink = 0; return; }
    SetThink(&RevolverWeapon::DispatchThink);
    pev->nextthink = cylinder && (!meleePending_ || cylinderDeadline_ <= meleeDeadline_) ?
        cylinderDeadline_ : meleeDeadline_;
}
void RevolverWeapon::ScheduleCylinder(CylinderThink kind, float delay) noexcept
{
    cylinderThink_ = kind;
    cylinderDeadline_ = gpGlobals->time + delay;
    ScheduleThink();
}
void RevolverWeapon::RunCylinder()
{
    const auto kind = cylinderThink_;
    cylinderThink_ = CylinderThink::None;
    switch (kind)
    {
    case CylinderThink::Reset: ResetCylinder(); break;
    case CylinderThink::Insert: ReloadInsert(); break;
    case CylinderThink::Set: SetCylinder(); break;
    case CylinderThink::None: break;
    }
}
void RevolverWeapon::DispatchThink()
{
    RevolverOwnerFrame frame{};
    if (!Check(frame)) { Invalidate(); return; }
    Lease l = lease_; l.active = true;
    // Original owner player think precedes selected-weapon ItemPostFrame.
    if (meleePending_ && gpGlobals->time >= meleeDeadline_) DelayedMelee();
    if (!Current(&l)) return;
    if (cylinderThink_ != CylinderThink::None && gpGlobals->time >= cylinderDeadline_) RunCylinder();
    if (!Current(&l)) return;
    ScheduleThink();
}
void RevolverWeapon::ResetCylinder()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    if (!frame.infiniteAmmo)
    {
        cylinder_.targetAngle = 0;
        RevolverEvent event{RevolverEffect::Cylinder}; event.value = 0;
        if (!Emit(event)) return;
    }
    ScheduleCylinder(CylinderThink::Insert, 1.3f);
    status_ = RevolverStatus::Applied;
}
void RevolverWeapon::ReloadInsert()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.reserve <= 0 || (cylinder_.cancel && action_.clip > 0))
    {
        cylinder_.loading = cylinder_.cancel = false; action_.action = 0;
        if (!View(10) || !ReloadAnimation(2)) return;
        ScheduleCylinder(CylinderThink::Set, .3f);
        action_.primary = action_.secondary = action_.extra = action_.idle = now + 1.0f;
    }
    else
    {
        if (action_.clip < 5)
        {
            cylinder_.pending = true; cylinder_.commitDeadline = AddDouble(now,.5);
            if (!Body(action_.clip + 1) || !View(5 + action_.clip)) return;
        }
        if (!ReloadAnimation(1)) return;
        action_.primary = action_.secondary = action_.extra = action_.idle = AddDouble(now,.83);
        if (action_.clip >= 5)
        {
            cylinder_.loading = cylinder_.cancel = false; action_.action = 0;
            if (!View(10) || !ReloadAnimation(2)) return;
            cylinderThink_ = CylinderThink::None;
            action_.primary = action_.secondary = action_.extra = action_.idle = AddDouble(now,.83);
            ScheduleThink();
        }
        else ScheduleCylinder(CylinderThink::Insert, .83f);
    }
    MirrorTimers(true); status_ = RevolverStatus::Applied;
}
void RevolverWeapon::SetCylinder()
{
    RevolverOwnerFrame frame{}; if (!Check(frame)) return;
    if (!frame.infiniteAmmo)
    {
        cylinder_.targetAngle = action_.clip * 72;
        RevolverEvent event{RevolverEffect::Cylinder};
        event.value = cylinder_.targetAngle; event.a = float(cylinder_.targetAngle);
        if (!Emit(event)) return;
    }
    status_ = RevolverStatus::Applied;
}

}
