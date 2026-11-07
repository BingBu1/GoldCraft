// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_skull3.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "skull3_ballistics.h"
#include "weapon_call_guard.h"
#include "../../weapons/skull3.hpp"
#include "../../weapons/skull3_resources.hpp"
#include "../../weapons/skull_balrog_damage.hpp"
#include <typeinfo>

namespace {
using WeaponCall = CSOLWeaponCall;
bool precached{}, damageLoaded{};
std::array<float, 6> damage{};
bool LoadDamage()
{
    int size{};
    auto* bytes = LOAD_FILE_FOR_ME("csol/weapons/skull3_damage.json", &size);
    if (!bytes) { ALERT(at_error, "CSOL: missing skull3_damage.json\n"); return false; }
    std::string error;
    const bool valid = size > 0 && size <= 65536 && csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes), std::size_t(size)}, csol::skull3::DamageFields, damage, error);
    FREE_FILE(bytes);
    if (!valid) { ALERT(at_error, "CSOL: invalid skull3_damage.json: %s\n", error.c_str()); return false; }
    damageLoaded = true;
    return true;
}
void ReloadDamage() { if (CMD_ARGC() == 1) LoadDamage(); }
int Damage(bool dual)
{
    // Original 111AC2C0 / 111AE8C0 test zombie mode first. JSON values are
    // the user's explicit customization; unimplemented enhancement is not fabricated.
    const auto mode = CSOL_ZombieMode() ? CSOLWeaponDamageMode::Zombie :
        CSOL_CurrentWeaponDamageMode(CSOLWeaponDamageMode::Normal);
    const int index = mode == CSOLWeaponDamageMode::Scenario ? 2 : mode == CSOLWeaponDamageMode::Zombie ? 1 : 0;
    return int(damage[(dual ? 3 : 0) + index]);
}
}

// Two separately owned native entities, sharing only the player's ammo index.
// The original purchase loop creates A then D. AddToPlayer must not spawn a peer.
class CSkull3 final : public CBasePlayerWeapon
{
public:
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_Skull3Ready(); }
    float GetMaxSpeed() override
    {
        if (Dual()) return csol::skull3::DualMaxSpeed;
        return m_pPlayer && m_pPlayer->m_iFOV != 90 ? csol::skull3::ZoomMaxSpeed : csol::skull3::MaxSpeed;
    }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal = 0) override { m_deployed = false; CBasePlayerWeapon::Holster(skiplocal); }
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void WeaponIdle() override;
    int Save(CSave& save) override;
    int Restore(CRestore& restore) override;
    static TYPEDESCRIPTION m_SaveData[];
    bool DeploymentComplete() const { return m_deployed; }
    CSOLSkull3PurchaseState PurchaseState() const { return {m_modeFlags, m_iClip, true}; }
    void RestorePurchaseState(const CSOLSkull3PurchaseState& state) { m_modeFlags = state.flags; m_iClip = state.clip; }
    void Pack(weapon_data_t& data) const;
    bool Dual() const { return FClassnameIs(pev, csol::skull3::DualClassname); }
private:
    void Fire(float spread, const WeaponCall& call);
    void RememberPosition()
    {
        m_pPlayer->m_csolSlotPos[PRIMARY_WEAPON_SLOT - 1] =
            m_pPlayer->m_csolValidSlotPos[PRIMARY_WEAPON_SLOT - 1] = csol::skull3::Position(Dual());
    }
    int m_modeFlags{};
    bool m_deployed{};
    unsigned short m_singleEvent{}, m_leftEvent{}, m_rightEvent{};
};
LINK_ENTITY_TO_CLASS(weapon_skull3, CSkull3, CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_skull3d, CSkull3, CCSPlayerWeapon)

TYPEDESCRIPTION CSkull3::m_SaveData[]{DEFINE_FIELD(CSkull3, m_modeFlags, FIELD_INTEGER)};
int CSkull3::Save(CSave& save)
{
    return CBasePlayerWeapon::Save(save) && save.WriteFields("CSkull3", this, m_SaveData, ARRAYSIZE(m_SaveData));
}
int CSkull3::Restore(CRestore& restore)
{
    if (!CBasePlayerWeapon::Restore(restore) || !restore.ReadFields("CSkull3", this, m_SaveData, ARRAYSIZE(m_SaveData))) return 0;
    Precache(); m_deployed = false;
    return 1;
}
bool CSOL_IsSkull3(const CBasePlayerWeapon& weapon)
{
    return typeid(weapon) == typeid(CSkull3) &&
        ((weapon.m_iId == csol::skull3::Id && FClassnameIs(weapon.pev, csol::skull3::Classname)) ||
         (weapon.m_iId == csol::skull3::DualId && FClassnameIs(weapon.pev, csol::skull3::DualClassname)));
}
bool CSOL_Skull3DeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_IsSkull3(weapon) && static_cast<const CSkull3&>(weapon).DeploymentComplete();
}
CSOLSkull3PurchaseState CSOL_SaveSkull3PurchaseState(const CBasePlayerWeapon& weapon)
{
    return CSOL_IsSkull3(weapon) ? static_cast<const CSkull3&>(weapon).PurchaseState() : CSOLSkull3PurchaseState{};
}
void CSOL_RestoreSkull3PurchaseState(CBasePlayerWeapon& weapon, const CSOLSkull3PurchaseState& state)
{
    if (state.valid && CSOL_IsSkull3(weapon)) static_cast<CSkull3&>(weapon).RestorePurchaseState(state);
}
void CSOL_PackSkull3Weapon(const CBasePlayerWeapon* weapon, weapon_data_t* data)
{
    if (weapon && data && CSOL_IsSkull3(*weapon)) static_cast<const CSkull3*>(weapon)->Pack(*data);
}
void CSkull3::Pack(weapon_data_t& data) const
{
    data.fuser1 = m_flAccuracy;
    data.fuser2 = Q_max(-.001f, m_flDecreaseShotsFired - gpGlobals->time);
    data.fuser3 = csol::skull3::PackShots(m_iShotsFired);
    data.m_iWeaponState = (m_modeFlags & csol::skull3::Left) |
        (m_bDelayFire ? csol::skull3::DelayedFire : 0) | (m_iDirection ? csol::skull3::Direction : 0);
}
void CSkull3::Precache()
{
    for (const char* path : csol::Skull3Resources)
    {
        const std::string_view resource(path);
        if (resource.starts_with("models/") || resource.ends_with(".spr")) PRECACHE_MODEL(const_cast<char*>(path));
        else if (resource.starts_with("sound/")) PRECACHE_SOUND(const_cast<char*>(path + 6));
        else if (!resource.starts_with("events/")) PRECACHE_GENERIC(const_cast<char*>(path));
    }
    m_singleEvent = PRECACHE_EVENT(1, csol::skull3::SingleEvent);
    m_leftEvent = PRECACHE_EVENT(1, csol::skull3::LeftEvent);
    m_rightEvent = PRECACHE_EVENT(1, csol::skull3::RightEvent);
}
void CSkull3::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache(); m_iId = Dual() ? csol::skull3::DualId : csol::skull3::Id;
    SET_MODEL(edict(), csol::skull3::WorldModel);
    m_iDefaultAmmo = csol::skull3::MaxClip(Dual());
    m_flAccuracy = 0; m_iShotsFired = 0; m_bDelayFire = false; m_modeFlags = 0;
    FallInit(); CBasePlayerWeapon::Spawn();
}
int CSkull3::GetItemInfo(ItemInfo* info)
{
    const bool dual = Dual();
    info->pszName = dual ? csol::skull3::DualClassname : csol::skull3::Classname;
    info->pszAmmo1 = csol::skull3::AmmoName; info->iMaxAmmo1 = csol::skull3::Reserve;
    info->pszAmmo2 = nullptr; info->iMaxAmmo2 = -1; info->iMaxClip = csol::skull3::MaxClip(dual);
    info->iSlot = 0; info->iPosition = csol::skull3::Position(dual);
    info->iId = m_iId = dual ? csol::skull3::DualId : csol::skull3::Id;
    info->iFlags = 0; info->iWeight = csol::skull3::Weight;
    return 1;
}
BOOL CSkull3::Deploy()
{
    const WeaponCall call(*this);
    if (!call.Current()) return FALSE;
    m_deployed = false;
    const bool dual = Dual();
    m_flAccuracy = 0; m_iShotsFired = 0; m_bDelayFire = false;
    const auto deployment = csol::skull3::Deploy(dual, m_pPlayer->m_csolSlotPos[PRIMARY_WEAPON_SLOT - 1],
        m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType], m_modeFlags);
    if (!DefaultDeploy(const_cast<char*>(csol::skull3::ViewModel),
        const_cast<char*>(dual ? csol::skull3::DualPlayerModel : csol::skull3::PlayerModel),
        deployment.sequence, const_cast<char*>(dual ? "dualpistols" : "carbine"), UseDecrement()) || !call.Current()) return FALSE;
    m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + deployment.nextAttack;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + deployment.idle;
    m_deployed = true;
    return TRUE;
}
void CSkull3::PrimaryAttack()
{
    const WeaponCall call(*this);
    if (!call.Current() || m_pPlayer->pev->deadflag != DEAD_NO || m_pPlayer->pev->health <= 0) return;
    const float spread = csol::skull3::Spread(Dual(), m_flAccuracy, (m_pPlayer->pev->flags & FL_ONGROUND) != 0);
    Fire(spread, call);
    if (!call.Current()) return;
    RememberPosition(); // Original Primary writes even on an empty attempt.
}
void CSkull3::Fire(float spread, const WeaponCall& call)
{
    const bool dual = Dual();
    const auto& mode = csol::skull3::Parameters(dual);
    m_iShotsFired = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(m_iShotsFired) + 1u);
    m_bDelayFire = true;
    m_flAccuracy = csol::skull3::AccuracyAfterTrigger(dual, m_iShotsFired);
    if (m_iClip <= 0)
    {
        if (m_fFireOnEmpty)
        {
            PlayEmptySound();
            if (!call.Current()) return;
            m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + .2f;
        }
        if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
        return;
    }
    --m_iClip; m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    const bool right = (m_modeFlags & csol::skull3::Left) != 0;
    m_pPlayer->SetAnimation(dual && !right ? PLAYER_ATTACK2 : PLAYER_ATTACK1);
    if (!call.Current()) return;
    if (dual)
    {
        m_modeFlags ^= csol::skull3::Left;
        m_pPlayer->m_iWeaponVolume = 2048; m_pPlayer->m_iWeaponFlash = 256;
    }
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    if (!call.Current()) return;
    const Vector direction = CSOL_Fire45ACPAZ(*m_pPlayer, m_pPlayer->GetGunPosition(), gpGlobals->v_forward,
        spread, csol::skull3::Distance, mode.penetration, Damage(dual), mode.rangeModifier, m_pPlayer->random_seed);
    if (!call.Current()) return;
    float zero[3]{};
    PLAYBACK_EVENT_FULL(FEV_NOTHOST, m_pPlayer->edict(), dual ? (right ? m_rightEvent : m_leftEvent) : m_singleEvent,
        0, zero, zero, direction.x, direction.y, int(m_pPlayer->pev->punchangle.x * 100),
        int(m_pPlayer->pev->punchangle.y * 100), dual && m_iClip <= 1, FALSE);
    if (!call.Current()) return;
    if (!dual) { m_pPlayer->m_iWeaponVolume = 600; m_pPlayer->m_iWeaponFlash = 128; }
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + mode.interval;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::skull3::FireIdle;
    if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);
    if (!call.Current()) return;
    const auto kick = csol::skull3::Recoil(dual, (m_pPlayer->pev->flags & FL_ONGROUND) != 0,
        m_pPlayer->pev->velocity.Length2D() > 0, (m_pPlayer->pev->flags & FL_DUCKING) != 0);
    KickBack(kick.upBase, kick.lateralBase, kick.upModifier, kick.lateralModifier,
        kick.upMaximum, kick.lateralMaximum, kick.directionChange);
}
void CSkull3::SecondaryAttack()
{
    if (Dual()) return;
    m_pPlayer->m_iFOV = m_pPlayer->m_iFOV == 90 ? 64 : 90;
    m_pPlayer->pev->fov = float(m_pPlayer->m_iFOV);
    m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + .3f;
    // Host speed is cached; the original GetMaxSpeed reads FOV directly.
    m_pPlayer->ResetMaxSpeed();
}
void CSkull3::Reload()
{
    const WeaponCall call(*this);
    if (!call.Current()) return;
    const bool dual = Dual();
    if (!DefaultReload(iMaxClip(), dual ? 14 : 1, csol::skull3::Parameters(dual).reloadTime) || !call.Current()) return;
    m_pPlayer->SetAnimation(PLAYER_RELOAD);
    if (!call.Current()) return;
    if (!dual && m_pPlayer->pev->fov != 90)
    {
        m_pPlayer->m_iFOV = 10; m_pPlayer->pev->fov = 10; SecondaryAttack();
        if (!call.Current()) return;
    }
    m_flAccuracy = 0; m_iShotsFired = 0;
}
void CSkull3::WeaponIdle()
{
    const WeaponCall call(*this);
    if (!call.Current()) return;
    ResetEmptySound(); m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if (!call.Current()) return;
    if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
    if (!Dual()) { m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20; SendWeaponAnim(0, UseDecrement()); }
    else if (m_iClip > 0)
    {
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 60; SendWeaponAnim(m_iClip == 1 ? 7 : 6, UseDecrement());
    }
    if (call.Current()) RememberPosition();
}
void CSOL_InitSkull3() { ADD_SERVER_COMMAND("csol_reload_skull3_damage", ReloadDamage); }
void CSOL_PrecacheSkull3()
{
    precached = false;
    if (!LoadDamage()) return;
    UTIL_PrecacheOtherWeapon(csol::skull3::Classname);
    UTIL_PrecacheOtherWeapon(csol::skull3::DualClassname);
    const auto* a = CSOL_ItemInfo(csol::skull3::Id);
    const auto* d = CSOL_ItemInfo(csol::skull3::DualId);
    precached = a && d && a->pszName && d->pszName &&
        !Q_strcmp(a->pszName, csol::skull3::Classname) && !Q_strcmp(d->pszName, csol::skull3::DualClassname);
}
bool CSOL_Skull3Ready() { return precached && damageLoaded; }
