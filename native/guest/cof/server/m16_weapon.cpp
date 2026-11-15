// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "m16_weapon.hpp"
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
goldcraft::scenario::GuestProfile M16Weapon::ScenarioGuestProfile() const noexcept
{
    using namespace goldcraft::scenario;
    return {M16HostId, GuestMetadata::NoCsoItem,
        GuestImpulse::WeaponOwnsHitBehavior, GuestKill::StandardPlayerProcessing};
}

namespace
{
bool Finite(const Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
constexpr unsigned OriginalId = 9;
constexpr std::uint64_t InventoryKind = 0x434f464d31360000ULL;
}


struct M16Weapon::InventoryState
{
    MagazineActionState action;
    SpreadState spread;
    SpreadTransitions transitions;
    float actionDeadline{},emptyDeadline{},wearDeadline{},nextThink{};
    float burstDeadline{},meleeDeadline{},armor{},spreadIncrement{};
    bool burst{};
    int body{};
    float primary{},secondary{},idle{},ownerAttack{};
    int reserveHud{},wearMaximum{},wear{},clip{},inReload{},fireOnEmpty{};
    int ownerSerial{},weaponSerial{},modelIndex{},ownerSpawn{};
    std::uintptr_t map{},ownerContext{};
    decltype(CBaseEntity::m_pfnThink) think{};
    bool selected{},meleePending{},burstPending{},burstDispatch{};
    M16Status status{};
};
bool M16Weapon::SaveInventorySnapshot(goldcraft::inventory::Snapshot& output) const noexcept
{
    const auto lease=lease_;
    if(!bound_||!pev||m_pPlayer!=lease.player||!Current(const_cast<Lease*>(&lease)))return false;
    static_assert(std::is_trivially_copyable_v<InventoryState> && sizeof(InventoryState)<=512);
    InventoryState saved{};
    saved.action=action_;saved.spread=spread_;saved.transitions=transitions_;
    saved.actionDeadline=actionDeadline_;saved.emptyDeadline=emptyDeadline_;saved.wearDeadline=wearDeadline_;
    saved.burstDeadline=burstDeadline_;saved.meleeDeadline=meleeDeadline_;
    saved.burst=burst_;saved.spreadIncrement=spreadIncrement_;
    saved.body=pev->body;saved.armor=pev->armorvalue;
    saved.nextThink=pev->nextthink;saved.think=m_pfnThink;
    saved.primary=m_flNextPrimaryAttack;saved.secondary=m_flNextSecondaryAttack;saved.idle=m_flTimeWeaponIdle;
    saved.ownerAttack=m_pPlayer->m_flNextAttack;saved.clip=m_iClip;
    saved.inReload=m_fInReload;saved.fireOnEmpty=m_fFireOnEmpty;
    saved.reserveHud=reserveHud_;saved.wearMaximum=wearMaximum_;saved.wear=wear_;
    saved.ownerSerial=lease.ownerSerial;saved.weaponSerial=lease.weaponSerial;saved.modelIndex=lease.modelIndex;
    saved.ownerSpawn=m_pPlayer->m_iNumSpawns;saved.map=reinterpret_cast<std::uintptr_t>(worldLease_.context);
    saved.ownerContext=reinterpret_cast<std::uintptr_t>(lease.context);
    saved.selected=selected_;saved.meleePending=meleePending_;saved.burstPending=burstPending_;
    saved.burstDispatch=burstDispatch_;saved.status=status_;
    output.kind=InventoryKind;output.bytes=sizeof(saved);
    std::memcpy(output.data.data(),&saved,sizeof(saved));return true;
}
bool M16Weapon::RestoreInventoryBinding(CBasePlayer& player) noexcept
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
    const EHANDLE owner(&player);M16Backends backend{};
    const bool found=cold.ownerBackends(cold.context,player,backend);
    if(!WorldCurrent(world)||owner.Get<CBaseEntity>()!=&player||!found||!sameInventory()||
        (backends_.ownerLife&&backends_.ownerLife!=backend.ownerLife)||
        (pending&&(!backend.ownerLife||pending->life!=backend.ownerLife)))return false;
    if(Bind(player,prepared,backend)!=M16Status::Ready)return false;
    const auto lease=lease_;AttachToPlayer(&player);
    if(!Current(const_cast<Lease*>(&lease)))return false;
    selected_=player.m_pActiveItem==this;
    if(pev->owner!=player.edict())return false;
    if(selected_&&!PublishOwnerState())
    {const auto result=status_;Invalidate();status_=result;return false;}
    if(pending)
    {
        burstPending_=pending->burstPending;burstDeadline_=pending->burstDeadline;
        meleePending_=pending->meleePending;meleeDeadline_=pending->meleeDeadline;
        ScheduleThink();
    }
    return true;
}
bool M16Weapon::RestoreInventorySnapshot(const goldcraft::inventory::Snapshot& input) noexcept
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
    burstDeadline_=saved.burstDeadline;meleeDeadline_=saved.meleeDeadline;
    burst_=saved.burst;spreadIncrement_=saved.spreadIncrement;
    pev->body=saved.body;pev->armorvalue=saved.armor;
    m_pfnThink=saved.think;pev->nextthink=saved.nextThink;
    m_flNextPrimaryAttack=saved.primary;m_flNextSecondaryAttack=saved.secondary;m_flTimeWeaponIdle=saved.idle;
    if(saved.selected)m_pPlayer->m_flNextAttack=saved.ownerAttack;
    m_iClip=saved.clip;m_fInReload=saved.inReload;m_fFireOnEmpty=saved.fireOnEmpty;
    reserveHud_=saved.reserveHud;wearMaximum_=saved.wearMaximum;wear_=saved.wear;
    selected_=saved.selected;meleePending_=saved.meleePending;burstPending_=saved.burstPending;
    burstDispatch_=saved.burstDispatch;status_=saved.status;
    return !selected_||PublishOwnerState();
}
bool M16Weapon::PublishOwnerState() noexcept
{
    auto lease=lease_;lease.active=true;
    if(!selected_||!Current(&lease)||!prepared_.aimMetadataReady)return false;
    M16Event event{M16Effect::Aim};event.enabled=action_.aiming;
    event.first=action_.aiming?prepared_.aimOffset:Vector{};
    event.value=action_.aiming?prepared_.aimZoom:0;event.a=350;
    if(!Emit(event))return false;
    lease.player->pev->fov=action_.aiming?float(prepared_.aimZoom):90.0F;
    return true;
}


void M16Weapon::OwnerModelChanging() noexcept
{
    M16OwnerFrame frame{};
    const auto dispatch=static_cast<decltype(m_pfnThink)>(&M16Weapon::DispatchThink);
    if(!Check(frame,false)||!ResourcesReady()||!backends_.ownerLife||
        selected_!=(m_pPlayer->m_pActiveItem==this)||
        ((burstPending_||meleePending_)&&m_pfnThink!=dispatch)||
        !std::isfinite(burstDeadline_)||!std::isfinite(meleeDeadline_))
    {Invalidate();return;}
    const ModelRebind pending{lease_.owner,worldLease_.context,backends_.ownerLife,
        lease_.ownerSerial,lease_.weaponSerial,m_pPlayer->m_iNumSpawns,
        burstDeadline_,meleeDeadline_,selected_,burstPending_,meleePending_};
    Invalidate();modelRebind_=pending;
}

bool M16Weapon::Current(void* context) noexcept
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

bool M16Weapon::WorldCurrent(const WorldLease& l) noexcept
{
    return l.current && l.current(l.context) && l.entity && !l.entity->free &&
        l.entity->serialnumber == l.serial && l.entity->pvPrivateData == l.item &&
        !(l.entity->v.flags & FL_KILLME);
}
bool M16Weapon::ResourcesReady() const noexcept
{
    const auto l = worldLease_;
    return preparedReady_ && WorldCurrent(l);
}
void M16Weapon::ConfigureColdHost(M16ColdHost host) noexcept
{
    Invalidate();
    preparedReady_ = worldUse_ = false;
    cold_ = host;
    worldLease_ = pev ? WorldLease{edict(), this, edict()->serialnumber, host.context, host.current} : WorldLease{};
}
M16Status M16Weapon::Prepare(const M16Prepared& p) noexcept
{
    preparedReady_ = false;
    status_ = M16Status::MissingBackend;
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
        p.hostSlot != M16HudSlot || p.hostPosition != M16HudPosition)
        return status_ = M16Status::InvalidInput;
    for (int i = 0; i < 38; ++i)
        if (!(p.sequences.present & (std::uint64_t{1} << i)) ||
            !std::isfinite(p.sequences.duration[i]) || p.sequences.duration[i] < 0)
            return status_ = M16Status::InvalidInput;
    prepared_ = p;
    m_iShellId = p.shellModel;
    preparedReady_ = true;
    return status_ = M16Status::Ready;
}
void M16Weapon::Precache()
{
    preparedReady_ = false;
    status_ = M16Status::MissingBackend;
    const auto l = worldLease_;
    const auto host = cold_;
    if (!host.prepare || !host.ownerBackends || !host.inventoryPickup || !WorldCurrent(l)) return;
    M16Prepared p{};
    const bool ready = host.prepare(host.context, *this, p);
    if (!WorldCurrent(l)) return;
    if (ready) Prepare(p);
}

M16Status M16Weapon::Bind(CBasePlayer& owner, const M16Prepared& p,
                          const M16Backends& backends) noexcept
{
    bound_ = selected_ = meleePending_ = burstPending_ = burstDispatch_ = false;
    modelRebind_.reset();
    status_ = M16Status::MissingBackend;
    if (!pev || !owner.pev || !backends.current || !backends.ownerFrame || !backends.effect ||
        backends.preparedEffects != M16RequiredEffects || !backends.playerAnimation)
        return status_;
    if (Prepare(p) != M16Status::Ready) return status_;
    if (m_iPrimaryAmmoType < 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
        return status_ = M16Status::InvalidInput;
    status_ = M16Status::MissingBackend;
    for (unsigned i = 0; i < PlayerActionCount; ++i)
        for (bool crouch : {false, true})
            if (!backends.playerAnimation->Select(backends.playerModelGeneration, LongGun::M16,
                static_cast<PlayerAction>(i), crouch)) return status_;
    const Lease l{owner.edict(), edict(), &owner, this, owner.edict()->serialnumber,
        edict()->serialnumber, owner.pev->modelindex, backends.context, backends.current};
    if (!Current(const_cast<Lease*>(&l))) return status_ = M16Status::Cancelled;
    lease_ = l;
    prepared_ = p;
    backends_ = backends;
    m_pPlayer = &owner;
    // This project already routes extension IDs through CSOL_ItemInfo,
    // CSOL_LegacyWeaponMask/GetItemById and the active prediction slot. Keep its
    // ten-bit identity; never alias M16 to an unrelated stock CS weapon.
    m_iId = M16HostId;
    m_iShellId = p.shellModel;
    bound_ = true;
    return status_ = M16Status::Ready;
}

void M16Weapon::Spawn()
{
    modelRebind_.reset();
    action_ = {};
    spread_ = {};
    transitions_ = {};
    actionDeadline_ = emptyDeadline_ = wearDeadline_ = meleeDeadline_ = burstDeadline_ = 0;
    reserveHud_ = wearMaximum_ = wear_ = 0;
    m_iDefaultAmmo = 20;
    burst_ = true; spreadIncrement_ = .75F;
    m_iSecondaryAmmoType = -1;
    m_iClip = 0;
    m_fInReload = m_fFireOnEmpty = 0;
    bound_ = selected_ = meleePending_ = burstPending_ = burstDispatch_ = false;
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
    { status_ = M16Status::MissingBackend; return; }
    if (!std::isfinite(pev->health) || pev->health >= 2147483648.0f)
    { status_ = M16Status::InvalidInput; return; }
    m_iId = M16HostId;
    m_pPlayer = nullptr;
    m_iPrimaryAmmoType = 0;
    m_iDefaultAmmo = pev->health > 0 ? static_cast<int>(pev->health) : 20;
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
    SetUse(&M16Weapon::WorldUse);
    if (pev->iuser4 == 1) SetTouch(&M16Weapon::WorldTouch);
    SetThink(&M16Weapon::WorldFallThink);
    pev->nextthink = gpGlobals->time + .1f;
    CBasePlayerWeapon::Spawn();
    if (WorldCurrent(l)) status_ = M16Status::Applied;
}

int M16Weapon::GetItemInfo(ItemInfo* info)
{
    // Original10083940 is callable before any owner/resource/life binding.
    if (!info) return 0;
    *info = {};
    info->pszName = "weapon_cof_m16";
    info->pszAmmo1 = "cof_m16";
    info->iMaxAmmo1 = 200;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 20;
    info->iId = m_iId = M16HostId;
    info->iSlot = M16HudSlot;
    info->iPosition = M16HudPosition;
    info->iWeight = 5;
    return 1;
}

int M16Weapon::ObjectCaps()
{
    return CBasePlayerWeapon::ObjectCaps() | (worldUse_ ? FCAP_IMPULSE_USE : 0);
}
void M16Weapon::WorldFallThink()
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    pev->nextthink = gpGlobals->time + .1f;
    if (!(pev->flags & FL_ONGROUND)) return;
    pev->angles.x = pev->angles.z = 0;
    WorldMaterialize();
}
void M16Weapon::WorldMaterialize() noexcept
{
    const auto l = worldLease_;
    if (!preparedReady_ || !WorldCurrent(l)) return;
    if (pev->effects & EF_NODRAW)
    { pev->effects &= ~EF_NODRAW; pev->effects |= EF_MUZZLEFLASH; }
    pev->solid = SOLID_TRIGGER;
    UTIL_SetOrigin(pev, pev->origin);
    if (!WorldCurrent(l)) return;
    worldUse_ = true;
    SetUse(&M16Weapon::WorldUse);
    SetThink(nullptr);
    if (pev->iuser4 == 1) SetTouch(&M16Weapon::WorldTouch);
}
void M16Weapon::WorldUse(CBaseEntity* activator, CBaseEntity*, USE_TYPE, float)
{
    WorldTouch(activator);
}
void M16Weapon::WorldTouch(CBaseEntity* other)
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
    if (!inventory) { status_ = M16Status::Ineligible; return; }
    const bool added = player->AddPlayerItem(this) != FALSE;
    if (!current()) return;
    if (added)
    {
        if (m_pPlayer != player) { status_ = M16Status::Cancelled; return; }
        AttachToPlayer(player);
        if (!current()) return;
        // 1011C7D0: quiet global and weapon armorvalue suppress this sound.
        // The authenticated audio consumer owns the quiet-global snapshot.
        M16Event event{M16Effect::PickupSound};
        event.a = pev->armorvalue;
        if (!Emit(event) || !current()) return;
    }
    SUB_UseTargets(player, USE_TOGGLE, 0);
}
int M16Weapon::AddToPlayer(CBasePlayer* player)
{
    const auto l = worldLease_;
    const auto host = cold_;
    if (!preparedReady_ || !player || !host.ownerBackends || !WorldCurrent(l)) return FALSE;
    M16Backends backends{};
    auto* owner = player->edict();
    const int serial = owner->serialnumber;
    const bool found = host.ownerBackends(host.context, *player, backends);
    if (!WorldCurrent(l) || owner->free || owner->serialnumber != serial || owner->pvPrivateData != player) return FALSE;
    if (!found || Bind(*player, prepared_, backends) != M16Status::Ready) return FALSE;
    M16OwnerFrame frame{};
    if (!Check(frame, false) || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(pev->aiment) == 0x44268000u) return FALSE;
    const auto ownerLease = lease_;
    const bool added = CBasePlayerWeapon::AddToPlayer(player) != FALSE;
    if (!Current(const_cast<Lease*>(&ownerLease))) return FALSE;
    if (!added || m_iPrimaryAmmoType <= 0 || m_iPrimaryAmmoType >= MAX_AMMO_SLOTS)
    { bound_ = false; status_ = M16Status::Ineligible; return FALSE; }
    return TRUE;
}
int M16Weapon::AddWeapon()
{
    // Original ignores an ordinary full-ammo rejection. Missing consumers or
    // cancelled lifetimes must not flow into the SDK pickup-success message.
    ExtractAmmo(this);
    const auto l = lease_;
    return bound_ && Current(const_cast<Lease*>(&l)) &&
        (status_ == M16Status::Ready || status_ == M16Status::Applied);
}
int M16Weapon::AddDuplicate(CBasePlayerItem* original)
{
    const auto l = worldLease_;
    if (!preparedReady_ || !original || !WorldCurrent(l)) return FALSE;
    return CBasePlayerWeapon::AddDuplicate(original);
}
int M16Weapon::ExtractAmmo(CBasePlayerWeapon* weapon)
{
    // Original 1011D030 preserves the -666 empty-world sentinel/default ammo.
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<M16Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    M16OwnerFrame frame{};
    if (!target->Check(frame, false)) return FALSE;
    Lease owner = target->lease_;
    const auto current = [&]() { return WorldCurrent(source) && Current(&owner); };
    if (!current() || m_iDefaultAmmo < 0 ||
        reinterpret_cast<std::uintptr_t>(target->pev->aiment) == 0x44268000u)
    { status_ = M16Status::UnsupportedState; return FALSE; }
    // Original aiment was also used for the exceptional666 owner branch.
    // The normal host follow attachment has a real aiment edict, so only its
    // raw original numeric sentinel is unsupported, never dereferenced here.
    int amount = m_iDefaultAmmo;
    if (target->m_iClip == 0)
    {
        const int ready = amount < 20 ? amount : 20;
        target->m_iClip = ready;
        amount -= ready;
    }
    const int ammo = owner.player->GiveAmmo(amount, "cof_m16", frame.reserveLimit);
    if (!current()) return FALSE;
    m_iDefaultAmmo = 0;
    if (ammo <= 0) return FALSE;
    target->m_iPrimaryAmmoType = ammo;
    const bool alreadyOwned = owner.player->HasPlayerItem(target);
    if (!current()) return FALSE;
    if (alreadyOwned && target->pev->armorvalue != 1)
        if (!target->Emit({M16Effect::AmmoPickupSound})) return FALSE;
    return TRUE;
}
int M16Weapon::ExtractClipAmmo(CBasePlayerWeapon* weapon)
{
    if (weapon && weapon->m_iClip == -666) return TRUE;
    auto* target = dynamic_cast<M16Weapon*>(weapon);
    const auto source = worldLease_;
    if (!target || !preparedReady_ || !WorldCurrent(source)) return FALSE;
    M16OwnerFrame frame{};
    if (!target->Check(frame, false) || (m_iClip < 0 && m_iClip != -1)) return FALSE;
    Lease owner = target->lease_;
    const int result = owner.player->GiveAmmo(m_iClip == -1 ? 0 : m_iClip, "cof_m16", frame.reserveLimit);
    return WorldCurrent(source) && Current(&owner) ? result : FALSE;
}
void M16Weapon::AttachToPlayer(CBasePlayer* player)
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
void M16Weapon::RetireWorld(bool kill) noexcept
{
    Invalidate();
    worldUse_ = false;
    if (!pev) return;
    SetUse(nullptr); SetTouch(nullptr);
    if (kill) pev->aiment = nullptr;
    SetThink(&CBaseEntity::SUB_Remove);
    pev->nextthink = gpGlobals->time + .1f;
}
void M16Weapon::Drop() { RetireWorld(false); }
void M16Weapon::Kill() { RetireWorld(true); }
void M16Weapon::WeaponReloadCompleted(int)
{
    // Unexpected host default-reload completion is a lifecycle mismatch.
    Invalidate();
    status_ = M16Status::UnsupportedState;
}

bool M16Weapon::Check(M16OwnerFrame& frame, bool active) noexcept
{
    if (!bound_) { status_ = M16Status::MissingBackend; return false; }
    Lease l = lease_; l.active = active;
    if (!Current(const_cast<Lease*>(&l)) || (active &&
        (!selected_ || m_pPlayer->m_pActiveItem != this)))
    { status_ = M16Status::Cancelled; return false; }
    if (!std::isfinite(gpGlobals->time) || action_.special || action_.action < 0 ||
        (action_.action > 2 && action_.action != 5))
    { status_ = M16Status::UnsupportedState; return false; }
    if (!backends_.ownerFrame(backends_.context, frame) || !Current(&l)) return false;
    if (!std::isfinite(frame.activityBlock) || !std::isfinite(frame.pendingGateTime) ||
        frame.bulletDamage < 0 || !Finite(m_pPlayer->pev->velocity) ||
        (frame.reserveLimit != 60 && frame.reserveLimit != 200) || !std::isfinite(frame.stamina) ||
        !Finite(m_pPlayer->pev->v_angle) || !Finite(m_pPlayer->pev->punchangle))
    { status_ = M16Status::InvalidInput; return false; }
    ReadAmmo();
    // Carry metadata limits pickups, not trusted scenario ammo already owned.
    if ((action_.clip < 0 && (active || action_.clip != -666)) ||
        action_.reserve < 0)
    { status_ = M16Status::InvalidInput; return false; }
    status_ = M16Status::Ready;
    return true;
}

bool M16Weapon::Emit(M16Event event) noexcept
{
    Lease l = lease_; l.active = selected_;
    auto effect = backends_.effect;
    if (!Current(&l)) return false;
    const bool delivered = effect(l.context, *l.player, *this, event);
    if (!Current(&l)) return false;
    if (!delivered) status_ = M16Status::MissingBackend;
    return delivered;
}
bool M16Weapon::Animate(PlayerAction action) noexcept
{
    Lease l = lease_; l.active = selected_;
    if (!Current(&l)) return false;
    const auto result = backends_.playerAnimation->Apply(*l.player,
        backends_.playerModelGeneration, LongGun::M16, action, gpGlobals->time);
    return Current(const_cast<Lease*>(&l)) && result != PlayerAnimationResult::Rejected;
}
bool M16Weapon::View(int sequence) noexcept
{
    Lease lease = lease_; lease.active = selected_;
    if (!Current(&lease)) return false;
    m_pPlayer->pev->weaponanim = sequence; // original1011F5D0, before transport
    M16Event event{M16Effect::ViewAnimation}; event.value = sequence;
    return Emit(event);
}
void M16Weapon::ReadAmmo() noexcept
{
    action_.clip = m_iClip;
    action_.reserve = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
}
void M16Weapon::WriteAmmo() noexcept
{
    m_iClip = action_.clip;
    m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = action_.reserve;
    m_fInReload = action_.loading;
}
void M16Weapon::MirrorTimers(bool selected, bool ownerGate) noexcept
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
BOOL M16Weapon::CanDeploy()
{
    M16OwnerFrame frame{};
    return Check(frame, false) && m_pPlayer->pev->movetype != 5;
}
BOOL M16Weapon::Deploy()
{
    M16OwnerFrame frame{};
    if (!Check(frame, false) || m_pPlayer->pev->movetype == 5) return FALSE;
    selected_ = true;
    if (action_.clip == -666) { action_.clip = 0; WriteAmmo(); }
    action_.action = action_.burstShots = 0;
    if (!Emit({M16Effect::DeployPrelude})) return FALSE;
    action_.aiming = false;
    spread_.scalar = 0;
    spread_.recoveryDeadline = gpGlobals->time;
    if (!Emit({M16Effect::DeployReset})) return FALSE;
    if (reserveHud_ != action_.reserve)
    {
        M16Event e{M16Effect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return FALSE;
        reserveHud_ = action_.reserve;
    }
    // Original 1011C3FE consumes pitch RNG even when armorvalue suppresses
    // one sleeve. Keep RNG ordering before ammo tabulation/player models.
    Lease sleeveLease = lease_; sleeveLease.active = true;
    const int pitch = g_engfuncs.pfnRandomLong(-5, 5) + 100;
    if (!Current(&sleeveLease)) return FALSE;
    if (pev->armorvalue != 1.0F)
    {
        const int sleeve = g_engfuncs.pfnRandomLong(0, 2);
        if (!Current(&sleeveLease)) return FALSE;
        M16Event event{M16Effect::Sleeve}; event.value = sleeve; event.other = pitch;
        event.a = 1; event.b = .8f;
        if (!Emit(event)) return FALSE;
    }
    else pev->armorvalue = 0;
    if (!Emit({M16Effect::InventoryChanged})) return FALSE;
    m_pPlayer->pev->viewmodel = prepared_.viewModelString;
    m_pPlayer->pev->weaponmodel = prepared_.playerModelString;
    std::memcpy(m_pPlayer->m_szAnimExtention, "mp5", sizeof("mp5"));
    const int sequence = burst_ ? 7 : 0;
    if (!View(sequence)) return FALSE;
    Lease l = lease_; l.active = true;
    SET_MODEL(edict(), STRING(prepared_.viewModelString));
    if (!Current(const_cast<Lease*>(&l))) return FALSE;
    const float deadline = gpGlobals->time + prepared_.sequences.duration[sequence];
    action_.ownerAttack = action_.primary = action_.secondary = action_.idle = deadline;
    MirrorTimers(true);
    status_ = M16Status::Applied;
    return TRUE;
}
void M16Weapon::Holster(int)
{
    M16OwnerFrame frame{};
    if (!Check(frame, false)) return;
    // Original10050EC0 preserves ordinary weapon/player think and aiming.
    // The existing owned burst continues after switch under its life/map lease.
    if (!View(burst_ ? 12 : 5)) return;
    action_.ownerAttack = AddDouble(gpGlobals->time, .5);
    selected_ = false;
    MirrorTimers(false);
    status_ = M16Status::Applied;
}

M16ControlHost M16Weapon::ControlHost(ActionBridge& bridge) noexcept {
    return {&bridge, [](void* p) noexcept {
        return Current(&static_cast<ActionBridge*>(p)->lease);
    }, [](void*) noexcept { return gpGlobals->time; }, ControlEvent};
}

bool M16Weapon::ControlEvent(void* context, const guest::cof::M16ControlEvent& e) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    auto& control = *bridge.control;
    self.action_ = control.action; self.burst_ = control.burst;
    self.WriteAmmo(); self.MirrorTimers(self.selected_);
    switch (e.kind)
    {
    case M16ControlEventKind::Shoot:
        self.Shoot();
        if (!Current(&bridge.lease)) return false;
        control = {self.action_, self.burst_};
        return self.status_ == M16Status::Applied || self.status_ == M16Status::Ineligible;
    case M16ControlEventKind::Reload:
        self.Reload();
        if (!Current(&bridge.lease)) return false;
        control = {self.action_, self.burst_};
        return self.status_ == M16Status::Applied || self.status_ == M16Status::Ineligible;
    case M16ControlEventKind::EmptyClick: return self.Emit({M16Effect::EmptyClick});
    case M16ControlEventKind::ViewAnimation: return self.View(e.sequence);
    case M16ControlEventKind::PlayerReloadLoaded:
    case M16ControlEventKind::PlayerReloadEmpty: return self.Animate(PlayerAction::Reload);
    }
    return false;
}
void M16Weapon::SetResult(M16ControlResult result) noexcept
{
    status_ = result == M16ControlResult::Applied || result == M16ControlResult::Deferred ?
        M16Status::Applied : result == M16ControlResult::Ineligible ? M16Status::Ineligible :
        result == M16ControlResult::Cancelled ? M16Status::Cancelled :
        result == M16ControlResult::UnsupportedState ? M16Status::UnsupportedState :
        result == M16ControlResult::MissingHost || result == M16ControlResult::ConsumerRejected ?
        M16Status::MissingBackend : M16Status::InvalidInput;
}
void M16Weapon::Reload()
{
    M16OwnerFrame frame{}; if (!Check(frame, !burstDispatch_)) return;
    M16ControlState control{action_, burst_};
    ActionBridge bridge{this, lease_, &control}; bridge.lease.active = selected_;
    const auto result = StartM16MagazineReload(control, prepared_.sequences, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    action_ = control.action; burst_ = control.burst;
    WriteAmmo(); MirrorTimers(selected_); SetResult(result);
}
void M16Weapon::SecondaryAttack()
{
    M16OwnerFrame frame{}; if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.primary > now || action_.secondary > now || action_.action ||
        action_.special || action_.aiming || frame.stamina <= 0)
    { status_ = M16Status::Ineligible; return; }
    // Original100520BD schedules the owner's MeleePushBack at0.23F. The SDK
    // multiplexes that task with the weapon's independent burst think.
    meleePending_ = true; meleeDeadline_ = now + .23F; ScheduleThink();
    if (!View(22) || !Animate(PlayerAction::WeaponMelee)) return;
    action_.primary = action_.secondary = action_.idle = gpGlobals->time + 1.F;
    MirrorTimers(true); status_ = M16Status::Applied;
}
void M16Weapon::DelayedMelee()
{
    M16OwnerFrame frame{}; if (!Check(frame, false) || !meleePending_) return;
    if (gpGlobals->time < meleeDeadline_) { status_ = M16Status::Ineligible; return; }
    meleePending_ = false; ScheduleThink();
    if (frame.stamina <= 0) { status_ = M16Status::Ineligible; return; }
    // The mandatory original pushback consumer owns current stamina, sphere
    // targets and original damage semantics. Never call pistol WeaponMelee.
    if (Emit({M16Effect::LongGunPushback})) status_ = M16Status::Applied;
}

void M16Weapon::ScheduleThink() noexcept
{
    // Host adaptation: preserve the original independent player-melee and
    // weapon-burst deadlines through the single SDK weapon callback slot.
    if (!meleePending_ && !burstPending_)
    { SetThink(nullptr); pev->nextthink = 0; return; }
    SetThink(&M16Weapon::DispatchThink);
    pev->nextthink = !meleePending_ ? burstDeadline_ : !burstPending_ ? meleeDeadline_ :
        (meleeDeadline_ < burstDeadline_ ? meleeDeadline_ : burstDeadline_);
}
void M16Weapon::DispatchThink()
{
    M16OwnerFrame frame{}; if (!Check(frame, false)) return;
    const bool burstFirst = burstPending_ && (!meleePending_ || burstDeadline_ <= meleeDeadline_);
    if (burstFirst) ScheduledBurst(); else DelayedMelee();
    const auto l = lease_;
    if (!Current(const_cast<Lease*>(&l))) return;
    if (burstFirst) DelayedMelee(); else ScheduledBurst();
}
void M16Weapon::ScheduledBurst()
{
    M16OwnerFrame frame{}; if (!Check(frame, false) || !burstPending_) return;
    if (gpGlobals->time < burstDeadline_) { status_ = M16Status::Ineligible; return; }
    burstPending_ = false;
    ScheduleThink();
    Shoot();
}

bool M16Weapon::SetAim(bool enabled) noexcept
{
    // Original10052C50 toggle, pending reload and hold-aim release set the bit
    // before entering the original aim entry/exit consumer.
    action_.aiming = enabled;
    M16Event e{M16Effect::Aim}; e.enabled = enabled;
    e.first = enabled ? prepared_.aimOffset : Vector{};
    e.value = enabled ? prepared_.aimZoom : 0; e.a = 350;
    if (!Emit(e)) return false;
    m_pPlayer->pev->fov = enabled ? float(prepared_.aimZoom) : 90.0f;
    const int sequence = enabled ? 18 : 20;
    if (!View(sequence)) return false;
    action_.aiming = enabled;
    action_.primary = action_.secondary = action_.extra = AddDouble(gpGlobals->time, .3);
    action_.idle = gpGlobals->time + prepared_.sequences.duration[sequence];
    MirrorTimers(true);
    return true;
}

void M16Weapon::PrimaryAttack()
{
    M16OwnerFrame frame{}; if (!Check(frame)) return;
    M16ControlState control{action_, burst_};
    ActionBridge bridge{this, lease_, &control}; bridge.lease.active = true;
    const auto result = M16PrimaryTrigger(control, (frame.pressed & 1U) != 0,
        m_fFireOnEmpty != 0, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    action_ = control.action; burst_ = control.burst;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}
void M16Weapon::ToggleFireMode()
{
    M16OwnerFrame frame{}; if (!Check(frame)) return;
    M16ControlState control{action_, burst_};
    ActionBridge bridge{this, lease_, &control}; bridge.lease.active = true;
    const auto result = M16ToggleFireMode(control, ControlHost(bridge));
    if (!Current(&bridge.lease)) return;
    action_ = control.action; burst_ = control.burst;
    WriteAmmo(); MirrorTimers(true); SetResult(result);
}

void M16Weapon::Shoot()
{
    M16OwnerFrame frame{}; if (!Check(frame, false)) return;
    PistolBallistics shot{{},{1,0,0},{spread_.vector[0],spread_.vector[1],spread_.vector[2]},
        {0,1,0},{0,0,1},frame.randomSeed,frame.bulletDamage,action_.aiming,
        PistolPlayerHits::OriginalCooperativeSkipPlayers,frame.sourceMode};
    Lease lease = lease_; lease.active = selected_;
    if (!action_.action && !action_.special && action_.clip > 0 && m_pPlayer->pev->waterlevel != 3 &&
        CheckPistolBallistics(shot,prepared_.ballistics,{Current,&lease}) != BallisticsStatus::Ready)
    { status_ = M16Status::MissingBackend; return; }
    M16ShotState state{{action_,burst_},spread_,spreadIncrement_,emptyDeadline_,burstDeadline_,burstPending_};
    struct Bridge
    {
        M16Weapon& item; M16ShotState& state; Lease lease; PistolBallistics shot;
        void Sync() noexcept
        {
            item.action_ = state.control.action; item.burst_ = state.control.burst;
            item.spread_ = state.spread; item.spreadIncrement_ = state.spreadIncrement;
            item.emptyDeadline_ = state.emptyDeadline;
            item.WriteAmmo(); item.MirrorTimers(item.selected_,false);
        }
        static Vector V(const M16Vector& v) noexcept { return {v[0],v[1],v[2]}; }
        static M16Vector A(const Vector& v) noexcept { return {v.x,v.y,v.z}; }
    } bridge{*this,state,lease,shot};
    M16ShotFrame request{};
    request.damage=float(frame.bulletDamage);request.waterLevel=m_pPlayer->pev->waterlevel;
    request.shellModel=prepared_.shellModel;request.weaponEntity=g_engfuncs.pfnIndexOfEdict(lease.weapon);
    if(!Current(&lease))return;
    request.viewModel=prepared_.viewModelString;request.ownerFlags=frame.originalFlags;
    request.randomSeed=frame.randomSeed;request.eventOwner2814=frame.eventOwner2814;
    request.fireOnEmpty=m_fFireOnEmpty!=0;request.infiniteAmmo=frame.infiniteAmmo;
    request.weaponOrigin=Bridge::A(pev->origin);request.weaponViewOffset=Bridge::A(pev->view_ofs);
    request.ownerOrigin=Bridge::A(m_pPlayer->pev->origin);request.viewAngles=Bridge::A(m_pPlayer->pev->v_angle);
    request.weaponVelocityY=pev->velocity.y;
    request.initialBasis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};
    const M16ShotHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p,const M16ShotEvent& value) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            M16Event event{M16Effect::EmptyClick};event.first=Bridge::V(value.first);event.second=Bridge::V(value.second);
            event.a=value.a;event.b=value.b;event.c=value.c;event.value=value.value;event.other=value.other;
            switch(value.kind)
            {
            case M16ShotEffect::Shell:event.kind=M16Effect::Shell;break;
            case M16ShotEffect::Gunsmoke:
                event.kind=M16Effect::Gunsmoke;event.enabled=true;event.modelString=value.other;event.other=0;break;
            case M16ShotEffect::FireEvent:event.kind=M16Effect::FireEvent;break;
            case M16ShotEffect::PlayerFire:return b.item.Animate(PlayerAction::Fire);
            case M16ShotEffect::Recoil:event.kind=M16Effect::Recoil;break;
            case M16ShotEffect::Shake:event.kind=M16Effect::Shake;break;
            case M16ShotEffect::ViewAnimation:return b.item.View(value.value);
            case M16ShotEffect::PlayerMove:return b.item.Animate(PlayerAction::Move);
            case M16ShotEffect::MuzzleFlash:b.item.m_pPlayer->pev->effects|=EF_MUZZLEFLASH;return true;
            case M16ShotEffect::ShotState:event.kind=M16Effect::ShotState;break;
            case M16ShotEffect::ShotLatch:event.kind=M16Effect::ShotLatch;break;
            case M16ShotEffect::EmptyClick:break;
            case M16ShotEffect::ScheduleBurst:
                b.item.burstDeadline_=value.a;b.item.burstPending_=true;b.item.ScheduleThink();return true;
            }
            return b.item.Emit(event);
        },
        [](void* p) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();b.item.burstDispatch_=true;b.item.Reload();b.item.burstDispatch_=false;
            if(!Current(&b.lease))return false;
            b.state.control={b.item.action_,b.item.burst_};
            return b.item.status_==M16Status::Applied||b.item.status_==M16Status::Ineligible;
        },
        [](void* p,const M16Vector& angles,M16Basis& basis) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();g_engfuncs.pfnMakeVectors(Bridge::V(angles));
            if(!Current(&b.lease))return false;
            basis={Bridge::A(gpGlobals->v_forward),Bridge::A(gpGlobals->v_right),Bridge::A(gpGlobals->v_up)};return true;
        },
        [](void* p,M16Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->GetGunPosition());
            return Current(&b.lease);
        },
        [](void* p,M16Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();
            out=Bridge::A(b.item.m_pPlayer->pev->v_angle+b.item.m_pPlayer->pev->punchangle);return Current(&b.lease);
        },
        [](void* p,float,M16Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);out=Bridge::A(gpGlobals->v_forward);return Current(&b.lease);
        },
        [](void* p,int low,int high,int& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=g_engfuncs.pfnRandomLong(low,high);return Current(&b.lease);
        },
        [](void* p,const M16Bullet& bullet) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();auto& shot=b.shot;
            shot.source=Bridge::V(bullet.source);shot.direction=Bridge::V(bullet.direction);shot.spread=Bridge::V(bullet.spread);
            shot.right=gpGlobals->v_right;shot.up=gpGlobals->v_up;shot.seed=bullet.seed;shot.damage=bullet.damage;
            const auto result=FirePistolBallistics(*b.item.m_pPlayer,shot,b.item.prepared_.ballistics,{Current,&b.lease});
            return Current(&b.lease)&&result.status==BallisticsStatus::Fired;
        },
        [](void* p,M16Vector& out) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();out=Bridge::A(b.item.m_pPlayer->EyePosition());return Current(&b.lease);
        },
        [](void* p,const M16Vector& position) noexcept
        {
            auto& b=*static_cast<Bridge*>(p);b.Sync();M16Event e{M16Effect::FireSound};
            e.first=Bridge::V(position);e.value=0;e.other=1;e.enabled=true;return b.item.Emit(e);
        }};
    const auto result=M16Shoot(state,request,host);
    if(!Current(&lease))return;
    bridge.Sync();burstDeadline_=state.burstDeadline;burstPending_=state.burstPending;
    status_=result==M16ShotResult::Fired||result==M16ShotResult::Empty||result==M16ShotResult::ReloadRequested?
        M16Status::Applied:result==M16ShotResult::Ineligible?M16Status::Ineligible:
        result==M16ShotResult::Cancelled?M16Status::Cancelled:result==M16ShotResult::InvalidInput?
        M16Status::InvalidInput:M16Status::MissingBackend;
}

void M16Weapon::ReloadCommitted(void* context, const MagazineReload& state) noexcept
{
    auto& bridge = *static_cast<ActionBridge*>(context);
    auto& self = *bridge.item;
    self.action_.clip = state.clip; self.action_.reserve = state.reserve;
    self.WriteAmmo();
    bridge.callbackFailed = !self.Emit({M16Effect::InventoryChanged});
}
void M16Weapon::AdvanceOwnerFrame() noexcept
{
    M16OwnerFrame frame{}; if (!Check(frame)) return;
    UpdateSpread(spread_, transitions_, {m_pPlayer->pev->velocity.x, m_pPlayer->pev->velocity.y,
        gpGlobals->time, frame.originalFlags, OriginalId, true});
}

void M16Weapon::ItemPostFrame()
{
    M16OwnerFrame frame{}; if (!Check(frame)) return;
    Lease l = lease_; l.active = true;
    if (gpGlobals->time < action_.ownerAttack) { status_ = M16Status::Ineligible; return; }
    struct Bridge { M16Weapon& item; Lease lease; } bridge{*this, l};
    const M16FrameHost host{&bridge,
        [](void* p) noexcept { return Current(&static_cast<Bridge*>(p)->lease); },
        [](void*) noexcept { return gpGlobals->time; },
        [](void* p, M16FrameOwner& out) noexcept
        {
            auto& b = *static_cast<Bridge*>(p);
            M16OwnerFrame owner;
            if (!b.item.backends_.ownerFrame(b.item.backends_.context, owner) || !Current(&b.lease)) return false;
            const auto& velocity = b.item.m_pPlayer->pev->velocity;
            out = {velocity.x, velocity.y, owner.pendingGateTime, owner.originalFlags,
                owner.condition3AA, owner.pendingGate};
            return true;
        },
        [](void* p, M16FrameEvent event, int sequence) noexcept
        {
            auto& b = *static_cast<Bridge*>(p);
            switch (event)
            {
            case M16FrameEvent::ViewAnimation: return b.item.View(sequence);
            case M16FrameEvent::ExitAim: return b.item.SetAim(false);
            case M16FrameEvent::Reload:
                b.item.Reload();
                return Current(&b.lease) && (b.item.status_ == M16Status::Applied ||
                    b.item.status_ == M16Status::Ineligible);
            }
            return false;
        },
        [](void* p, float& duration) noexcept
        {
            auto& item = static_cast<Bridge*>(p)->item;
            const int sequence = item.m_pPlayer->pev->weaponanim;
            // Original1000F390 returns zero beyond the actual model's38 sequences.
            if (sequence >= 38) { duration = 0; return true; }
            if (sequence < 0 || !(item.prepared_.sequences.present & (std::uint64_t{1} << sequence))) return false;
            duration = item.prepared_.sequences.duration[sequence];
            return true;
        }};
    const auto derived = AdvanceM16DerivedFrame(action_, actionDeadline_, host);
    if (!Current(&l)) return;
    if (derived != M16ControlResult::Applied) { SetResult(derived); return; }
    // Original common1011D560 starts with empty-sound reset and fidget.
    if (!Check(frame)) return;
    const float now = gpGlobals->time;
    if (action_.clip > 0) m_iPlayEmptySound = 1;
    // Original10050D10 consumes the chance RNG even while aiming; only
    // a50 result tests aiming before the second RNG. Both modes allow empty.
    if (action_.primary <= now && action_.secondary <= now && action_.idle <= now &&
        !action_.action && !action_.special)
    {
        const int chance = g_engfuncs.pfnRandomLong(0, 599);
        if (!Current(&l)) return;
        if (chance == 50 && !action_.aiming)
        {
            const int choice = g_engfuncs.pfnRandomLong(0, 2);
            if (!Current(&l)) return;
            if (choice >= 0 && choice <= 2 && !View((burst_ ? 29 : 26) + choice)) return;
        }
    }
    if (now > wearDeadline_) { wearDeadline_ = AddDouble(now,.75); if (wear_ > 0) --wear_; }
    if (wear_ < 0) wear_ = 0;
    if (wear_ > wearMaximum_) wear_ = wearMaximum_; // original max hook is empty for M16
    if (reserveHud_ != action_.reserve)
    {
        M16Event e{M16Effect::ReserveHud}; e.value = action_.reserve;
        if (!Emit(e)) return; reserveHud_ = action_.reserve;
    }
    if (action_.loading)
    {
        MagazineReload reload{action_.clip,action_.reserve,action_.ownerAttack,true};
        ActionBridge bridge{this, lease_}; bridge.lease.active = true;
        CommitMagazineReload(reload,20,0,now,ReloadCommitted,&bridge);
        if (!Current(&bridge.lease)) return;
        if (bridge.callbackFailed) return;
        action_.clip = reload.clip; action_.reserve = reload.reserve; action_.loading = reload.loading;
        WriteAmmo();
    }
    const unsigned buttons = m_pPlayer->pev->button;
    const bool allowed = frame.activityBlock <= 0 && m_pPlayer->pev->movetype != 5;
    if ((buttons & 0x800u) && frame.activityBlock <= 0 && action_.secondary <= now)
    {
        if (allowed) { if (!Emit({M16Effect::InventoryChanged})) return; SecondaryAttack(); }
    }
    else if ((buttons & 0x40u) && allowed && action_.ownerAttack <= now)
    {
        if (!Emit({M16Effect::InventoryChanged})) return;
        if (action_.primary <= now && action_.secondary <= now && action_.extra <= now && !action_.action &&
            (frame.pressed & 0x40u) && (frame.toggleAim || !action_.aiming))
            if (!SetAim(!action_.aiming)) return;
    }
    else if ((buttons & 1u) && frame.activityBlock <= 0 && action_.primary <= now)
    {
        if (allowed)
        {
            if (!action_.clip) m_fFireOnEmpty = 1;
            if (!Emit({M16Effect::InventoryChanged})) return;
            PrimaryAttack();
        }
    }
    else if ((buttons & 0x2000u) && !action_.loading)
    { if (m_pPlayer->pev->movetype != 5) Reload(); }
    else if (!(buttons & 0x841u)) m_fFireOnEmpty = 0;
    if (!Current(const_cast<Lease*>(&l))) return;
    WriteAmmo(); MirrorTimers(true);
}
void M16Weapon::WeaponIdle()
{
    // Original10052D10 is empty; common Post never auto-plays Idle.
}
void M16Weapon::Invalidate() noexcept
{
    modelRebind_.reset();
    bound_ = selected_ = meleePending_ = burstPending_ = burstDispatch_ = false;
    SetThink(nullptr); if (pev) pev->nextthink = 0;
    status_ = M16Status::Cancelled;
}
}
