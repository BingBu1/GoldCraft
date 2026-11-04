// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_balrog11.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "../balrog11.hpp"
#include "../balrog11_flame.hpp"
#include "../skull_balrog_damage.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <typeinfo>
#include <vector>

namespace
{
bool precached{}, damageLoaded{};
int emitterMessage{};
std::array<float, 6> damage{};
constexpr const char* FlameModel = "sprites/flame_puff01.spr";
constexpr const char* ChargeSound = "weapons/balrog9_charge_finish1.wav";

bool LoadDamage()
{
    int size{};
    auto* bytes = LOAD_FILE_FOR_ME("csol/weapons/balrog11_damage.json", &size);
    if (!bytes) { ALERT(at_error, "CSOL: missing balrog11_damage.json\n"); return false; }
    std::string error;
    const bool valid = size > 0 && size <= 65536 && csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size)},
        csol::balrog11::DamageFields, damage, error);
    FREE_FILE(bytes);
    if (!valid) { ALERT(at_error, "CSOL: invalid balrog11_damage.json: %s\n", error.c_str()); return false; }
    damageLoaded = true;
    return true;
}
void ReloadDamage() { if (CMD_ARGC() == 1) LoadDamage(); }
int Damage(bool flame)
{
    const auto mode = CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode() ?
        CSOLWeaponDamageMode::Zombie : CSOLWeaponDamageMode::Normal);
    const int profile = mode == CSOLWeaponDamageMode::Scenario ? 2 : mode == CSOLWeaponDamageMode::Zombie ? 1 : 0;
    return static_cast<int>(damage[profile + (flame ? 3 : 0)]);
}
struct FlameVolley
{
    // All five projectiles share the original per-volley hit list. EHANDLE
    // prevents an edict recycled during the volley from inheriting immunity.
    std::vector<EHANDLE> hits;
};
}

class CBalrog11Flame : public CBaseEntity, public CSOLWeaponDamageSource
{
public:
    void Spawn() override;
    void Precache() override { PRECACHE_MODEL(const_cast<char*>(FlameModel)); }
    void OnDestroy() override { m_volley.reset(); CBaseEntity::OnDestroy(); }
    int DamageWeaponId() const override { return csol::balrog11::Id; }
    static void Shoot(CBasePlayer& owner, const Vector& origin, const Vector& direction,
        int damage, const std::shared_ptr<FlameVolley>& volley);
    void AnimationThink();
    void FlameTouch(CBaseEntity* other);
private:
    EHANDLE m_owner;
    std::shared_ptr<FlameVolley> m_volley;
    Vector m_launchVelocity;
    float m_expire{}, m_lastFrame{}, m_damage{};
    int m_frames{1};
};
LINK_ENTITY_TO_CLASS(balrog11_cannon, CBalrog11Flame, CCSEntity)

void CBalrog11Flame::Spawn()
{
    Precache();
    SET_MODEL(edict(), FlameModel);
    pev->movetype = MOVETYPE_FLYMISSILE;
    pev->solid = SOLID_TRIGGER;
    pev->rendermode = kRenderTransAdd;
    pev->renderamt = 90;
    pev->rendercolor = Vector(255, 255, 255);
    pev->scale = .8f;
    pev->frame = 0;
    pev->framerate = RANDOM_LONG(1, 30);
    pev->effects = 0;
    pev->takedamage = DAMAGE_NO;
    UTIL_SetSize(pev, g_vecZero, g_vecZero);
    m_frames = std::max(1, MODEL_FRAMES(pev->modelindex));
    m_lastFrame = gpGlobals->time;
    m_expire = gpGlobals->time + csol::balrog11::FlameLife;
    SetThink(&CBalrog11Flame::AnimationThink);
    SetTouch(&CBalrog11Flame::FlameTouch);
    pev->nextthink = gpGlobals->time + .01f;
}
void CBalrog11Flame::Shoot(CBasePlayer& owner, const Vector& origin, const Vector& direction,
    int damage, const std::shared_ptr<FlameVolley>& volley)
{
    auto* flame = GetClassPtr<CCSEntity>(static_cast<CBalrog11Flame*>(nullptr));
    if (!flame) return;
    flame->pev->classname = MAKE_STRING("balrog11_cannon");
    flame->Spawn();
    flame->m_owner = &owner;
    flame->m_volley = volley;
    flame->m_damage = damage;
    flame->pev->owner = owner.edict();
    UTIL_SetOrigin(flame->pev, origin);
    flame->m_launchVelocity = direction * csol::balrog11::FlameSpeed;
    flame->pev->velocity = flame->m_launchVelocity;
    flame->pev->angles = UTIL_VecToAngles(direction);
    flame->pev->angles.z = RANDOM_LONG(0, 180);
    flame->pev->avelocity.z = RANDOM_LONG(5, 50) * (RANDOM_LONG(0, 1) ? -1 : 1);
    if (emitterMessage >= 64)
    {
        // Original Spawn106249E5 ->11493570 broadcasts one entity-following
        // TempEmit. Particles have their own .3s lifetime after being emitted.
        const auto payload = csol::balrog11::flame::Write({flame->entindex(), RANDOM_LONG(1, 180)});
        if (payload)
        {
            MESSAGE_BEGIN(MSG_BROADCAST, emitterMessage);
            for (auto byte : *payload) WRITE_BYTE(byte);
            MESSAGE_END();
        }
    }
}
void CBalrog11Flame::AnimationThink()
{
    if (gpGlobals->time >= m_expire)
    {
        SetThink(nullptr);
        SetTouch(nullptr);
        m_volley.reset();
        UTIL_Remove(this);
        return;
    }
    // MP 106243F9 restores the launch velocity every think. GoldSrc clips
    // FLYMISSILE velocity on monster impact even for this trigger projectile.
    // Restore the original direction after clipping; BSP contacts still remove it.
    pev->velocity = m_launchVelocity;
    pev->frame += (gpGlobals->time - m_lastFrame) * pev->framerate;
    const float lastFrame = static_cast<float>(std::max(1, m_frames - 1));
    if (pev->frame > lastFrame) pev->frame = std::fmod(pev->frame, lastFrame);
    m_lastFrame = gpGlobals->time;
    pev->nextthink = gpGlobals->time + .02f;
}
void CBalrog11Flame::FlameTouch(CBaseEntity* other)
{
    if (!other || other == m_owner || dynamic_cast<CBalrog11Flame*>(other)) return;
    if (!m_volley) return;
    for (const auto& prior : m_volley->hits) if (prior == other) return;
    auto* owner = dynamic_cast<CBasePlayer*>(static_cast<CBaseEntity*>(m_owner));
    if (owner && GETPLAYERUSERID(owner->edict()) > 0 && !CSOL_ZombiePlayer(owner) && !(other->pev->flags & FL_KILLME) &&
        other->pev->takedamage != DAMAGE_NO)
    {
        // Claim before entering damage callbacks, which can re-enter Touch.
        // MP's five projectiles share one hit vector rather than multiplying
        // the 2400 scenario base damage by five for one target.
        m_volley->hits.emplace_back(other);
        other->TakeDamage(pev, owner->pev, m_damage, DMG_GENERIC);
    }
    if (other->IsBSPModel())
    {
        SetThink(nullptr);
        SetTouch(nullptr);
        m_volley.reset();
        UTIL_Remove(this);
    }
}

class CBalrog11 : public CBasePlayerWeapon
{
public:
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    float GetMaxSpeed() override { return csol::balrog11::MaxSpeed; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_Balrog11Ready(); }
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
    csol::balrog11::ChargeState Charge() const { return m_charge; }
private:
    void Event(bool secondary, float volume);
    bool m_deployed{};
    void Fired(float interval);
    csol::balrog11::ChargeState m_charge;
    unsigned short m_fireEvent{};
};
LINK_ENTITY_TO_CLASS(weapon_balrog11, CBalrog11, CCSPlayerWeapon)

bool CSOL_Balrog11EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    // MP11856758 v1E4/v1E8 both10611B90 (ret8).
    return typeid(weapon) == typeid(CBalrog11) && weapon.m_iId == csol::balrog11::Id;
}
bool CSOL_Balrog11DeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_Balrog11EmptyMonsterKillCallbacks(weapon) && static_cast<const CBalrog11&>(weapon).DeploymentComplete();
}
void CSOL_PackBalrog11Weapon(const CBasePlayerWeapon* weapon, weapon_data_t* data)
{
    if (!weapon || !data || !CSOL_Balrog11EmptyMonsterKillCallbacks(*weapon)) return;
    const auto charge = static_cast<const CBalrog11*>(weapon)->Charge();
    data->m_iWeaponState = csol::balrog11::PackCharge(charge);
}
int CSOL_Balrog11Charges(const CBasePlayerWeapon* weapon)
{
    return weapon && CSOL_Balrog11EmptyMonsterKillCallbacks(*weapon) ?
        static_cast<const CBalrog11*>(weapon)->Charge().charges : 0;
}
void CBalrog11::Precache()
{
    for (const char* path : {csol::balrog11::ViewModel, csol::balrog11::PlayerModel,
        csol::balrog11::WorldModel, FlameModel, "models/shotgunshell.mdl", "models/shell_bcs.mdl",
        "sprites/csol/640hud89.spr", "sprites/csol/640hud3_v2.spr", "sprites/csol/640hud7_v2.spr", "sprites/csol/640hud11.spr",
        "sprites/muzzleflash17.spr", "sprites/muzzleflash27.spr"})
        PRECACHE_MODEL(const_cast<char*>(path));
    for (const char* path : {"weapons/balrog11-1.wav", "weapons/balrog11-2.wav",
        "weapons/balrog11_insert.wav", "weapons/balrog11_draw.wav", ChargeSound})
        PRECACHE_SOUND(const_cast<char*>(path));
    PRECACHE_GENERIC("sprites/csol/weapon_balrog11_v3.txt");
    m_fireEvent = PRECACHE_EVENT(1, csol::balrog11::Event);
}
void CBalrog11::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();
    m_iId = csol::balrog11::Id;
    SET_MODEL(edict(), csol::balrog11::WorldModel);
    m_iDefaultAmmo = csol::balrog11::Clip;
    m_charge = {};
    FallInit();
    CBasePlayerWeapon::Spawn();
}
int CBalrog11::GetItemInfo(ItemInfo* info)
{
    info->pszName = csol::balrog11::Classname;
    info->pszAmmo1 = csol::balrog11::AmmoName;
    info->iMaxAmmo1 = csol::balrog11::Reserve;
    info->pszAmmo2 = csol::balrog11::SecondaryAmmoName;
    info->iMaxAmmo2 = csol::balrog11::SecondaryAmmoReserve;
    info->iMaxClip = csol::balrog11::Clip;
    info->iSlot = 0;
    info->iPosition = 1;
    info->iId = m_iId = csol::balrog11::Id;
    info->iFlags = 0;
    info->iWeight = 20;
    return 1;
}
BOOL CBalrog11::Deploy()
{
    m_deployed=false;
    m_charge.Idle();
    if (!DefaultDeploy(const_cast<char*>(csol::balrog11::ViewModel), const_cast<char*>(csol::balrog11::PlayerModel),
        csol::balrog11::Draw, "m249", UseDecrement())) return FALSE;
    m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + .75f;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.5f;
    m_deployed=true;
    return TRUE;
}
void CBalrog11::Event(bool secondary, float volume)
{
    float zero[3]{};
    PLAYBACK_EVENT_FULL(FEV_NOTHOST, m_pPlayer->edict(), m_fireEvent, 0, zero, zero,
        // MP initializes weapon+0x1A4 to zero; it is not player punch pitch.
        // The client event applies iparam2 to yaw, not pitch.
        volume, csol::balrog11::Spread, 7, 0,
        secondary, FALSE);
}
void CBalrog11::Fired(float interval)
{
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + interval;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + (m_iClip ? csol::balrog11::FireIdle : csol::balrog11::EmptyIdle);
    m_fInSpecialReload = 0;
}
void CBalrog11::PrimaryAttack()
{
    if (m_pPlayer->pev->waterlevel == 3)
    {
        PlayEmptySound();
        m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + .15f;
        return;
    }
    if (m_iClip <= 0)
    {
        Reload();
        if (!m_iClip) PlayEmptySound();
        if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
        m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1;
        return;
    }
    --m_iClip;
    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    Vector origin = m_pPlayer->GetGunPosition(), forward = gpGlobals->v_forward;
    Vector cone(csol::balrog11::Spread, csol::balrog11::Spread, 0);
    m_pPlayer->FireBuckshots(csol::balrog11::Pellets, origin, forward, cone,
        csol::balrog11::Distance, 0, Damage(false), m_pPlayer->pev);
    const bool gained = m_charge.PrimaryFired();
    if (gained)
    {
        EMIT_SOUND_DYN(m_pPlayer->edict(), CHAN_ITEM, ChargeSound, 1, .8f, 0, 100);
        CSOL_SendAmmoPickup(m_pPlayer, csol::balrog11::SecondaryAmmoName, 1);
    }
    EMIT_SOUND_DYN(m_pPlayer->edict(), CHAN_WEAPON, "weapons/balrog11-1.wav",
        gained ? .2f : 1.f, .52f, 0, 94 + RANDOM_LONG(0, 15));
    Event(false, gained ? .2f : 1.f);
    Fired(csol::balrog11::Interval);
    if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);
    const bool grounded = (m_pPlayer->pev->flags & FL_ONGROUND) != 0;
    m_pPlayer->pev->punchangle.x -= UTIL_SharedRandomLong(m_pPlayer->random_seed + 1,
        grounded ? 3 : 7, grounded ? 4 : 10);
}
void CBalrog11::SecondaryAttack()
{
    if (!m_charge.ConsumeSecondary()) return;
    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    const Vector forward = gpGlobals->v_forward;
    const Vector right = gpGlobals->v_right;
    const Vector up = gpGlobals->v_up;
    const Vector origin = m_pPlayer->GetGunPosition() - up * 2 + forward * csol::balrog11::FlameForwardMargin;
    auto volley = std::make_shared<FlameVolley>();
    const int damage = Damage(true);
    for (const float degrees : csol::balrog11::FlameAngles)
    {
        const float radians = degrees * static_cast<float>(M_PI / 180);
        // Original Rodrigues rotation is about v_up, retaining pitched aim.
        const Vector direction = forward * std::cos(radians) - right * std::sin(radians);
        CBalrog11Flame::Shoot(*m_pPlayer, origin, direction, damage, volley);
    }
    Event(true, 1);
    Fired(csol::balrog11::SecondaryInterval);
}
void CBalrog11::Reload()
{
    if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip >= iMaxClip() ||
        m_flNextPrimaryAttack > UTIL_WeaponTimeBase()) return;
    if (!m_fInSpecialReload)
    {
        m_pPlayer->SetAnimation(PLAYER_RELOAD);
        SendWeaponAnim(csol::balrog11::StartReload, UseDecrement());
        m_fInSpecialReload = 1;
        m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + csol::balrog11::ReloadFirst;
        m_flTimeWeaponIdle = m_flNextPrimaryAttack = m_flNextSecondaryAttack =
            UTIL_WeaponTimeBase() + csol::balrog11::ReloadFirst;
    }
    else if (m_fInSpecialReload == 1)
    {
        if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
        m_fInSpecialReload = 2;
        EMIT_SOUND_DYN(m_pPlayer->edict(), CHAN_ITEM, "weapons/balrog11_insert.wav",
            1, .8f, 0, 85 + RANDOM_LONG(0, 31));
        SendWeaponAnim(csol::balrog11::Insert, UseDecrement());
        m_flNextReload = m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::balrog11::ReloadInsert;
    }
    else
    {
        ++m_iClip;
        --m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
        m_fInSpecialReload = 1;
    }
}
void CBalrog11::WeaponIdle()
{
    m_charge.Idle();
    ResetEmptySound();
    m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);
    if (m_flTimeWeaponIdle >= UTIL_WeaponTimeBase()) return;
    if (!m_iClip && !m_fInSpecialReload && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0) Reload();
    else if (m_fInSpecialReload)
    {
        if (m_iClip < iMaxClip() && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0) Reload();
        else
        {
            SendWeaponAnim(csol::balrog11::AfterReload, UseDecrement());
            m_fInSpecialReload = 0;
            m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::balrog11::ReloadFinish;
        }
    }
    else
    {
        SendWeaponAnim(csol::balrog11::Idle, UseDecrement());
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + csol::balrog11::IdleTime;
    }
}
void CSOL_InitBalrog11() { ADD_SERVER_COMMAND("csol_reload_balrog11_damage", ReloadDamage); }
void CSOL_PrecacheBalrog11()
{
    precached = false;
    if (!LoadDamage()) return;
    emitterMessage = REG_USER_MSG(csol::balrog11::flame::Message, -1);
    if (emitterMessage < 64 || emitterMessage > 255) return;
    UTIL_PrecacheOtherWeapon(csol::balrog11::Classname);
    const auto* info = CSOL_ItemInfo(csol::balrog11::Id);
    precached = info && info->pszName && !Q_strcmp(info->pszName, csol::balrog11::Classname);
}
bool CSOL_Balrog11Ready() { return precached && damageLoaded; }
