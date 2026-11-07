// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_m249ex.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "skull_ballistics.h"
#include "../../weapons/m249ex.hpp"
#include "../../weapons/skull_balrog_damage.hpp"
#include <typeinfo>

namespace
{
bool precached{}, damageLoaded{};
std::array<float, 3> damage{};
bool LoadDamage()
{
    int size{};
    auto* bytes = LOAD_FILE_FOR_ME("csol/weapons/m249ex_damage.json", &size);
    if (!bytes) { ALERT(at_error, "CSOL: missing m249ex_damage.json\n"); return false; }
    std::string error;
    const bool valid = size > 0 && size <= 65536 && csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size)},
        csol::m249ex::DamageFields, damage, error);
    FREE_FILE(bytes);
    if (!valid) { ALERT(at_error, "CSOL: invalid m249ex_damage.json: %s\n", error.c_str()); return false; }
    damageLoaded = true;
    return true;
}
void ReloadDamage() { if (CMD_ARGC() == 1) LoadDamage(); }
int Damage()
{
    const auto mode = CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode() ?
        CSOLWeaponDamageMode::Zombie : CSOLWeaponDamageMode::Normal);
    return static_cast<int>(damage[mode == CSOLWeaponDamageMode::Scenario ? 2 :
        mode == CSOLWeaponDamageMode::Zombie ? 1 : 0]);
}
}

class CM249EX : public CBasePlayerWeapon
{
public:
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    float GetMaxSpeed() override { return m_pPlayer && m_pPlayer->m_iFOV == 65 ? 200 : 220; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_M249EXReady(); }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal=0) override { m_deployed=false; CBasePlayerWeapon::Holster(skiplocal); }
    bool DeploymentComplete() const { return m_deployed; }
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void WeaponIdle() override;
private:
    bool m_deployed{};
    unsigned short m_fireEvent{};
};
LINK_ENTITY_TO_CLASS(weapon_m249ex, CM249EX, CCSPlayerWeapon)

bool CSOL_M249EXEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    // MP1193B13C v1E4/v1E8 both10611B90 (ret8).
    return typeid(weapon) == typeid(CM249EX) && weapon.m_iId == csol::m249ex::Id;
}
bool CSOL_M249EXDeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_M249EXEmptyMonsterKillCallbacks(weapon) && static_cast<const CM249EX&>(weapon).DeploymentComplete();
}
void CSOL_PackM249EXWeapon(const CBasePlayerWeapon* weapon, weapon_data_t* data)
{
    if (!weapon || !data || !CSOL_M249EXEmptyMonsterKillCallbacks(*weapon)) return;
    data->fuser1 = weapon->m_flAccuracy;
    data->fuser2 = Q_max(-.001f, weapon->m_flDecreaseShotsFired - gpGlobals->time);
    data->fuser3 = csol::m249ex::PackShots(weapon->m_iShotsFired);
    data->m_iWeaponState = (weapon->m_bDelayFire ? 1 : 0) | (weapon->m_iDirection ? 2 : 0);
}
void CM249EX::Precache()
{
    for (const char* path : {csol::m249ex::ViewModel, csol::m249ex::PlayerModel,
        csol::m249ex::WorldModel, csol::m249ex::ShellModel,
        "sprites/csol/640hud51.spr", "sprites/csol/640hud3_v2.spr", "sprites/csol/640hud7_v2.spr", "sprites/csol/crosshairs.spr"})
        PRECACHE_MODEL(const_cast<char*>(path));
    for (const char* path : {csol::m249ex::FireSound, "weapons/zoom.wav", "weapons/m249ex_clipout1.wav",
        "weapons/m249ex_clipout2.wav", "weapons/m249ex_clipin1.wav", "weapons/m249ex_clipin2.wav",
        "weapons/m249ex_clipin3.wav"}) PRECACHE_SOUND(const_cast<char*>(path));
    PRECACHE_GENERIC("sprites/csol/weapon_m249ex_v2.txt");
    m_fireEvent = PRECACHE_EVENT(1, csol::m249ex::Event);
}
void CM249EX::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();
    m_iId = csol::m249ex::Id;
    SET_MODEL(edict(), csol::m249ex::WorldModel);
    m_iDefaultAmmo = csol::m249ex::Clip;
    m_flAccuracy = csol::m249ex::InitialAccuracy;
    m_iShotsFired = 0;
    FallInit();
    CBasePlayerWeapon::Spawn();
}
int CM249EX::GetItemInfo(ItemInfo* info)
{
    info->pszName = csol::m249ex::Classname;
    info->pszAmmo1 = csol::m249ex::AmmoName;
    info->iMaxAmmo1 = csol::m249ex::Reserve;
    info->pszAmmo2 = nullptr;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = csol::m249ex::Clip;
    info->iSlot = 0;
    info->iPosition = 1;
    info->iId = m_iId = csol::m249ex::Id;
    info->iFlags = 0;
    info->iWeight = 25;
    return 1;
}
BOOL CM249EX::Deploy()
{
    m_deployed=false;
    m_flAccuracy = csol::m249ex::InitialAccuracy;
    m_iShotsFired = 0;
    if (!DefaultDeploy(const_cast<char*>(csol::m249ex::ViewModel), const_cast<char*>(csol::m249ex::PlayerModel),
        csol::m249ex::Draw, "m249", UseDecrement())) return FALSE;
    m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + csol::m249ex::DeployTime;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::m249ex::DeployIdle;
    m_deployed=true;
    return TRUE;
}
void CM249EX::PrimaryAttack()
{
    const bool zoomed = m_pPlayer->m_iFOV == 65;
    const bool grounded = (m_pPlayer->pev->flags & FL_ONGROUND) != 0;
    const bool ducking = (m_pPlayer->pev->flags & FL_DUCKING) != 0;
    const float speed = m_pPlayer->pev->velocity.Length2D();
    const float spread = csol::m249ex::Spread(m_flAccuracy, grounded, speed, zoomed, ducking);
    m_bDelayFire = true;
    ++m_iShotsFired;
    m_flAccuracy = csol::m249ex::AccuracyAfterTrigger(m_iShotsFired);
    if (m_iClip <= 0)
    {
        if (m_fFireOnEmpty) { PlayEmptySound(); m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + .2f; }
        if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
        return;
    }
    --m_iClip;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    const Vector direction = CSOL_Fire556AZ(*m_pPlayer, m_pPlayer->GetGunPosition(), gpGlobals->v_forward,
        spread, Damage(), zoomed ? .98f : .97f, m_pPlayer->random_seed);
    float zero[3]{};
    PLAYBACK_EVENT_FULL(FEV_NOTHOST, m_pPlayer->edict(), m_fireEvent, 0, zero, zero,
        direction.x, direction.y, int(m_pPlayer->pev->punchangle.x * 100),
        int(m_pPlayer->pev->punchangle.y * 100), FALSE, FALSE);
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() +
        (zoomed ? csol::m249ex::ZoomInterval : csol::m249ex::Interval);
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::m249ex::FireIdle;
    if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);
    const auto kick = csol::m249ex::Recoil(grounded, speed > 0, zoomed, ducking);
    KickBack(kick.upBase, kick.lateralBase, kick.upModifier, kick.lateralModifier,
        kick.upMaximum, kick.lateralMaximum, kick.directionChange);
}
void CM249EX::SecondaryAttack()
{
    m_pPlayer->m_iFOV = m_pPlayer->pev->fov = m_pPlayer->m_iFOV == 90 ? 65 : 90;
    m_pPlayer->ResetMaxSpeed();
    EMIT_SOUND_DYN(m_pPlayer->edict(), CHAN_ITEM, "weapons/zoom.wav", .2f, 2.4f, 0, 100);
    m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + csol::m249ex::ZoomTime;
}
void CM249EX::Reload()
{
    if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
    if (DefaultReload(iMaxClip(), csol::m249ex::Reload, csol::m249ex::ReloadTime))
    {
        m_pPlayer->SetAnimation(PLAYER_RELOAD);
        if (m_pPlayer->m_iFOV != 90) SecondaryAttack();
        m_flAccuracy = csol::m249ex::InitialAccuracy;
        m_iShotsFired = 0;
        m_bDelayFire = false;
    }
}
void CM249EX::WeaponIdle()
{
    ResetEmptySound();
    m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
    SendWeaponAnim(csol::m249ex::Idle, UseDecrement());
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::m249ex::IdleTime;
}
void CSOL_InitM249EX() { ADD_SERVER_COMMAND("csol_reload_m249ex_damage", ReloadDamage); }
void CSOL_PrecacheM249EX()
{
    precached = false;
    if (!LoadDamage()) return;
    UTIL_PrecacheOtherWeapon(csol::m249ex::Classname);
    const auto* info = CSOL_ItemInfo(csol::m249ex::Id);
    precached = info && info->pszName && !Q_strcmp(info->pszName, csol::m249ex::Classname);
}
bool CSOL_M249EXReady() { return precached && damageLoaded; }
