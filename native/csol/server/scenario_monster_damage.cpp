// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_damage.h"
#include "game_rules_gate.h"
#include "scenario_monster_ordinary.h"
#include "weapons.h"

#include <cmath>
#include <algorithm>
#include <limits>

#pragma clang fp contract(off)

namespace
{
CBaseEntity* Live(const EHANDLE& handle)
{
    auto* entity = handle.Get<CBaseEntity>();
    return entity && !(entity->pev->flags & FL_KILLME) ? entity : nullptr;
}
int Team(CBaseEntity& entity)
{
    // ReGameDLL does not mirror a player's authoritative team into pev.team.
    const auto* player = dynamic_cast<CBasePlayer*>(&entity);
    if (player) return static_cast<int>(player->m_iTeam);
    if (const auto* grenade = dynamic_cast<CGrenade*>(&entity)) return grenade->m_iTeam;
    return entity.pev->team;
}

bool ApplyOrdinaryKnockback(CSOLMonsterDamageCall& call, const EHANDLE& attribution,
    csol::scenario::ConfigError& error)
{
    auto* monster = call.Current();
    if (!monster) return false;
    auto* player = dynamic_cast<CBasePlayer*>(attribution.Get<CBaseEntity>());
    if (!player || (player->pev->flags & FL_KILLME) || !monster->Type() ||
        monster->Type()->priority != 0) return true;
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
    if (!weapon || (weapon->pev->flags & FL_KILLME)) return true;
    // These actual MP v22C implementations return false. Nonzero values in
    // the shared weapon table alone do not enable ordinary-monster knockback.
    switch (weapon->m_iId)
    {
    case WEAPON_P228: case WEAPON_SCOUT: case WEAPON_HEGRENADE:
    case WEAPON_XM1014: case WEAPON_C4: case WEAPON_MAC10: case WEAPON_AUG:
    case WEAPON_SMOKEGRENADE: case WEAPON_ELITE: case WEAPON_FIVESEVEN:
    case WEAPON_UMP45: case WEAPON_SG550: case WEAPON_GALIL: case WEAPON_FAMAS:
    case WEAPON_USP: case WEAPON_GLOCK18: case WEAPON_AWP: case WEAPON_MP5N:
    case WEAPON_M249: case WEAPON_M3: case WEAPON_M4A1: case WEAPON_TMP:
    case WEAPON_G3SG1: case WEAPON_FLASHBANG: case WEAPON_DEAGLE:
    case WEAPON_SG552: case WEAPON_AK47: case WEAPON_KNIFE: case WEAPON_P90:
    case CSOL_WEAPON_TKNIFEEX2: case CSOL_WEAPON_BEAMGUN: case CSOL_WEAPON_BEAMGUNLE:
        return true;
    case CSOL_WEAPON_CANNON: break;
    default:
        error.message = "ordinary knockback needs a verified weapon predicate and force record";
        return false;
    }
    if (monster->HostState().knockbackBlocked || call.context.bytes[5]) return true;
    // HW WeaponMgr record 144: +48/+4C/+50/+54 = 1100/500/700/400;
    // +58 is float32 0x3F666666. MP vD4 writes recovery, then v190 halves it
    // even when the later airborne direction gate rejects velocity changes.
    monster->SpeedState().recovery = 0.9f * 0.5f;
    const Vector offset = monster->pev->origin - player->pev->origin;
    const float lengthSquared = (offset.y * offset.y + offset.x * offset.x) + offset.z * offset.z;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0)
    {
        error.message = "ordinary knockback direction is not finite";
        return false;
    }
    const float length = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
    Vector direction(0, 0, 1);
    if (length > 0)
    {
        const float inverse = 1.0f / length;
        direction = offset * inverse;
    }
    float force{};
    if (monster->pev->flags & FL_ONGROUND)
    {
        const int group = monster->HostState().lastHitgroup;
        force = (monster->pev->flags & FL_DUCKING) || group == HITGROUP_LEFTLEG ||
            group == HITGROUP_RIGHTLEG ? 400.0f : 1100.0f;
    }
    else
    {
        const Vector velocity = monster->pev->velocity;
        const float dot = (velocity.x * direction.x + velocity.y * direction.y) + velocity.z * direction.z;
        if (!(dot < 0)) return true;
        const float planarSquared = velocity.y * velocity.y + velocity.x * velocity.x;
        const float planarSpeed = static_cast<float>(std::sqrt(static_cast<double>(planarSquared)));
        force = planarSpeed < 0.2f ? 500.0f : 700.0f;
    }
    monster->pev->velocity.x = direction.x * force;
    monster->pev->velocity.y = direction.y * force;
    return true; // Retain Z and overwrite XY; this is not an additive impulse.
}
}

CSOLMonsterDamageCall::CSOLMonsterDamageCall(CSOLMonsterSpawnCall& spawn,
    EHANDLE inflictor, EHANDLE attacker, float input, int damageBits,
    CSOLMonsterDeathContext damageContext,
    const bool& ownerActive, bool (*ownerValid)(const void*), const void* ownerContext)
    : damage(input), bits(damageBits), context(damageContext), m_spawn(spawn),
      m_inflictor(inflictor), m_attacker(attacker), m_originalDamage(input),
      m_hadInflictor(inflictor.Get<CBaseEntity>() != nullptr),
      m_hadAttacker(attacker.Get<CBaseEntity>() != nullptr),
      m_ownerActive(ownerActive), m_ownerValid(ownerValid), m_ownerContext(ownerContext)
{
    if (auto* monster = spawn.Current()) m_behavior = monster->Behavior();
}

CCSOLScenarioMonster* CSOLMonsterDamageCall::Current() const
{
    // Spawn checks map/round/death generation before accessing old edicts.
    if (!m_ownerActive) return nullptr;
    auto* monster = m_spawn.Current();
    if (!monster || !m_ownerValid || !m_ownerValid(m_ownerContext)) return nullptr;
    if (!monster || monster->Behavior() != m_behavior || !monster->Pool()->Borrowed(*monster) ||
        monster->pev->deadflag != DEAD_NO || monster->pev->takedamage == DAMAGE_NO ||
        (monster->pev->effects & EF_NODRAW) || (monster->pev->flags & FL_KILLME)) return nullptr;
    if ((m_hadInflictor && !Live(m_inflictor)) || (m_hadAttacker && !Live(m_attacker))) return nullptr;
    return monster;
}
CBaseEntity* CSOLMonsterDamageCall::Inflictor() const { return Current() ? Live(m_inflictor) : nullptr; }
CBaseEntity* CSOLMonsterDamageCall::Attacker() const { return Current() ? Live(m_attacker) : nullptr; }
CBasePlayer* CSOLMonsterDamageCall::Player() const { return dynamic_cast<CBasePlayer*>(Attacker()); }

BOOL CCSOLOrdinaryMonsterDamage::Apply(CSOLMonsterSpawnCall& spawn,
    entvars_t* inflictor, entvars_t* attacker, float input, int bits,
    csol::scenario::ConfigError& error, bool (*ownerValid)(const void*),
    const void* ownerContext, CSOLMonsterDeathContext context)
{
    auto services = m_services.lock();
    if (!services) return FALSE;
    auto rewards = m_rewards;
    auto players = m_players;
    auto equipment = m_equipment;
    auto weapons = m_weapons;
    const auto valid = [&]()
    {
        return m_valid && spawn.Current() && ownerValid && ownerValid(ownerContext);
    };
    if (!valid()) return FALSE;
    const auto fail = [&](const char* reason) -> BOOL
    {
        if (valid() && error.message.empty()) error.message = reason;
        return FALSE;
    };
    if (!services || !rewards || !players || !equipment || !weapons || !weapons->Current())
        return fail("ordinary damage requires mode15 rules, upgrades, equipment, weapons and hit rewards");
    if (!m_map.Tables() || m_map.Tables()->mapName != "zs_deadend" || m_map.Tables()->scenarioType != 1)
        return fail("ordinary damage policy requires zs_deadend subtype1");
    if (!error.message.empty()) return FALSE;
    if (!std::isfinite(input)) return fail("ordinary damage input is not finite");
    EHANDLE source, actor;
    if (inflictor) source = CBaseEntity::Instance(inflictor);
    if (!valid()) return FALSE;
    if (attacker) actor = CBaseEntity::Instance(attacker);
    if (!valid()) return FALSE;
    if ((inflictor && !Live(source)) || (attacker && !Live(actor))) return FALSE;
    CSOLMonsterDamageCall call(spawn, source, actor, input, bits, context, m_valid, ownerValid, ownerContext);
    const auto current = [&]() { return m_valid ? call.Current() : nullptr; };
    const auto step = [&](bool ok, const char* reason)
    {
        if (!current()) return false;
        if (!ok) { fail(reason); return false; }
        if (!std::isfinite(call.damage)) { fail("ordinary damage stage produced a nonfinite value"); return false; }
        return error.message.empty();
    };
    auto* monster = current();
    if (!monster) return FALSE;
    auto behavior = monster->RetainBehavior();
    auto* ordinary = dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(behavior.get());
    if (!ordinary) return fail("ordinary damage cannot handle a different monster behavior");
    EHANDLE knockbackAttribution;
    try
    {
        const bool suppressed = services->DamageSuppressed();
        if (!current() || suppressed) return FALSE;
        if (call.Player())
        {
            const auto upgrades = players->Upgrades(actor);
            const auto samePlayer = [&]()
            {
                return current() && upgrades && upgrades->Current() && current() &&
                    players->Upgrades(actor) == upgrades && current();
            };
            if (!samePlayer()) return fail("scenario attacker upgrade state is unavailable");
            const bool blocked = services->WeaponEffectsBlocked21F2(call);
            if (!samePlayer()) return FALSE;
            if (!step(weapons->ApplyItemMultiplier(call, blocked, error), "mode15 weapon damage rules failed") ||
                !samePlayer()) return FALSE;
            // Map98 has neither the FrozenWrath nor the map219..275 gate.
            // All its Classify17 creatures take modern stat1 here, including
            // projectile hits. Do not gate this multiplier on direct damage.
            float attack{};
            if (!upgrades->StatValue(csol::scenario::UpgradeStat::Damage, attack))
                return fail("scenario attack stat is unavailable");
            if (!samePlayer()) return FALSE;
            call.damage *= attack;
            if (!step(true, "scenario attack multiplier failed")) return FALSE;
            const bool skillBoss = current()->Type()->skill >= 1000;
            if (!step(equipment->ModifyCreatureDamage(actor, skillBoss, call.damage),
                    "scenario creature damage effect failed") || !samePlayer()) return FALSE;
            if (skillBoss)
            {
                const float experience = std::min(call.damage * 0.2f, 1500.0f);
                const int amount = experience < -2147483648.0f ? std::numeric_limits<int>::min() :
                    static_cast<int>(experience);
                const bool awarded = upgrades->AddExperience(amount);
                if (!samePlayer()) return FALSE;
                if (!step(awarded, "scenario Boss hit experience failed")) return FALSE;
            }
            float ratio{};
            const bool scaled = CSOL_ReadScenarioDamageScale(m_map, ratio);
            if (!samePlayer()) return FALSE;
            if (!step(scaled, "scenario player-count damage scale owner is unavailable")) return FALSE;
            call.damage *= ratio;
            if (!step(true, "scenario player-count damage scale failed")) return FALSE;
            if (!step(services->RulesAfterAttackDamage(call, error), "mode15 addon/statistics stage failed") ||
                !samePlayer()) return FALSE;
        }
        monster = current();
        if (auto* entity = call.Attacker(); entity && Team(*entity) == monster->pev->team) return FALSE;

        // Capture before v150/hitgroup/armor, as in the original TakeDamage.
        // Later weapon changes still affect vD4, but cannot rewrite attribution.
        if (auto* entity = call.Inflictor())
        {
            if (call.Player()) knockbackAttribution = actor;
            const int classification = entity->Classify();
            if (!current()) return FALSE;
            if (classification == 21) knockbackAttribution = source;
            else
            {
                entity = call.Inflictor();
                if (!entity) return FALSE;
                const int secondClassification = entity->Classify();
                if (!current()) return FALSE;
                if (secondClassification == 22) knockbackAttribution = nullptr;
            }
        }

        // Ordinary v150 is empty; v154 clears/reuses the host hitgroup and
        // applies the direct active weapon floor before its own multiplier.
        const auto flags = static_cast<std::uint32_t>(call.bits);
        if (!flags || (flags & 0x01000060u) || ((flags & 0x08000000u) && !call.context.bytes[6]))
            monster->HostState().lastHitgroup = 0;
        int weaponId{};
        if (auto* player = call.Player(); player && call.Inflictor() == player)
        {
            auto* weapon = player->m_pActiveItem;
            if (weapon && !(weapon->pev->flags & FL_KILLME)) weaponId = weapon->m_iId;
        }
        if (!csol::scenario::ResolveOrdinaryMonsterDamage(call.damage,
                monster->HostState().lastHitgroup, weaponId, call.damage))
            return fail("ordinary hitgroup damage is invalid");
        csol::scenario::MonsterArmorDamage armor;
        if (!csol::scenario::ResolveMonsterArmorDamage(call.damage, monster->pev->armorvalue, flags, armor))
            return fail("ordinary armor damage is invalid");
        monster->pev->armorvalue = armor.remainingArmor;
        call.damage = armor.healthDamage; // Ordinary v158/v15C preserve this value.
        if (call.damage == 0.0f) return TRUE;
        if (monster->HostState().damageScaleCount > 0)
            call.damage *= monster->HostState().damageScale;
        if (!step(true, "ordinary timed damage multiplier failed")) return FALSE;

        if (call.Player())
        {
            const bool blocked21F2 = services->WeaponEffectsBlocked21F2(call);
            if (!current()) return FALSE;
            const bool blocked34F0 = services->WeaponEffectsBlocked34F0(call);
            if (!current()) return FALSE;
            if (!step(weapons->ApplyPartDamage(call, blocked21F2, blocked34F0, error),
                    "ordinary weapon part damage failed")) return FALSE;
        }
        CSOLMonsterHitAccount account;
        if (!step(services->BeforeHitReward(call, account, error), "ordinary player damage effects failed")) return FALSE;
        const auto credited = rewards->Credit(spawn, actor, source, call.damage,
            account.moneyExtractor, account.roomOption, [](const void* pointer)
            {
                return static_cast<const CSOLMonsterDamageCall*>(pointer)->Current() != nullptr;
            }, &call);
        if (!current() || credited == CSOLMonsterRewardResult::Cancelled) return FALSE;
        if (credited == CSOLMonsterRewardResult::Failed)
            return fail(rewards->Error().message.c_str());
        if (!step(services->AfterHitReward(call, error), "ordinary post-reward weapon effects failed")) return FALSE;

        // 1138707B..11387105. These overrides do not undo armor or rewards.
        monster = current();
        if ((static_cast<std::uint32_t>(call.bits) & 0x40000000u) && call.damage > 0)
            call.damage = monster->pev->health + 1000.0f;
        if (call.context.bytes[0x10] && call.damage > 0) call.damage = call.OriginalDamage();
        if (!std::isfinite(monster->pev->health) || !std::isfinite(call.damage))
            return fail("ordinary health damage is not finite");
        if (call.damage > monster->pev->health) call.damage = monster->pev->health;
        const float health = monster->pev->health - call.damage;
        if (!std::isfinite(health)) return fail("ordinary health subtraction overflowed");
        monster->pev->health = health;
        // Ordinary v160 is empty. Rules v1DC and the remaining statistics run
        // before deciding whether to dispatch a death or play a flinch.
        if (!step(services->AfterHealthDamage(call, error), "mode15 post-damage rules failed")) return FALSE;
        monster = current();
        if (!std::isfinite(monster->pev->health)) return fail("post-damage health is not finite");
        if (monster->pev->health <= 0)
        {
            CSOLMonsterDeathArguments arguments;
            arguments.inflictor = source;
            arguments.attacker = actor;
            arguments.context = call.context;
            const auto result = m_deaths.Request(*monster, arguments);
            if (m_valid && spawn.SameSession() && result == CSOLDeathRequestResult::Failed)
                fail(m_deaths.Error().c_str());
            // Original TakeDamage returns FALSE for a lethal hit, including
            // deferred death. Never Free/Killed/delete around the dispatcher.
            return FALSE;
        }
        const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
        if (!appearance) return fail("ordinary damage appearance is unavailable");
        if (monster->HostState().frozenRemaining <= 0 && monster->HostState().flinchEnabled &&
            !appearance->healthSummary && monster->TypeId() <= 29999)
            if (!step(ordinary->Flinch(spawn), "ordinary flinch failed")) return FALSE;
        if (!step(ApplyOrdinaryKnockback(call, knockbackAttribution, error), "ordinary weapon knockback failed")) return FALSE;
        return TRUE;
    }
    catch (...)
    {
        return fail("ordinary damage callback threw");
    }
}
