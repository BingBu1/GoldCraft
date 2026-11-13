// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "glock_weapon.hpp"
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
using csol::scenario::custom::ActionResumeResult;
using csol::scenario::custom::ActionResumeToken;
namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
constexpr unsigned OriginalId = 3;
constexpr std::uint32_t EffectBit(GlockEffect effect) noexcept
{ return 1u << static_cast<unsigned>(effect); }
template<class... T> constexpr std::uint32_t Effects(T... effects) noexcept
{ return (EffectBit(effects) | ...); }
}

goldcraft::scenario::GuestProfile GlockWeapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {Pistols[0].hostId,GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior,GuestKill::StandardPlayerProcessing};
}
struct GlockWeapon::InventoryState
{
    MagazineActionState action;
    SpreadState spread;
    SpreadTransitions transitions;
    float actionDeadline{},emptyDeadline{},wearDeadline{},nextThink{};
    float primary{},secondary{},idle{},ownerAttack{};
    int reserveHud{},wearMaximum{},wear{},clip{},inReload{},fireOnEmpty{},ownerSerial{},weaponSerial{},modelIndex{},ownerSpawn{},viewSequence{};
    std::uintptr_t map{},ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    unsigned lightThink{};
    bool selected{},meleePending{},taclightPending{},taclightInstalled{};
    GlockStatus status{};
};
bool GlockWeapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease=lease_;
    if(!bound_||!pev||m_pPlayer!=lease.player||!Current(const_cast<Lease*>(&lease)))return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState)<=512);
    InventoryState saved{};
    saved.action=action_;saved.spread=spread_;saved.transitions=transitions_;
    saved.actionDeadline=actionDeadline_;saved.emptyDeadline=emptyDeadline_;saved.wearDeadline=wearDeadline_;
    saved.nextThink=pev->nextthink;saved.think=m_pfnThink;saved.lightThink=lightThink_;
    saved.primary=m_flNextPrimaryAttack;saved.secondary=m_flNextSecondaryAttack;saved.idle=m_flTimeWeaponIdle;
    saved.ownerAttack=m_pPlayer->m_flNextAttack;saved.clip=m_iClip;
    saved.inReload=m_fInReload;saved.fireOnEmpty=m_fFireOnEmpty;
    saved.reserveHud=reserveHud_;saved.wearMaximum=wearMaximum_;saved.wear=wear_;
    saved.ownerSerial=lease.ownerSerial;saved.weaponSerial=lease.weaponSerial;saved.modelIndex=lease.modelIndex;
    saved.ownerSpawn=m_pPlayer->m_iNumSpawns;
    saved.viewSequence=viewSequence_;
    saved.map=reinterpret_cast<std::uintptr_t>(worldLease_.context);
    saved.ownerContext=reinterpret_cast<std::uintptr_t>(lease.context);
    saved.selected=selected_;saved.meleePending=meleePending_;
    saved.taclightPending=taclightAttachPending_;saved.taclightInstalled=taclightInstalled_;saved.status=status_;
    output.kind=0x434f46474c4f434bULL;output.bytes=sizeof(saved);
    std::memcpy(output.data.data(),&saved,sizeof(saved));return true;
}
bool GlockWeapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
{
    if(overlay_.token.generation||!pev||m_pPlayer!=&player)return false;
    const auto previous=lease_;
    if(bound_&&Current(const_cast<Lease*>(&previous))&&pev->owner==player.edict())return true;
    const auto world=worldLease_;const auto cold=cold_;const auto prepared=prepared_;
    if(!WorldCurrent(world)||!cold.ownerBackends)return false;
    const EHANDLE owner(&player);GlockBackends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found)return false;
    if(Bind(player,prepared,backend)!=GlockStatus::Ready)return false;
    const auto lease=lease_;
    AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    return !selected_||PublishOwnerAim();
}
bool GlockWeapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
{
    const auto lease=lease_;
    if(overlay_.token.generation||input.kind!=0x434f46474c4f434bULL||input.bytes!=sizeof(InventoryState)||!bound_||
       !Current(const_cast<Lease*>(&lease))||m_pPlayer!=lease.player)return false;
    InventoryState saved{};std::memcpy(&saved,input.data.data(),sizeof(saved));
    if(saved.ownerSerial!=lease.ownerSerial||saved.weaponSerial!=lease.weaponSerial||saved.modelIndex!=lease.modelIndex||
       saved.ownerSpawn!=m_pPlayer->m_iNumSpawns||
       saved.map!=reinterpret_cast<std::uintptr_t>(worldLease_.context)||
       saved.ownerContext!=reinterpret_cast<std::uintptr_t>(lease.context)||
       (saved.selected && m_pPlayer->m_pActiveItem!=this))return false;
    action_=saved.action;spread_=saved.spread;transitions_=saved.transitions;
    actionDeadline_=saved.actionDeadline;emptyDeadline_=saved.emptyDeadline;wearDeadline_=saved.wearDeadline;
    m_pfnThink=saved.think;pev->nextthink=saved.nextThink;lightThink_=saved.lightThink;
    m_flNextPrimaryAttack=saved.primary;m_flNextSecondaryAttack=saved.secondary;m_flTimeWeaponIdle=saved.idle;
    if(saved.selected)m_pPlayer->m_flNextAttack=saved.ownerAttack;
    m_iClip=saved.clip;m_fInReload=saved.inReload;m_fFireOnEmpty=saved.fireOnEmpty;
    reserveHud_=saved.reserveHud;wearMaximum_=saved.wearMaximum;wear_=saved.wear;
    selected_=saved.selected;meleePending_=saved.meleePending;taclightAttachPending_=saved.taclightPending;
    taclightInstalled_=saved.taclightInstalled;status_=saved.status;
    viewSequence_=saved.viewSequence;
    return !selected_||PublishOwnerAim();
}
bool GlockWeapon::PublishOwnerAim() noexcept
{
    auto lease=lease_;lease.active=true;
    if(!selected_||!Current(&lease)||!prepared_.aimMetadataReady||!RequireEffects(EffectBit(GlockEffect::Aim)))return false;
    // Host ownership handoff only: resend the existing proved state without
    // invoking an aim transition, animation or new action deadline.
    GlockEvent event{GlockEffect::Aim};event.enabled=action_.aiming;
    event.first=action_.aiming?prepared_.aimOffset:Vector{};
    event.value=action_.aiming?prepared_.aimZoom:0;event.a=350;
    if(!Emit(event))return false;
    if (!overlay_.token.generation || overlay_.resuming)
        lease.player->pev->fov=action_.aiming?float(prepared_.aimZoom):90.0F;
    return true;
}

bool GlockWeapon::OverlayCurrent() const noexcept
{
    auto lease = overlay_.lease;
    return overlay_.token.generation && Current(&lease) &&
        lease.player->m_iNumSpawns == overlay_.spawn && lease_.context == lease.context &&
        lease_.owner == lease.owner && lease_.weapon == lease.weapon;
}
ActionResumeResult GlockWeapon::SuspendWeaponAction(ActionResumeToken token) noexcept
{
    if (!token.generation || overlay_.token.generation || !bound_ || viewSequence_ < 0)
        return ActionResumeResult::Rejected;
    auto lease = lease_; lease.active = true;
    if (!Current(&lease)) return ActionResumeResult::Cancelled;
    // Reserve before the authenticated owner-frame callback: synchronous reentry
    // cannot acquire a second overlay over this same weapon action.
    overlay_ = {token, lease, lease.player->m_iNumSpawns, false};
    GlockOwnerFrame frame{};
    if (!Check(frame) || !prepared_.aimMetadataReady ||
        !RequireEffects(Effects(GlockEffect::Aim, GlockEffect::ViewAnimation)) ||
        !RequireSequence(viewSequence_) || !OverlayCurrent() || overlay_.token != token)
    {
        if (overlay_.token == token) overlay_ = {};
        return ActionResumeResult::Rejected;
    }
    return ActionResumeResult::Applied;
}
void GlockWeapon::CancelWeaponAction(ActionResumeToken token) noexcept
{
    if (!token.generation || overlay_.token != token) return;
    overlay_ = {};
    // Retirement is deliberately different from undoing failed overlay
    // acquisition. The caller uses Resume for that non-destructive undo.
    meleePending_ = false; lightThink_ = 0;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
}
ActionResumeResult GlockWeapon::ResumeWeaponAction(ActionResumeToken token) noexcept
{
    if (!token.generation || overlay_.token != token || overlay_.resuming)
        return ActionResumeResult::Rejected;
    if (!OverlayCurrent())
    { CancelWeaponAction(token); return ActionResumeResult::Cancelled; }
    overlay_.resuming = true;
    GlockOwnerFrame frame{};
    const auto current = [&]() noexcept { return overlay_.token == token && OverlayCurrent(); };
    const auto rejected = [&]() noexcept
    {
        if (overlay_.token == token) overlay_.resuming = false;
        return ActionResumeResult::Rejected;
    };
    if (!Check(frame) || !current()) return rejected();
    if (action_.loading)
    {
        if (!RequireEffects(EffectBit(GlockEffect::InventoryChanged))) return rejected();
        MagazineReload reload{action_.clip, action_.reserve, action_.ownerAttack, true};
        ActionBridge bridge{this, lease_}; bridge.lease.active = true;
        const auto result = CommitMagazineReload(reload, 15, 0, gpGlobals->time, ReloadCommitted, &bridge);
        if (!Current(&bridge.lease)) return ActionResumeResult::Cancelled;
        // The transfer already happened before the observer. Retain its consumed
        // loading bit even if transport rejects; retry may never duplicate ammo.
        action_.clip = reload.clip; action_.reserve = reload.reserve; action_.loading = reload.loading;
        WriteAmmo();
        if (result.status == ReloadCommitStatus::Committed)
        {
            if (action_.action == 2) action_.action = 0;
            viewSequence_ = (action_.aiming ? 32 : 0) + (action_.clip == 0);
        }
        if (!current() || bridge.callbackFailed || result.status == ReloadCommitStatus::Invalid)
            return rejected();
    }
    if (!UpdateBody(frame.lightOn) || !current() || !PublishOwnerAim() || !current()) return rejected();
    const int sequence = viewSequence_;
    if (!RequireSequence(sequence)) return rejected();
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "onehanded", sizeof("onehanded"));
    if (!View(sequence) || !current()) return rejected();
    // Overlay still owns the player gate. Mirror only the live absolute weapon
    // clocks; the generic controller releases its own lock after return.
    MirrorTimers(true);
    overlay_ = {};
    status_ = GlockStatus::Applied;
    return ActionResumeResult::Applied;
}

bool GlockWeapon::Current(void* context) noexcept
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

bool GlockWeapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool GlockWeapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void GlockWeapon::ConfigureColdHost(GlockColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
GlockStatus GlockWeapon::Prepare(const GlockPrepared& p) noexcept
{
    preparedReady_ = false;
    status_ = GlockStatus::MissingBackend;
    // World/inventory identity is unconditional. Combat, aim and attachment
    // resources are admitted by the operation that actually consumes them.
    if (!p.modelsReady || !p.inventoryReady || p.world != MeleeWorld::Multiplayer ||
        !p.worldModelString || !p.inventoryItemString || !p.viewModelString || !p.playerModelString ||
        !g_engfuncs.pfnSetModel)
        return status_;
    if ((p.aimMetadataReady && (!Finite(p.aimOffset) || p.aimZoom < 0 || p.aimZoom > 255)) ||
        p.hostSlot != GlockHudSlot || p.hostPosition != GlockHudPosition)
        return status_ = GlockStatus::InvalidInput;
    for (int i = 0; i <= 42; ++i)
        if ((p.sequences.present & (std::uint64_t{1} << i)) &&
            (!std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0))
            return status_ = GlockStatus::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = GlockStatus::Ready;
}
void GlockWeapon::Precache()
{
    preparedReady_ = false;
    status_ = GlockStatus::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    GlockPrepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

GlockStatus GlockWeapon::Bind(CBasePlayer& owner, const GlockPrepared& p,
                          const GlockBackends& backends) noexcept
{
    if (overlay_.token.generation) CancelWeaponAction(overlay_.token);
    bound_ = selected_ = meleePending_ = false;
    status_ = GlockStatus::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect)
        return status_;
    if (Prepare(p) != GlockStatus::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = GlockStatus::InvalidInput;
    status_ = GlockStatus::MissingBackend;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = GlockStatus::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias Glock to an unrelated stock CS weapon.
    m_iId = FindPistol(Pistol::Glock)->hostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = GlockStatus::Ready;
}

void GlockWeapon::Spawn()
{
    overlay_ = {}; viewSequence_ = -1;
    action_ = {};
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 15;
    m_iSecondaryAmmoType = -1;
    m_iClip = 0;
    m_fInReload = m_fFireOnEmpty = 0;
    bound_ = selected_ = meleePending_ = false;
    taclightAttachPending_ = taclightInstalled_ = false; lightThink_ = 0;
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
    { status_ = GlockStatus::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = GlockStatus::InvalidInput; return; }
    m_iId = FindPistol(Pistol::Glock)->hostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 15;
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
    SetUse(&GlockWeapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&GlockWeapon::WorldTouch);
    SetThink(&GlockWeapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = GlockStatus::Applied;
}

int GlockWeapon::GetItemInfo(ItemInfo* info)
{
    // Original 10045AC0 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_glock";
    info->pszAmmo1 = "cof_glock";
    info->iMaxAmmo1 = 150;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 15;
    info->iId = m_iId = FindPistol(Pistol::Glock)->hostId;
    info->iSlot = GlockHudSlot;
    info->iPosition = GlockHudPosition;
    info->iWeight = 5;
    return 1;
}

int GlockWeapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void GlockWeapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void GlockWeapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&GlockWeapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&GlockWeapon::WorldTouch);
}
void GlockWeapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void GlockWeapon::WorldTouch(CBaseEntity* other)
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
    GlockBackends pickupBackends{};
    GlockOwnerFrame pickupFrame{};
    if (!host.ownerBackends || !host.ownerBackends(host.context, *player, pickupBackends) || !current() ||
        !pickupBackends.current || !pickupBackends.current(pickupBackends.context) || !current() ||
        !pickupBackends.ownerFrame || !pickupBackends.effect ||
        !pickupBackends.ownerFrame(pickupBackends.context, pickupFrame) || !current() ||
        !pickupBackends.current(pickupBackends.context))
    { status_ = GlockStatus::MissingBackend; return; }
    const bool alreadyOwned = player->HasPlayerItem(this);
    if (!current()) return;
    if ((!alreadyOwned && (!(pickupBackends.preparedEffects & EffectBit(GlockEffect::PickupSound)) ||
            (!pickupFrame.quietAudio && pev->armorvalue != 1 && !prepared_.pickupSoundReady))) ||
        (alreadyOwned && pev->armorvalue != 1 && (!prepared_.pickupSoundReady ||
            !(pickupBackends.preparedEffects & EffectBit(GlockEffect::AmmoPickupSound)))))
    { status_ = GlockStatus::MissingBackend; return; }
    if (pev->iuser4 == 1) pev->iuser4 = 0;
    const bool inventory = host.inventoryPickup(host.context, *player, *this, prepared_.inventoryItemString);
    if (!current()) return;
    if (!inventory) { status_ = GlockStatus::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = GlockStatus::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        GlockEvent event{GlockEffect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int GlockWeapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    GlockBackends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != GlockStatus::Ready) return FALSE;
    GlockOwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool alreadyOwned = player->HasPlayerItem(this);
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (alreadyOwned && pev->armorvalue != 1 &&
        (!prepared_.pickupSoundReady || !RequireEffects(EffectBit(GlockEffect::AmmoPickupSound))))
    { status_ = GlockStatus::MissingBackend; return FALSE; }
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = GlockStatus::Ineligible; return FALSE; }
    return TRUE;
}
int GlockWeapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == GlockStatus::Ready || status_ == GlockStatus::Applied);
}
int GlockWeapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int GlockWeapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<GlockWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    GlockOwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = GlockStatus::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1 &&
        (!target->prepared_.pickupSoundReady || !target->RequireEffects(EffectBit(GlockEffect::AmmoPickupSound))))
    { status_ = GlockStatus::MissingBackend; return FALSE; }
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 15 ? amount : 15;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, "cof_glock", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({GlockEffect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int GlockWeapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<GlockWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    GlockOwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_glock", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void GlockWeapon::AttachToPlayer(CBasePlayer* player)
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
void GlockWeapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void GlockWeapon::Drop() { RetireWorld(false); }
void GlockWeapon::Kill() { RetireWorld(true); }
void GlockWeapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = GlockStatus::UnsupportedState;
}

bool GlockWeapon::Check(GlockOwnerFrame& frame, bool active) noexcept
{
    if (overlay_.token.generation && !OverlayCurrent())
    { CancelWeaponAction(overlay_.token); status_ = GlockStatus::Cancelled; return false; }
    if (!bound_) { status_ = GlockStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = GlockStatus::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = GlockStatus::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || frame.meleeDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 45 && frame.reserveLimit != 150) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = GlockStatus::InvalidInput; return false; }
    ReadAmmo();
    // Native CS owns both counts. Scenario skills can overfill the physical
    // magazine; that trusted state remains usable after the buff expires.
    if ((action_.clip < 0 && (active || action_.clip != -666)) ||
        action_.reserve < 0)
    { status_ = GlockStatus::InvalidInput; return false; }
    status_ = GlockStatus::Ready;
    return true;
}

bool GlockWeapon::RequireEffects(std::uint32_t effects) noexcept
{
    if ((backends_.preparedEffects & effects) == effects) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::RequireSequence(int sequence) noexcept
{
    if (RequireEffects(EffectBit(GlockEffect::ViewAnimation)) && sequence >= 0 && sequence <= 42 &&
        (prepared_.sequences.present & (std::uint64_t{1} << sequence))) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::RequireAnimation(PlayerAction action) noexcept
{
    if (backends_.playerAnimation && backends_.playerAnimation->Select(backends_.playerModelGeneration,
        Pistol::Glock, action, (m_pPlayer->pev->flags & FL_DUCKING) != 0)) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::ReloadReady(bool resumePending) noexcept
{
    const float now = gpGlobals->time;
    if (action_.reserve <= 0 || action_.primary > now || action_.action == 1 ||
        action_.action > 2 || action_.special || (action_.pendingReload && !resumePending) || action_.aiming || action_.clip == 15)
        return true;
    return RequireSequence(action_.clip ? 4 : 5) && RequireAnimation(PlayerAction::Reload) &&
        RequireEffects(EffectBit(GlockEffect::InventoryChanged));
}
bool GlockWeapon::MeleeReady(const GlockOwnerFrame& frame) noexcept
{
    const float now = gpGlobals->time;
    if (action_.primary > now || action_.secondary > now || action_.action || action_.special || action_.aiming)
        return true;
    if (!RequireSequence(action_.clip ? 27 : 28) || !RequireAnimation(PlayerAction::WeaponMelee)) return false;
    if (!prepared_.melee.soundsReady || !ValidPistolMeleeSamples(prepared_.melee.samples))
    { status_ = GlockStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = true;
    if (CheckPistolWeaponMelee(*m_pPlayer, {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer},
        prepared_.melee, {Current, &l}) == MeleeStatus::Ready) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::PrimaryReady(const GlockOwnerFrame& frame, bool forceEmpty) noexcept
{
    const float now = gpGlobals->time;
    if (action_.primary > now || !(frame.pressed & 1u) || action_.action) return true;
    if (m_pPlayer->pev->waterlevel == 3 || action_.clip <= 0)
    {
        if (m_pPlayer->pev->waterlevel != 3 && !m_fFireOnEmpty && !forceEmpty) return ReloadReady();
        if (!RequireSequence(action_.aiming ? 35 : 20)) return false;
        if (now <= emptyDeadline_) return true;
        if (prepared_.soundsReady && RequireEffects(EffectBit(GlockEffect::EmptyClick))) return true;
        status_ = GlockStatus::MissingBackend;
        return false;
    }
    if (!prepared_.particleReady || !prepared_.soundsReady || prepared_.shellModel <= 0 ||
        !g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnIndexOfEdict)
    { status_ = GlockStatus::MissingBackend; return false; }
    if (!RequireEffects(Effects(GlockEffect::Gunsmoke, GlockEffect::FireEvent, GlockEffect::Recoil,
        GlockEffect::Shake, GlockEffect::ShotState, GlockEffect::FireSound, GlockEffect::ShotLatch)) ||
        !RequireSequence(action_.aiming ? ((frame.infiniteAmmo || action_.clip > 1) ? 34 : 38) :
            ((frame.infiniteAmmo || action_.clip > 1) ? 2 : 3)) ||
        !RequireAnimation(PlayerAction::Fire) || !RequireAnimation(PlayerAction::Move)) return false;
    const PistolBallistics shot{{}, {1,0,0}, {spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0}, {0,0,1}, frame.randomSeed, frame.bulletDamage, action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers, frame.sourceMode};
    Lease l = lease_; l.active = true;
    if (CheckPistolBallistics(shot, prepared_.ballistics, {Current, &l}) == BallisticsStatus::Ready) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::AttachmentReady() noexcept
{
    if (!prepared_.taclightSoundsReady ||
        !RequireEffects(Effects(GlockEffect::AttachmentSound, GlockEffect::Body, GlockEffect::Tutorial)) ||
        !RequireSequence(action_.clip ? 8 : 9) || !RequireSequence(action_.clip ? 6 : 7))
    { status_ = GlockStatus::MissingBackend; return false; }
    return true;
}

bool GlockWeapon::Emit(GlockEvent event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l) || !RequireEffects(EffectBit(event.kind))) return false;
    // Custom overlay owns presentation, not the weapon's action state. Original
    // Think callbacks may progress while their gun view/body/aim is obscured.
    if (overlay_.token.generation && !overlay_.resuming &&
        (event.kind == GlockEffect::ViewAnimation || event.kind == GlockEffect::Aim ||
         event.kind == GlockEffect::Body)) return OverlayCurrent();
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = GlockStatus::MissingBackend;
    return delivered;
}
bool GlockWeapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l) || !RequireAnimation(action)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, Pistol::Glock, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
bool GlockWeapon::View(int sequence) noexcept
{
    if (!RequireSequence(sequence)) return false;
    GlockEvent event{GlockEffect::ViewAnimation}; event.value = sequence;
    if (!Emit(event)) return false;
    viewSequence_ = sequence;
    return true;
}
void GlockWeapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void GlockWeapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
void GlockWeapon::MirrorTimers(bool selected) noexcept
{
    // UseDecrement=false keeps weapon clocks absolute. ReGameDLL's player gate
    // is nevertheless a CLIENT_WEAPONS countdown; this selected adapter owns
    // its absolute ownerAttack gate. Holster restores a native countdown lock.
    m_flNextPrimaryAttack = action_.primary;
    m_flNextSecondaryAttack = action_.secondary;
    m_flTimeWeaponIdle = action_.idle;
#ifdef CLIENT_WEAPONS
    if (!overlay_.token.generation)
        m_pPlayer->m_flNextAttack = selected ? 0.0f : action_.ownerAttack - gpGlobals->time;
#else
    if (!overlay_.token.generation)
        m_pPlayer->m_flNextAttack = selected ? gpGlobals->time : action_.ownerAttack;
#endif
}
BOOL GlockWeapon::CanDeploy()
{
    GlockOwnerFrame frame{};
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL GlockWeapon::Deploy()
{
    if (overlay_.token.generation) CancelWeaponAction(overlay_.token);
    GlockOwnerFrame frame{};
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    std::uint32_t effects = Effects(GlockEffect::DeployPrelude, GlockEffect::DeployReset, GlockEffect::InventoryChanged);
    if (!frame.firstDrawn) effects |= EffectBit(GlockEffect::FirstDraw);
    if (reserveHud_ != action_.reserve) effects |= EffectBit(GlockEffect::ReserveHud);
    if (pev->armorvalue != 1) effects |= EffectBit(GlockEffect::Sleeve);
    if (taclightInstalled_ || pev->body != 0) effects |= EffectBit(GlockEffect::Body);
    if (!prepared_.deployPreludeReady || (pev->armorvalue != 1 && !prepared_.soundsReady) ||
        !g_engfuncs.pfnRandomLong ||
        !RequireEffects(effects) || !RequireSequence(frame.firstDrawn ? (action_.clip ? 6 : 7) : 29))
    { status_ = GlockStatus::MissingBackend; return FALSE; }
    selected_ = true;
    if (!UpdateBody(frame.lightOn)) return FALSE;
    if (action_.clip == -666) { action_.clip = 0; WriteAmmo(); }
    action_.action = 0;
    if (!frame.firstDrawn && !Emit({GlockEffect::FirstDraw})) return FALSE;
    if (!Emit({GlockEffect::DeployPrelude})) return FALSE;
    action_.aiming = false;
    spread_.scalar = 0;
    spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({GlockEffect::DeployReset})) return FALSE;
    if (reserveHud_ != action_.reserve)
    {
        GlockEvent e{GlockEffect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return FALSE;
        reserveHud_ = action_.reserve;
    }
    // Original 1011C3FE consumes pitch RNG even when armorvalue suppresses
    // one sleeve. Keep RNG ordering before ammo tabulation/player models.
    Lease sleeveLease = lease_; sleeveLease.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&sleeveLease)) return FALSE;
    if (pev->armorvalue != 1)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&sleeveLease)) return FALSE;
        GlockEvent event{GlockEffect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8f;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({GlockEffect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "onehanded", sizeof("onehanded"));
    const int sequence = frame.firstDrawn ? (action_.clip ? 6 : 7) : 29;
    if (!View(sequence)) return FALSE;
    Lease l = lease_; l.active = true;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(const_cast<Lease*>(&l))) return FALSE;
    const float deadline = gpGlobals->time + prepared_.sequences.duration[sequence];
    action_.ownerAttack = action_.primary = action_.secondary = action_.idle = deadline;
    MirrorTimers(true);
    status_ = GlockStatus::Applied;
    return TRUE;
}
void GlockWeapon::Holster(int)
{
    if (overlay_.token.generation) CancelWeaponAction(overlay_.token);
    GlockOwnerFrame frame{};
    if (!Check(frame, false)) return;
    if (!RequireSequence(action_.clip ? 8 : 9) ||
        (frame.lightOn && !RequireEffects(EffectBit(GlockEffect::LightState)))) return;
    // Safety adaptation: original normal Holster did not clear the player think.
    // A detached CoF weapon may never damage as a subsequently selected CS item.
    meleePending_ = false;
    SetThink(nullptr);
    pev->nextthink = 0;
    if (!View(action_.clip ? 8 : 9)) return;
    if (frame.lightOn)
    { GlockEvent light{GlockEffect::LightState}; light.enabled = false; if (!Emit(light)) return; }
    lightThink_ = 0;
    action_.ownerAttack = AddDouble(gpGlobals->time, .5);
    selected_ = false;
    MirrorTimers(false);
    status_ = GlockStatus::Applied;
}

MagazineActionHost GlockWeapon::ActionHost(ActionBridge& bridge) noexcept { return {&bridge, [](void* p) noexcept {
    auto& b = *static_cast<ActionBridge*>(p);
    return Current(&b.lease);
}, MagazineEvent}; }

bool GlockWeapon::MagazineEvent(void* context, const guest::cof::MagazineEvent& e) noexcept
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
        self.SetThink(&GlockWeapon::DelayedMelee);
        self.pev->nextthink = gpGlobals->time + e.delay;
        return true;
    }
    return false;
}
void GlockWeapon::SetResult(MagazineActionResult result) noexcept
{
    status_ = result == MagazineActionResult::Applied || result == MagazineActionResult::Deferred ?
        GlockStatus::Applied : result == MagazineActionResult::Ineligible ? GlockStatus::Ineligible :
        result == MagazineActionResult::Cancelled ? GlockStatus::Cancelled : GlockStatus::InvalidInput;
}
void GlockWeapon::Reload()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame) || !ReloadReady()) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartMagazineReload(Pistol::Glock, action_, gpGlobals->time,
                                           prepared_.sequences, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void GlockWeapon::SecondaryAttack()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame) || !MeleeReady(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartPistolMelee(Pistol::Glock, action_, gpGlobals->time, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void GlockWeapon::DelayedMelee()
{
    GlockOwnerFrame frame{}; if (!Check(frame) || !meleePending_) return;
    if (gpGlobals->time < pev->nextthink) { status_ = GlockStatus::Ineligible; return; }
    Lease check = lease_; check.active = true;
    if (CheckPistolWeaponMelee(*m_pPlayer, {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer},
        prepared_.melee, {Current, &check}) != MeleeStatus::Ready)
    { status_ = GlockStatus::MissingBackend; return; }
    meleePending_ = false; SetThink(nullptr); pev->nextthink = 0;
    Lease l = lease_; l.active = true;
    const auto result = PistolWeaponMelee(*m_pPlayer,
        {frame.meleeDamage, frame.quietAudio, MeleeWorld::Multiplayer}, prepared_.melee, {Current, &l});
    if (!Current(const_cast<Lease*>(&l))) return;
    status_ = result == MeleeStatus::Hit || result == MeleeStatus::Miss ? GlockStatus::Applied :
        result == MeleeStatus::Cancelled ? GlockStatus::Cancelled : GlockStatus::MissingBackend;
}

bool GlockWeapon::AimReady(bool enabled) noexcept
{
    if (prepared_.aimMetadataReady && RequireEffects(EffectBit(GlockEffect::Aim)) &&
        RequireSequence((enabled ? 30 : 36) + (action_.clip == 0))) return true;
    status_ = GlockStatus::MissingBackend;
    return false;
}
bool GlockWeapon::SetAim(bool enabled) noexcept
{
    if (!AimReady(enabled)) return false;
    // All ordinary callers set the aim bit before entering the original
    // entry/exit method (F980 toggle, pending reload and hold-aim release).
    action_.aiming = enabled;
    GlockEvent e{GlockEffect::Aim}; e.enabled = enabled;
    e.first = enabled ? prepared_.aimOffset : Vector{};
    e.value = enabled ? prepared_.aimZoom : 0; e.a = 350;
    if (!Emit(e)) return false;
    m_pPlayer->pev->fov = enabled ? float(prepared_.aimZoom) : 90.0f;
    const int sequence = (enabled ? 30 : 36) + (action_.clip == 0);
    if (!View(sequence)) return false;
    action_.aiming = enabled;
    action_.primary = action_.secondary = action_.extra = AddDouble(gpGlobals->time, .3);
    action_.idle = gpGlobals->time + prepared_.sequences.duration[sequence];
    MirrorTimers(true);
    return true;
}

void GlockWeapon::PrimaryAttack()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame) || !PrimaryReady(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || !(frame.pressed & 1u) || action_.action)
    { status_ = GlockStatus::Ineligible; return; }
    if (m_pPlayer->pev->waterlevel == 3 || action_.clip <= 0)
    {
        if (m_pPlayer->pev->waterlevel != 3 && !m_fFireOnEmpty) Reload();
        else
        {
            // Original animates every dry attempt, even when its click is locked.
            if (!View(action_.aiming ? 35 : 20)) return;
            if (now > emptyDeadline_)
            {
                if (!Emit({GlockEffect::EmptyClick})) return;
                emptyDeadline_ = AddDouble(now,.5);
            }
        }
        action_.primary = action_.secondary = AddDouble(now,.5);
        MirrorTimers(true); status_ = GlockStatus::Applied; return;
    }
    PistolBallistics shot{{}, {1,0,0}, {spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0}, {0,0,1}, frame.randomSeed, frame.bulletDamage, action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers, frame.sourceMode};
    Lease l = lease_; l.active = true;
    GlockEvent smoke{GlockEffect::Gunsmoke};
    smoke.value = g_engfuncs.pfnIndexOfEdict(l.weapon);
    if (!Current(&l)) return;
    smoke.enabled = true; smoke.modelString = prepared_.viewModelString;
    if (!Emit(smoke)) return;
    if (!frame.infiniteAmmo) --action_.clip;
    WriteAmmo();
    g_engfuncs.pfnMakeVectors(m_pPlayer->pev->v_angle);
    if (!Current(&l)) return;
    const Vector gun = m_pPlayer->GetGunPosition();
    if (!Current(&l)) return;
    GlockEvent fire{GlockEffect::FireEvent}; fire.first = m_pPlayer->pev->origin;
    fire.second = gun + gpGlobals->v_right * 32.0f;
    fire.value = OriginalId; fire.other = frame.eventOwner2814 != 1;
    if (!Emit(fire) || !Animate(PlayerAction::Fire) || !Animate(PlayerAction::Move)) return;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    g_engfuncs.pfnMakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    if (!Current(&l)) return;
    shot.source = m_pPlayer->GetGunPosition();
    if (!Current(&l)) return;
    shot.direction = gpGlobals->v_forward; shot.right = gpGlobals->v_right; shot.up = gpGlobals->v_up;
    spread_.scalar += 1.25f; spread_.recoveryDeadline = AddDouble(now,.55);
    const auto result = FirePistolBallistics(*m_pPlayer,shot,prepared_.ballistics,{Current,&l});
    if (!Current(&l)) return;
    if (result.status != BallisticsStatus::Fired) { status_ = GlockStatus::Cancelled; return; }
    action_.idle = now + 1.0f;
    const int recoil = g_engfuncs.pfnRandomLong(-2,2);
    if (!Current(&l)) return;
    GlockEvent kick{GlockEffect::Recoil}; kick.first = {float(recoil),1,1};
    if (!Emit(kick)) return;
    GlockEvent shake{GlockEffect::Shake};
    const bool crouch = (frame.originalFlags & 0x4000u) != 0;
    shake.a = crouch ? 2.0f : 5.0f; shake.b = crouch ? .1f : .25f;
    shake.c = crouch ? .1f : .15f; shake.value = 2;
    if (!Emit(shake) || !View(action_.aiming ? (action_.clip ? 34 : 38) : (action_.clip ? 2 : 3))) return;
    GlockEvent shotState{GlockEffect::ShotState}; shotState.a = 3;
    if (!Emit(shotState) || !Emit({GlockEffect::FireSound})) return;
    GlockEvent latch{GlockEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(now,.2);
    if (!Emit(latch)) return;
    action_.primary = AddDouble(now,.1); action_.secondary = now + 1.0f;
    if (action_.clip <= 0) action_.primary = now + 1.0f;
    MirrorTimers(true); status_ = GlockStatus::Applied;
}

bool GlockWeapon::Fidget() noexcept
{
    const float now = gpGlobals->time;
    if (action_.primary > now || action_.secondary > now || action_.idle > now ||
        action_.action || action_.aiming) return true;
    if (!g_engfuncs.pfnRandomLong || !RequireSequence(21 + (action_.clip == 0)) ||
        !RequireSequence(23 + (action_.clip == 0)) || !RequireSequence(25 + (action_.clip == 0)))
    { status_ = GlockStatus::MissingBackend; return false; }
    Lease l = lease_; l.active = true;
    const int chance = g_engfuncs.pfnRandomLong(0,599);
    if (!Current(&l)) return false;
    if (chance != 50) return true;
    const int choice = g_engfuncs.pfnRandomLong(0,2);
    if (!Current(&l)) return false;
    return choice < 0 || choice > 2 ? false : View(21 + choice*2 + (action_.clip == 0));
}
bool GlockWeapon::UpdateBody(bool lightOn) noexcept
{
    const int body = taclightInstalled_ ? (lightOn ? 2 : 1) : 0;
    if (!taclightInstalled_ && pev->body == 0) return true;
    if (!RequireEffects(EffectBit(GlockEffect::Body))) return false;
    pev->body = body;
    GlockEvent event{GlockEffect::Body}; event.value = body;
    return Emit(event);
}
GlockStatus GlockWeapon::RequestTaclightAttachment() noexcept
{
    GlockOwnerFrame frame{};
    if (!Check(frame,false) || !AttachmentReady()) return status_;
    // Original inventory combine100E3020 writes the pending byte on gun id3.
    // The parent must first validate and consume the actual inventory recipe.
    taclightAttachPending_ = true;
    return status_ = GlockStatus::Applied;
}
void GlockWeapon::WeaponFunction()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || action_.secondary > now || !taclightInstalled_ ||
        action_.aiming || action_.action)
    { status_ = GlockStatus::Ineligible; return; }
    if (!RequireEffects(Effects(GlockEffect::LightState, GlockEffect::Body)) ||
        !RequireSequence(action_.clip ? 17 : 18)) return;
    if (!View(action_.clip ? 17 : 18)) return;
    lightThink_ = 1; SetThink(&GlockWeapon::ToggleSwitch); pev->nextthink = now + .3f;
    action_.primary = action_.secondary = action_.idle = AddDouble(now,.66);
    MirrorTimers(true); status_ = GlockStatus::Applied;
}
void GlockWeapon::ToggleSwitch()
{
    GlockOwnerFrame frame{}; if (!Check(frame) || lightThink_ != 1) return;
    if (gpGlobals->time < pev->nextthink) { status_ = GlockStatus::Ineligible; return; }
    if (!RequireEffects(EffectBit(GlockEffect::LightState))) return;
    lightThink_ = 0; SetThink(nullptr); pev->nextthink = 0;
    GlockEvent event{GlockEffect::LightState}; event.enabled = !frame.lightOn;
    if (Emit(event)) status_ = GlockStatus::Applied;
}
void GlockWeapon::MidAttachTaclight()
{
    GlockOwnerFrame frame{}; if (!Check(frame) || lightThink_ != 2) return;
    if (gpGlobals->time < pev->nextthink) { status_ = GlockStatus::Ineligible; return; }
    if (!AttachmentReady()) return;
    GlockEvent sound{GlockEffect::AttachmentSound}; sound.value = 1;
    if (!Emit(sound)) return;
    lightThink_ = 3; SetThink(&GlockWeapon::AttachTaclight); pev->nextthink = gpGlobals->time + 1.0f;
    action_.ownerAttack = action_.primary = action_.secondary = action_.extra = gpGlobals->time + 5.0f;
    MirrorTimers(true); status_ = GlockStatus::Applied;
}
void GlockWeapon::AttachTaclight()
{
    GlockOwnerFrame frame{}; if (!Check(frame) || lightThink_ != 3) return;
    if (gpGlobals->time < pev->nextthink) { status_ = GlockStatus::Ineligible; return; }
    if (!AttachmentReady()) return;
    GlockEvent sound{GlockEffect::AttachmentSound}; sound.value = 2;
    if (!Emit(sound)) return;
    taclightInstalled_ = true;
    if (!UpdateBody(frame.lightOn)) return;
    taclightAttachPending_ = false;
    GlockEvent tutorial{GlockEffect::Tutorial}; tutorial.value = 1;
    if (!Emit(tutorial) || !View(action_.clip ? 6 : 7)) return;
    lightThink_ = 0; SetThink(nullptr); pev->nextthink = 0;
    action_.ownerAttack = action_.primary = action_.secondary = action_.extra = gpGlobals->time + 1.0f;
    MirrorTimers(true); status_ = GlockStatus::Applied;
}

void GlockWeapon::ReloadCommitted(void* context, const MagazineReload& state) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    self.action_.clip = state.clip; self.action_.reserve = state.reserve;
    if (self.overlay_.token.generation && self.overlay_.resuming)
    {
        // Emergency resume owns this additional transaction boundary. An
        // observer can synchronously Holster/select another gun, so consume
        // this committed reload before it can retire the overlay. Ordinary
        // CoF reload keeps its original observer ordering.
        self.action_.loading = false;
        if (self.action_.action == 2) self.action_.action = 0;
        self.viewSequence_ = (self.action_.aiming ? 32 : 0) + (state.clip == 0);
    }
    self.WriteAmmo();
    bridge.callbackFailed = !self.Emit({GlockEffect::InventoryChanged});
}
void GlockWeapon::AdvanceOwnerFrame() noexcept
{
    GlockOwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

void GlockWeapon::ItemPostFrame()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame)) return;
    Lease l = lease_; l.active = true;
    const float now = gpGlobals->time;
    if (now < action_.ownerAttack) { status_ = GlockStatus::Ineligible; return; }
    if (taclightAttachPending_ && !AttachmentReady()) return;
    if (!action_.action && now > action_.primary && now > action_.secondary && now > action_.extra &&
        !frame.toggleAim && action_.aiming &&
        !((m_pPlayer->pev->button | frame.pressed | frame.released) & 0x40u))
        if (!SetAim(false)) return;
    if (action_.pendingReload && !frame.pendingGate && frame.pendingGateTime <= now &&
        action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action)
    {
        if (action_.aiming) { if (!SetAim(false)) return; }
        else
        {
            if (!ReloadReady(true)) return;
            action_.pendingReload = false; Reload(); if (!Current(&l)) return;
        }
    }
    if (!UpdateBody(frame.lightOn)) return;
    if (taclightAttachPending_ && !frame.pendingGate && frame.pendingGateTime <= now &&
        action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action)
    {
        if (action_.aiming && !SetAim(false)) return;
        if (!View(action_.clip ? 8 : 9)) return;
        action_.ownerAttack = AddDouble(now,.5);
        lightThink_ = 2; SetThink(&GlockWeapon::MidAttachTaclight); pev->nextthink = now + 1.0f;
        GlockEvent sound{GlockEffect::AttachmentSound}; sound.value = 0;
        if (!Emit(sound)) return;
        action_.ownerAttack = action_.primary = action_.secondary = action_.extra = now + 5.0f;
    }
    if (action_.action == 2 && !action_.loading) action_.action = 0;
    const float speedSquared = m_pPlayer->pev->velocity.x*m_pPlayer->pev->velocity.x +
                               m_pPlayer->pev->velocity.y*m_pPlayer->pev->velocity.y;
    const float speed = static_cast<float>(std::sqrt(double(speedSquared)));
    if (!action_.loading && action_.action != 2 && now > action_.primary)
    {
        if (frame.condition3AA && !action_.aiming && now > action_.extra)
        {
            if (!action_.action)
            {
                const int seq = action_.clip ? 39 : 40; if (!RequireSequence(seq)) return;
                action_.action = 5;
                if (!View(seq)) return;
                const float duration = prepared_.sequences.duration[seq];
                action_.primary = static_cast<float>(double(now) + double(duration)*.5);
                actionDeadline_ = now + duration;
            }
        }
        else if (action_.action == 5)
        {
            const int seq = action_.clip ? 41 : 42; if (!View(seq)) return;
            action_.primary = action_.idle = now + prepared_.sequences.duration[seq]; action_.action = 0;
        }
        const bool sprint = (frame.originalFlags & 0x40000u) && speed > 0;
        if (sprint && action_.action != 5 && action_.action != 1)
        {
            const int seq = action_.clip ? 10 : 11; if (!RequireSequence(seq)) return;
            action_.action = 1; if (!View(seq)) return;
            const float duration = prepared_.sequences.duration[seq];
            action_.primary = static_cast<float>(double(now) + double(duration)*.5);
            actionDeadline_ = now + duration;
        }
        if (sprint && action_.action == 1 && now > actionDeadline_)
        {
            const int seq = action_.clip ? 12 : 13; if (!View(seq)) return;
            actionDeadline_ = now + prepared_.sequences.duration[seq];
        }
        if (!sprint && action_.action == 1 && now > actionDeadline_)
        {
            const int seq = action_.clip ? 14 : 15; if (!View(seq)) return;
            action_.primary = AddDouble(now,.33); action_.idle = AddDouble(now,.35); action_.action = 0;
        }
    }
    if (action_.clip > 0) m_iPlayEmptySound = 1;
    if (!Fidget()) return;
    if (now > wearDeadline_) { wearDeadline_ = AddDouble(now,.75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_;
    if (reserveHud_ != action_.reserve)
    {
        GlockEvent e{GlockEffect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return; reserveHud_ = action_.reserve;
    }
    if (action_.loading)
    {
        if (!RequireEffects(EffectBit(GlockEffect::InventoryChanged))) return;
        MagazineReload reload{action_.clip,action_.reserve,action_.ownerAttack,true};
        ActionBridge bridge{this, lease_}; bridge.lease.active = true;
        CommitMagazineReload(reload,15,0,now,ReloadCommitted,&bridge);
        if (!Current(&bridge.lease)) return;
        if (bridge.callbackFailed) return;
        action_.clip = reload.clip; action_.reserve = reload.reserve; action_.loading = reload.loading;
        WriteAmmo();
    }
    const unsigned buttons = m_pPlayer->pev->button;
    const bool allowed = frame.activityBlock <= 0 && m_pPlayer->pev->movetype != 5;
    if ((buttons & 0x800u) && frame.activityBlock <= 0 && action_.secondary <= now)
    {
        if (allowed) { if (!MeleeReady(frame) || !Emit({GlockEffect::InventoryChanged})) return; SecondaryAttack(); }
    }
    else if ((buttons & 0x40u) && allowed && action_.ownerAttack <= now)
    {
        const bool changeAim = action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action &&
            (frame.pressed & 0x40u) && (frame.toggleAim || !action_.aiming);
        if ((changeAim && !AimReady(!action_.aiming)) || !Emit({GlockEffect::InventoryChanged})) return;
        if (changeAim)
            if (!SetAim(!action_.aiming)) return;
    }
    else if ((buttons & 1u) && frame.activityBlock <= 0 && action_.primary <= now)
    {
        if (allowed)
        {
            if (!PrimaryReady(frame, !action_.clip) || !RequireEffects(EffectBit(GlockEffect::InventoryChanged))) return;
            if (!action_.clip) m_fFireOnEmpty = 1;
            if (!Emit({GlockEffect::InventoryChanged})) return;
            PrimaryAttack();
        }
    }
    else if ((buttons & 0x2000u) && !action_.loading)
    { if (m_pPlayer->pev->movetype != 5) Reload(); }
    else if (!(buttons & 0x841u)) m_fFireOnEmpty = 0;
    if (!Current(const_cast<Lease*>(&l))) return;
    WriteAmmo(); MirrorTimers(true);
}
void GlockWeapon::WeaponIdle()
{
    if (overlay_.token.generation) return;
    GlockOwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    if (action_.idle <= gpGlobals->time && !action_.action && !action_.special &&
        !RequireSequence((action_.aiming ? 32 : 0) + (action_.clip == 0))) return;
    const auto result = MagazineIdle(Pistol::Glock, action_, gpGlobals->time, prepared_.sequences, ActionHost(bridge));
    if (!Current(&bridge.lease)) return;
    MirrorTimers(true); SetResult(result);
}
void GlockWeapon::Invalidate() noexcept
{
    overlay_ = {};
    bound_ = selected_ = meleePending_ = false; lightThink_ = 0;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = GlockStatus::Cancelled;
}
}
