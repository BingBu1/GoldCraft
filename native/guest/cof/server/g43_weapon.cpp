// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "g43_weapon.hpp"
#include "player_think.hpp"
#include "../magazine_reload.hpp"
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
goldcraft::scenario::GuestProfile G43Weapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {G43HostId, GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior, GuestKill::StandardPlayerProcessing};
}

namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
constexpr unsigned OriginalId = 26;
constexpr std::uint64_t InventoryKind = 0x434f464734330001ULL;
}


struct G43Weapon::InventoryState
{
    MagazineActionState action;
    SpreadState spread;
    SpreadTransitions transitions;
    float actionDeadline{},emptyDeadline{},wearDeadline{},nextThink{};
    float armor{},spreadIncrement{};
    int body{};
    float primary{},secondary{},idle{},ownerAttack{};
    int reserveHud{},wearMaximum{},wear{},clip{},inReload{},fireOnEmpty{};
    int ownerSerial{},weaponSerial{},modelIndex{},ownerSpawn{};
    std::uintptr_t map{},ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    bool selected{};
    G43Status status{};
};
bool G43Weapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease=lease_;
    if(!bound_||!pev||m_pPlayer!=lease.player||!Current(const_cast<Lease*>(&lease)))return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState)<=512);
    InventoryState saved{};
    saved.action=action_;saved.spread=spread_;saved.transitions=transitions_;
    saved.actionDeadline=actionDeadline_;saved.emptyDeadline=emptyDeadline_;saved.wearDeadline=wearDeadline_;
    saved.spreadIncrement=spreadIncrement_;
    saved.body=pev->body;saved.armor=pev->armorvalue;
    saved.nextThink=pev->nextthink;saved.think=m_pfnThink;
    saved.primary=m_flNextPrimaryAttack;saved.secondary=m_flNextSecondaryAttack;saved.idle=m_flTimeWeaponIdle;
    saved.ownerAttack=m_pPlayer->m_flNextAttack;saved.clip=m_iClip;
    saved.inReload=m_fInReload;saved.fireOnEmpty=m_fFireOnEmpty;
    saved.reserveHud=reserveHud_;saved.wearMaximum=wearMaximum_;saved.wear=wear_;
    saved.ownerSerial=lease.ownerSerial;saved.weaponSerial=lease.weaponSerial;saved.modelIndex=lease.modelIndex;
    saved.ownerSpawn=m_pPlayer->m_iNumSpawns;saved.map=reinterpret_cast<std::uintptr_t>(worldLease_.context);
    saved.ownerContext=reinterpret_cast<std::uintptr_t>(lease.context);
    saved.selected=selected_;
    saved.status=status_;
    output.kind=InventoryKind;output.bytes=sizeof(saved);
    std::memcpy(output.data.data(),&saved,sizeof(saved));return true;
}
bool G43Weapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
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
    const EHANDLE owner(&player);G43Backends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found||!sameInventory()||
        (backends_.ownerLife&&backends_.ownerLife!=backend.ownerLife)||
        (pending&&(!backend.ownerLife||pending->life!=backend.ownerLife)))return false;
    if(Bind(player,prepared,backend)!=G43Status::Ready)return false;
    const auto lease=lease_;AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    if(selected_&&!PublishOwnerState())
    {const auto result=status_;Invalidate();status_=result;return false;}
    return true;
}
bool G43Weapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
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
    actionDeadline_=saved.actionDeadline;emptyDeadline_=saved.emptyDeadline;wearDeadline_=saved.wearDeadline;
    spreadIncrement_=saved.spreadIncrement;
    pev->body=saved.body;pev->armorvalue=saved.armor;
    m_pfnThink=saved.think;pev->nextthink=saved.nextThink;
    m_flNextPrimaryAttack=saved.primary;m_flNextSecondaryAttack=saved.secondary;m_flTimeWeaponIdle=saved.idle;
    if(saved.selected)m_pPlayer->m_flNextAttack=saved.ownerAttack;
    m_iClip=saved.clip;m_fInReload=saved.inReload;m_fFireOnEmpty=saved.fireOnEmpty;
    reserveHud_=saved.reserveHud;wearMaximum_=saved.wearMaximum;wear_=saved.wear;
    selected_=saved.selected;status_=saved.status;
    return !selected_||PublishOwnerState();
}
bool G43Weapon::PublishOwnerState() noexcept
{
    auto lease = lease_; lease.active = true;
    if (!selected_ || !Current(&lease) || !prepared_.aimMetadataReady) return false;
    G43Event event{G43Effect::Aim}; event.enabled = action_.aiming;
    event.first = action_.aiming ? prepared_.aimOffset : Vector{};
    event.value = action_.aiming ? prepared_.aimZoom : 0;
    if (!Emit(event)) return false;
    event = {G43Effect::CameraZoom}; event.a = 350;
    if (!Emit(event)) return false;
    lease.player->pev->fov = action_.aiming ? float(prepared_.aimZoom) : 90.F;
    return true;
}


void G43Weapon::OwnerModelChanging() noexcept
{
    G43OwnerFrame frame{};
    if (!Check(frame, false) || !ResourcesReady() || !backends_.ownerLife ||
        selected_ != (m_pPlayer->m_pActiveItem == this)) { Invalidate(); return; }
    const ModelRebind pending{lease_.owner, worldLease_.context, backends_.ownerLife,
        lease_.ownerSerial, lease_.weaponSerial, m_pPlayer->m_iNumSpawns, selected_};
    Invalidate(); modelRebind_ = pending;
}

bool G43Weapon::Current(void* context) noexcept
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

bool G43Weapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool G43Weapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void G43Weapon::ConfigureColdHost(G43ColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
G43Status G43Weapon::Prepare(const G43Prepared& p) noexcept
{
    preparedReady_ = false;
    status_ = G43Status::MissingBackend;
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
        p.hostSlot != G43HudSlot || p.hostPosition != G43HudPosition ||
        p.csPenetration<0 || p.csPenetration>32 || (p.csPenetration && p.csPenetration<3))
        return status_ = G43Status::InvalidInput;
    for (int i = 0; i < 38; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = G43Status::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = G43Status::Ready;
}
void G43Weapon::Precache()
{
    preparedReady_ = false;
    status_ = G43Status::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    G43Prepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

G43Status G43Weapon::Bind(CBasePlayer& owner, const G43Prepared& p,
                          const G43Backends& backends) noexcept
{
    bound_ = selected_ = false;
    modelRebind_.reset();
    status_ = G43Status::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != G43RequiredEffects || !backends.playerAnimation ||
        !backends.playerThink || !backends.playerThink->PreparedFor(owner, PlayerThinkKind::Pushback,
            backends.playerModelGeneration.mapEpoch, backends.ownerLife))
        return status_;
    if (Prepare(p) != G43Status::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = G43Status::InvalidInput;
    status_ = G43Status::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, LongGun::Rifle,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = G43Status::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias G43 to an unrelated stock CS weapon.
    m_iId = G43HostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = G43Status::Ready;
}

void G43Weapon::Spawn()
{
    modelRebind_.reset();
    action_ = {};
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 10;
    spreadIncrement_ = 2.4F;
    static_assert(std::bit_cast<std::uint32_t>(2.4F) == 1075419546U);
    m_iSecondaryAmmoType = -1;
    m_iClip = 0;
    m_fInReload = m_fFireOnEmpty = 0;
    bound_ = selected_ = false;
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
    { status_ = G43Status::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = G43Status::InvalidInput; return; }
    m_iId = G43HostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 10;
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
    SetUse(&G43Weapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&G43Weapon::WorldTouch);
    SetThink(&G43Weapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = G43Status::Applied;
}

int G43Weapon::GetItemInfo(ItemInfo* info)
{
    // Original100435C0 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_g43";
    info->pszAmmo1 = "cof_g43";
    info->iMaxAmmo1 = 40;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 10;
    info->iId = m_iId = G43HostId;
    info->iSlot = G43HudSlot;
    info->iPosition = G43HudPosition;
    info->iWeight = 5;
    return 1;
}

int G43Weapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void G43Weapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void G43Weapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&G43Weapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&G43Weapon::WorldTouch);
}
void G43Weapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void G43Weapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = G43Status::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = G43Status::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        G43Event event{G43Effect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int G43Weapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    G43Backends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != G43Status::Ready) return FALSE;
    G43OwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = G43Status::Ineligible; return FALSE; }
    return TRUE;
}
int G43Weapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == G43Status::Ready || status_ == G43Status::Applied);
}
int G43Weapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int G43Weapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<G43Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    G43OwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = G43Status::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 10 ? amount : 10;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, "cof_g43", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({G43Effect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int G43Weapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<G43Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    G43OwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_g43", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void G43Weapon::AttachToPlayer(CBasePlayer* player)
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
void G43Weapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void G43Weapon::Drop() { RetireWorld(false); }
void G43Weapon::Kill() { RetireWorld(true); }
void G43Weapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = G43Status::UnsupportedState;
}

bool G43Weapon::Check(G43OwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = G43Status::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = G43Status::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = G43Status::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 30 && frame.reserveLimit != 40) || (frame.stamina && !std::isfinite(*frame.stamina)) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = G43Status::InvalidInput; return false; }
    ReadAmmo();
    // Carry metadata limits pickups, not trusted scenario ammo already owned.
    if ((action_.clip < 0 && (active || action_.clip != -666)) ||
        action_.reserve < 0)
    { status_ = G43Status::InvalidInput; return false; }
    status_ = G43Status::Ready;
    return true;
}

bool G43Weapon::Emit(G43Event event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l)) return false;
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = G43Status::MissingBackend;
    return delivered;
}
bool G43Weapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, LongGun::Rifle, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
bool G43Weapon::View(int sequence) noexcept
{
    Lease lease = lease_; lease.active = selected_;
    if (!Current(&lease)) return false;
    m_pPlayer->pev->weaponanim = sequence; // original1011F5D0, before transport
    G43Event event{G43Effect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void G43Weapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void G43Weapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
void G43Weapon::MirrorTimers(bool selected, bool ownerGate) noexcept
{
    // UseDecrement=false keeps weapon clocks absolute. ReGameDLL's player gate
    // is nevertheless a CLIENT_WEAPONS countdown; this selected adapter owns
    // its absolute ownerAttack gate. Holster restores a native countdown lock.
    m_flNextPrimaryAttack = action_.primary;
    m_flNextSecondaryAttack = action_.secondary;
    m_flTimeWeaponIdle = action_.idle;
    if (!ownerGate) return;
#ifdef CLIENT_WEAPONS
    m_pPlayer->m_flNextAttack = selected ? 0.0f : action_.ownerAttack - gpGlobals->time;
#else
    m_pPlayer->m_flNextAttack = selected ? gpGlobals->time : action_.ownerAttack;
#endif
}
BOOL G43Weapon::CanDeploy()
{
    G43OwnerFrame frame{};
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL G43Weapon::Deploy()
{
    G43OwnerFrame frame{};
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_ = true;
    if (action_.clip == -666) { action_.clip = 0; WriteAmmo(); }
    action_.action = 0;
    if (!Emit({G43Effect::DeployPrelude})) return FALSE;
    action_.aiming = false; spread_.scalar = 0; spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({G43Effect::DeployReset})) return FALSE;
    if (reserveHud_ != action_.reserve)
    {
        G43Event event{G43Effect::ReserveHud}; event.value = action_.reserve;
        if (!Emit(event)) return FALSE;
        reserveHud_ = action_.reserve;
    }
    Lease lease = lease_; lease.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&lease)) return FALSE;
    if (pev->armorvalue != 1.F)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&lease)) return FALSE;
        G43Event event{G43Effect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8F;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({G43Effect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    // Authored host pose. Original CoF's own model uses onehanded.
    std::memcpy(m_pPlayer->m_szAnimExtention, "rifle", sizeof("rifle"));
    if (!View(action_.clip ? 0 : 1)) return FALSE;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(&lease)) return FALSE;
    float duration{};
    if (!CurrentDuration(duration)) { status_ = G43Status::InvalidInput; return FALSE; }
    action_.ownerAttack = action_.primary = action_.secondary = action_.idle = duration + gpGlobals->time;
    MirrorTimers(true); status_ = G43Status::Applied; return TRUE;
}
void G43Weapon::Holster(int)
{
    G43OwnerFrame frame{}; if (!Check(frame, false)) return;
    // Original ordinary100436D9. Self-harm special1/2 is not admitted.
    if (action_.aiming)
    {
        G43Event event{G43Effect::Aim};
        if (!Emit(event)) return;
        event = {G43Effect::CameraZoom}; event.a = 350;
        if (!Emit(event)) return;
        m_pPlayer->pev->fov = 90.F; action_.aiming = false;
    }
    if (!View(action_.clip ? 7 : 8)) return;
    action_.ownerAttack = AddDouble(gpGlobals->time, 1.05);
    selected_ = false; MirrorTimers(false); status_ = G43Status::Applied;
}

G43ControlHost G43Weapon::ControlHost(ActionBridge& bridge) noexcept {
    return {&bridge, [](void* p) noexcept {
        return Current(&static_cast<ActionBridge*>(p)->lease);
    }, [](void*) noexcept { return gpGlobals->time; }, ControlEvent};
}

bool G43Weapon::ControlEvent(void* context, const G43ControlEvent& event) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context); auto& self = *bridge.item;
    self.WriteAmmo(); self.MirrorTimers(self.selected_);
    switch (event.kind)
    {
    case G43ControlEventKind::SchedulePushback:
        return self.backends_.playerThink->Schedule(*self.m_pPlayer, PlayerThinkKind::Pushback,
            event.delay, self.backends_.playerModelGeneration.mapEpoch, self.backends_.ownerLife, G43HostId);
    case G43ControlEventKind::ViewAnimation: return self.View(event.argument);
    case G43ControlEventKind::PlayerWeaponMelee: return self.Animate(PlayerAction::WeaponMelee);
    case G43ControlEventKind::PlayerReloadLoaded:
    case G43ControlEventKind::PlayerReloadEmpty: return self.Animate(PlayerAction::Reload);
    }
    return false;
}
void G43Weapon::SetResult(G43ControlResult result) noexcept
{
    status_ = result == G43ControlResult::Applied || result == G43ControlResult::Deferred ? G43Status::Applied :
        result == G43ControlResult::Ineligible ? G43Status::Ineligible :
        result == G43ControlResult::Cancelled ? G43Status::Cancelled :
        result == G43ControlResult::InvalidInput || result == G43ControlResult::MissingSequence ?
        G43Status::InvalidInput : G43Status::MissingBackend;
}
void G43Weapon::Reload()
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartG43MagazineReload(action_, prepared_.sequences, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void G43Weapon::SecondaryAttack()
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    if (!frame.stamina) { status_ = G43Status::Ineligible; return; }
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartG43Pushback(action_, *frame.stamina, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}

bool G43Weapon::CurrentDuration(float& duration) noexcept
{
    const int sequence = m_pPlayer->pev->weaponanim;
    if (sequence >= 38) { duration = 0; return true; }
    if (sequence < 0 || !(prepared_.sequences.present & (std::uint64_t{1} << sequence))) return false;
    duration = prepared_.sequences.duration[sequence];
    return true;
}
bool G43Weapon::SetAim(bool enabled, bool input) noexcept
{
    struct Bridge { G43Weapon& item; Lease lease; } bridge{*this, lease_};
    bridge.lease.active = true;
    const G43AimHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, const G43AimEvent& value) noexcept
        {
            auto& self = static_cast<Bridge*>(p)->item;
            self.WriteAmmo(); self.MirrorTimers(true, false);
            G43Event event{G43Effect::Aim}; event.first = {value.offset[0], value.offset[1], value.offset[2]};
            event.enabled = value.enabled; event.value = value.value;
            switch (value.kind)
            {
            case G43AimEventKind::Presentation: return self.Emit(event);
            case G43AimEventKind::CameraZoom:
                event = {G43Effect::CameraZoom}; event.a = value.amount; return self.Emit(event);
            case G43AimEventKind::PlayerFov: self.m_pPlayer->pev->fov = value.amount; return true;
            case G43AimEventKind::ViewAnimation: return self.View(value.value);
            }
            return false;
        },
        [](void* p, float& duration) noexcept { return static_cast<Bridge*>(p)->item.CurrentDuration(duration); }};
    const G43AimMetadata metadata{{prepared_.aimOffset.x, prepared_.aimOffset.y, prepared_.aimOffset.z}, prepared_.aimZoom};
    G43OwnerFrame frame{};
    if (input && !Check(frame)) return false;
    const auto result = input ? ToggleG43Aim(action_, (frame.pressed & 0x40U) != 0, frame.toggleAim, metadata, host) :
        ApplyG43Aim(action_, enabled, metadata, host);
    if (!Current(&bridge.lease)) return false;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
    return result == G43ControlResult::Applied || result == G43ControlResult::Ineligible;
}

void G43Weapon::PrimaryAttack()
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    PistolBallistics shot{{},{1,0,0},{spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0},{0,0,1},frame.randomSeed,frame.bulletDamage,action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers,frame.sourceMode};
    Lease lease = lease_; lease.active = true;
    if (!action_.action && !action_.special && action_.clip > 0 && m_pPlayer->pev->waterlevel != 3 &&
        CheckPistolBallistics(shot,prepared_.ballistics,{Current,&lease}) != BallisticsStatus::Ready)
    { status_ = G43Status::MissingBackend; return; }
    G43ShotState state{action_,spread_,spreadIncrement_,emptyDeadline_};
    struct Bridge
    {
        G43Weapon& item; G43ShotState& state; Lease lease; PistolBallistics shot;
        void Sync() noexcept
        {
            item.action_ = state.action;
            item.spread_ = state.spread; item.spreadIncrement_ = state.spreadIncrement;
            item.emptyDeadline_ = state.emptyDeadline;
            item.WriteAmmo(); item.MirrorTimers(item.selected_,false);
        }
        static Vector V(const G43Vector& v) noexcept { return {v[0],v[1],v[2]}; }
        static G43Vector A(const Vector& v) noexcept { return {v.x,v.y,v.z}; }
    } bridge{*this,state,lease,shot};
    G43ShotFrame request{};
    request.damage=float(frame.bulletDamage);request.waterLevel=m_pPlayer->pev->waterlevel;
    request.shellModel=prepared_.shellModel;request.weaponEntity=g_engfuncs.pfnIndexOfEdict(lease.weapon);
    if(!Current(&lease))return;
    request.viewModel=prepared_.viewModelString;request.ownerFlags=frame.originalFlags;
    request.randomSeed=frame.randomSeed;request.eventOwner2814=frame.eventOwner2814;
    request.attackPressed=(frame.pressed&1U)!=0;request.fireOnEmpty=m_fFireOnEmpty!=0;request.infiniteAmmo=frame.infiniteAmmo;
    request.weaponOrigin=Bridge::A(pev->origin);request.weaponViewOffset=Bridge::A(pev->view_ofs);
    request.ownerOrigin=Bridge::A(m_pPlayer->pev->origin);request.viewAngles=Bridge::A(m_pPlayer->pev->v_angle);
    request.weaponVelocityY=pev->velocity.y;
    request.initialBasis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};
    const G43ShotHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p,const G43ShotEvent& value) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            G43Event event{G43Effect::EmptyClick};event.first=Bridge::V(value.first);event.second=Bridge::V(value.second);
            event.a=value.a;event.b=value.b;event.c=value.c;event.value=value.value;event.other=value.other;
            switch(value.kind)
            {
            case G43ShotEffect::Shell:event.kind=G43Effect::Shell;break;
            case G43ShotEffect::Gunsmoke:
                event.kind=G43Effect::Gunsmoke;event.enabled=true;event.modelString=value.other;event.other=1;break;
            case G43ShotEffect::FireEvent:event.kind=G43Effect::FireEvent;break;
            case G43ShotEffect::PlayerFire:return b.item.Animate(PlayerAction::Fire);
            case G43ShotEffect::Recoil:event.kind=G43Effect::Recoil;break;
            case G43ShotEffect::Shake:event.kind=G43Effect::Shake;break;
            case G43ShotEffect::ViewAnimation:return b.item.View(value.value);
            case G43ShotEffect::PlayerMove:return b.item.Animate(PlayerAction::Move);
            case G43ShotEffect::MuzzleFlash:b.item.m_pPlayer->pev->effects|=EF_MUZZLEFLASH;return true;
            case G43ShotEffect::ShotState:event.kind=G43Effect::ShotState;break;
            case G43ShotEffect::ShotLatch:event.kind=G43Effect::ShotLatch;break;
            case G43ShotEffect::EmptyClick:break;
            }
            return b.item.Emit(event);
        },
        [](void* p) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();b.item.Reload();
            if(!Current(&b.lease))return false;
            b.state.action=b.item.action_;
            return b.item.status_==G43Status::Applied||b.item.status_==G43Status::Ineligible;
        },
        [](void* p,const G43Vector& angles,G43Basis& basis) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();g_engfuncs.pfnMakeVectors(Bridge::V(angles));
            if(!Current(&b.lease))return false;
            basis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};return true;
        },
        [](void* p,G43Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->GetGunPosition());
            return Current(&b.lease);
        },
        [](void* p,G43Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            out=Bridge::A(b.item.m_pPlayer->pev->v_angle+b.item.m_pPlayer->pev->punchangle);return Current(&b.lease);
        },
        [](void* p,float,G43Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);out=Bridge::A(gpGlobals->v_forward);return Current(&b.lease);
        },
        [](void* p,int low,int high,int& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=g_engfuncs.pfnRandomLong(low,high);return Current(&b.lease);
        },
        [](void* p,const G43Bullet& bullet) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();auto& shot=b.shot;
            shot.source=Bridge::V(bullet.source);shot.direction=Bridge::V(bullet.direction);shot.spread=Bridge::V(bullet.spread);
            shot.right=gpGlobals->v_right;shot.up=gpGlobals->v_up;shot.seed=bullet.seed;shot.damage=bullet.damage;
            const auto result=b.item.prepared_.csPenetration?
                FireCSRifleBallistics(*b.item.m_pPlayer,shot,b.item.prepared_.ballistics,{Current,&b.lease},
                    b.item.prepared_.csPenetration,CSRifleMaterial::Rifle762):
                FirePistolBallistics(*b.item.m_pPlayer,shot,b.item.prepared_.ballistics,{Current,&b.lease});
            return Current(&b.lease)&&result.status==BallisticsStatus::Fired;
        },
        [](void* p,G43Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->EyePosition());return Current(&b.lease);
        },
        [](void* p,const G43Vector& position) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();G43Event e{G43Effect::FireSound};
            e.first=Bridge::V(position);e.value=0;e.other=1;e.enabled=true;return b.item.Emit(e);
        }};
    const auto result=G43PrimaryAttack(state,request,host);
    if(!Current(&lease))return;
    bridge.Sync();
    status_=result==G43ShotResult::Fired||result==G43ShotResult::Empty||result==G43ShotResult::ReloadRequested?
        G43Status::Applied:result==G43ShotResult::Ineligible?G43Status::Ineligible:
        result==G43ShotResult::Cancelled?G43Status::Cancelled:result==G43ShotResult::InvalidInput?
        G43Status::InvalidInput:G43Status::MissingBackend;
}

void G43Weapon::ReloadCommitted(void* context, const MagazineReload& state) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    self.action_.clip = state.clip; self.action_.reserve = state.reserve;
    self.WriteAmmo();
    bridge.callbackFailed = !self.Emit({G43Effect::InventoryChanged});
}
void G43Weapon::AdvanceOwnerFrame() noexcept
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

void G43Weapon::ItemPostFrame()
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    Lease lease = lease_; lease.active = true;
    if (gpGlobals->time < action_.ownerAttack) { status_ = G43Status::Ineligible; return; }
    struct Bridge { G43Weapon& item; Lease lease; } bridge{*this, lease};
    const G43FrameHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, G43FrameOwner& output) noexcept
        {
            auto& b = *static_cast<Bridge*>(p); G43OwnerFrame owner;
            if (!b.item.backends_.ownerFrame(b.item.backends_.context, owner) || !Current(&b.lease)) return false;
            const auto& velocity = b.item.m_pPlayer->pev->velocity;
            output = {velocity.x, velocity.y, owner.pendingGateTime, owner.originalFlags,
                owner.pressed, owner.released, static_cast<std::uint32_t>(b.item.m_pPlayer->pev->button),
                owner.condition3AA, owner.pendingGate, owner.toggleAim};
            return true;
        },
        [](void* p, G43FrameEvent event, int sequence) noexcept
        {
            auto& b = *static_cast<Bridge*>(p);
            switch (event)
            {
            case G43FrameEvent::ViewAnimation: return b.item.View(sequence);
            case G43FrameEvent::ExitAim: return b.item.SetAim(false);
            case G43FrameEvent::Reload:
                b.item.Reload();
                return Current(&b.lease) && (b.item.status_ == G43Status::Applied || b.item.status_ == G43Status::Ineligible);
            }
            return false;
        },
        [](void* p, float& duration) noexcept { return static_cast<Bridge*>(p)->item.CurrentDuration(duration); }};
    const auto derived = AdvanceG43DerivedFrame(action_, actionDeadline_, host);
    if (!Current(&lease)) return;
    if (derived != G43FrameResult::Applied)
    {
        status_ = derived == G43FrameResult::Cancelled ? G43Status::Cancelled :
            derived == G43FrameResult::UnsupportedState ? G43Status::UnsupportedState :
            derived == G43FrameResult::InvalidInput ? G43Status::InvalidInput : G43Status::MissingBackend;
        return;
    }
    if (!Check(frame)) return;
    if (action_.clip > 0) m_iPlayEmptySound = 1;
    // Original100435B0 is an empty fidget hook. No M16 RNG/animations here.
    if (gpGlobals->time > wearDeadline_)
    { wearDeadline_ = AddDouble(gpGlobals->time, .75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_;
    if (reserveHud_ != action_.reserve)
    {
        G43Event event{G43Effect::ReserveHud}; event.value = action_.reserve;
        if (!Emit(event)) return;
        reserveHud_ = action_.reserve;
    }
    if (action_.loading)
    {
        MagazineReload reload{action_.clip, action_.reserve, action_.ownerAttack, true};
        ActionBridge commit{this, lease};
        CommitMagazineReload(reload, 10, 0, gpGlobals->time, ReloadCommitted, &commit);
        if (!Current(&lease) || commit.callbackFailed) return;
        action_.clip = reload.clip; action_.reserve = reload.reserve; action_.loading = reload.loading;
        WriteAmmo();
    }
    // This is the shared original1011D9BC segment; its legacy C++ name does
    // not add an M16 mode, burst scheduler, fidget or derived-frame behavior.
    const M16InputHost input{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, M16InputOwner& output) noexcept
        {
            auto& b = *static_cast<Bridge*>(p); G43OwnerFrame owner;
            if (!b.item.backends_.ownerFrame(b.item.backends_.context, owner) || !Current(&b.lease)) return false;
            output = {static_cast<std::uint32_t>(b.item.m_pPlayer->pev->button), owner.activityBlock,
                b.item.m_pPlayer->pev->movetype}; return true;
        },
        [](void* p, M16InputEvent event) noexcept
        {
            auto& b = *static_cast<Bridge*>(p);
            b.item.WriteAmmo(); b.item.MirrorTimers(true);
            switch (event)
            {
            case M16InputEvent::InventoryChanged: return b.item.Emit({G43Effect::InventoryChanged});
            case M16InputEvent::Primary: b.item.PrimaryAttack(); break;
            case M16InputEvent::Secondary: b.item.SecondaryAttack(); break;
            case M16InputEvent::Reload: b.item.Reload(); break;
            case M16InputEvent::Aim: return b.item.SetAim(false, true);
            }
            return Current(&b.lease) && (b.item.status_ == G43Status::Applied || b.item.status_ == G43Status::Ineligible);
        }};
    const auto dispatched = DispatchM16FrameInput(action_, m_fFireOnEmpty, input);
    if (!Current(&lease)) return;
    status_ = dispatched == M16ControlResult::Applied ? G43Status::Applied :
        dispatched == M16ControlResult::Cancelled ? G43Status::Cancelled :
        dispatched == M16ControlResult::InvalidInput ? G43Status::InvalidInput : G43Status::MissingBackend;
    if (dispatched != M16ControlResult::Applied) return;
    WriteAmmo(); MirrorTimers(true);
}
void G43Weapon::WeaponIdle()
{
    G43OwnerFrame frame{}; if (!Check(frame)) return;
    // Original100DFEA0 only copies global forward into an unused local. Its
    // eliminated pure copy must not become CS's different autoaim behavior.
    const float now = gpGlobals->time;
    if (action_.idle > now || action_.primary > now || action_.secondary > now || action_.action || action_.special)
    { status_ = G43Status::Ineligible; return; }
    if (!View(action_.aiming ? (action_.clip ? 18 : 19) : (action_.clip ? 9 : 10))) return;
    float duration{};
    if (!CurrentDuration(duration)) { status_ = G43Status::InvalidInput; return; }
    action_.idle = duration + gpGlobals->time;
    MirrorTimers(true); status_ = G43Status::Applied;
}
void G43Weapon::Invalidate() noexcept
{
    modelRebind_.reset();
    bound_ = selected_ = false;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = G43Status::Cancelled;
}
}
