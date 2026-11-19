// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "famas_weapon.hpp"
#include "player_think.hpp"
#include "../famas_reload.hpp"
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
goldcraft::scenario::GuestProfile FamasWeapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {FamasHostId, GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior, GuestKill::StandardPlayerProcessing};
}

namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
constexpr unsigned OriginalId = 23;
constexpr char Ammunition[] = "556Nato";
constexpr std::uint64_t InventoryKind = 0x434f4646414d0001ULL;
}


struct FamasWeapon::InventoryState
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
    FamasStatus status{};
};
bool FamasWeapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
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
bool FamasWeapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
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
    const EHANDLE owner(&player);FamasBackends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found||!sameInventory()||
        (pending&&(!backend.ownerLife||pending->life!=backend.ownerLife)))return false;
    if(Bind(player,prepared,backend)!=FamasStatus::Ready)return false;
    const auto lease=lease_;AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    if(selected_&&!PublishOwnerState())
    {const auto result=status_;Invalidate();status_=result;return false;}
    return true;
}
bool FamasWeapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
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
bool FamasWeapon::PublishOwnerState() noexcept
{
    auto lease=lease_; lease.active=true;
    if (!selected_ || !Current(&lease)) return false;
    FamasEvent event{FamasEffect::Aim};
    const bool aimReady=prepared_.aimMetadataReady && Finite(prepared_.aimOffset) &&
        prepared_.aimZoom>=0 && prepared_.aimZoom<=255;
    event.enabled=action_.aiming && aimReady;
    event.first=event.enabled ? prepared_.aimOffset : Vector{};
    event.value=event.enabled ? prepared_.aimZoom : 0;
    const bool aim=Emit(event);
    if (!Current(&lease)) return false;
    event={FamasEffect::CameraZoom}; event.a=350;
    const bool zoom=Emit(event);
    if (!Current(&lease)) return false;
    lease.player->pev->fov=action_.aiming && aimReady ? float(prepared_.aimZoom) : 90.F;
    if (!aim || !zoom || (action_.aiming && !aimReady)) status_=FamasStatus::AppliedWithoutPresentation;
    return true;
}


void FamasWeapon::OwnerModelChanging() noexcept
{
    FamasOwnerFrame frame{};
    if (!Check(frame, false) || !ResourcesReady() || !backends_.ownerLife ||
        selected_ != (m_pPlayer->m_pActiveItem == this)) { Invalidate(); return; }
    const ModelRebind pending{lease_.owner, worldLease_.context, backends_.ownerLife,
        lease_.ownerSerial, lease_.weaponSerial, m_pPlayer->m_iNumSpawns, selected_};
    Invalidate(); modelRebind_ = pending;
}

bool FamasWeapon::Current(void* context) noexcept
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

bool FamasWeapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool FamasWeapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void FamasWeapon::ConfigureColdHost(FamasColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
FamasStatus FamasWeapon::Prepare(const FamasPrepared& p) noexcept
{
    preparedReady_=false; status_=FamasStatus::MissingBackend;
    if (!p.modelsReady || !p.inventoryReady || p.world!=MeleeWorld::Multiplayer ||
        p.worldModelString<=0 || p.inventoryItemString<=0 || p.viewModelString<=0)
        return status_;
    if (p.hostSlot!=FamasHudSlot || p.hostPosition!=FamasHudPosition ||
        p.csPenetration<0 || p.csPenetration>32 || (p.csPenetration && p.csPenetration<3))
        return status_=FamasStatus::InvalidInput;
    // Individual actions validate the metadata/timing they actually consume.
    // An unavailable idle animation must not revoke the firearm's inventory.
    prepared_=p; m_iShellId=p.shellModel; preparedReady_=true;
    return status_=FamasStatus::Ready;
}
void FamasWeapon::Precache()
{
    preparedReady_ = false;
    status_ = FamasStatus::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    FamasPrepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

FamasStatus FamasWeapon::Bind(CBasePlayer& owner, const FamasPrepared& p,
                          const FamasBackends& backends) noexcept
{
    bound_=selected_=false; modelRebind_.reset(); status_=FamasStatus::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame) return status_;
    if (Prepare(p)!=FamasStatus::Ready) return status_;
    if (m_iPrimaryAmmoType<0 || m_iPrimaryAmmoType>=MAX_AMMO_SLOTS)
        return status_=FamasStatus::InvalidInput;
    const Lease l{owner.edict(),edict(),&owner,this,owner.edict()->serialnumber,
        edict()->serialnumber,owner.pev->modelindex,backends.context,backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_=FamasStatus::Cancelled;
    lease_=l; backends_=backends; m_pPlayer=&owner; m_iId=FamasHostId; bound_=true;
    return status_=FamasStatus::Ready;
}

void FamasWeapon::Spawn()
{
    modelRebind_.reset();
    action_ = {};
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 30;
    spreadIncrement_ = .15F;
    static_assert(std::bit_cast<std::uint32_t>(.15F) == 1041865114U);
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
    if (!preparedReady_ || !m_pEntity || !g_engfuncs.pfnSetModel ||
        !g_engfuncs.pfnSetOrigin || !g_engfuncs.pfnSetSize)
    { status_ = FamasStatus::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = FamasStatus::InvalidInput; return; }
    m_iId = FamasHostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 30;
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
    SetUse(&FamasWeapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&FamasWeapon::WorldTouch);
    SetThink(&FamasWeapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = FamasStatus::Applied;
}

int FamasWeapon::GetItemInfo(ItemInfo* info)
{
    // Original1003ADA0 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_famas";
    info->pszAmmo1 = Ammunition;
    info->iMaxAmmo1 = 150;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 30;
    info->iId = m_iId = FamasHostId;
    info->iSlot = FamasHudSlot;
    info->iPosition = FamasHudPosition;
    info->iWeight = 5;
    return 1;
}

int FamasWeapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void FamasWeapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void FamasWeapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&FamasWeapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&FamasWeapon::WorldTouch);
}
void FamasWeapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void FamasWeapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = FamasStatus::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = FamasStatus::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        FamasEvent event{FamasEffect::PickupSound};
        event.a = pev->armorvalue;
        Emit(event);
        if (!current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int FamasWeapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    FamasBackends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != FamasStatus::Ready) return FALSE;
    FamasOwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = FamasStatus::Ineligible; return FALSE; }
    return TRUE;
}
int FamasWeapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == FamasStatus::Ready || status_ == FamasStatus::Applied || status_ == FamasStatus::AppliedWithoutPresentation);
}
int FamasWeapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int FamasWeapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<FamasWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    FamasOwnerFrame frame{};
    if (!target->Check(frame, false) || (frame.reserveLimit!=90 && frame.reserveLimit!=150)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = FamasStatus::UnsupportedState; return FALSE; }
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
    const int ammo = owner.player->GiveAmmo(amount, Ammunition, frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        target->Emit({FamasEffect::AmmoPickupSound});
    if (!current()) return FALSE;
    return TRUE;
}
int FamasWeapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<FamasWeapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    FamasOwnerFrame frame{};
    if (!target->Check(frame, false) || (frame.reserveLimit!=90 && frame.reserveLimit!=150) ||
        (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, Ammunition, frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void FamasWeapon::AttachToPlayer(CBasePlayer* player)
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
void FamasWeapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void FamasWeapon::Drop() { RetireWorld(false); }
void FamasWeapon::Kill() { RetireWorld(true); }
void FamasWeapon::WeaponReloadCompleted(int)
{
    // This class owns absolute-time completion, including ID23's free refill.
    // Restore its pending flag after an unsolicited native completion callback;
    // never invalidate a firearm or apply the transfer for a second time.
    m_fInReload=action_.loading;
}

bool FamasWeapon::EnsureBinding() noexcept
{
    Lease l=lease_;
    if (bound_ && Current(&l)) return true;
    // Host recovery only for the same real inventory entity. Authenticate the
    // new owner life with the cold provider; never reuse the retired backend.
    const auto world=worldLease_;
    if (!WorldCurrent(world) || !pev || !m_pPlayer || !pev->owner ||
        pev->owner->free || pev->owner->pvPrivateData!=m_pPlayer ||
        m_pPlayer->pev!=&pev->owner->v || m_pPlayer->pev->deadflag!=DEAD_NO ||
        m_pPlayer->pev->health<=0)
    {status_=FamasStatus::MissingBackend; return false;}
    // Match the actual inventory object, rather than SDK HasPlayerItem's
    // classname comparison: a different same-class item does not own this one.
    for (auto* item=m_pPlayer->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]; item; item=item->m_pNext)
        if (item==this) return RestoreInventoryBinding(*m_pPlayer);
    status_=FamasStatus::MissingBackend; return false;
}

bool FamasWeapon::Check(FamasOwnerFrame& frame, bool active) noexcept
{
    if (!EnsureBinding()) return false;
    Lease l=lease_; l.active=active;
    if (!Current(&l)) {status_=FamasStatus::Cancelled; return false;}
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action<0 ||
        (action_.action>2 && action_.action!=5))
    {status_=FamasStatus::UnsupportedState; return false;}
    if (!backends_.ownerFrame(backends_.context,frame) || !Current(&l))
    {status_=Current(&l)?FamasStatus::MissingBackend:FamasStatus::Cancelled; return false;}
    if (m_iPrimaryAmmoType<0 || m_iPrimaryAmmoType>=MAX_AMMO_SLOTS)
    {status_=FamasStatus::InvalidInput; return false;}
    ReadAmmo();
    if ((action_.clip<0 && (active || action_.clip!=-666)) || action_.reserve<0)
    {status_=FamasStatus::InvalidInput; return false;}
    status_=FamasStatus::Ready; return true;
}

bool FamasWeapon::Emit(FamasEvent event) noexcept
{
    Lease l=lease_; l.active=selected_;
    if (!Current(&l)) return false;
    const auto effect=backends_.effect;
    const bool delivered=effect && effect(l.context,*l.player,*this,event);
    if (!Current(&l)) return false;
    if (!delivered) status_=FamasStatus::AppliedWithoutPresentation;
    return delivered;
}
bool FamasWeapon::Animate(PlayerAction action) noexcept
{
    Lease l=lease_; l.active=selected_;
    if (!Current(&l) || !backends_.playerAnimation) return false;
    const auto result=backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration,LongGun::Rifle,action,gpGlobals->time);
    return Current(&l) && result!=PlayerAnimationResult::Rejected;
}
bool FamasWeapon::View(int sequence) noexcept
{
    Lease lease = lease_; lease.active = selected_;
    if (!Current(&lease)) return false;
    m_pPlayer->pev->weaponanim = sequence; // original1011F5D0, before transport
    FamasEvent event{FamasEffect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void FamasWeapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void FamasWeapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
void FamasWeapon::MirrorTimers(bool selected, bool ownerGate) noexcept
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
BOOL FamasWeapon::CanDeploy()
{
    return EnsureBinding() && m_pPlayer->pev->movetype!=5;
}
BOOL FamasWeapon::Deploy()
{
    if (!EnsureBinding()) return FALSE;
    ReadAmmo(); action_.action=0; action_.special=action_.special==666;
    if (action_.clip==-666) {action_.clip=0; WriteAmmo();}
    Lease lease=lease_;
    bool partial=false, first=false;
    const bool known=backends_.firstEquip && backends_.firstEquip(backends_.context,first);
    if (!Current(&lease)) return FALSE;
    if (known && first)
    {
        FamasEvent sound{FamasEffect::FirstDrawSound};
        sound.a=1; sound.b=.8F; sound.value=2; sound.other=100;
        partial=!Emit(sound);
        if (!Current(&lease)) return FALSE;
        partial=!Emit({FamasEffect::FirstDrawSubtitle}) || partial;
        if (!Current(&lease)) return FALSE;
    }
    if (!CanDeploy()) return FALSE;
    selected_=true; lease.active=true;
    partial=!Emit({FamasEffect::DeployPrelude}) || partial;
    if (!Current(&lease)) return FALSE;
    action_.aiming=false; spread_.scalar=0; spread_.recoveryDeadline=gpGlobals->time;
    partial=!Emit({FamasEffect::DeployReset}) || partial;
    if (!Current(&lease)) return FALSE;
    if (reserveHud_!=action_.reserve)
    {
        FamasEvent event{FamasEffect::ReserveHud}; event.value=action_.reserve;
        const bool delivered=Emit(event); partial=!delivered || partial;
        if (!Current(&lease)) return FALSE;
        if (delivered) reserveHud_=action_.reserve;
    }
    if (g_engfuncs.pfnRandomLong)
    {
        const int pitch=g_engfuncs.pfnRandomLong(-5,5)+100;
        if (!Current(&lease)) return FALSE;
        if (pev->armorvalue!=1.F)
        {
            const int sleeve=g_engfuncs.pfnRandomLong(0,2);
            if (!Current(&lease)) return FALSE;
            FamasEvent event{FamasEffect::Sleeve}; event.value=sleeve; event.other=pitch;
            event.a=1; event.b=.8F;
            partial=!Emit(event) || partial;
            if (!Current(&lease)) return FALSE;
        }
        else pev->armorvalue=0;
    }
    else {partial=true; if (pev->armorvalue==1.F) pev->armorvalue=0;}
    partial=!Emit({FamasEffect::InventoryChanged}) || partial;
    if (!Current(&lease)) return FALSE;
    m_pPlayer->pev->viewmodel=prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel=prepared_.playerModelString;
    // Authored CS/CSO upper-body pose; original CoF uses onehanded/null P.
    std::memcpy(m_pPlayer->m_szAnimExtention,"rifle",sizeof("rifle"));
    if (known)
    {
        partial=!View(first?2:1) || partial;
        if (!Current(&lease)) return FALSE;
    }
    else partial=true;
    if (g_engfuncs.pfnSetModel)
    {
        SET_MODEL(edict(),STRING(prepared_.viewModelString));
        if (!Current(&lease)) return FALSE;
    }
    else partial=true;
    float duration{};
    if (known && CurrentDuration(duration))
    {
        action_.ownerAttack=action_.primary=action_.secondary=duration+gpGlobals->time;
        action_.idle=duration+gpGlobals->time;
    }
    else partial=true;
    MirrorTimers(true);
    status_=partial?FamasStatus::AppliedWithoutPresentation:FamasStatus::Applied;
    return TRUE;
}
void FamasWeapon::Holster(int)
{
    if (!EnsureBinding()) return;
    Lease lease=lease_;
    const bool view=View(3);
    if (!Current(&lease)) return;
    action_.ownerAttack=AddDouble(gpGlobals->time,.5);
    // Host adaptation releases shared CS presentation for the next weapon.
    // The original aim bit is retained until FAMAS's next DefaultDeploy.
    const bool presentation=Emit({FamasEffect::HolsterPresentation});
    if (!Current(&lease)) return;
    m_pPlayer->pev->fov=90.F;
    selected_=false; MirrorTimers(false);
    status_=view && presentation?FamasStatus::Applied:FamasStatus::AppliedWithoutPresentation;
}

FamasControlHost FamasWeapon::ControlHost(ActionBridge& bridge) noexcept {
    return {&bridge, [](void* p) noexcept {
        return Current(&static_cast<ActionBridge*>(p)->lease);
    }, [](void*) noexcept { return gpGlobals->time; }, ControlEvent};
}

bool FamasWeapon::ControlEvent(void* context, const FamasControlEvent& event) noexcept
{
    auto& bridge=*static_cast<ActionBridge*>(context); auto& self=*bridge.item;
    self.WriteAmmo(); self.MirrorTimers(self.selected_);
    switch (event.kind)
    {
    case FamasControlEventKind::SchedulePushback:
        return self.backends_.playerThink && self.backends_.playerThink->Schedule(*self.m_pPlayer,
            PlayerThinkKind::Pushback,event.delay,self.backends_.playerModelGeneration.mapEpoch,
            self.backends_.ownerLife,FamasHostId);
    case FamasControlEventKind::ViewAnimation:return self.View(event.argument);
    }
    return false;
}
void FamasWeapon::SetResult(FamasControlResult result) noexcept
{
    switch (result)
    {
    case FamasControlResult::Applied:
    case FamasControlResult::Deferred:status_=FamasStatus::Applied;break;
    case FamasControlResult::AppliedWithoutView:status_=FamasStatus::AppliedWithoutPresentation;break;
    case FamasControlResult::Ineligible:status_=FamasStatus::Ineligible;break;
    case FamasControlResult::Cancelled:status_=FamasStatus::Cancelled;break;
    case FamasControlResult::InvalidInput:status_=FamasStatus::InvalidInput;break;
    default:status_=FamasStatus::MissingBackend;break;
    }
}
void FamasWeapon::Reload()
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    ActionBridge bridge{this, lease_}; bridge.lease.active = true;
    const auto result = StartFamasMagazineReload(action_, prepared_.sequences, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void FamasWeapon::SecondaryAttack()
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    if (!frame.stamina || !backends_.playerThink ||
        !backends_.playerThink->PreparedFor(*m_pPlayer,PlayerThinkKind::Pushback,
            backends_.playerModelGeneration.mapEpoch,backends_.ownerLife))
    {status_=FamasStatus::Ineligible; return;}
    ActionBridge bridge{this,lease_}; bridge.lease.active=true;
    const auto result=StartFamasPushback(action_,*frame.stamina,ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}

bool FamasWeapon::CurrentDuration(float& duration) noexcept
{
    const int sequence=m_pPlayer->pev->weaponanim;
    if (sequence>=24) {duration=0; return true;}
    if (sequence<0 || !(prepared_.sequences.present & (std::uint64_t{1}<<sequence))) return false;
    duration=prepared_.sequences.duration[sequence];
    return std::isfinite(duration) && duration>=0;
}
bool FamasWeapon::SetAim(bool enabled, bool input) noexcept
{
    if (!prepared_.aimMetadataReady) {status_=FamasStatus::MissingBackend; return false;}
    struct Bridge { FamasWeapon& item; Lease lease; } bridge{*this, lease_};
    bridge.lease.active = true;
    const FamasAimHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, const FamasAimEvent& value) noexcept
        {
            auto& self = static_cast<Bridge*>(p)->item;
            self.WriteAmmo(); self.MirrorTimers(true, false);
            FamasEvent event{FamasEffect::Aim}; event.first = {value.offset[0], value.offset[1], value.offset[2]};
            event.enabled = value.enabled; event.value = value.value;
            switch (value.kind)
            {
            case FamasAimEventKind::Presentation: return self.Emit(event);
            case FamasAimEventKind::CameraZoom:
                event = {FamasEffect::CameraZoom}; event.a = value.amount; return self.Emit(event);
            case FamasAimEventKind::PlayerFov: self.m_pPlayer->pev->fov = value.amount; return true;
            case FamasAimEventKind::ViewAnimation: return self.View(value.value);
            }
            return false;
        },
        [](void* p, float& duration) noexcept { return static_cast<Bridge*>(p)->item.CurrentDuration(duration); }};
    const FamasAimMetadata metadata{{prepared_.aimOffset.x, prepared_.aimOffset.y, prepared_.aimOffset.z}, prepared_.aimZoom};
    FamasOwnerFrame frame{};
    if (input && !Check(frame)) return false;
    const auto result = input ? ToggleFamasAim(action_, (frame.pressed & 0x40U) != 0, frame.toggleAim, metadata, host) :
        ApplyFamasAim(action_, enabled, metadata, host);
    if (!Current(&bridge.lease)) return false;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
    return result == FamasControlResult::Applied || result == FamasControlResult::AppliedWithoutView ||
        result == FamasControlResult::Ineligible;
}

void FamasWeapon::PrimaryAttack()
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    if (!action_.action && !action_.special && action_.clip>0 && m_pPlayer->pev->waterlevel!=3 &&
        (!g_engfuncs.pfnMakeVectors || !g_engfuncs.pfnRandomLong || !backends_.recoil))
    {status_=FamasStatus::MissingBackend; return;}
    PistolBallistics shot{{},{1,0,0},{spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0},{0,0,1},frame.randomSeed,frame.bulletDamage,action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers,frame.sourceMode};
    Lease lease = lease_; lease.active = true;
    if (!action_.action && !action_.special && action_.clip>0 && m_pPlayer->pev->waterlevel!=3)
    {
        const auto ready=prepared_.csPenetration?
            CheckCSRifleBallistics(shot,{Current,&lease},prepared_.csPenetration,CSRifleMaterial::Rifle556):
            CheckPistolBallistics(shot,prepared_.ballistics,{Current,&lease});
        if (ready!=BallisticsStatus::Ready) {status_=FamasStatus::MissingBackend; return;}
    }
    FamasShotState state{action_,spread_,spreadIncrement_,emptyDeadline_};
    struct Bridge
    {
        FamasWeapon& item; FamasShotState& state; Lease lease; PistolBallistics shot;
        void Sync() noexcept
        {
            item.action_ = state.action;
            item.spread_ = state.spread; item.spreadIncrement_ = state.spreadIncrement;
            item.emptyDeadline_ = state.emptyDeadline;
            item.WriteAmmo(); item.MirrorTimers(item.selected_,false);
        }
        static Vector V(const FamasVector& v) noexcept { return {v[0],v[1],v[2]}; }
        static FamasVector A(const Vector& v) noexcept { return {v.x,v.y,v.z}; }
    } bridge{*this,state,lease,shot};
    FamasShotFrame request{};
    request.damage=float(frame.bulletDamage);request.waterLevel=m_pPlayer->pev->waterlevel;
    request.shellModel=prepared_.shellModel;request.weaponEntity=g_engfuncs.pfnIndexOfEdict?g_engfuncs.pfnIndexOfEdict(lease.weapon):0;
    if(!Current(&lease))return;
    request.viewModel=prepared_.viewModelString;request.ownerFlags=frame.originalFlags;
    request.randomSeed=frame.randomSeed;request.eventOwner2814=frame.eventOwner2814;
    request.fireOnEmpty=m_fFireOnEmpty!=0;request.infiniteAmmo=frame.infiniteAmmo;
    request.weaponOrigin=Bridge::A(pev->origin);request.weaponViewOffset=Bridge::A(pev->view_ofs);
    request.ownerOrigin=Bridge::A(m_pPlayer->pev->origin);request.viewAngles=Bridge::A(m_pPlayer->pev->v_angle);
    request.weaponVelocityY=pev->velocity.y;
    request.initialBasis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};
    const FamasShotHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p,const FamasShotEvent& value) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            FamasEvent event{FamasEffect::EmptyClick};event.first=Bridge::V(value.first);event.second=Bridge::V(value.second);
            event.a=value.a;event.b=value.b;event.c=value.c;event.value=value.value;event.other=value.other;
            switch(value.kind)
            {
            case FamasShotEffect::Shell:event.kind=FamasEffect::Shell;break;
            case FamasShotEffect::Gunsmoke:
                event.kind=FamasEffect::Gunsmoke;event.enabled=true;event.modelString=value.other;event.other=1;break;
            case FamasShotEffect::FireEvent:event.kind=FamasEffect::FireEvent;break;
            case FamasShotEffect::Shake:event.kind=FamasEffect::Shake;break;
            case FamasShotEffect::ViewAnimation:return b.item.View(value.value);
            case FamasShotEffect::PlayerMove:return b.item.Animate(PlayerAction::Move);
            case FamasShotEffect::MuzzleFlash:b.item.m_pPlayer->pev->effects|=EF_MUZZLEFLASH;return true;
            case FamasShotEffect::ShotState:event.kind=FamasEffect::ShotState;break;
            case FamasShotEffect::ShotLatch:event.kind=FamasEffect::ShotLatch;break;
            case FamasShotEffect::EmptyClick:break;
            }
            return b.item.Emit(event);
        },
        [](void* p) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();b.item.Reload();
            if(!Current(&b.lease))return false;
            b.state.action=b.item.action_;
            return b.item.status_==FamasStatus::Applied||b.item.status_==FamasStatus::AppliedWithoutPresentation||b.item.status_==FamasStatus::Ineligible;
        },
        [](void* p,const FamasVector& angles,FamasBasis& basis) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();g_engfuncs.pfnMakeVectors(Bridge::V(angles));
            if(!Current(&b.lease))return false;
            basis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};return true;
        },
        [](void* p,FamasVector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->GetGunPosition());
            return Current(&b.lease);
        },
        [](void* p,const FamasVector& recoil) noexcept
        {
            auto& b=*static_cast<Bridge*>(p); b.Sync();
            const auto consumer=b.item.backends_.recoil;
            if (!consumer) return false;
            const bool applied=consumer(b.item.backends_.context,*b.item.m_pPlayer,Bridge::V(recoil));
            return Current(&b.lease) && applied;
        },
        [](void* p,FamasVector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            out=Bridge::A(b.item.m_pPlayer->pev->v_angle+b.item.m_pPlayer->pev->punchangle);return Current(&b.lease);
        },
        [](void* p,float,FamasVector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);out=Bridge::A(gpGlobals->v_forward);return Current(&b.lease);
        },
        [](void* p,int low,int high,int& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=g_engfuncs.pfnRandomLong(low,high);return Current(&b.lease);
        },
        [](void* p,const FamasBullet& bullet) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();auto& shot=b.shot;
            shot.source=Bridge::V(bullet.source);shot.direction=Bridge::V(bullet.direction);shot.spread=Bridge::V(bullet.spread);
            shot.right=gpGlobals->v_right;shot.up=gpGlobals->v_up;shot.seed=bullet.seed;shot.damage=bullet.damage;
            const auto result=b.item.prepared_.csPenetration?
                FireCSRifleBallistics(*b.item.m_pPlayer,shot,b.item.prepared_.ballistics,{Current,&b.lease},
                    b.item.prepared_.csPenetration,CSRifleMaterial::Rifle556):
                FirePistolBallistics(*b.item.m_pPlayer,shot,b.item.prepared_.ballistics,{Current,&b.lease});
            return Current(&b.lease)&&result.status==BallisticsStatus::Fired;
        },
        [](void* p,FamasVector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->EyePosition());return Current(&b.lease);
        },
        [](void* p,const FamasVector& position) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();FamasEvent e{FamasEffect::FireSound};
            e.first=Bridge::V(position);e.value=0;e.other=1;e.enabled=true;return b.item.Emit(e);
        }};
    const auto result=FamasPrimaryAttack(state,request,host);
    if(!Current(&lease))return;
    bridge.Sync();
    switch(result)
    {
    case FamasShotResult::Fired:
    case FamasShotResult::Empty:
    case FamasShotResult::ReloadRequested:status_=FamasStatus::Applied;break;
    case FamasShotResult::FiredWithoutPresentation:
    case FamasShotResult::EmptyWithoutPresentation:status_=FamasStatus::AppliedWithoutPresentation;break;
    case FamasShotResult::Ineligible:status_=FamasStatus::Ineligible;break;
    case FamasShotResult::Cancelled:status_=FamasStatus::Cancelled;break;
    case FamasShotResult::InvalidInput:status_=FamasStatus::InvalidInput;break;
    default:status_=FamasStatus::MissingBackend;break;
    }
}

void FamasWeapon::AdvanceOwnerFrame() noexcept
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

void FamasWeapon::Fidget() noexcept
{
    const float now=gpGlobals->time;
    if (action_.primary>now || action_.secondary>now || action_.idle>now ||
        action_.action || action_.special) return;
    if (!g_engfuncs.pfnRandomLong) {status_=FamasStatus::AppliedWithoutPresentation; return;}
    Lease lease=lease_; lease.active=true;
    const int chance=g_engfuncs.pfnRandomLong(0,599);
    if (!Current(&lease) || chance!=50 || action_.aiming) return;
    const int choice=g_engfuncs.pfnRandomLong(0,2);
    if (!Current(&lease)) return;
    if (choice>=0 && choice<=2) View(12+choice);
}

void FamasWeapon::ItemPostFrame()
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    Lease lease=lease_; lease.active=true;
    if (gpGlobals->time<action_.ownerAttack) {status_=FamasStatus::Ineligible; return;}
    struct Bridge {FamasWeapon& item; Lease lease; bool partial{};} bridge{*this,lease};
    const FamasFrameHost host{&bridge,
        [](void* p) noexcept {return Current(&static_cast<Bridge*>(p)->lease);},
        [](void*) noexcept {return gpGlobals->time;},
        [](void* p,FamasFrameOwner& output) noexcept
        {
            auto& b=*static_cast<Bridge*>(p); FamasOwnerFrame owner;
            if (!b.item.backends_.ownerFrame(b.item.backends_.context,owner) || !Current(&b.lease)) return false;
            const auto& velocity=b.item.m_pPlayer->pev->velocity;
            output={velocity.x,velocity.y,owner.pendingGateTime,owner.originalFlags,
                owner.condition3AA,owner.pendingGate}; return true;
        },
        [](void* p,FamasFrameEvent event,int sequence) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);
            switch(event)
            {
            case FamasFrameEvent::ViewAnimation:return b.item.View(sequence);
            case FamasFrameEvent::ExitAim:return b.item.SetAim(false);
            case FamasFrameEvent::Reload:
                b.item.Reload();
                return Current(&b.lease) && (b.item.status_==FamasStatus::Applied ||
                    b.item.status_==FamasStatus::AppliedWithoutPresentation || b.item.status_==FamasStatus::Ineligible);
            }
            return false;
        },
        [](void* p,float& duration) noexcept {return static_cast<Bridge*>(p)->item.CurrentDuration(duration);}};
    const auto derived=AdvanceFamasDerivedFrame(action_,actionDeadline_,host);
    if (!Current(&lease)) return;
    bridge.partial=derived==FamasFrameResult::AppliedWithoutPresentation;
    if (derived!=FamasFrameResult::Applied && !bridge.partial)
    {
        status_=derived==FamasFrameResult::Cancelled?FamasStatus::Cancelled:
            derived==FamasFrameResult::UnsupportedState?FamasStatus::UnsupportedState:
            derived==FamasFrameResult::InvalidInput?FamasStatus::InvalidInput:FamasStatus::MissingBackend;
        return;
    }
    if (!Check(frame)) return;
    if (action_.clip>0) m_iPlayEmptySound=1;
    Fidget();
    if (!Current(&lease)) return;
    bridge.partial=bridge.partial || status_==FamasStatus::AppliedWithoutPresentation;
    if (gpGlobals->time>wearDeadline_)
    {wearDeadline_=AddDouble(gpGlobals->time,.75); if (wear_>0) --wear_;}
    if (wear_<0) wear_=0;
    if (wear_>wearMaximum_) wear_=wearMaximum_;
    if (reserveHud_!=action_.reserve)
    {
        FamasEvent event{FamasEffect::ReserveHud}; event.value=action_.reserve;
        const bool delivered=Emit(event);
        if (!Current(&lease)) return;
        if (delivered) reserveHud_=action_.reserve;
        else bridge.partial=true;
    }
    if (action_.loading)
    {
        MagazineReload reload{action_.clip,action_.reserve,action_.ownerAttack,true};
        const auto committed=CommitFamasReload(reload,30,0,gpGlobals->time);
        if (committed.status==ReloadCommitStatus::Invalid) {status_=FamasStatus::InvalidInput; return;}
        action_.clip=reload.clip; action_.reserve=reload.reserve; action_.loading=reload.loading;
        WriteAmmo();
    }
    // Shared original1011D9BC dispatcher; ID23 never enters ID17 auto-idle.
    const M16InputHost input{&bridge,
        [](void* p) noexcept {return Current(&static_cast<Bridge*>(p)->lease);},
        [](void*) noexcept {return gpGlobals->time;},
        [](void* p,M16InputOwner& output) noexcept
        {
            auto& b=*static_cast<Bridge*>(p); FamasOwnerFrame owner;
            if (!b.item.backends_.ownerFrame(b.item.backends_.context,owner) || !Current(&b.lease)) return false;
            output={static_cast<std::uint32_t>(b.item.m_pPlayer->pev->button),owner.activityBlock,
                b.item.m_pPlayer->pev->movetype}; return true;
        },
        [](void* p,M16InputEvent event) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);
            b.item.WriteAmmo(); b.item.MirrorTimers(true);
            switch(event)
            {
            case M16InputEvent::InventoryChanged:
                b.partial=!b.item.Emit({FamasEffect::InventoryChanged}) || b.partial;
                return Current(&b.lease);
            case M16InputEvent::Primary:b.item.PrimaryAttack();break;
            case M16InputEvent::Secondary:b.item.SecondaryAttack();break;
            case M16InputEvent::Reload:b.item.Reload();break;
            case M16InputEvent::Aim:
            {
                const bool applied=b.item.SetAim(false,true);
                b.partial=b.partial || b.item.status_==FamasStatus::AppliedWithoutPresentation;
                return applied;
            }
            }
            b.partial=b.partial || b.item.status_==FamasStatus::AppliedWithoutPresentation;
            return Current(&b.lease) && (b.item.status_==FamasStatus::Applied ||
                b.item.status_==FamasStatus::AppliedWithoutPresentation || b.item.status_==FamasStatus::Ineligible);
        }};
    const auto dispatched=DispatchM16FrameInput(action_,m_fFireOnEmpty,input);
    if (!Current(&lease)) return;
    status_=dispatched==M16ControlResult::Applied?
        (bridge.partial?FamasStatus::AppliedWithoutPresentation:FamasStatus::Applied):
        dispatched==M16ControlResult::Cancelled?FamasStatus::Cancelled:
        dispatched==M16ControlResult::InvalidInput?FamasStatus::InvalidInput:FamasStatus::MissingBackend;
    WriteAmmo(); MirrorTimers(true);
}
void FamasWeapon::WeaponIdle()
{
    FamasOwnerFrame frame{}; if (!Check(frame)) return;
    // Original100DFEA0 is only an unused pure forward copy here.
    if (action_.idle>gpGlobals->time || action_.action)
    {status_=FamasStatus::Ineligible; return;}
    Lease lease=lease_; lease.active=true;
    const bool delivered=View(action_.aiming?16:0);
    if (!Current(&lease)) return;
    float duration{};
    const bool timed=CurrentDuration(duration);
    if (timed) action_.idle=duration+gpGlobals->time;
    MirrorTimers(true);
    status_=delivered && timed?FamasStatus::Applied:FamasStatus::AppliedWithoutPresentation;
}
void FamasWeapon::Invalidate() noexcept
{
    modelRebind_.reset();
    bound_ = selected_ = false;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = FamasStatus::Cancelled;
}
}
