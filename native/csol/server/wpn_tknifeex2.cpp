// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"

namespace
{
cvar_t csol_weapons = { "csol_weapons", "0", FCVAR_SERVER, 0, nullptr };
// One ready throw plus 29 reserve throws, 30 in total.
constexpr int TKNIFE_DEFAULT_GIVE = 30;
constexpr int TKNIFE_MAX_CARRY = TKNIFE_DEFAULT_GIVE - 1;
constexpr char TKNIFE_AMMO[] = "TKnife";
enum tknifeex2_e { TKNIFE_IDLE, TKNIFE_SHOOT, TKNIFE_DRAW, TKNIFE_SLASH };
const char* const impactSounds[] = {
    "weapons/tknife_stone1.wav", "weapons/tknife_stone2.wav",
    "weapons/tknife_wood1.wav", "weapons/tknife_wood2.wav",
    "weapons/tknife_metal1.wav", "weapons/tknife_metal2.wav", "weapons/tknife_metal3.wav"
};
const char* const fleshSounds[] = {
    "weapons/combatknife_hit_1.wav", "weapons/axe_hit1.wav", "weapons/axe_hit2.wav"
};

void FleshSound(CBaseEntity* entity)
{
    EMIT_SOUND_DYN(entity->edict(), CHAN_BODY, fleshSounds[RANDOM_LONG(0, 2)], 1, ATTN_NORM, 0, PITCH_NORM);
}
}

class CThrowingKnife : public CBaseEntity
{
public:
    void Spawn() override;
    void Precache() override;
    int ObjectCaps() override { return FCAP_DONT_SAVE; }
    static void Shoot(CBasePlayer* owner, const Vector& origin, const Vector& direction);
    void FlyingThink();
    void KnifeTouch(CBaseEntity* other);
    void ImpactSound(const TraceResult& trace);

private:
    EHANDLE m_hThrower;
    Vector m_vecPreviousVelocity;
    float m_flExpireTime;
    float m_flDamage;
};

LINK_ENTITY_TO_CLASS(throwingknife_entity, CThrowingKnife, CCSEntity)

void CThrowingKnife::Precache()
{
    PRECACHE_MODEL("models/tknifeb.mdl");
    for (auto sound : impactSounds) PRECACHE_SOUND(const_cast<char*>(sound));
    for (auto sound : fleshSounds) PRECACHE_SOUND(const_cast<char*>(sound));
}

void CThrowingKnife::Spawn()
{
    Precache();
    SET_MODEL(edict(), "models/tknifeb.mdl");
    pev->movetype = MOVETYPE_BOUNCE;
    pev->solid = SOLID_BBOX;
    pev->gravity = 0.2f;
    pev->health = 1;
    pev->takedamage = DAMAGE_NO;
    UTIL_SetSize(pev, g_vecZero, g_vecZero);
    m_flExpireTime = gpGlobals->time + 10.0f;
    m_flDamage = 70.0f;
    SetTouch(&CThrowingKnife::KnifeTouch);
    SetThink(&CThrowingKnife::FlyingThink);
    pev->nextthink = gpGlobals->time + 0.1f;
}

void CThrowingKnife::Shoot(CBasePlayer* owner, const Vector& origin, const Vector& direction)
{
    auto* knife = GetClassPtr<CCSEntity>(static_cast<CThrowingKnife*>(nullptr));
    if (!knife) return;
    knife->pev->classname = MAKE_STRING("throwingknife_entity");
    knife->Spawn();
    knife->m_hThrower = owner;
    knife->pev->owner = owner->edict();
    UTIL_SetOrigin(knife->pev, origin);
    knife->pev->velocity = direction * 2000.0f + owner->pev->velocity * 0.5f;
    knife->m_vecPreviousVelocity = knife->pev->velocity;
    knife->pev->angles = UTIL_VecToAngles(direction);
    knife->pev->angles.z += 90.0f;
    knife->pev->avelocity.x = -1800.0f;
}

void CThrowingKnife::FlyingThink()
{
    if (!IsInWorld() || gpGlobals->time >= m_flExpireTime)
    {
        UTIL_Remove(this);
        return;
    }
    m_vecPreviousVelocity = pev->velocity;
    pev->nextthink = gpGlobals->time + 0.1f;
}

void CThrowingKnife::ImpactSound(const TraceResult& trace)
{
    const Vector direction = m_vecPreviousVelocity.Normalize();
    const char* texture = TRACE_TEXTURE(trace.pHit, pev->origin - direction, pev->origin + direction);
    char material = CHAR_TEX_CONCRETE;
    if (texture)
    {
        if (*texture == '-' || *texture == '+') texture += 2;
        if (*texture == '{' || *texture == '!' || *texture == '~' || *texture == ' ') ++texture;
        material = TEXTURETYPE_Find(const_cast<char*>(texture));
    }
    int index = RANDOM_LONG(0, 1);
    if (material == CHAR_TEX_WOOD) index += 2;
    else if (material == CHAR_TEX_METAL || material == CHAR_TEX_GRATE) index = 4 + RANDOM_LONG(0, 2);
    EMIT_SOUND_DYN(edict(), CHAN_BODY, impactSounds[index], 0.9f, ATTN_NORM, 0, RANDOM_LONG(96, 111));
}

void CThrowingKnife::KnifeTouch(CBaseEntity* other)
{
    if (other == m_hThrower) return;
    TraceResult trace = UTIL_GetGlobalTrace();
    if (other->pev->takedamage != DAMAGE_NO)
    {
        // EHANDLE prevents a disconnected owner's reused edict becoming the attacker.
        CBaseEntity* owner = m_hThrower;
        auto* attacker = owner ? owner->pev : pev;
        ClearMultiDamage();
        other->TraceAttack(attacker, m_flDamage, m_vecPreviousVelocity.Normalize(), &trace, DMG_BULLET);
        ApplyMultiDamage(pev, attacker);
        FleshSound(this);
        UTIL_Remove(this);
        return;
    }
    ImpactSound(trace);
    if (other->IsBSPModel() && m_vecPreviousVelocity.Length() > 0 &&
        -DotProduct(m_vecPreviousVelocity.Normalize(), trace.vecPlaneNormal) >= 0.258819045f)
    {
        // Original CSO converts an embedded knife into a client temp model.
        // Keep the same visible orientation/lifetime as a networked entity here.
        pev->angles = UTIL_VecToAngles(-m_vecPreviousVelocity);
        pev->angles.z += 90.0f;
        pev->velocity = pev->avelocity = g_vecZero;
        pev->movetype = MOVETYPE_NONE;
        pev->solid = SOLID_NOT;
        SetTouch(nullptr);
        m_flExpireTime = gpGlobals->time + 5.0f;
        return;
    }
    // GoldSrc's bounce mover handles plane reflection after Touch.
    pev->velocity = pev->velocity * 0.25f;
}

class CTKnifeEx2 : public CBasePlayerWeapon
{
public:
    int iItemSlot() override { return PISTOL_SLOT; }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal = 0) override;
    BOOL UseDecrement() override { return TRUE; }
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void ItemPostFrame() override;
    void WeaponIdle() override;
    void ThrowKnife(int ordinal);

private:
    int m_iBurstRemaining;
    float m_flNextThrow;
};

LINK_ENTITY_TO_CLASS(weapon_tknifeex2, CTKnifeEx2, CCSPlayerWeapon)

void CTKnifeEx2::Spawn()
{
    Precache();
    m_iId = CSOL_WEAPON_TKNIFEEX2;
    SET_MODEL(edict(), "models/w_tknifeex2.mdl");
    m_iDefaultAmmo = TKNIFE_DEFAULT_GIVE;
    m_iBurstRemaining = 0;
    m_flNextThrow = 0;
    FallInit();
    CBasePlayerWeapon::Spawn();
}

void CTKnifeEx2::Precache()
{
    PRECACHE_MODEL("models/v_tknifeex2.mdl");
    PRECACHE_MODEL("models/p_tknifeex2.mdl");
    PRECACHE_MODEL("models/w_tknifeex2.mdl");
    UTIL_PrecacheOther("throwingknife_entity");
    PRECACHE_SOUND("weapons/tknife_shoot1.wav");
    PRECACHE_SOUND("weapons/tknifeex2_draw.wav");
    PRECACHE_SOUND("weapons/tknifeex2_shoot1.wav");
    PRECACHE_GENERIC("sprites/weapon_tknifeex2.txt");
    PRECACHE_MODEL("sprites/csol/640hud97.spr");
    PRECACHE_MODEL("sprites/csol/640hud12.spr");
    PRECACHE_MODEL("sprites/csol/640hud7.spr");
}

int CTKnifeEx2::GetItemInfo(ItemInfo* info)
{
    info->pszName = "weapon_tknifeex2";
    info->pszAmmo1 = const_cast<char*>(TKNIFE_AMMO);
    info->iMaxAmmo1 = TKNIFE_MAX_CARRY;
    info->pszAmmo2 = nullptr;
    info->iMaxAmmo2 = -1;
    info->iMaxClip = 1;
    info->iSlot = 1;
    info->iPosition = 6;
    info->iId = CSOL_WEAPON_TKNIFEEX2;
    info->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;
    info->iWeight = 5;
    return 1;
}

BOOL CTKnifeEx2::Deploy()
{
    m_iBurstRemaining = 0;
    m_fMaxSpeed = 250;
    return DefaultDeploy("models/v_tknifeex2.mdl", "models/p_tknifeex2.mdl", TKNIFE_DRAW, "grenade", 0);
}

void CTKnifeEx2::Holster(int skiplocal)
{
    m_iBurstRemaining = 0;
    m_fInReload = FALSE;
    CBasePlayerWeapon::Holster(skiplocal);
}

void CTKnifeEx2::ThrowKnife(int ordinal)
{
    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    float spread = !(m_pPlayer->pev->flags & FL_ONGROUND) ? 0.04f :
        m_pPlayer->pev->velocity.Length2D() > 0 ? 0.02f :
        (m_pPlayer->pev->flags & FL_DUCKING) ? 0.0001f : 0.0003f;
    spread += ordinal * 0.01f;
    const unsigned seed = m_pPlayer->random_seed;
    const float x = UTIL_SharedRandomFloat(seed, -0.5, 0.5) + UTIL_SharedRandomFloat(seed + 1, -0.5, 0.5);
    const float y = UTIL_SharedRandomFloat(seed + 2, -0.5, 0.5) + UTIL_SharedRandomFloat(seed + 3, -0.5, 0.5);
    const Vector direction = gpGlobals->v_forward + gpGlobals->v_right * (x * spread) + gpGlobals->v_up * (y * spread);
    Vector origin = m_pPlayer->GetGunPosition();
    if (ordinal)
    {
        const float radius = ordinal * 5.0f;
        for (int i = 0; i < 3; ++i) origin[i] += RANDOM_FLOAT(-radius, radius);
    }
    CThrowingKnife::Shoot(m_pPlayer, origin, direction);
}

void CTKnifeEx2::PrimaryAttack()
{
    if (m_iClip <= 0) { Reload(); return; }
    --m_iClip;
    SendWeaponAnim(TKNIFE_SHOOT, UseDecrement());
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    ThrowKnife(0);
    m_iBurstRemaining = 2;
    m_flNextThrow = gpGlobals->time + 0.05f;
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = 0.9f;
    m_flTimeWeaponIdle = 1.6f;
}

void CTKnifeEx2::SecondaryAttack()
{
    if (!m_pPlayer->m_bCanShoot || g_pGameRules->IsFreezePeriod()) return;
    SendWeaponAnim(TKNIFE_SLASH, UseDecrement());
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    EMIT_SOUND_DYN(m_pPlayer->edict(), CHAN_WEAPON, "weapons/tknifeex2_shoot1.wav", 0.5f, ATTN_NORM, 0, PITCH_NORM);
    UTIL_MakeVectors(m_pPlayer->pev->v_angle);
    const Vector source = m_pPlayer->GetGunPosition();
    const Vector forward = gpGlobals->v_forward;
    CBaseEntity* target = nullptr;
    while ((target = UTIL_FindEntityInSphere(target, source, 85.0f)))
    {
        if (target == m_pPlayer || target->pev->takedamage == DAMAGE_NO) continue;
        const Vector delta = target->BodyTarget(source) - source;
        if (DotProduct(forward.Make2D().Normalize(), delta.Make2D().Normalize()) < 0.5f) continue;
        TraceResult trace;
        UTIL_TraceLine(source, target->BodyTarget(source), dont_ignore_monsters, m_pPlayer->edict(), &trace);
        if (trace.pHit != target->edict()) continue;
        ClearMultiDamage();
        target->TraceAttack(m_pPlayer->pev, 35.0f, delta.Normalize(), &trace, DMG_BULLET);
        ApplyMultiDamage(m_pPlayer->pev, m_pPlayer->pev);
        FleshSound(m_pPlayer);
    }
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = 1.0f;
    m_flTimeWeaponIdle = 1.1f;
}

void CTKnifeEx2::Reload()
{
    if (m_iClip || m_iBurstRemaining || m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
    // No reload animation: original CSO transfers one reserve round next frame.
    m_fInReload = TRUE;
    m_pPlayer->m_flNextAttack = 0;
}

void CTKnifeEx2::ItemPostFrame()
{
    if (m_iBurstRemaining && gpGlobals->time >= m_flNextThrow)
    {
        ThrowKnife(3 - m_iBurstRemaining);
        --m_iBurstRemaining;
        m_flNextThrow = gpGlobals->time + 0.05f;
    }
    // CSO retires an exhausted knife after the last projectile in the burst,
    // even while the attack button remains down.
    if (!m_iBurstRemaining && !m_iClip && !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
    {
        RetireWeapon();
        DestroyItem();
        return;
    }
    CBasePlayerWeapon::ItemPostFrame();
}

void CTKnifeEx2::WeaponIdle()
{
    if (m_flTimeWeaponIdle > 0 || m_iBurstRemaining) return;
    ResetEmptySound();
    SendWeaponAnim(TKNIFE_IDLE, UseDecrement());
    m_flTimeWeaponIdle = 20.0f;
}

void CSOL_InitWeapons()
{
    CVAR_REGISTER(&csol_weapons);
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
    CSOL_InitWeaponTests();
#endif
}

bool CSOL_IsWeapon(const CBasePlayerWeapon* weapon)
{
    return weapon && weapon->m_iId == CSOL_WEAPON_TKNIFEEX2 &&
        !Q_strcmp(STRING(weapon->pev->classname), "weapon_tknifeex2");
}

const char* CSOL_WorldModel(const CBasePlayerItem* item)
{
    if (item && item->m_iId == CSOL_WEAPON_TKNIFEEX2 &&
        !Q_strcmp(STRING(item->pev->classname), "weapon_tknifeex2"))
        return "models/w_tknifeex2.mdl";
    return nullptr;
}

void CSOL_PrecacheWeapons()
{
    if (csol_weapons.value != 0) UTIL_PrecacheOtherWeapon("weapon_tknifeex2");
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
    CSOL_PrecacheWeaponTests();
#endif
}
