// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"
#include "wpn_melee.h"
#include "melee_trace.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../../weapons/melee_damage.hpp"
#include "../../weapons/melee_identity.hpp"
#include "../../weapons/melee_prediction.hpp"
#include "../../weapons/melee_resources.hpp"
#include <cmath>
#include <array>
#include <typeinfo>

namespace {
using namespace csol::melee;
struct Settings { DamageConfig damage; bool loaded{}, precached{}; } settings[2];
int completionMessage{};
struct SerialOwner { int userid{}; std::uint16_t value[2]{}; };
std::array<SerialOwner, 33> completionSerials{};
std::uint16_t& CompletionSerial(CBasePlayer& player, Variant variant)
{
    auto& state = completionSerials[player.entindex()];
    const int userid = GETPLAYERUSERID(player.edict());
    if (state.userid != userid) state = {userid, {}};
    return state.value[variant == Variant::DragonSword ? 0 : 1];
}
// -1 uses the installed CS/ZP/scenario rules. Explicit nonnegative values
// refer to original CSO mode IDs, never the bridge's csol_game_mode enum.
char originalModeName[] = "csol_melee_original_mode", originalModeDefault[] = "-1";
cvar_t originalMode = {originalModeName, originalModeDefault, FCVAR_SERVER, -1, nullptr};
Settings& Config(Variant variant) { return settings[variant == Variant::DragonSword ? 0 : 1]; }
bool LoadDamage(Variant variant)
{
    const auto& resource = Resource(variant);
    int size{};
    auto* bytes = LOAD_FILE_FOR_ME(resource.damageConfig, &size);
    if (!bytes) { ALERT(at_error, "CSOL: missing %s\n", resource.damageConfig); return false; }
    auto& config = Config(variant);
    const bool good = size > 0 && size <= 65536 && ParseDamageConfig(
        {reinterpret_cast<const char*>(bytes), std::size_t(size)}, config.damage);
    FREE_FILE(bytes);
    if (!good) { ALERT(at_error, "CSOL: invalid %s; previous values retained\n", resource.damageConfig); return false; }
    config.loaded = true;
    return true;
}
void ReloadDamage()
{
    if (CMD_ARGC() != 1) return;
    LoadDamage(Variant::DragonSword);
    LoadDamage(Variant::SkullAxe);
}
float DamageScale(const DamageConfig& config)
{
    if (originalMode.value >= 0) return DamageMultiplier(config, int(originalMode.value));
    const auto mode = CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode()
        ? CSOLWeaponDamageMode::Zombie : CSOLWeaponDamageMode::Normal);
    if (mode == CSOLWeaponDamageMode::Scenario) return config.scenarioGroupMultiplier;
    if (mode == CSOLWeaponDamageMode::Zombie || mode == CSOLWeaponDamageMode::ZombieEscape)
        return config.zombieGroupMultiplier;
    return 1;
}
Vector MeleeBodyTarget(CBaseEntity& target, const Vector& origin, float)
{
    // MP player 1074B5F0. CBaseEntity, CBaseMonster, CMonster and CBreakable
    // all use v124=10611DE0, forwarding to virtual Center (v118).
    if (target.IsPlayer()) return target.Center() + target.pev->view_ofs * RANDOM_FLOAT(.5f, 1.1f);
    return target.BodyTarget(origin);
}
template<class Current>
bool PlayAttackSound(CBasePlayer& player, const Attack& attack, Contact contact, const Current& current)
{
    const char* sample;
    int channel = CHAN_WEAPON, pitch = PITCH_NORM;
    if (attack.variant == Variant::DragonSword) {
        // MP 39f962f3, 110B7C2C/110B7E2F and 110B851C/110B870C:
        // type16's hit and miss table slots, not the sample's filename.
        const bool secondSlash = attack.button == Button::Primary && attack.animation == 2;
        if (contact == Contact::Damageable)
            sample = secondSlash ? "weapons/dragonsword_hit2.wav" : "weapons/dragonsword_hit1.wav";
        else if (contact == Contact::World) sample = "weapons/skullaxe_wall.wav";
        else if (attack.button == Button::Secondary) sample = "weapons/dragonsword_stab_hit.wav";
        else sample = secondSlash ? "weapons/dragonsword_slash2.wav" : "weapons/dragonsword_slash1.wav";
    } else {
        // Type10 has three miss entries, but one wall and one hit entry.
        // Preserve the original RNG bounds even for those singleton lists.
        const auto choose = [](const auto& sounds) {
            return sounds[RANDOM_LONG(0, int(sounds.size()) - 1)];
        };
        if (contact == Contact::Damageable)
            sample = choose(std::array{"weapons/skullaxe_hit.wav"});
        else if (contact == Contact::World)
            sample = choose(std::array{"weapons/skullaxe_wall.wav"});
        else {
            sample = choose(std::array{"weapons/skullaxe_slash1.wav", "weapons/skullaxe_slash2.wav",
                "weapons/skullaxe_miss.wav"});
            // MP 110D3376/110D3CD9: empty swings use AUTO and pitch94.
            channel = CHAN_AUTO; pitch = 94;
        }
    }
    if (!current()) return false; // RANDOM_LONG is an engine boundary too.
    EMIT_SOUND_DYN(player.edict(), channel, sample, 1, ATTN_NORM, 0, pitch);
    return current();
}
}

class CCSOLMelee : public CBasePlayerWeapon {
public:
    virtual csol::melee::Variant Variant() const = 0;
    int iItemSlot() override { return KNIFE_SLOT; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDrop() override { return FALSE; }
    float GetMaxSpeed() override { return 250; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_MeleeReady(csol::melee::WeaponId(Variant())); }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal) override;
    void PrimaryAttack() override { Begin(csol::melee::Button::Primary); }
    void SecondaryAttack() override { Begin(csol::melee::Button::Secondary); }
    void Reload() override {}
    void WeaponIdle() override;
    void DelayedAttack();
    void Pack(weapon_data_s& data) const;
    bool DeploymentComplete() const { return deploymentComplete; }
    CSOLMeleePurchaseState PurchaseState() const { return {prediction, deploymentComplete}; }
    void RestorePurchaseState(const CSOLMeleePurchaseState& state)
    { prediction = state.prediction; deploymentComplete = state.deploymentComplete; }
private:
    void Begin(csol::melee::Button button);
    void CancelAttack();
    csol::melee::Prediction prediction;
    bool deploymentComplete{};
};
class CDragonSword final : public CCSOLMelee {
    csol::melee::Variant Variant() const override { return csol::melee::Variant::DragonSword; }
};
class CSkullAxe final : public CCSOLMelee {
    csol::melee::Variant Variant() const override { return csol::melee::Variant::SkullAxe; }
};
LINK_ENTITY_TO_CLASS(weapon_dragonsword, CDragonSword, CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_skullaxe, CSkullAxe, CCSPlayerWeapon)

bool CSOL_MeleeEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    return CSOL_MeleeItemIdentity(weapon) != 0;
}
int CSOL_MeleeItemIdentity(const CBasePlayerWeapon& weapon)
{
    using namespace csol::melee;
    // Original CKnife1192FA9C v1E4/v1E8=10611B90 (ret8). The additional
    // monster impulse helper v44=10D1BE10 returns AL0 for both knife types.
    if (typeid(weapon) == typeid(CDragonSword) && weapon.m_iId == DragonSwordId &&
        FClassnameIs(weapon.pev, DragonSwordResources.classname)) return DragonSwordItemId;
    if (typeid(weapon) == typeid(CSkullAxe) && weapon.m_iId == SkullAxeId &&
        FClassnameIs(weapon.pev, SkullAxeResources.classname)) return SkullAxeItemId;
    return 0;
}
bool CSOL_MeleeDeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_MeleeItemIdentity(weapon) != 0 &&
        static_cast<const CCSOLMelee&>(weapon).DeploymentComplete();
}
bool CSOL_SaveMeleePurchaseState(const CBasePlayerWeapon& weapon, CSOLMeleePurchaseState& state)
{
    if (!CSOL_MeleeItemIdentity(weapon)) return false;
    state = static_cast<const CCSOLMelee&>(weapon).PurchaseState();
    return true;
}
bool CSOL_RestoreMeleePurchaseState(CBasePlayerWeapon& weapon, const CSOLMeleePurchaseState& state)
{
    if (!CSOL_MeleeItemIdentity(weapon)) return false;
    static_cast<CCSOLMelee&>(weapon).RestorePurchaseState(state);
    return true;
}

void CCSOLMelee::Precache()
{
    using namespace csol::melee;
    const auto& resource = Resource(Variant());
    PRECACHE_MODEL(const_cast<char*>(resource.viewModel));
    PRECACHE_MODEL(const_cast<char*>(resource.playerModel));
    PRECACHE_MODEL(const_cast<char*>(resource.worldModel));
    PRECACHE_GENERIC(const_cast<char*>(resource.originalHud));
    if (Variant() == Variant::DragonSword) {
        PRECACHE_GENERIC("sprites/weapon_dragonsword.txt");
        PRECACHE_MODEL("models/ef_dragonsword.mdl");
        PRECACHE_MODEL("sprites/csol/640hud74.spr");
        PRECACHE_MODEL("sprites/csol/640hud10.spr");
        for (auto* sound : DragonSwordSounds) PRECACHE_SOUND(const_cast<char*>(sound));
    } else {
        PRECACHE_GENERIC("sprites/weapon_skullaxe.txt");
        PRECACHE_MODEL("sprites/csol/640hud54.spr");
        PRECACHE_MODEL("sprites/csol/640hud4.spr");
        for (auto* sound : SkullAxeSounds) PRECACHE_SOUND(const_cast<char*>(sound));
    }
}
void CCSOLMelee::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();
    m_iId = csol::melee::WeaponId(Variant());
    m_iClip = WEAPON_NOCLIP;
    m_iDefaultAmmo = 0;
    SET_MODEL(edict(), csol::melee::Resource(Variant()).worldModel);
    FallInit();
    CBasePlayerWeapon::Spawn();
}
int CCSOLMelee::GetItemInfo(ItemInfo* info)
{
    *info = {};
    info->pszName = csol::melee::Resource(Variant()).classname;
    info->iMaxAmmo1 = info->iMaxAmmo2 = -1;
    info->iMaxClip = WEAPON_NOCLIP;
    info->iSlot = 2; info->iPosition = 1;
    info->iId = csol::melee::WeaponId(Variant());
    info->iWeight = KNIFE_WEIGHT;
    return 1;
}
void CCSOLMelee::CancelAttack()
{
    prediction.pendingAnimation = 0;
    prediction.pendingTime = -1;
    pev->nextthink = 0;
    SetThink(nullptr);
}
BOOL CCSOLMelee::Deploy()
{
    deploymentComplete = false;
    CSOLScenarioSdkScope sdk;
    auto* player = m_pPlayer;
    if (!sdk.Current() || !player) return FALSE;
    const EHANDLE self(this), owner(player);
    const auto current = [&]() {
        return sdk.Current() && self.Get<CBaseEntity>() == this && owner.Get<CBaseEntity>() == player &&
            m_pPlayer == player && !(pev->flags & FL_KILLME) && player->IsAlive();
    };
    const auto& resource = csol::melee::Resource(Variant());
    // Native CS characters retain a valid knife fallback; CSO characters use
    // their real dragonsword/skullaxe sequence family, never a firearm pose.
    const char* pose = resource.animationExtension;
    char aim[64], shoot[64];
    Q_snprintf(aim, sizeof(aim), "ref_aim_%s", pose);
    Q_snprintf(shoot, sizeof(shoot), "ref_shoot_%s", pose);
    const int aimSequence = player->LookupSequence(aim);
    if (!current()) return FALSE;
    if (aimSequence < 0) pose = "knife";
    else {
        const int shootSequence = player->LookupSequence(shoot);
        if (!current()) return FALSE;
        if (shootSequence < 0) pose = "knife";
    }
    if (!DefaultDeploy(const_cast<char*>(resource.viewModel), const_cast<char*>(resource.playerModel),
        3, const_cast<char*>(pose), UseDecrement())) return FALSE;
    if (!current()) return FALSE;
    CancelAttack(); prediction.primaryOrdinal = 0;
    // Retain the owner's last serial across same-knife purchase/replacement;
    // otherwise a fresh entity starts at zero and its hits look like old packets.
    const auto serial = CompletionSerial(*player, Variant());
    if (!current()) return FALSE;
    prediction.serial = serial;
    m_pPlayer->m_flNextAttack = resource.deployDelay;
    m_flTimeWeaponIdle = resource.deployIdle;
    m_fMaxSpeed = 250;
    deploymentComplete = true;
    return TRUE;
}
void CCSOLMelee::Holster(int)
{
    deploymentComplete = false;
    CancelAttack();
    m_pPlayer->m_flNextAttack = .5f;
    // Inventory timers are decremented by ReGameDLL PlayerPostThink. Do not
    // reset attack cooldowns here or on Deploy: rapid switching cannot cheat.
}
void CCSOLMelee::Begin(csol::melee::Button button)
{
    using namespace csol::melee;
    if (prediction.pendingAnimation || !m_pPlayer || !m_pPlayer->IsAlive() ||
        !m_pPlayer->m_bCanShoot || g_pGameRules->IsFreezePeriod()) return;
    const auto attack = BeginAttack(Variant(), button, prediction.primaryOrdinal);
    if (button == Button::Primary) ++prediction.primaryOrdinal;
    ++prediction.serial;
    CompletionSerial(*m_pPlayer, Variant()) = prediction.serial;
    prediction.pendingAnimation = attack.animation;
    prediction.pendingTime = attack.hitDelay;
    m_flNextPrimaryAttack = attack.initial.primary;
    m_flNextSecondaryAttack = attack.initial.secondary;
    m_flTimeWeaponIdle = attack.initial.idle;
    SendWeaponAnim(attack.animation, UseDecrement());
    if (Variant() == Variant::SkullAxe || button == Button::Secondary)
        m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    SetThink(&CCSOLMelee::DelayedAttack);
    pev->nextthink = gpGlobals->time + attack.hitDelay;
}
void CCSOLMelee::DelayedAttack()
{
    using namespace csol::melee;
    CSOLScenarioSdkScope scope;
    if (!scope.Current()) return;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    const auto session = runtime ? runtime->session : nullptr;
    const auto round = session ? session->Map().RoundGeneration() : 0;
    const auto generation = runtime ? runtime->Generation() : 0;
    if (!prediction.pendingAnimation || !m_pPlayer || !m_pPlayer->IsAlive() || m_pPlayer->m_pActiveItem != this) {
        CancelAttack(); return;
    }
    const auto attack = PendingAttack(Variant(), prediction.pendingAnimation);
    CancelAttack();
    const auto& config = Config(Variant()).damage;
    // Both original callbacks select PriFullDmg (Skull v94=10D1B8B0,
    // property+38). PriFastDmg remains a distinct preserved config field.
    const float damage = BaseDamage(config.base, attack.button, false) * DamageScale(config);
    UTIL_MakeVectors(m_pPlayer->pev->v_angle);
    const Vector forward = gpGlobals->v_forward;
    const float horizontalLength = forward.Length2D();
    const Vector horizontal = horizontalLength > 0
        ? Vector(forward.x / horizontalLength, forward.y / horizontalLength, 0) : g_vecZero;
    const Vector origin = m_pPlayer->GetGunPosition();
    const float cosine = float(std::cos(double(attack.fullAngle * .5f * (1.f / 180.f)) * 3.14159265358979323846));
    Contact contact = Contact::None;
    EHANDLE self(this), owner(m_pPlayer);
    const auto current = [&] {
        return scope.Current() && (!runtime || (!runtime->Closing() && runtime->Generation() == generation &&
            runtime->session == session && session && session->Map().RoundGeneration() == round)) &&
            self.Get<CBaseEntity>() == this && owner.Get<CBasePlayer>() == m_pPlayer &&
            m_pPlayer->m_pActiveItem == this && m_pPlayer->IsAlive();
    };
    // Keep an edict index, not the previous C++ target pointer. Death hooks
    // may destroy private data before the next engine FindEntityInSphere.
    int cursor = 0;
    for (;;) {
        if (!current() || !gpGlobals || cursor >= gpGlobals->maxEntities) return;
        auto* found = g_engfuncs.pfnFindEntityInSphere(cursor ? INDEXENT(cursor) : nullptr, origin, attack.distance);
        if (!current()) return;
        if (FNullEnt(found)) break;
        const int index = ENTINDEX(found);
        if (index <= cursor || index >= gpGlobals->maxEntities) break;
        cursor = index;
        auto* target = CBaseEntity::Instance(found);
        if (!target) continue;
        if (target == m_pPlayer || target->pev->flags < 0 || target->pev->takedamage == DAMAGE_NO) continue;
        TraceResult trace{};
        const bool hit = CSOL_TraceMeleeTarget(*target, *m_pPlayer, origin, forward, horizontal,
            attack.distance, cosine, MeleeBodyTarget, trace);
        if (!current()) return;
        if (!hit) continue;
        contact = Contact::Damageable;
        float hitDamage = damage;
        if (target->IsPlayer() || target->Classify() == 17) {
            UTIL_MakeVectors(target->pev->angles);
            if (CSOL_MeleeBackstabFacing(target->pev->origin, m_pPlayer->pev->origin, gpGlobals->v_forward)) hitDamage *= 3;
        }
        const EHANDLE receiver(target);
        const auto* monster = dynamic_cast<CCSOLScenarioMonster*>(target);
        auto* pool = monster ? monster->Pool() : nullptr;
        const auto lease = pool ? pool->Lease(*monster) : 0;
        // SkullAxe emits its hit cue for each accepted target before damage
        // (MP 110D2E6B/110D3838); DragonSword emits once after the sector scan.
        if (Variant() == Variant::SkullAxe) {
            if (!PlayAttackSound(*m_pPlayer, attack, Contact::Damageable, current)) return;
            if (receiver.Get<CBaseEntity>() != target || (pool &&
                (monster->Pool() != pool || pool->Lease(*monster) != lease))) continue;
        }
        ClearMultiDamage();
        // Original 0x20001002 has CSO-only attack metadata. The shared native
        // damage semantics are BULLET|NEVERGIB; CS high bits mean other things.
        target->TraceAttack(m_pPlayer->pev, hitDamage, forward, &trace, DMG_BULLET | DMG_NEVERGIB);
        if (!current()) return;
        if (receiver.Get<CBaseEntity>() != target || (pool &&
            (monster->Pool() != pool || pool->Lease(*monster) != lease))) { ClearMultiDamage(); continue; }
        ApplyMultiDamage(m_pPlayer->pev, m_pPlayer->pev);
        if (!current()) return;
    }
    if (contact == Contact::None) {
        TraceResult wall{};
        // MP shortens this human hull by sqrt(512), independently of the
        // target-sector test. It is the wall/empty-space animation decision.
        const float distance = attack.distance - float(std::sqrt(512.0));
        UTIL_TraceHull(origin, origin + forward * distance, dont_ignore_monsters,
            human_hull, m_pPlayer->edict(), &wall);
        if (!current()) return;
        if (wall.flFraction != 1) contact = Contact::World;
    }
    // Original callbacks emit the result cue before completion timers and
    // the follow-through sequence. Skull flesh hits were emitted per target.
    if ((Variant() == Variant::DragonSword || contact != Contact::Damageable) &&
        !PlayAttackSound(*m_pPlayer, attack, contact, current)) return;
    const auto complete = CompleteAttack(attack, contact);
    m_flNextPrimaryAttack = complete.remaining.primary;
    m_flNextSecondaryAttack = complete.remaining.secondary;
    m_flTimeWeaponIdle = complete.remaining.idle;
    if (complete.playerAttackAnimation) m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    if (!current()) return;
    if (complete.animation >= 0) SendWeaponAnim(complete.animation, UseDecrement());
    if (!current()) return;
    const int id = m_iId, serial = prediction.serial;
    const int animOffset = int((m_pPlayer->pev->animtime - gpGlobals->time) * 100);
    MESSAGE_BEGIN(MSG_ONE, completionMessage, nullptr, m_pPlayer->pev);
        WRITE_BYTE(1); WRITE_SHORT(id); WRITE_SHORT(serial);
        WRITE_BYTE(attack.animation); WRITE_BYTE(int(contact));
        WRITE_SHORT(animOffset);
    MESSAGE_END();
}
void CCSOLMelee::WeaponIdle()
{
    ResetEmptySound(); m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if (prediction.pendingAnimation || m_flTimeWeaponIdle > 0) return;
    SendWeaponAnim(0, UseDecrement());
    m_flTimeWeaponIdle = Variant() == csol::melee::Variant::DragonSword ? 18 : 20;
}
void CCSOLMelee::Pack(weapon_data_s& data) const
{
    auto packed = prediction;
    packed.pendingTime = packed.pendingAnimation ? Q_max(-.001f, pev->nextthink - gpGlobals->time) : -1;
    csol::melee::WritePrediction(packed, data);
}
void CSOL_InitMeleeWeapons()
{
    CVAR_REGISTER(&originalMode);
    ADD_SERVER_COMMAND("csol_reload_melee_damage", ReloadDamage);
}
void CSOL_PrecacheMeleeWeapons()
{
    using namespace csol::melee;
    completionSerials = {};
    if (!completionMessage) completionMessage = REG_USER_MSG(EffectMessage, EffectBytes);
    for (auto variant : {Variant::DragonSword, Variant::SkullAxe}) {
        auto& config = Config(variant); config.precached = false;
        if (!LoadDamage(variant)) continue;
        UTIL_PrecacheOtherWeapon(Resource(variant).classname);
        const auto* info = CSOL_ItemInfo(WeaponId(variant));
        config.precached = info && info->pszName && !Q_strcmp(info->pszName, Resource(variant).classname);
    }
}
bool CSOL_MeleeReady(int id)
{
    if (!csol::melee::IsWeapon(id)) return false;
    const auto& config = Config(csol::melee::WeaponVariant(id));
    return config.loaded && config.precached;
}
void CSOL_PackMeleeWeapon(const CBasePlayerWeapon* weapon, weapon_data_s* data)
{
    if (weapon && data && CSOL_MeleeEmptyMonsterKillCallbacks(*weapon))
        static_cast<const CCSOLMelee*>(weapon)->Pack(*data);
}
