// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"
#include "wpn_beamgun.h"
#include "weapon_damage_runtime.h"
#include "weapon_service.h"
#include "../beamgun_prediction.hpp"
#include "../beamgun_resources.hpp"
#include "../beamgun_effects.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <typeinfo>

namespace
{
constexpr float Metre = 39.37f;
constexpr int ClipSize = 80, ReserveSize = 200;
cvar_t damageMode = {"csol_beam_damage_mode", "0", FCVAR_SERVER, 0, nullptr};
int effectMessage{},guardMessage{};
bool beamPrecached{};
struct BeamSkin
{
    int id;
    const char *name, *ammo, *energy, *view, *player, *world;
};
constexpr BeamSkin Skins[] = {
    {725, "weapon_beamgun", "beamgun", "beamgun_c", "models/v_beamgun.mdl", "models/p_beamgun.mdl", "models/w_beamgun.mdl"},
    {726, "weapon_beamgunle", "beamgunle", "beamgunle_c", "models/v_beamgunle.mdl", "models/p_beamgunle.mdl", "models/w_beamgunle.mdl"}
};
}

class CBeamGun : public CBasePlayerWeapon
{
public:
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    float GetMaxSpeed() override { return 245; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && beamPrecached && CSOL_BeamDamageReady(); }
    BOOL IsUseable() override { return TRUE; } // B attacks do not consume the ordinary clip.
    BOOL Deploy() override;
    void Holster(int skiplocal = 0) override;
    void Drop() override;
    void Kill() override;
    int AddToPlayer(CBasePlayer* player) override;
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void WeaponIdle() override;
    void ItemPostFrame() override;
    void Advance(float seconds);
    void Pack(weapon_data_t* output);
    void Removed();
    bool Guarded() const { return gpGlobals->time < m_flGuardUntil; }
    float GuardUntil() const { return m_flGuardUntil; }
    void ResetGuard() { m_flGuardUntil = 0; }

private:
    const BeamSkin& Skin() const { return Skins[FClassnameIs(pev, "weapon_beamgunle") ? 1 : 0]; }
    bool CanAttackNow() const;
    float Damage(CSOLBeamDamage kind) const;
    void Pull();
    void Push();
    void Apply(const csol::BeamAction& action);
    void Effect(int mode, int phase, const Vector& end, const std::array<int, 4>& targets = {}, int count = 0);
    void Blast(const Vector& origin, float radius, CSOLBeamDamage damage);
    void FirePrimary();
    void Wingmen();
    const char* PlayerAnimationExtension();
    int m_iAnimationModel = -1;
    const char* m_pszAnimationExtension = "m249";
    csol::BeamWeaponState m_state;
    int m_iExplosionShots{};
    float m_flLastShot{}, m_flNextCharge{}, m_flNextWing{};
    float m_flGuardUntil{};
};
LINK_ENTITY_TO_CLASS(weapon_beamgun, CBeamGun, CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_beamgunle, CBeamGun, CCSPlayerWeapon)

bool CSOL_BeamEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    return typeid(weapon) == typeid(CBeamGun) &&
        (weapon.m_iId == CSOL_WEAPON_BEAMGUN || weapon.m_iId == CSOL_WEAPON_BEAMGUNLE);
}

bool CSOL_BeamInflictor(const entvars_t* inflictor)
{
    return inflictor && (FStrEq(STRING(inflictor->classname), "weapon_beamgun") ||
        FStrEq(STRING(inflictor->classname), "weapon_beamgunle"));
}

void CBeamGun::Precache()
{
    const auto& resources = Skin().id == 726 ? csol::BeamgunLEResources : csol::BeamgunResources;
    for (const auto* path : resources)
        if (std::strstr(path, ".txt") || std::strstr(path, ".tga") || std::strstr(path, ".webm")) PRECACHE_GENERIC(const_cast<char*>(path));
        else PRECACHE_MODEL(const_cast<char*>(path));
    for (const auto* path : csol::BeamgunSounds) PRECACHE_SOUND(const_cast<char*>(path + 6));
    PRECACHE_MODEL("sprites/csol/640hud7.spr");
}

void CBeamGun::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();
    m_iId = Skin().id;
    SET_MODEL(edict(), Skin().world);
    m_iDefaultAmmo = ClipSize;
    m_flAccuracy = 0.3f;
    m_state = {};
    FallInit();
    CBasePlayerWeapon::Spawn();
}

int CBeamGun::GetItemInfo(ItemInfo* info)
{
    const auto& skin = Skin();
    info->pszName = skin.name;
    info->pszAmmo1 = skin.ammo; info->iMaxAmmo1 = ReserveSize;
    info->pszAmmo2 = skin.energy; info->iMaxAmmo2 = 100;
    info->iMaxClip = ClipSize; info->iSlot = 0; info->iPosition = 0;
    info->iId = m_iId = skin.id; info->iFlags = ITEM_FLAG_NOAUTOSWITCHEMPTY;
    info->iWeight = 20;
    return 1;
}

void CBeamGun::Pull()
{
    m_state.clip = m_iClip; m_state.reloading = m_fInReload != FALSE;
    m_state.energy = m_pPlayer ? m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType] : 0;
    m_state.nextPrimary = m_flNextPrimaryAttack; m_state.nextSecondary = m_flNextSecondaryAttack;
    m_state.idleTime = m_flTimeWeaponIdle;
}
void CBeamGun::Push()
{
    m_iClip = m_state.clip; m_fInReload = m_state.reloading;
    if (m_pPlayer) m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType] = m_state.energy;
    m_flNextPrimaryAttack = m_state.nextPrimary; m_flNextSecondaryAttack = m_state.nextSecondary;
    m_flTimeWeaponIdle = m_state.idleTime;
    // Original entvars field0x120 is skin, not body (field0x124).
    // Skin1 swaps the two charge textures; body selects the character's hands.
    pev->skin = m_state.energy >= 100 ? 1 : 0;
}
bool CBeamGun::CanAttackNow() const
{
    return m_pPlayer && m_pPlayer->IsAlive() && !m_pPlayer->m_bIsDefusing &&
        m_pPlayer->m_bCanShoot && !g_pGameRules->IsFreezePeriod() && CSOL_BeamDamageReady();
}
float CBeamGun::Damage(CSOLBeamDamage kind) const
{
    // Authoritative server setting, never userinfo. Normal CS defaults to 0;
    // the owning game mode explicitly selects Zombie=1 or Scenario=2.
    const auto mode = damageMode.value == 1 ? CSOLDamageMode::Zombie :
        damageMode.value == 2 ? CSOLDamageMode::Scenario : CSOLDamageMode::Normal;
    return CSOL_BeamDamage(Skin().id == 726 ? CSOLBeamVariant::BeamgunLE : CSOLBeamVariant::Beamgun, mode, kind);
}

int CBeamGun::AddToPlayer(CBasePlayer* player)
{
    const int result = CBasePlayerWeapon::AddToPlayer(player);
    m_state.Attached(result != 0);
    if (result) ResetGuard();
    return result;
}
BOOL CBeamGun::Deploy()
{
    Pull();
    const int animation = m_state.BeginDeploy();
    const BOOL result = DefaultDeploy(const_cast<char*>(Skin().view), const_cast<char*>(Skin().player),
        animation, const_cast<char*>(PlayerAnimationExtension()), UseDecrement());
    if (result)
    {
        Pull(); m_state.FinishDeploy(true);
        // PlayerPostThink advances every inventory weapon's countdown once.
        // Drawing only imposes a minimum delay; it must not forgive an attack.
        m_state.nextPrimary = Q_max(m_state.nextPrimary, 0.1f);
        m_state.nextSecondary = Q_max(m_state.nextSecondary, 0.1f);
        m_pPlayer->m_flNextAttack = 0.1f;
        m_flNextCharge = gpGlobals->time + 1.0f;
        m_flNextWing = gpGlobals->time + 0.6f;
        Push();
    }
    return result;
}
const char* CBeamGun::PlayerAnimationExtension()
{
    if (!m_pPlayer) return "m249";
    if (m_iAnimationModel != m_pPlayer->pev->modelindex)
    {
        m_iAnimationModel = m_pPlayer->pev->modelindex;
        const char* names[] = {"ref_aim_skull8", "ref_shoot_skull8", "ref_shoot2_skull8", "ref_reload_skull8",
            "crouch_aim_skull8", "crouch_shoot_skull8", "crouch_shoot2_skull8", "crouch_reload_skull8"};
        bool complete = true;
        for (const char* name : names) complete = complete && m_pPlayer->LookupSequence(name) >= 0;
        // Stock characters have no skull8 tracks. Keep their native skeleton
        // and working aim/attack/reload rather than selecting sequence zero.
        m_pszAnimationExtension = complete ? "skull8" : "m249";
    }
    return m_pszAnimationExtension;
}
void CBeamGun::Holster(int skiplocal)
{
    if (m_pPlayer) Effect(4, 0, m_pPlayer->pev->origin);
    Pull(); m_state.Holster(); Push();
    m_iExplosionShots = 0;
    CBasePlayerWeapon::Holster(skiplocal);
}

void CBeamGun::Removed()
{
    ResetGuard();
    if (m_pPlayer && m_state.dive != csol::BeamDive::None) Effect(4, 0, m_pPlayer->pev->origin);
    m_state.Removed(); m_state.ResetAction(); m_state.reloading = false;
    m_fInReload = FALSE;
    m_iExplosionShots = 0;
}
void CBeamGun::Drop() { Removed(); CBasePlayerWeapon::Drop(); }
void CBeamGun::Kill() { Removed(); CBasePlayerWeapon::Kill(); }

void CBeamGun::Effect(int mode, int phase, const Vector& end, const std::array<int, 4>& targets, int count)
{
    csol::BeamEffect effect;
    effect.weapon = Skin().id; effect.owner = m_pPlayer->entindex();
    effect.mode = mode; effect.phase = phase; effect.targets = targets; effect.count = count;
    const Vector origin = (mode == 0 || mode == 3) ? m_pPlayer->GetGunPosition() : m_pPlayer->pev->origin;
    for (int i = 0; i < 3; ++i) { effect.origin[i] = origin[i]; effect.end[i] = end[i]; }
    const auto bytes = csol::EncodeBeamEffect(effect);
    if (bytes[0] != std::byte{1}) return;
    MESSAGE_BEGIN(MSG_ALL, effectMessage);
    for (auto byte : bytes) WRITE_BYTE(std::to_integer<unsigned char>(byte));
    MESSAGE_END();
}

void CBeamGun::Blast(const Vector& origin, float radius, CSOLBeamDamage kind)
{
    const float damage = Damage(kind);
    if (damage <= 0) return;
    // Explicit visibility and owner exclusion: stock CS RadiusDamage can pass
    // through walls and damage the emitter. Native TakeDamage retains armor,
    // friendly-fire rules and the weapon/player damage attribution.
    // The linear falloff is a conservative fallback; the missing original MP
    // hit implementation has not established CSO's exact distance curve.
    const float range = radius * Metre;
    const bool water = UTIL_PointContents(origin) == CONTENTS_WATER;
    CBaseEntity* target = nullptr;
    while ((target = UTIL_FindEntityInSphere(target, origin, range)))
    {
        if (target == m_pPlayer || target->pev->takedamage == DAMAGE_NO ||
            (water && target->pev->waterlevel == 0) || (!water && target->pev->waterlevel == 3)) continue;
        const Vector point = target->BodyTarget(origin);
        TraceResult trace;
        UTIL_TraceLine(origin, point, dont_ignore_monsters, m_pPlayer->edict(), &trace);
        if (trace.flFraction < 1 && trace.pHit != target->edict()) continue;
        const float adjusted = damage * Q_max(0.0f, 1.0f - (point - origin).Length() / range);
        if (adjusted > 0) target->TakeDamage(pev, m_pPlayer->pev, adjusted, DMG_BLAST);
    }
}

void CBeamGun::FirePrimary()
{
    m_bDelayFire = true; ++m_iShotsFired;
    m_flAccuracy = Q_min(0.9f, m_iShotsFired * m_iShotsFired * m_iShotsFired / 210.0f + 0.3f);
    float spread = m_flAccuracy * 0.02f;
    if (!(m_pPlayer->pev->flags & FL_ONGROUND)) spread = 0.02f + m_flAccuracy * 0.2f;
    else if (m_pPlayer->pev->velocity.Length2D() > 0) spread = 0.02f + m_flAccuracy * 0.01f;
    else if (m_pPlayer->pev->flags & FL_DUCKING) spread = m_flAccuracy * 0.01f;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    Vector source = m_pPlayer->GetGunPosition(), aim = gpGlobals->v_forward;
    const Vector traceSource = source; // FireBullets3 advances source when penetrating surfaces.
    const Vector right = gpGlobals->v_right, up = gpGlobals->v_up;
    // CS FireBullets3 takes integer base damage; its trace, penetration,
    // hitgroup, armor and friendly-fire rules remain authoritative.
    const Vector offset = m_pPlayer->FireBullets3(source, aim, spread, 100 * Metre, 3,
        BULLET_PLAYER_556MM, int(Damage(CSOLBeamDamage::ShootA)), 0.97f, m_pPlayer->pev, false, m_pPlayer->random_seed);
    TraceResult trace;
    const Vector direction = aim + right * offset.x + up * offset.y;
    UTIL_TraceLine(traceSource, traceSource + direction * (100 * Metre), dont_ignore_monsters, m_pPlayer->edict(), &trace);
    if (gpGlobals->time - m_flLastShot > 5.0f) m_iExplosionShots = 0;
    m_flLastShot = gpGlobals->time;
    ++m_iExplosionShots;
    const int explosion = m_iExplosionShots % 10 == 0 ? 4 : m_iExplosionShots % 2 == 0 ? 1 : 0;
    if (explosion && !trace.fStartSolid && trace.flFraction < 1)
        Blast(trace.vecEndPos + trace.vecPlaneNormal, explosion == 4 ? 4 : 2,
            explosion == 4 ? CSOLBeamDamage::ShootAFullExplosion : CSOLBeamDamage::ShootAExplosion);
    Effect(0, explosion, trace.vecEndPos);
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
    if (!(m_pPlayer->pev->flags & FL_ONGROUND)) KickBack(1.2, 0.5, 0.3, 0.25, 4, 4, 6);
    else if (m_pPlayer->pev->velocity.Length2D() > 0) KickBack(1, 0.3, 0.3, 0.03, 3, 3, 9);
    else if (m_pPlayer->pev->flags & FL_DUCKING) KickBack(0.55, 0.22, 0.02, 0.01, 2, 1.5, 10);
    else KickBack(0.7, 0.25, 0.25, 0.015, 2.5, 2, 9);
}

void CBeamGun::Apply(const csol::BeamAction& action)
{
    using csol::BeamAttack;
    Push();
    if (action.animation >= 0) SendWeaponAnim(action.animation, UseDecrement());
    if (action.playerAnimation >= 0)
    {
        const auto animation = static_cast<PLAYER_ANIM>(action.playerAnimation);
        // Stock m249 has a single shooting track; skull8 has distinct A/B.
        m_pPlayer->SetAnimation(animation == PLAYER_ATTACK2 && Q_strcmp(PlayerAnimationExtension(), "skull8") ? PLAYER_ATTACK1 : animation);
    }
    if (action.descend) m_pPlayer->pev->velocity.z = -500;
    if (action.stopVelocity) m_pPlayer->pev->velocity = g_vecZero;
    if (action.clearGround) m_pPlayer->pev->flags &= ~FL_ONGROUND;
    if (action.attack == BeamAttack::Empty) { PlayEmptySound(); return; }
    if (action.attack == BeamAttack::Primary) { FirePrimary(); return; }
    if (action.attack == BeamAttack::None) return;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle);
    const Vector source = m_pPlayer->GetGunPosition();
    TraceResult trace;
    switch (action.attack)
    {
    case BeamAttack::Secondary1:
        Blast(source, 5.5f, CSOLBeamDamage::ShootB1); Effect(1, 0, source); break;
    case BeamAttack::Secondary2:
        UTIL_TraceLine(source, source + gpGlobals->v_forward * (10 * Metre), dont_ignore_monsters, m_pPlayer->edict(), &trace);
        Blast(trace.vecEndPos + trace.vecPlaneNormal, 4, CSOLBeamDamage::ShootB2);
        Effect(2, damageMode.value==1 ? 1 : damageMode.value==2 ? 2 : 0, trace.vecEndPos); break;
    case BeamAttack::ChargedPrimary:
        UTIL_TraceLine(source, source + gpGlobals->v_forward * (100 * Metre), dont_ignore_monsters, m_pPlayer->edict(), &trace);
        Blast(trace.vecEndPos + trace.vecPlaneNormal, 6, CSOLBeamDamage::ChargeShootA);
        Effect(3, 0, trace.vecEndPos); KickBack(5, 0, 0, 0, 10, 0, 0); break;
    case BeamAttack::FullCharge:
        // CSO FullChargeGodTime=1; independent of FullChargeDuration=7.
        if (CSOL_ZombieHuman(m_pPlayer)) m_flGuardUntil = gpGlobals->time + 1.0f;
        m_flNextCharge = gpGlobals->time + 7.0f;
        Blast(source, 6, CSOLBeamDamage::FullChargeExplosion); Effect(6, 0, source); break;
    case BeamAttack::DiveStart:
    {
        // CSO ChargeShootBGodTime=1. Grant once on the attack transition.
        if (CSOL_ZombieHuman(m_pPlayer)) m_flGuardUntil = gpGlobals->time + 1.0f;
        // Forward/backward addition is supported by the supplied DEM. Strafe
        // extension follows the same view basis; diagonal behavior is provisional.
        const int buttons = m_pPlayer->pev->button;
        const int forward = !!(buttons & IN_FORWARD) - !!(buttons & IN_BACK);
        const int side = !!(buttons & IN_MOVERIGHT) - !!(buttons & IN_MOVELEFT);
        m_pPlayer->pev->velocity.x += 250 * (forward * gpGlobals->v_forward.x + side * gpGlobals->v_right.x);
        m_pPlayer->pev->velocity.y += 250 * (forward * gpGlobals->v_forward.y + side * gpGlobals->v_right.y);
        m_pPlayer->pev->velocity.z = 350;
        m_pPlayer->m_flFallVelocity = 0;
        Effect(4, 1, m_pPlayer->pev->origin); break;
    }
    case BeamAttack::DiveEnd:
        m_pPlayer->m_flFallVelocity = 0;
        Blast(m_pPlayer->pev->origin, 7, CSOLBeamDamage::ChargeShootBExplosion);
        Effect(4, 3, m_pPlayer->pev->origin); break;
    default: break;
    }
}

void CBeamGun::PrimaryAttack()
{
    if (!CanAttackNow()) return;
    Pull(); Apply(m_state.Primary(m_fFireOnEmpty != FALSE));
}
void CBeamGun::SecondaryAttack()
{
    if (!CanAttackNow()) return;
    Pull(); Apply(m_state.Secondary());
}
void CBeamGun::Reload()
{
    if (m_state.dive != csol::BeamDive::None || m_state.pending != csol::BeamPending::None) return;
    Pull();
    const bool reloaded = DefaultReload(ClipSize, m_state.ReloadAnimation(), 2.3f) != FALSE;
    Pull(); Apply(m_state.ReloadStarted(reloaded));
    if (reloaded) { m_flAccuracy = 0.3f; m_iShotsFired = 0; m_bDelayFire = false; }
}
void CBeamGun::WeaponIdle() { Pull(); Apply(m_state.Idle()); }
void CBeamGun::ItemPostFrame()
{
    Pull();
    if (!CanAttackNow())
    {
        // Complete an already-started native reload even when firing becomes
        // disallowed; the prediction adapter uses the same transfer boundary.
        if (m_pPlayer && m_pPlayer->IsAlive() && m_fInReload) CBasePlayerWeapon::ItemPostFrame();
        return;
    }
    const auto full = m_state.FullCharge(m_pPlayer->m_afButtonPressed);
    if (full.attack != csol::BeamAttack::None) { Apply(full); return; }
    Apply(m_state.DelayedAttack());
    Apply(m_state.ExpireCharge());
    if (m_state.dive != csol::BeamDive::None)
    {
        Apply(m_state.UpdateDive((m_pPlayer->pev->flags & FL_ONGROUND) != 0, m_pPlayer->pev->movetype));
        return;
    }
    CBasePlayerWeapon::ItemPostFrame();
}

void CBeamGun::Wingmen()
{
    if (gpGlobals->time < m_flNextWing || !CanAttackNow()) return;
    struct Target { CBasePlayer* player; float distance; };
    std::array<Target, 32> candidates{};
    int count = 0;
    const Vector source = m_pPlayer->GetGunPosition();
    for (int i = 1; i <= gpGlobals->maxClients && count < int(candidates.size()); ++i)
    {
        auto* target = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));
        if (!target || target == m_pPlayer || target->IsDormant() || !target->IsAlive() ||
            target->m_iTeam == m_pPlayer->m_iTeam) continue;
        const float distance = (target->pev->origin - m_pPlayer->pev->origin).Length();
        if (distance > 8 * Metre) continue;
        TraceResult trace;
        UTIL_TraceLine(source, target->BodyTarget(source), dont_ignore_monsters, m_pPlayer->edict(), &trace);
        if (trace.pHit == target->edict()) candidates[count++] = {target, distance};
    }
    // Multi-target order is a deterministic fallback until an original
    // multi-target recording is available. Test2.dem has1/4 damage callbacks
    // but1/2 visual tracers per far/near event; these are separate counts.
    std::sort(candidates.begin(), candidates.begin() + count, [](const auto& a, const auto& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.player->entindex() < b.player->entindex();
    });
    const bool inner = count && candidates[0].distance <= 5 * Metre;
    m_flNextWing = gpGlobals->time + (inner ? 0.3f : 0.6f);
    if (!count) return;
    std::array<int, 4> targets{};
    const int shots = inner ? 4 : std::min(count, 4);
    for (int shot = 0; shot < shots; ++shot)
    {
        auto* target = candidates[shot % std::min(count, 4)].player;
        targets[shot] = target->entindex();
        const float damage = Damage(inner ? CSOLBeamDamage::WingManAttack2 : CSOLBeamDamage::WingManAttack1);
        if (damage > 0 && target->IsAlive()) target->TakeDamage(pev, m_pPlayer->pev, damage, DMG_BULLET);
    }
    if(inner)
    {
        targets={candidates[0].player->entindex(),candidates[0].player->entindex(),0,0};
        Effect(5, 1, candidates[0].player->BodyTarget(source), targets, 2);
    }
    else Effect(5, 0, candidates[0].player->BodyTarget(source), targets, shots);
}

void CBeamGun::Advance(float seconds)
{
    Pull(); m_state.Decrement(seconds);
    if (m_pPlayer && !m_pPlayer->IsAlive())
    {
        ResetGuard();
        if (m_state.dive != csol::BeamDive::None) Effect(4, 0, m_pPlayer->pev->origin);
        m_state.Removed(); m_state.ResetAction(); m_state.reloading = false;
    }
    if (m_pPlayer && m_pPlayer->IsAlive() && m_pPlayer->m_pActiveItem == this)
    {
        Q_strlcpy(m_pPlayer->m_szAnimExtention, PlayerAnimationExtension());
        if (m_state.charge != csol::BeamCharge::Beta && gpGlobals->time >= m_flNextCharge)
        {
            m_state.energy = Q_min(100, m_state.energy + 5);
            m_flNextCharge = gpGlobals->time + 1.0f;
        }
        Wingmen();
    }
    Push();
}
void CBeamGun::Pack(weapon_data_t* output) { Pull(); csol::WriteBeamPrediction(m_state, *output); }
void CSOL_InitBeamWeapons() { CVAR_REGISTER(&damageMode); }
void CSOL_PrecacheBeamWeapons()
{
    beamPrecached = false;
    if (!effectMessage) effectMessage = REG_USER_MSG(csol::BeamEffectMessage, csol::BeamEffectBytes);
    if (!guardMessage) guardMessage = REG_USER_MSG("CSOGuard",3);
    if (!CSOL_BeamDamageReady()) return;
    PRECACHE_MODEL("sprites/csol/640hud37.spr");
    UTIL_PrecacheOtherWeapon("weapon_beamgun");
    UTIL_PrecacheOtherWeapon("weapon_beamgunle");
    const auto* normal = CSOL_ItemInfo(725); const auto* le = CSOL_ItemInfo(726);
    beamPrecached = normal && normal->pszName && le && le->pszName;
}
void CSOL_PackBeamWeapon(CBasePlayerWeapon* weapon, weapon_data_t* data)
{
    if (CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId)) static_cast<CBeamGun*>(weapon)->Pack(data);
}
void CSOL_AdvanceBeamWeapon(CBasePlayerWeapon* weapon, float seconds)
{
    if (CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId)) static_cast<CBeamGun*>(weapon)->Advance(seconds);
}
void CSOL_RemoveBeamWeapon(CBasePlayerItem* item)
{
    if (!item || !item->IsWeapon()) return;
    auto* weapon = static_cast<CBasePlayerWeapon*>(item);
    if (CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId)) static_cast<CBeamGun*>(weapon)->Removed();
}

bool CSOL_BeamGuardBlocks(CBasePlayer* player, float damage)
{
    if (!(damage > 0) || !CSOL_ZombieHuman(player)) return false;
    for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
        for (auto* item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext)
            if (item->IsWeapon()) {
                auto* weapon = static_cast<CBasePlayerWeapon*>(item);
                if (CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId) &&
                    static_cast<CBeamGun*>(weapon)->Guarded()) return true;
            }
    return false;
}
void CSOL_ResetBeamGuard(CBasePlayer* player)
{
    if (!player) return;
    for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
        for (auto* item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext)
            if (item->IsWeapon()) {
                auto* weapon = static_cast<CBasePlayerWeapon*>(item);
                if (CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId))
                    static_cast<CBeamGun*>(weapon)->ResetGuard();
            }
}

void CSOL_UpdateBeamGuardHUD(CBasePlayer* player)
{
    // Reliable owner-only state. Holstering retains protection; removing the
    // item, dying or changing ZP role makes the next update send zero.
    struct State { int userid{}; float until{}; };
    static std::array<State,33> states{};
    if(!guardMessage)return;
    const int index=player->entindex();
    if(index<1 || index>=int(states.size()))return;
    auto& cached=states[index];
    const int userid=GETPLAYERUSERID(player->edict());
    float until=0;
    if(CSOL_ZombieHuman(player))
        for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
            for(auto* item=player->m_rgpPlayerItems[slot];item;item=item->m_pNext)
                if(item->IsWeapon()){
                    auto* weapon=static_cast<CBasePlayerWeapon*>(item);
                    if(CSOL_IsWeapon(weapon) && csol::BeamWeaponId(weapon->m_iId)){
                        const auto* beam=static_cast<CBeamGun*>(weapon);
                        if(beam->Guarded())until=Q_max(until,beam->GuardUntil());
                    }
                }
    if(cached.userid==userid && cached.until==until && player->m_fWeapon)return;
    MESSAGE_BEGIN(MSG_ONE,guardMessage,nullptr,player->pev);
        WRITE_BYTE(1);
        WRITE_SHORT(int(std::ceil(std::clamp(until-gpGlobals->time,0.0f,1.0f)*1000.0f)));
    MESSAGE_END();
    cached={userid,until};
}
