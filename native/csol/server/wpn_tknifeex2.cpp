// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"
#include "weapon_damage_runtime.h"
#include "wpn_beamgun.h"
#include "wpn_cannon.h"
#include "characters.h"
#include "../projectile_effects.hpp"

namespace
{
cvar_t csol_weapons = { "csol_weapons", "0", FCVAR_SERVER, 0, nullptr };
bool weaponsPrecached{};
int knifeTrailModelIndex{};
int knifeTrailMessage{};
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
cvar_t csol_projectile_trace = { "csol_projectile_trace", "0", 0, 0, nullptr };
void ProjectileTrace(const char* event, CBaseEntity* knife, float age, float damage = 0)
{
    if (csol_projectile_trace.value == 0) return;
    SERVER_PRINT(UTIL_VarArgs("[CSOLProjectile] event=%s entity=%d age=%.5f damage=%.5f move=%d vx=%.5f vy=%.5f vz=%.5f\n",
        event, knife->entindex(), age, damage, knife->pev->movetype,
        knife->pev->velocity.x, knife->pev->velocity.y, knife->pev->velocity.z));
}
#else
void ProjectileTrace(const char*, CBaseEntity*, float, float = 0) {}
#endif
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

class CThrowingKnife : public CBaseEntity, public CSOLWeaponDamageSource
{
public:
    void Spawn() override;
    void Precache() override;
    int ObjectCaps() override { return FCAP_DONT_SAVE; }
    static CThrowingKnife* Shoot(CBasePlayer* owner, int weaponId, const Vector& origin, const Vector& direction);
    int DamageWeaponId() const override { return m_iSourceWeaponId; }
    void FlyingThink();
    void KnifeTouch(CBaseEntity* other);
    void ImpactSound(const TraceResult& trace);
    void Bounce(const TraceResult& trace);
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
    static void TestProjectile();
    static void MapProjectile();
#endif

private:
    int m_iSourceWeaponId{};
    EHANDLE m_hThrower;
    Vector m_vecPreviousVelocity;
    Vector m_vecPreviousTouchOrigin;
    float m_flSpawnTime;
    float m_flPreviousTouchTime;
    float m_flExpireTime;
    float m_flDamage;
};

LINK_ENTITY_TO_CLASS(throwingknife_entity, CThrowingKnife, CCSEntity)

const char* CSOL_DeathWeapon(const entvars_t* inflictor)
{
    // The projectile keeps its weapon identity even if its owner switches
    // weapons before impact. Melee already uses the active weapon's name.
    return inflictor && FStrEq(STRING(inflictor->classname), "throwingknife_entity") ? "tknifeex2" : nullptr;
}

void CThrowingKnife::Precache()
{
    PRECACHE_MODEL("models/tknifeb.mdl");
    knifeTrailModelIndex = PRECACHE_MODEL("sprites/smoke.spr");
    for (auto sound : impactSounds) PRECACHE_SOUND(const_cast<char*>(sound));
    for (auto sound : fleshSounds) PRECACHE_SOUND(const_cast<char*>(sound));
}

void CThrowingKnife::Spawn()
{
    if (!weaponsPrecached) { UTIL_Remove(this); return; }
    Precache();
    SET_MODEL(edict(), "models/tknifeb.mdl");
    pev->movetype = MOVETYPE_BOUNCE;
    pev->solid = SOLID_BBOX;
    pev->gravity = 0.2f;
    pev->health = 1;
    pev->takedamage = DAMAGE_NO;
    UTIL_SetSize(pev, g_vecZero, g_vecZero);
    m_flSpawnTime = gpGlobals->time;
    m_flExpireTime = m_flSpawnTime + 10.0f;
    m_flPreviousTouchTime = 0;
    m_vecPreviousTouchOrigin = g_vecZero;
    m_flDamage = 70.0f;
    SetTouch(&CThrowingKnife::KnifeTouch);
    SetThink(&CThrowingKnife::FlyingThink);
    pev->nextthink = gpGlobals->time + 0.1f;
    MESSAGE_BEGIN(MSG_ALL, knifeTrailMessage);
        WRITE_BYTE(csol::KnifeEffectVersion);
        WRITE_SHORT(entindex());
        WRITE_SHORT(knifeTrailModelIndex);
    MESSAGE_END();
}

CThrowingKnife* CThrowingKnife::Shoot(CBasePlayer* owner, int weaponId, const Vector& origin, const Vector& direction)
{
    if (!weaponsPrecached) return nullptr;
    auto* knife = GetClassPtr<CCSEntity>(static_cast<CThrowingKnife*>(nullptr));
    if (!knife) return nullptr;
    knife->pev->classname = MAKE_STRING("throwingknife_entity");
    knife->Spawn();
    knife->m_iSourceWeaponId = weaponId;
    knife->m_hThrower = owner;
    knife->pev->owner = owner->edict();
    UTIL_SetOrigin(knife->pev, origin);
    knife->pev->velocity = direction * 2000.0f + owner->pev->velocity * 0.5f;
    // CSO starts this at zero; the first 100 ms Think records the velocity.
    // Initializing it to launch speed makes every nearby wall embed too early.
    knife->m_vecPreviousVelocity = g_vecZero;
    knife->pev->angles = UTIL_VecToAngles(direction);
    knife->pev->angles.z += 90.0f;
    knife->pev->avelocity.x = -1800.0f;
    return knife;
}

void CThrowingKnife::FlyingThink()
{
    if (!IsInWorld() || gpGlobals->time > m_flExpireTime)
    {
        ProjectileTrace("expired", this, gpGlobals->time - m_flSpawnTime);
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
    if (!other || other == m_hThrower) return;
    TraceResult trace = UTIL_GetGlobalTrace();
    if (other->pev->takedamage != DAMAGE_NO)
    {
        const bool transparentBrush = other->pev->rendermode != kRenderNormal &&
            (FClassnameIs(other->pev, "func_breakable") || FClassnameIs(other->pev, "func_pushable"));
        // EHANDLE prevents a disconnected owner's reused edict becoming the attacker.
        CBaseEntity* owner = m_hThrower;
        auto* attacker = owner ? owner->pev : pev;
        float damage = m_flDamage - 10.0f * (gpGlobals->time - m_flSpawnTime);
        if (damage < 1.0f) damage = 0;
        ProjectileTrace("damage", this, gpGlobals->time - m_flSpawnTime, damage);
        ClearMultiDamage();
        other->TraceAttack(attacker, damage, pev->velocity.Normalize(), &trace, DMG_BULLET);
        ApplyMultiDamage(pev, attacker);
        if (transparentBrush)
        {
            // Honor CS breakable material/trigger rules through TakeDamage.
            // A surviving pane bounces once in SV_Physics_Toss after Touch.
            // Negating here would reflect twice and trap the knife in glass.
            if (other->pev->solid != SOLID_NOT && other->pev->health > 0)
            {
                Bounce(trace);
                return;
            }
            ProjectileTrace("glass-broken", this, gpGlobals->time - m_flSpawnTime, damage);
            UTIL_Remove(this);
            return;
        }
        FleshSound(this);
        UTIL_Remove(this);
        return;
    }
    if (other->IsBSPModel() && m_vecPreviousVelocity.Length() > 0 &&
        -DotProduct(m_vecPreviousVelocity.Normalize(), trace.vecPlaneNormal) >= 0.258819045f)
    {
        ImpactSound(trace);
        // Original CSO converts an embedded knife into a client temp model.
        // Temporary network-entity fallback; TE138 orientation/fade is still pending.
        pev->angles = UTIL_VecToAngles(-m_vecPreviousVelocity);
        pev->angles.z += 90.0f;
        pev->velocity = pev->avelocity = g_vecZero;
        pev->movetype = MOVETYPE_NONE;
        pev->solid = SOLID_NOT;
        SetTouch(nullptr);
        m_flExpireTime = gpGlobals->time + 5.0f;
        ProjectileTrace("embedded", this, gpGlobals->time - m_flSpawnTime);
        return;
    }
    Bounce(trace);
}

void CThrowingKnife::Bounce(const TraceResult& trace)
{
    pev->velocity = pev->velocity * 0.5f;
    if (m_flPreviousTouchTime != 0 && gpGlobals->time - m_flPreviousTouchTime < 0.2f &&
        (m_vecPreviousTouchOrigin - pev->origin).Length() < 1.0f)
    {
        m_flPreviousTouchTime = gpGlobals->time;
        pev->velocity = pev->avelocity = g_vecZero;
        pev->movetype = MOVETYPE_TOSS;
        SetTouch(nullptr);
        ProjectileTrace("stopped", this, gpGlobals->time - m_flSpawnTime);
        return;
    }
    m_flPreviousTouchTime = gpGlobals->time;
    m_vecPreviousTouchOrigin = pev->origin;
    // CSO applies its 0.5 factor twice. GoldSrc reflects the plane after Touch.
    pev->velocity = pev->velocity * 0.5f;
    ProjectileTrace("bounce", this, gpGlobals->time - m_flSpawnTime);
    ImpactSound(trace);
}

#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
namespace
{
class CKnifeDamageProbe : public CBaseEntity
{
public:
    void TraceAttack(entvars_t*, float damage, Vector, TraceResult*, int) override { receivedDamage = damage; }
    float receivedDamage = -1;
};
}

void CThrowingKnife::TestProjectile()
{
    if (!CSOL_WeaponsReady()) { SERVER_PRINT("[CSOLProjectileTest] unavailable\n"); return; }
    int passed = 0, total = 0;
    auto check = [&](const char* name, bool valid, float actual)
    {
        ++total;
        if (valid) ++passed;
        SERVER_PRINT(UTIL_VarArgs("[CSOLProjectileTest] case=%s pass=%d actual=%.5f\n", name, valid, actual));
    };
    auto makeKnife = []()
    {
        auto* knife = GetClassPtr<CCSEntity>(static_cast<CThrowingKnife*>(nullptr));
        knife->pev->classname = MAKE_STRING("throwingknife_entity");
        knife->Spawn();
        knife->pev->velocity = Vector(100, -200, 50);
        knife->m_vecPreviousVelocity = knife->pev->velocity;
        knife->pev->avelocity = Vector(-1800, 0, 0);
        return knife;
    };
    auto* target = GetClassPtr<CCSEntity>(static_cast<CKnifeDamageProbe*>(nullptr));
    target->pev->classname = MAKE_STRING("csol_damage_probe");
    target->pev->solid = SOLID_NOT;
    target->pev->takedamage = DAMAGE_YES;
    // Original x86 oracle vectors, through the actual Touch implementation.
    struct DamageCase { float age, base, expected; const char* name; };
    const DamageCase damageCases[] = {
        {0, 70, 70, "fresh"}, {0.5f, 70, 65, "half-second"}, {1, 70, 60, "one-second"},
        {5, 70, 20, "five-seconds"}, {7, 70, 0, "exhausted"},
        {0, 1, 1, "one-damage-kept"}, {0, 0.99f, 0, "below-one-zero"}
    };
    for (const auto& test : damageCases)
    {
        auto* knife = makeKnife();
        knife->m_flSpawnTime = gpGlobals->time - test.age;
        knife->m_flDamage = test.base;
        target->receivedDamage = -1;
        knife->KnifeTouch(target);
        check(test.name, fabs(target->receivedDamage - test.expected) < 0.001f && (knife->pev->flags & FL_KILLME), target->receivedDamage);
        UTIL_Remove(knife);
    }
    for (const char* name : {"func_breakable", "func_pushable"})
    {
        auto* knife = makeKnife();
        target->pev->classname = ALLOC_STRING(name);
        target->pev->rendermode = kRenderTransTexture;
        target->pev->solid = SOLID_BSP;
        target->pev->health = 100;
        target->receivedDamage = -1;
        knife->KnifeTouch(target);
        // This branch intentionally follows the user's CS glass requirement;
        // it is not the older CSO binary's transparent-object immunity branch.
        check(name, target->receivedDamage == 70 && knife->pev->velocity == Vector(25, -50, 12.5f) &&
            !(knife->pev->flags & FL_KILLME) && knife->m_flDamage == 70, knife->pev->velocity.x);
        UTIL_Remove(knife);
    }
    // Non-normal rendering alone must not make an ordinary damage target bounce.
    target->pev->classname = MAKE_STRING("csol_damage_probe");
    auto* knife = makeKnife();
    knife->KnifeTouch(target);
    check("transparent-other-damaged", target->receivedDamage == 70, target->receivedDamage);
    UTIL_Remove(knife);
    target->pev->classname = MAKE_STRING("func_breakable");
    target->pev->rendermode = kRenderNormal;
    knife = makeKnife();
    knife->KnifeTouch(target);
    check("opaque-breakable-damaged", target->receivedDamage == 70, target->receivedDamage);
    UTIL_Remove(knife);
    UTIL_Remove(target);

    TraceResult trace{};
    trace.pHit = INDEXENT(0);
    struct BounceCase { float gap, distance; bool previous, stops; const char* name; };
    const BounceCase bounceCases[] = {
        {0.1f, 0, false, false, "first-collision"}, {0.1f, 0.5f, true, true, "rapid-near-collision"},
        {0.1f, 1, true, false, "one-unit-keeps-bouncing"}, {0.3f, 0.5f, true, false, "slow-near-collision"}
    };
    for (const auto& test : bounceCases)
    {
        knife = makeKnife();
        knife->m_flPreviousTouchTime = test.previous ? gpGlobals->time - test.gap : 0;
        knife->m_vecPreviousTouchOrigin = knife->pev->origin - Vector(test.distance, 0, 0);
        knife->Bounce(trace);
        bool valid = test.stops ? knife->pev->velocity == g_vecZero && knife->pev->avelocity == g_vecZero &&
            knife->pev->movetype == MOVETYPE_TOSS && !knife->m_pfnTouch :
            knife->pev->velocity == Vector(25, -50, 12.5f) && knife->pev->movetype == MOVETYPE_BOUNCE && knife->m_pfnTouch;
        check(test.name, valid, knife->pev->velocity.x);
        UTIL_Remove(knife);
    }
    for (float remaining : {0.01f, 0.0f, -0.01f})
    {
        knife = makeKnife();
        knife->m_flExpireTime = gpGlobals->time + remaining;
        knife->FlyingThink();
        check(remaining > 0 ? "before-expiry" : remaining == 0 ? "exact-expiry-kept" : "after-expiry-removed",
            bool(knife->pev->flags & FL_KILLME) == (remaining < 0), knife->pev->flags & FL_KILLME);
        UTIL_Remove(knife);
    }
    SERVER_PRINT(UTIL_VarArgs("[CSOLProjectileTest] passed=%d total=%d\n", passed, total));
}

void CThrowingKnife::MapProjectile()
{
    const char* action = CMD_ARGV(1);
    if (!Q_strcmp(action, "status"))
    {
        for (const char* classname : {"func_breakable", "throwingknife_entity"})
        {
            if (CMD_ARGC() == 3 && !Q_strcmp(CMD_ARGV(2), "knives") && Q_strcmp(classname, "throwingknife_entity")) continue;
            CBaseEntity* entity = nullptr;
            while ((entity = UTIL_FindEntityByClassname(entity, classname)))
            {
                const auto* v = entity->pev;
                SERVER_PRINT(UTIL_VarArgs("[CSOLMapProjectile] entity=%d class=%s health=%.3f damage=%.0f solid=%d render=%d move=%d origin=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f spin=%.3f bounds=%.3f,%.3f,%.3f:%.3f,%.3f,%.3f\n",
                    entity->entindex(), classname, v->health, v->takedamage, v->solid, v->rendermode, v->movetype,
                    v->origin.x, v->origin.y, v->origin.z, v->velocity.x, v->velocity.y, v->velocity.z, v->avelocity.x,
                    v->absmin.x, v->absmin.y, v->absmin.z, v->absmax.x, v->absmax.y, v->absmax.z));
            }
        }
        return;
    }
    if (Q_strcmp(action, "shoot") || (CMD_ARGC() != 4 && CMD_ARGC() != 5) || !CSOL_WeaponsReady()) return;
    const int index = Q_atoi(CMD_ARGV(2));
    const float distance = Q_atof(CMD_ARGV(3));
    const float age = CMD_ARGC() == 5 ? Q_atof(CMD_ARGV(4)) : 0;
    if (age < 0 || age > 9) return;
    if (index <= gpGlobals->maxClients || index >= gpGlobals->maxEntities || distance < 16 || distance > 1024) return;
    auto* target = CBaseEntity::Instance(INDEXENT(index));
    if (!target || !FClassnameIs(target->pev, "func_breakable") || target->pev->solid == SOLID_NOT) return;
    CBasePlayer* player = nullptr;
    for (int i = 1; i <= gpGlobals->maxClients; ++i)
    {
        auto* candidate = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));
        if (candidate && !candidate->IsDormant() && !(candidate->pev->flags & FL_FAKECLIENT)) { player = candidate; break; }
    }
    if (!player) return;
    const Vector center = (target->pev->absmin + target->pev->absmax) * 0.5f;
    const Vector size = target->pev->absmax - target->pev->absmin;
    int axis = size.x < size.y ? 0 : 1;
    if (size.z < size[axis]) axis = 2;
    Vector normal = g_vecZero;
    normal[axis] = player->pev->origin[axis] >= center[axis] ? 1.0f : -1.0f;
    const Vector start = center + normal * (size[axis] * 0.5f + distance);
    TraceResult trace;
    UTIL_TraceLine(start, center, dont_ignore_monsters, player->edict(), &trace);
    if (trace.fStartSolid || trace.pHit != target->edict()) { SERVER_PRINT("[CSOLMapProjectile] blocked\n"); return; }
    auto* knife = Shoot(player, CSOL_WEAPON_TKNIFEEX2, start, -normal);
    if (!knife) return;
    knife->m_flSpawnTime -= age;
    knife->m_flExpireTime -= age;
    SERVER_PRINT(UTIL_VarArgs("[CSOLMapProjectile] launched entity=%d target=%d health=%.3f distance=%.3f age=%.3f\n",
        knife->entindex(), index, target->pev->health, distance, age));
}
#endif

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
    if (!CSOL_WeaponsReady())
    {
        ALERT(at_console, "CSOL: weapon_tknifeex2 unavailable; enable csol_weapons before loading the map.\n");
        UTIL_Remove(this);
        return;
    }
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
    CThrowingKnife::Shoot(m_pPlayer, m_iId, origin, direction);
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
    CSOL_InitBeamDamage();
    CSOL_InitBeamWeapons();
    CSOL_InitCannon();
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
    CVAR_REGISTER(&csol_projectile_trace);
    ADD_SERVER_COMMAND("tknife_projectile_test", CThrowingKnife::TestProjectile);
    ADD_SERVER_COMMAND("tknife_map_projectile", CThrowingKnife::MapProjectile);
    CSOL_InitWeaponTests();
#endif
}

bool CSOL_IsWeapon(const CBasePlayerWeapon* weapon)
{
    if (!weapon) return false;
    const char* name = STRING(weapon->pev->classname);
    return (weapon->m_iId == CSOL_WEAPON_TKNIFEEX2 && !Q_strcmp(name, "weapon_tknifeex2")) ||
        (weapon->m_iId == CSOL_WEAPON_BEAMGUN && !Q_strcmp(name, "weapon_beamgun")) ||
        (weapon->m_iId == CSOL_WEAPON_BEAMGUNLE && !Q_strcmp(name, "weapon_beamgunle")) ||
        (weapon->m_iId == CSOL_WEAPON_CANNON && !Q_strcmp(name, "weapon_cannon"));
}

const char* CSOL_WorldModel(const CBasePlayerItem* item)
{
    if (item && item->m_iId == CSOL_WEAPON_CANNON && FClassnameIs(item->pev, "weapon_cannon"))
        return "models/w_cannon.mdl";
    if (item && item->m_iId == CSOL_WEAPON_TKNIFEEX2 &&
        !Q_strcmp(STRING(item->pev->classname), "weapon_tknifeex2"))
        return "models/w_tknifeex2.mdl";
    if (item && item->m_iId == CSOL_WEAPON_BEAMGUN && FClassnameIs(item->pev, "weapon_beamgun"))
        return "models/w_beamgun.mdl";
    if (item && item->m_iId == CSOL_WEAPON_BEAMGUNLE && FClassnameIs(item->pev, "weapon_beamgunle"))
        return "models/w_beamgunle.mdl";
    return nullptr;
}

void CSOL_PrecacheWeapons()
{
    CSOL_PrecacheCharacters();
    weaponsPrecached = false;
    if (!knifeTrailMessage) knifeTrailMessage = REG_USER_MSG(csol::KnifeTrailMessage, csol::KnifeTrailMessageBytes);
    if (csol_weapons.value != 0)
    {
        CSOL_ReloadBeamDamage();
        UTIL_PrecacheOtherWeapon("weapon_tknifeex2");
        const auto* info = CSOL_ItemInfo(CSOL_WEAPON_TKNIFEEX2);
        weaponsPrecached = info && info->pszName && !Q_strcmp(info->pszName, "weapon_tknifeex2");
        CSOL_PrecacheBeamWeapons();
        CSOL_PrecacheCannon();
    }
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
    CSOL_PrecacheWeaponTests();
#endif
}

bool CSOL_WeaponsReady() { return weaponsPrecached && csol_weapons.value != 0; }
