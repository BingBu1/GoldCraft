// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_k3.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "../../weapons/k3.hpp"
#include <rapidjson/document.h>
#include <cmath>
#include <typeinfo>

namespace
{
bool precached{}, damageLoaded{};
float normalDamage{}, zombieDamage{}, scenarioDamage{};
constexpr const char* ModelResources[]{
    "models/v_k3.mdl", "models/p_k3.mdl", "models/w_k3.mdl", "models/rshell.mdl",
    "sprites/csol/640hud85.spr", "sprites/csol/640hud7.spr", "sprites/csol/640hud11.spr"
};
constexpr const char* SoundResources[]{
    "weapons/K3-1.wav", "weapons/K3_clipout1.wav", "weapons/K3_clipout2.wav",
    "weapons/K3_clipin1.wav", "weapons/K3_clipin2.wav", "weapons/K3_clipin3.wav"
};

bool LoadDamage()
{
    int size{};
    auto* bytes = LOAD_FILE_FOR_ME("csol/weapons/k3_damage.json", &size);
    if (!bytes)
    {
        ALERT(at_error, "CSOL: missing csol/weapons/k3_damage.json\n");
        return false;
    }
    rapidjson::Document document;
    if (size > 0 && size <= 65536)
        document.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<const char*>(bytes), size);
    FREE_FILE(bytes);
    constexpr const char* fields[]{"Damage", "DamageZombie", "DamageScenario"};
    float values[3]{};
    bool valid = !document.HasParseError() && document.IsObject() && document.MemberCount() == 4 &&
        document.HasMember("version") && document["version"].IsInt() && document["version"].GetInt() == 1;
    for (int i = 0; valid && i < 3; ++i)
    {
        valid = document.HasMember(fields[i]) && document[fields[i]].IsNumber();
        if (valid)
        {
            const double value = document[fields[i]].GetDouble();
            valid = std::isfinite(value) && value >= 0 && value <= 1000000;
            values[i] = static_cast<float>(value);
        }
    }
    if (!valid)
    {
        ALERT(at_error, "CSOL: invalid k3_damage.json; retaining previous valid values\n");
        return false;
    }
    normalDamage = values[0];
    zombieDamage = values[1];
    scenarioDamage = values[2];
    damageLoaded = true;
    return true;
}

void ReloadDamage()
{
    if (CMD_ARGC() == 1) LoadDamage();
}

int Damage()
{
    const auto mode = CSOL_CurrentWeaponDamageMode(
        CSOL_ZombieMode() ? CSOLWeaponDamageMode::Zombie : CSOLWeaponDamageMode::Normal);
    // Original MP truncates the selected property before FireBullets3.
    return static_cast<int>(mode == CSOLWeaponDamageMode::Scenario ? scenarioDamage :
        mode == CSOLWeaponDamageMode::Zombie ? zombieDamage : normalDamage);
}
}

class CK3 : public CBasePlayerWeapon
{
public:
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    float GetMaxSpeed() override { return 235.0f; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_K3Ready(); }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void PrimaryAttack() override;
    void SecondaryAttack() override {}
    void Reload() override;
    void WeaponIdle() override;
private:
    void Fire(float spread);
    unsigned short m_fireEvent{};
};
LINK_ENTITY_TO_CLASS(weapon_k3, CK3, CCSPlayerWeapon)

bool CSOL_K3EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    // MP39f962f3 CK3 vtable11929A34, v1E4/v1E8 both10611B90 (ret8).
    return typeid(weapon) == typeid(CK3) && weapon.m_iId == csol::k3::Id &&
        FClassnameIs(weapon.pev, csol::k3::Classname);
}

void CSOL_PackK3Weapon(const CBasePlayerWeapon* weapon, weapon_data_t* data)
{
    if (!weapon || !data || !CSOL_K3EmptyMonsterKillCallbacks(*weapon)) return;
    data->fuser1 = weapon->m_flAccuracy;
    // The server decay deadline is absolute; prediction consumes a countdown.
    data->fuser2 = Q_max(-0.001f, weapon->m_flDecreaseShotsFired - gpGlobals->time);
    data->iuser1 = weapon->m_iShotsFired;
    data->iuser2 = weapon->m_iDirection;
    data->iuser3 = weapon->m_bDelayFire ? 1 : 0;
}

void CK3::Precache()
{
    for (const auto* path : ModelResources) PRECACHE_MODEL(const_cast<char*>(path));
    for (const auto* path : SoundResources) PRECACHE_SOUND(const_cast<char*>(path));
    PRECACHE_GENERIC("sprites/weapon_k3.txt");
    m_fireEvent = PRECACHE_EVENT(1, "events/k3.sc");
}

void CK3::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();
    m_iId = csol::k3::Id;
    SET_MODEL(edict(), "models/w_k3.mdl");
    m_iDefaultAmmo = csol::k3::Clip;
    m_flAccuracy = 0.2f;
    m_iShotsFired = 0;
    FallInit();
    CBasePlayerWeapon::Spawn();
}

int CK3::GetItemInfo(ItemInfo* info)
{
    info->pszName = "weapon_k3";
    info->pszAmmo1 = "556NatoBox";
    info->iMaxAmmo1 = csol::k3::Reserve;
    info->pszAmmo2 = nullptr;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = csol::k3::Clip;
    info->iSlot = 0;
    info->iPosition = 1;
    info->iId = m_iId = csol::k3::Id;
    info->iFlags = 0;
    info->iWeight = 25;
    return 1;
}

BOOL CK3::Deploy()
{
    m_flAccuracy = 0.2f;
    m_iShotsFired = 0;
    if (!DefaultDeploy("models/v_k3.mdl", "models/p_k3.mdl", csol::k3::Draw, "m249", UseDecrement()))
        return FALSE;
    m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + csol::k3::DeployTime;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::k3::DeployIdle;
    return TRUE;
}

void CK3::PrimaryAttack()
{
    // The current shot uses the old accuracy. Fire updates it for the next one.
    if (!(m_pPlayer->pev->flags & FL_ONGROUND))
        Fire(0.05f + 0.5f * m_flAccuracy);
    else if (m_pPlayer->pev->velocity.Length2D() > 140)
        Fire(0.045f + 0.09f * m_flAccuracy);
    else
        Fire(0.03f * m_flAccuracy);
}

void CK3::Fire(float spread)
{
    m_bDelayFire = true;
    ++m_iShotsFired;
    const float shots = static_cast<float>(m_iShotsFired);
    m_flAccuracy = Q_min(shots * shots * shots / 175.0f + 0.35f, 0.95f);
    if (m_iClip <= 0)
    {
        if (m_fFireOnEmpty)
        {
            PlayEmptySound();
            m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.2f;
        }
        if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
        return;
    }
    --m_iClip;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
    Vector origin = m_pPlayer->GetGunPosition();
    Vector aiming = gpGlobals->v_forward;
    const Vector direction = m_pPlayer->FireBullets3(origin, aiming,
        spread, 8192, 2, BULLET_PLAYER_556MM, Damage(), 0.98f,
        m_pPlayer->pev, false, m_pPlayer->random_seed);
#ifdef CLIENT_WEAPONS
    constexpr int eventFlags = FEV_NOTHOST;
#else
    constexpr int eventFlags = 0;
#endif
    float zero[3]{};
    PLAYBACK_EVENT_FULL(eventFlags, m_pPlayer->edict(), m_fireEvent, 0,
        zero, zero, direction.x, direction.y,
        static_cast<int>(m_pPlayer->pev->punchangle.x * 100),
        static_cast<int>(m_pPlayer->pev->punchangle.y * 100), FALSE, FALSE);
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + csol::k3::Interval;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::k3::FireIdle;
    if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);

    if (!(m_pPlayer->pev->flags & FL_ONGROUND))
        KickBack(2, 0.7, 0.5, 0.15, 5, 3.5, 8);
    else if (m_pPlayer->pev->velocity.Length2D() > 0)
        KickBack(1.4, 0.6, 0.45, 0.075, 4, 3, 8);
    else if (m_pPlayer->pev->flags & FL_DUCKING)
        KickBack(0.8, 0.35, 0.3, 0.04, 3.5, 2, 9);
    else
        KickBack(1, 0.4, 0.45, 0.035, 3.75, 3, 9);
}

void CK3::Reload()
{
    if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
    if (DefaultReload(iMaxClip(), 3, csol::k3::ReloadTime))
    {
        m_pPlayer->SetAnimation(PLAYER_RELOAD);
        m_flAccuracy = 0.2f;
        m_bDelayFire = false;
        m_iShotsFired = 0;
    }
}

void CK3::WeaponIdle()
{
    ResetEmptySound();
    m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::k3::IdleTime;
    SendWeaponAnim(0, UseDecrement());
}

void CSOL_InitK3()
{
    ADD_SERVER_COMMAND("csol_reload_k3_damage", ReloadDamage);
}

void CSOL_PrecacheK3()
{
    precached = false;
    if (!LoadDamage()) return;
    UTIL_PrecacheOtherWeapon("weapon_k3");
    const auto* info = CSOL_ItemInfo(csol::k3::Id);
    precached = info && info->pszName && !Q_strcmp(info->pszName, "weapon_k3");
}

bool CSOL_K3Ready() { return precached && damageLoaded; }
