// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../scenario/map_support.hpp"
#include "scenario_monster_damage.h"
#include "scenario_incoming_damage.h"
#include "scenario_zombie_bomb.h"
#include "game_rules_gate.h"
#include "scenario_monster_ordinary.h"
#include "scenario_entity_lookup.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "scenario_death_services.h"
#include "scenario_character_skills/elite_manager.hpp"
#include "../scenario/monster_appearances.hpp"
#include "../scenario/difficulty_monsters.hpp"
#include "weapons.h"
#include "wpn_melee.h"
#include "wpn_m249ex.h"
#include "wpn_skull1.h"
#include "wpn_skull2.h"
#include "wpn_skull5.h"
#include "wpn_skull3.h"
#include "wpn_balrog11.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <typeinfo>
#include <cstring>
#include <bit>

#pragma clang fp contract(off)

bool CSOL_ScenarioApplyMonsterIncomingDamage(CBasePlayer& player,
    entvars_t* attacker, float& damage, int damageType, entvars_t* inflictor)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario()) return true;
    if (!scope.Current()) return false;
    const EHANDLE victim(&player);
    const EHANDLE source(attacker ? CBaseEntity::Instance(attacker) : nullptr);
    if (!scope.Current() || victim.Get<CBasePlayer>() != &player) return false;
    auto* monster = dynamic_cast<CCSOLScenarioMonster*>(source.Get<CBaseEntity>());
    if (!monster) return true;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    const auto session = runtime ? runtime->session : nullptr;
    const auto players = runtime ? runtime->players : nullptr;
    if (!runtime || !session || !players || runtime->Closing()) return false;
    const auto state = players->Find(victim);
    const auto life = state ? state->skillLifeSerial : 0;
    const auto round = session->Map().RoundGeneration();
    const auto current = [&]
    {
        return scope.Current() && !runtime->Closing() && runtime->session == session &&
            runtime->players == players && players->Active() &&
            session->Map().RoundGeneration() == round && victim.Get<CBasePlayer>() == &player &&
            !(player.pev->flags & FL_KILLME) && players->Find(victim) == state &&
            (!state || state->skillLifeSerial == life);
    };
    if (!current()) return false;
    if (!state || monster->Pool() != &session->Monsters() || !monster->Type() ||
        !session->Monsters().Borrowed(*monster) ||
        (monster->pev->flags & FL_KILLME)) return false;
    const auto lease = session->Monsters().Lease(*monster);
    const auto* type = monster->Type();
    csol::scenario::ConfigError error;
    float factor{};
    if (!csol::scenario::MonsterIncomingDamageFactor(*type, session->Difficulty(), factor, error)) return false;
    const auto equipment = session->EquipmentState();
    if (!equipment) return false;
    float candidate = damage;
    // MP10826F4F tests inflictor v138==4 and dmg==999 after the separate
    // Classify17 attacker gate. The resistance also applies to DMG_BLAST.
    const EHANDLE projectile(inflictor ? CBaseEntity::Instance(inflictor) : nullptr);
    if (!current()) return false;
    const auto* bomb = dynamic_cast<CCSOLZombieBomb*>(projectile.Get<CBaseEntity>());
    const bool zombieBomb = bomb && bomb->IsDamageSource();
    // A pre-damage hook may retire or reuse the throwing monster's slot.
    // Reject that stale credit before reading the replacement's modifiers.
    if (bomb && (!zombieBomb || bomb->DamageCreator() != monster)) return false;
    const bool suicideBomb = type->appearance == "boomer_zombi_host" ||
        type->appearance == "boomer_zombi_origin";
    if (!equipment->ModifyIncomingDamage(victim, (damageType & DMG_EXPLOSION) != 0,
        suicideBomb, zombieBomb, candidate) || !current() || session->EquipmentState() != equipment ||
        source.Get<CBaseEntity>() != monster || !session->Monsters().Borrowed(*monster) ||
        session->Monsters().Lease(*monster) != lease) return false;
    candidate *= factor;
    if (!std::isfinite(candidate)) return false;
    damage = candidate;
    return true;
}

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
    // Both helpers belong to original CKnife (ID29). Its v22C at
    // 1192FCC8 points to 105FFBE0 (xor al,al; ret), independently of v230.
    if (CSOL_MeleeEmptyMonsterKillCallbacks(*weapon)) return true;
    // CM249EX/CBalrog11/CSkull5 point at 105FFBE0; match their compiled types.
    if (CSOL_M249EXEmptyMonsterKillCallbacks(*weapon) ||
        CSOL_Balrog11EmptyMonsterKillCallbacks(*weapon) || CSOL_IsSkull3(*weapon) || CSOL_IsSkull5(*weapon)) return true;
    std::array<float,5> knockback{1100,500,700,400,.9f};
    // SKULL-1 v22C is true and overrides vD4 with scenario-specific force.
    // Its v230 remains false; that independent predicate is handled below.
    if (!CSOL_Skull1ScenarioKnockback(*weapon,knockback) &&
        !CSOL_Skull2MonsterKnockback(*weapon,knockback)) switch (weapon->m_iId)
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
    case CSOL_WEAPON_K3: // MP CK3 v22C -> 105FFBE0, xor al,al; ret.
        return true;
    case CSOL_WEAPON_CANNON: break;
    default:
        error.message = "ordinary knockback needs a verified weapon predicate and force record";
        return false;
    }
    if (monster->HostState().knockbackBlocked || call.context.bytes[5]) return true;
    // Cannon retains HW record144; SKULL-1 uses its verified vD4 override.
    // MP vD4 writes recovery, then v190 halves it
    // even when the later airborne direction gate rejects velocity changes.
    monster->SpeedState().recovery = knockback[4] * 0.5f;
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
            group == HITGROUP_RIGHTLEG ? knockback[3] : knockback[0];
    }
    else
    {
        const Vector velocity = monster->pev->velocity;
        const float dot = (velocity.x * direction.x + velocity.y * direction.y) + velocity.z * direction.z;
        if (!(dot < 0)) return true;
        const float planarSquared = velocity.y * velocity.y + velocity.x * velocity.x;
        const float planarSpeed = static_cast<float>(std::sqrt(static_cast<double>(planarSquared)));
        force = planarSpeed < 0.2f ? knockback[1] : knockback[2];
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
    if (m_cancelled || !m_ownerActive) return nullptr;
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

bool CSOL_ConsumeMonsterDamageEffect(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        if (!runtime || runtime->Closing() || !session || runtime->session != session ||
            !session->Map().EntityAccessAllowed() || !call.Spawn().BelongsTo(session->Map())) return nullptr;
        return call.Current();
    };
    auto* monster = current();
    if (!monster) return false;
    if (!monster->HostState().damageEffect) return true;
    const auto finish = [&](bool result)
    {
        auto* target = current();
        if (!target) return false;
        target->HostState().damageEffect = 0;
        target->HostState().damageEffectMagnitude = 0;
        return result;
    };
    const auto fail = [&](const char* reason)
    {
        if (!current()) return false;
        if (error.message.empty()) error.message = reason;
        return finish(false);
    };
    auto* attacker = call.Attacker();
    if (!attacker) return finish(true);
    const bool isPlayer = attacker->IsPlayer() != FALSE;
    if (!current() || call.Attacker() != attacker) return false;
    if (!isPlayer) return finish(true);
    auto* recipient = attacker->pev ? attacker->pev->pContainingEntity : nullptr;
    if (!recipient) return finish(true);
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
        return fail("scenario damage effect recipient identity query is unavailable");
    const int offset = OFFSET(recipient);
    if (!current()) return false;
    if (!offset) return finish(true);
    // Original monster_entity vBC is false and vC8 is true. This consumer's
    // call owns that concrete host, so no guessed SDK vtable slot is needed.
    monster = current();
    if (monster->pev->rendermode == kRenderTransAlpha) return finish(true);
    const float height = monster->pev->maxs.z;
    const int effect = monster->HostState().damageEffect;
    int subtype{};
    switch (effect)
    {
    case 8: subtype = 43; break;
    case 10: subtype = 46; break;
    case 11: subtype = 47; break;
    case 15: subtype = 53; break;
    default: return finish(true);
    }
    if (!std::isfinite(height)) return fail("scenario damage effect height is not finite");
    if (!g_engfuncs.pfnIndexOfEdict)
        return fail("scenario damage effect target index query is unavailable");
    const auto messages = session->Messages();
    const auto send = [&](edict_t* destination)
    {
        auto* target = current();
        if (!target) return false;
        const EHANDLE receiver(destination);
        const int index = ENTINDEX(target->edict());
        if (!current()) return false;
        if (receiver.Get() != destination) return true;
        if (index <= 0 || index > 0xffff)
            return fail("scenario damage effect target index is not representable");
        const float magnitude = effect == 8 ? 0.0f :
            static_cast<float>(current()->HostState().damageEffectMagnitude);
        if (!std::isfinite(magnitude)) return fail("scenario damage effect magnitude is not finite");
        // Snapshot before Begin: engine message hooks may retire the host.
        // Each later recipient gets a fresh magnitude, but the initial height
        // and selected subtype remain fixed as in the original traversal.
        const bool sent = messages.SendMonsterDamageEffect(destination, index, subtype, height, magnitude);
        if (!current()) return false;
        return sent || fail("scenario damage effect packet could not be sent");
    };
    if (effect == 8) return send(recipient) && finish(true);
    if (!g_engfuncs.pfnPEntityOfEntIndex)
        return fail("scenario damage effect player lookup is unavailable");
    const auto access = session->Map().EntityAccess();
    for (int slot = 1;; ++slot)
    {
        if (!current()) return false;
        if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > MAX_CLIENTS ||
            gpGlobals->maxClients >= gpGlobals->maxEntities)
            return fail("scenario damage effect has an invalid client capacity");
        if (slot > gpGlobals->maxClients) return finish(true);
        auto* entity = CSOL_ScenarioPlayerEntity(slot, access);
        if (!current()) return false;
        if (!entity || !entity->pev || !entity->pev->pContainingEntity) continue;
        const EHANDLE handle(entity);
        auto* destination = entity->pev->pContainingEntity;
        const int identity = OFFSET(destination);
        if (!current()) return false;
        if (!identity || handle.Get<CBaseEntity>() != entity) continue;
        // No alive, team, dormant, Bot or IsPlayer filter on the original
        // per-client path. A private entity in a valid client slot is enough.
        if (!send(destination)) return false;
    }
}

bool CCSOLMonsterDamageBehavior::ResolvePreArmorDamage(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError&)
{
    // The inherited ordinary/Oberon v158 returns its input unchanged.
    return call.Current() ? true : call.Cancel();
}

bool CCSOLOrdinaryMonsterBehavior::ResolveHitDamage(CSOLMonsterDamageCall& call,
    int weaponId, csol::scenario::ConfigError& error)
{
    auto* monster = call.Current();
    if (!monster) return call.Cancel();
    if (csol::scenario::ResolveOrdinaryMonsterDamage(call.damage,
            monster->HostState().lastHitgroup, weaponId, call.damage)) return true;
    error.message = "ordinary hitgroup damage is invalid";
    return false;
}

bool CCSOLOrdinaryMonsterBehavior::DamageFlinch(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    const bool result = Flinch(call);
    if (!result && call.Current() && error.message.empty()) error = Error();
    return result;
}

bool CSOL_ResolveOberonHitDamage(CSOLMonsterDamageCall& call, int weaponId,
    csol::scenario::ConfigError& error)
{
    auto* monster = call.Current();
    if (!monster) return call.Cancel();
    if (!csol::scenario::ResolveOrdinaryMonsterDamage(call.damage,
            monster->HostState().lastHitgroup, weaponId, call.damage))
    { error.message = "Oberon common hitgroup damage is invalid"; return false; }
    auto* source = call.Inflictor();
    if (!source) return true;
    const auto classname = source->pev->classname;
    if (!std::strcmp(STRING(classname), "at4rocket"))
    {
        const auto* projectile = dynamic_cast<const CSOLWeaponDamageSource*>(source);
        if (!projectile)
        { error.message = "Oberon AT4 source identity is unavailable"; return false; }
        const int sourceId = projectile->DamageWeaponId();
        if (!call.Current()) return call.Cancel();
        // 10623830 first copies the selected weapon's parameters into the
        // shared AT4 workspace. Reading the DLL's initial workspace alone
        // would incorrectly assign3.0 to both ID446 branches.
        if (sourceId == 133 || sourceId == 275) call.damage *= 3.0f;
        else if (sourceId == 446)
        {
            const auto* rocket = dynamic_cast<const CSOLAt4RocketDamageState*>(source);
            if (!rocket) { error.message = "Oberon AT4 variant state is unavailable"; return false; }
            const bool alternate = rocket->AlternateBossDamage();
            if (!call.Current()) return call.Cancel();
            call.damage *= alternate ? 3.5f : 7.0f;
        }
    }
    else if (!std::strcmp(STRING(classname), "block_missile"))
    {
        const auto* projectile = dynamic_cast<const CSOLBlockMissileDamageSource*>(source);
        if (!projectile)
        { error.message = "Oberon block missile parameter owner is unavailable"; return false; }
        float multiplier{};
        const bool resolved = projectile->OberonDamageMultiplier(call, multiplier, error);
        if (!call.Current()) return call.Cancel();
        if (!resolved || !error.message.empty()) return false;
        call.damage *= multiplier;
    }
    if (std::isfinite(call.damage)) return true;
    error.message = "Oberon source damage multiplier is invalid";
    return false;
}

BOOL CCSOLOrdinaryMonsterDamage::Apply(CSOLMonsterSpawnCall& spawn,
    entvars_t* inflictor, entvars_t* attacker, float input, int bits,
    csol::scenario::ConfigError& error, bool (*ownerValid)(const void*),
    const void* ownerContext, CSOLMonsterDeathContext context)
{
    auto services = m_services.lock();
    if (!services) return FALSE;
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
    if (!players || !equipment || !weapons || !weapons->Current())
        return fail("ordinary damage requires mode15 rules, upgrades, equipment and weapons");
    if (!m_map.Tables() || !csol::scenario::HasMapGameplay(m_map.Tables()->mapName, m_map.Tables()->scenarioType))
        return fail("ordinary damage policy requires an implemented map and matching subtype");
    if (!error.message.empty()) return FALSE;
    if (!std::isfinite(input)) return fail("ordinary damage input is not finite");
    EHANDLE source, actor;
    if (inflictor) source = CBaseEntity::Instance(inflictor);
    if (!valid()) return FALSE;
    if (attacker) actor = CBaseEntity::Instance(attacker);
    if (!valid()) return FALSE;
    if ((inflictor && !Live(source)) || (attacker && !Live(actor))) return FALSE;
    CSOLMonsterDamageCall call(spawn, source, actor, input, bits, context, m_valid, ownerValid, ownerContext);
    // Providers may report an error before a later callback cancels the hit.
    // Keep it local until the still-current stage commits its failure, so an
    // abandoned hit cannot permanently stop this monster's Think chain.
    csol::scenario::ConfigError stageError;
    const auto current = [&]() { return m_valid ? call.Current() : nullptr; };
    const auto step = [&](bool ok, const char* reason)
    {
        if (!current()) return false;
        if (!ok || !stageError.message.empty())
        {
            if (error.message.empty()) error = std::move(stageError);
            fail(reason);
            return false;
        }
        if (!std::isfinite(call.damage)) { fail("ordinary damage stage produced a nonfinite value"); return false; }
        return error.message.empty();
    };
    auto* monster = current();
    if (!monster) return FALSE;
    auto behavior = monster->RetainBehavior();
    auto* damageBehavior = dynamic_cast<CCSOLMonsterDamageBehavior*>(behavior.get());
    if (!damageBehavior) return fail("monster damage behavior is unavailable");
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
            const int capturedSource = CSOL_DamageWeaponId(call.Inflictor(), call.Player());
            if (!samePlayer()) return FALSE;
            const bool blocked = services->WeaponEffectsBlocked21F2(call);
            if (!samePlayer()) return FALSE;
            if (!step(weapons->ApplyItemMultiplier(call, capturedSource, blocked, stageError), "mode15 weapon damage rules failed") ||
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
            if (!step(services->RulesAfterAttackDamage(call, capturedSource, stageError), "mode15 addon/statistics stage failed") ||
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

        // Both verified v150 entries are empty. The host clears/reuses its
        // hitgroup before calling the actual behavior's v154 modifier.
        const auto flags = static_cast<std::uint32_t>(call.bits);
        if (!flags || (flags & 0x01000060u) || ((flags & 0x08000000u) && !call.context.bytes[6]))
            monster->HostState().lastHitgroup = 0;
        int weaponId{};
        if (auto* player = call.Player(); player && call.Inflictor() == player)
        {
            auto* weapon = player->m_pActiveItem;
            if (weapon && !(weapon->pev->flags & FL_KILLME)) weaponId = weapon->m_iId;
        }
        if (!step(damageBehavior->ResolveHitDamage(call, weaponId, stageError),
                "monster hitgroup damage is invalid")) return FALSE;
        // MP113863D3 invokes v158 before113863D9 begins the armor stage.
        if (!step(damageBehavior->ResolvePreArmorDamage(call, stageError),
                "monster pre-armor damage is invalid")) return FALSE;
        monster = current();
        csol::scenario::MonsterArmorDamage armor;
        if (!csol::scenario::ResolveMonsterArmorDamage(call.damage, monster->pev->armorvalue, flags, armor))
            return fail("ordinary armor damage is invalid");
        monster->pev->armorvalue = armor.remainingArmor;
        call.damage = armor.healthDamage; // Verified v15C preserves this value.
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
            if (!step(weapons->ApplyPartDamage(call, blocked21F2, blocked34F0, stageError),
                    "ordinary weapon part damage failed")) return FALSE;
        }
        CSOLMonsterHitAccount account;
        if (!step(services->BeforeHitReward(call, account, stageError), "ordinary player damage effects failed")) return FALSE;
        const auto rewards = account.rewards;
        if (!rewards) return fail("ordinary damage requires current hit rewards");
        const auto credited = rewards->Credit(spawn, actor, source, call.damage,
            account.moneyExtractor, account.roomOption, [](const void* pointer)
            {
                return static_cast<const CSOLMonsterDamageCall*>(pointer)->Current() != nullptr;
            }, &call);
        if (!current() || credited == CSOLMonsterRewardResult::Cancelled) return FALSE;
        if (credited == CSOLMonsterRewardResult::Failed)
            return fail(rewards->Error().message.c_str());
        if (!step(services->AfterHitReward(call, stageError), "ordinary post-reward weapon effects failed")) return FALSE;

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
        // Both verified v160 entries are empty. Rules v1DC and the statistics run
        // before deciding whether to dispatch a death or play a flinch.
        if (!step(services->AfterHealthDamage(call, stageError), "mode15 post-damage rules failed")) return FALSE;
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
            if (!step(damageBehavior->DamageFlinch(spawn, stageError), "monster flinch failed")) return FALSE;
        if (!step(ApplyOrdinaryKnockback(call, knockbackAttribution, stageError), "ordinary weapon knockback failed")) return FALSE;
        return TRUE;
    }
    catch (...)
    {
        if (!current()) return FALSE;
        return fail("ordinary damage callback threw");
    }
}


bool CSOL_ApplyMonsterMeleeImpulse(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto damageServices = runtime ? runtime->monsterDamageServices : nullptr;
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->monsterDamageServices == damageServices &&
        session && runtime->session == session &&
        call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!owned()) return call.Cancel();
    if (call.context.bytes[5] || (static_cast<std::uint32_t>(call.bits) & 0x08000000u)) return true;
    auto* player = call.Player();
    if (!player || call.Inflictor() != player || call.Current()->Type()->skill >= 1000) return true;
    const auto players = runtime->players;
    const EHANDLE actor(player);
    const auto state = players ? players->Find(actor) : nullptr;
    if (!state) { error.message = "scenario melee impulse player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && runtime->players == players && players->Find(actor) == state && call.Player() == player; };
    const auto active = [&]() -> CBasePlayerWeapon*
    { auto* item = player->m_pActiveItem; return item && !(item->pev->flags & FL_KILLME) ?
        dynamic_cast<CBasePlayerWeapon*>(item) : nullptr; };
    try
    {
        auto* weapon = active();
        if (!weapon) return true;
        const EHANDLE before(weapon);
        if (weapon->m_iId != WEAPON_KNIFE)
        {
            if (auto* provider = dynamic_cast<CSOLWeaponMonsterMelee*>(weapon))
            {
                const auto eligible = provider->MonsterMeleeType();
                if (!current()) return call.Cancel();
                if (player->m_pActiveItem == weapon &&
                    (before.Get<CBaseEntity>() != weapon || !Live(before))) return call.Cancel();
                if (!eligible) return true;
            }
            else
            {
                // Each exact stock type was checked against its original
                // final vtable. Do not infer this from a numeric ID range.
                const auto& type = typeid(*weapon);
                if (type == typeid(CP228) || type == typeid(CSCOUT) || type == typeid(CHEGrenade) ||
                    type == typeid(CXM1014) || type == typeid(CC4) || type == typeid(CMAC10) ||
                    type == typeid(CAUG) || type == typeid(CSmokeGrenade) || type == typeid(CELITE) ||
                    type == typeid(CFiveSeven) || type == typeid(CUMP45) || type == typeid(CSG550) ||
                    type == typeid(CGalil) || type == typeid(CFamas) || type == typeid(CUSP) ||
                    type == typeid(CGLOCK18) || type == typeid(CAWP) || type == typeid(CMP5N) ||
                    type == typeid(CM249) || type == typeid(CM3) || type == typeid(CM4A1) ||
                    type == typeid(CTMP) || type == typeid(CG3SG1) || type == typeid(CFlashbang) ||
                    type == typeid(CDEAGLE) || type == typeid(CSG552) || type == typeid(CAK47) ||
                    type == typeid(CP90) || CSOL_CannonEmptyMonsterKillCallbacks(*weapon) ||
                    CSOL_BeamEmptyMonsterKillCallbacks(*weapon) || CSOL_TKnifeEmptyMonsterKillCallbacks(*weapon) ||
                    CSOL_K3EmptyMonsterKillCallbacks(*weapon) || CSOL_MeleeEmptyMonsterKillCallbacks(*weapon) ||
                    CSOL_M249EXEmptyMonsterKillCallbacks(*weapon) || CSOL_Balrog11EmptyMonsterKillCallbacks(*weapon) ||
                    CSOL_Skull1EmptyMonsterKillCallbacks(*weapon) || CSOL_IsSkull2(*weapon) || CSOL_IsSkull3(*weapon) || CSOL_IsSkull5(*weapon))
                    // CK3 v230 and both knife helpers have a verified zero result.
                    return true;
                error.message = "scenario weapon melee-type predicate is not implemented";
                return false;
            }
        }
        weapon = active(); // Original re-reads active after the type predicate.
        if (!weapon) return true;
        const EHANDLE parametersOwner(weapon);
        std::array<float, 7> parameters{1, 1, 1, 1, 1, 1, 0.7f};
        if (auto* provider = dynamic_cast<CSOLWeaponMonsterMelee*>(weapon))
        {
            const auto accepted = provider->MonsterMeleeParameters(parameters);
            if (!current()) return call.Cancel();
            if (parametersOwner.Get<CBaseEntity>() != weapon || !Live(parametersOwner)) return call.Cancel();
            if (accepted != 1) return true;
        }
        else if (typeid(*weapon) == typeid(CKnife) && !state->profile.weaponEffectsBlocked21F2)
        {
            const auto weapons = session->Weapons();
            std::int32_t item{};
            if (!weapons || !weapons->Item(*player, WEAPON_KNIFE, item, error)) return false;
            if (!current() || session->Weapons() != weapons) return call.Cancel();
            if (item == 161) return true; // Original key0 default strategy's v44 returns AL0.
            error.message = "scenario selected knife melee strategy is not implemented";
            return false;
        }
        else
        {
            error.message = "scenario weapon melee parameters are not implemented";
            return false;
        }
        auto* monster = call.Current();
        const Vector offset = monster->pev->origin - player->pev->origin;
        const float squared = (offset.y * offset.y + offset.x * offset.x) + offset.z * offset.z;
        const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
        const float x = length > 0 ? offset.x / length : 0;
        const float y = length > 0 ? offset.y / length : 0;
        const Vector velocity(monster->pev->velocity.x + (x * 200.0f) * parameters[4],
            monster->pev->velocity.y + (y * 200.0f) * parameters[4],
            monster->pev->velocity.z + parameters[5] * 250.0f);
        if (!gpGlobals || !std::isfinite(squared) || !std::isfinite(length) ||
            !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
            !std::isfinite(gpGlobals->time + 1.0f))
        { error.message = "scenario melee impulse is not finite"; return false; }
        monster->pev->velocity = velocity;
        monster->HostState().moveAfter = gpGlobals->time + 1.0f;
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

struct CCSOLScenarioDamageServices::BossState
{
    struct Participant { EHANDLE player; float damage{}; };
    std::array<Participant, 33> participants;
    float total{}, maximum{}, health{}, sentHealth{}, nextHealth{};
    std::uint32_t hits{};
    int appearance{-1}, entityIndex{};
    std::uint64_t round{};
    EHANDLE entity;
    std::uint64_t lease{}, revision{};
    bool healthVisible{};
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::vector<std::pair<int, int>> retiredHealth;
};

CCSOLScenarioDamageServices::CCSOLScenarioDamageServices(std::weak_ptr<CCSOLScenarioRuntime> runtime)
    : m_runtime(std::move(runtime)), m_boss(std::make_shared<BossState>()) {}

void CCSOLScenarioDamageServices::ResetBossDamageState()
{
    auto state = std::make_shared<BossState>();
    // The manager's formal round reset disables health polling until a new
    // summary monster is initialized (11382D39, float bits47C34F80).
    state->nextHealth = 99999.0f;
    if (const auto runtime = m_runtime.lock(); runtime && !runtime->Closing() && runtime->session)
    {
        state->round = runtime->session->Map().RoundGeneration();
        state->access = runtime->session->Map().EntityAccess();
        if (m_boss && state->access && m_boss->access == state->access && state->access->Allowed())
        {
            state->retiredHealth = m_boss->retiredHealth;
            if (m_boss->appearance >= 0 && m_boss->entityIndex > 0)
                state->retiredHealth.emplace_back(m_boss->appearance, m_boss->entityIndex);
        }
    }
    m_boss = std::move(state);
}

bool CCSOLScenarioDamageServices::ClearRetiredBossHealth(csol::scenario::ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    const auto state = m_boss;
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this || !runtime->session) return false;
    const auto session = runtime->session;
    const auto valid = [&] { return !runtime->Closing() && runtime->monsterDamageServices.get() == this &&
        runtime->session == session && state == m_boss && state->access && state->access->Allowed() &&
        session->Map().EntityAccess() == state->access && session->Map().RoundGeneration() == state->round; };
    if (!valid()) return false;
    while (!state->retiredHealth.empty())
    {
        const auto item = state->retiredHealth.front();
        state->retiredHealth.erase(state->retiredHealth.begin());
        const bool sent = session->Messages().SendBossHealthRatio(item.first, 0, 0, item.second);
        if (!valid()) return false;
        if (!sent)
        {
            state->retiredHealth.insert(state->retiredHealth.begin(), item);
            error.message = "Boss retired health clear message failed";
            return false;
        }
    }
    return true;
}

void CCSOLScenarioDamageServices::RemoveBossParticipant(const EHANDLE& player)
{
    const auto state = m_boss;
    if (!state || !player.Get()) return;
    for (auto& entry : state->participants)
        if (entry.player.Get() == player.Get() && entry.player.GetSerialNumber() == player.GetSerialNumber())
        {
            state->total -= entry.damage;
            entry = {};
            return;
        }
}

namespace
{
// CVTTSS2SI returns the indefinite signed value for NaN/out-of-range. Keep
// that wire behavior (including zero-total0/0), without undefined C++ casts.
std::int32_t BossTruncate(float value)
{
    return std::isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f ?
        static_cast<std::int32_t>(value) : std::numeric_limits<std::int32_t>::min();
}
}

bool CCSOLScenarioDamageServices::RecordBossParticipation(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    auto* player = call.Player();
    if (!player || static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) return true;
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    if (m_boss->round != round) ResetBossDamageState();
    const auto state = m_boss;
    const auto current = [&]
    { return Pin(call) == runtime && runtime->session == session && m_boss == state &&
        session->Map().RoundGeneration() == round; };
    if (!gpGlobals || gpGlobals->maxClients < 1 || gpGlobals->maxClients > 32)
    { error.message = "Boss contribution client capacity is invalid"; return false; }
    const int index = ENTINDEX(player->edict());
    if (!current()) return call.Cancel();
    if (index < 1 || index > gpGlobals->maxClients)
    { error.message = "Boss contribution player index is invalid"; return false; }
    auto& participant = state->participants[index];
    // Remove a retired occupant even if engine teardown preceded its explicit
    // disconnect callback. A recycled client slot cannot inherit contribution.
    if (participant.player.Get() != player->edict())
    { state->total -= participant.damage; participant = {}; }
    participant.player = player;
    const float accumulated = participant.damage + call.damage;
    const float total = state->total + call.damage;
    if (!std::isfinite(accumulated) || !std::isfinite(total))
    { error.message = "Boss contribution accumulation overflowed"; return false; }
    ++state->hits;
    participant.damage = accumulated;
    state->total = total;
    struct Rank { int index; float damage; };
    std::array<Rank, 33> ranking;
    for (int i = 0; i < 33; ++i) ranking[i] = {i, state->participants[i].damage};
    std::sort(ranking.begin(), ranking.end(), [](const Rank& a, const Rank& b) { return a.damage > b.damage; });
    const auto send = [&](edict_t* recipient, int rank)
    {
        const auto& entry = ranking[rank];
        const bool result = session->Messages().SendBossParticipation(recipient, rank,
            entry.damage > 0 ? entry.index : 0, BossTruncate(entry.damage / 100.0f),
            BossTruncate((entry.damage / state->total) * 100.0f));
        if (!current()) return call.Cancel();
        if (!result) error.message = "Boss contribution message failed";
        return result;
    };
    int attackerRank = -1;
    for (int rank = 0; rank < 33; ++rank)
        if (ranking[rank].index == index) attackerRank = rank;
    for (int rank = 0; rank < 3; ++rank) if (!send(nullptr, rank)) return false;
    if (attackerRank >= 3 && !send(player->edict(), attackerRank)) return false;
    // Original uses the incremented hit count, so the first poll is slot2.
    const int polled = std::bit_cast<std::int32_t>(state->hits) % gpGlobals->maxClients + 1;
    auto* entity = CSOL_ScenarioPlayerEntity(polled, session->Map().EntityAccess());
    if (!current()) return call.Cancel();
    auto* recipient = dynamic_cast<CBasePlayer*>(entity);
    if (recipient && !(recipient->pev->flags & FL_KILLME))
    {
        const bool playerType = recipient->IsPlayer() != 0;
        if (!current()) return call.Cancel();
        if (!playerType) return true;
        for (int rank = 0; rank < 33; ++rank)
            if (ranking[rank].index == polled) return send(recipient->edict(), rank);
    }
    return true;
}

bool CCSOLScenarioDamageServices::InitializeBossHealth(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this ||
        !runtime->session || !call.BelongsTo(runtime->session->Map()) || !call.Current()) return false;
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    if (m_boss->round != round) ResetBossDamageState();
    const auto state = m_boss;
    auto revision = state->revision;
    const auto current = [&]
    { return !runtime->Closing() && runtime->monsterDamageServices.get() == this &&
        runtime->session == session && m_boss == state && state->revision == revision &&
        session->Map().RoundGeneration() == round && call.Current(); };
    auto* monster = call.Current();
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance) { error.message = "Boss health appearance is unavailable"; return false; }
    if (!appearance->healthSummary) return true;
    if (!ClearRetiredBossHealth(error) || !current()) return false;
    const int index = ENTINDEX(monster->edict());
    if (!current()) return false;
    if (!std::isfinite(monster->pev->health) || !std::isfinite(monster->pev->max_health) || index <= 0)
    { error.message = "Boss initial health is invalid"; return false; }
    if (state->appearance >= 0 && state->appearance != appearance->index && state->entityIndex > 0)
    {
        state->retiredHealth.emplace_back(state->appearance, state->entityIndex);
        if (!ClearRetiredBossHealth(error) || !current()) return false;
    }
    state->maximum = monster->pev->max_health;
    state->health = monster->pev->health;
    state->sentHealth = 0;
    state->nextHealth = 0;
    state->appearance = appearance->index;
    state->entityIndex = index;
    state->access = session->Map().EntityAccess();
    state->entity = monster;
    state->lease = session->Monsters().Lease(*monster);
    revision = ++state->revision;
    state->healthVisible = state->health > 0;
    const bool initialized = session->Messages().SendBossHealthInit(state->appearance,
        BossTruncate(state->maximum), index);
    if (!current()) return false;
    if (!initialized) { error.message = "Boss health initialization message failed"; return false; }
    const bool sent = session->Messages().SendBossHealthRatio(state->appearance, state->health, state->maximum, index);
    if (!current()) return false;
    if (!sent) error.message = "Boss initial health ratio message failed";
    return sent;
}

bool CCSOLScenarioDamageServices::RecordBossHealth(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    if (m_boss->round != round) ResetBossDamageState();
    const auto state = m_boss;
    const auto revision = state->revision;
    const auto current = [&]
    { return Pin(call) == runtime && runtime->session == session && m_boss == state &&
        state->revision == revision && session->Map().RoundGeneration() == round; };
    auto* monster = call.Current();
    // A late hit on an older instance of this appearance must not remove the
    // health panel of the Boss initialized after it (including pool reuse).
    if (state->entity.Get<CBaseEntity>() != monster || monster->Pool() != &session->Monsters() ||
        !session->Monsters().Borrowed(*monster) || session->Monsters().Lease(*monster) != state->lease) return true;
    const auto* record = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!record) { error.message = "Boss damaged health appearance is unavailable"; return false; }
    const int appearance = record->index;
    const int index = ENTINDEX(monster->edict());
    if (!current()) return call.Cancel();
    if (!std::isfinite(monster->pev->health) || !std::isfinite(monster->pev->max_health) || index <= 0)
    { error.message = "Boss damaged health is invalid"; return false; }
    if (monster->pev->health <= 0)
    {
        const bool sent = session->Messages().SendBossHealthRatio(appearance, 0, 0, index);
        if (!current()) return call.Cancel();
        if (!sent) { error.message = "Boss lethal health ratio message failed"; return false; }
        state->healthVisible = false;
        monster->pev->health = 0;
    }
    state->health = monster->pev->health;
    state->maximum = monster->pev->max_health;
    state->appearance = appearance;
    state->entityIndex = index;
    return true;
}

bool CCSOLScenarioDamageServices::UpdateBossHealth(float now, csol::scenario::ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this || !runtime->session) return false;
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    if (m_boss->round != round) ResetBossDamageState();
    const auto state = m_boss;
    if (!ClearRetiredBossHealth(error) || m_boss != state) return false;
    const auto revision = state->revision;
    if (!std::isfinite(now)) { error.message = "Boss health update time is invalid"; return false; }
    if (!(now > state->nextHealth)) return true;
    state->nextHealth = now + 0.3f;
    if (state->appearance < 0 || state->entityIndex <= 0) return true;
    auto* monster = state->access && state->access->Allowed() && session->Map().EntityAccess() == state->access ?
        dynamic_cast<CCSOLScenarioMonster*>(state->entity.Get<CBaseEntity>()) : nullptr;
    if (!monster || monster->Pool() != &session->Monsters() || !session->Monsters().Borrowed(*monster) ||
        session->Monsters().Lease(*monster) != state->lease || monster->pev->deadflag != DEAD_NO)
        state->health = 0;
    else
    {
        state->health = monster->pev->health;
        state->maximum = monster->pev->max_health;
    }
    // Init already made the panel visible even when the first periodic
    // sample has not run. A world kill or pool return must still clear it.
    if (state->health == state->sentHealth && !(state->healthVisible && state->health <= 0)) return true;
    const bool sent = session->Messages().SendBossHealthRatio(state->appearance, state->health,
        state->maximum, state->entityIndex);
    if (runtime->Closing() || runtime->monsterDamageServices.get() != this || runtime->session != session ||
        m_boss != state || state->revision != revision || session->Map().RoundGeneration() != round) return false;
    if (!sent) { error.message = "Boss health update message failed"; return false; }
    state->sentHealth = state->health;
    state->healthVisible = state->health > 0;
    return true;
}

bool CCSOLScenarioDamageServices::ReplayBossHealth(const EHANDLE& recipient,
    csol::scenario::ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this || !runtime->session) return false;
    const auto session = runtime->session;
    const auto state = m_boss;
    if (state->appearance < 0 || state->entityIndex <= 0) return true;
    const auto revision = state->revision;
    const auto current = [&]() -> CBasePlayer*
    {
        if (runtime->Closing() || runtime->monsterDamageServices.get() != this || runtime->session != session ||
            m_boss != state || state->revision != revision || !state->access || !state->access->Allowed() ||
            session->Map().EntityAccess() != state->access || session->Map().RoundGeneration() != state->round) return nullptr;
        auto* player = dynamic_cast<CBasePlayer*>(recipient.Get<CBaseEntity>());
        return player && !player->has_disconnected && !(player->pev->flags & FL_KILLME) ? player : nullptr;
    };
    auto* player = current();
    if (!player) return false;
    const auto liveBoss = [&]() -> CCSOLScenarioMonster*
    {
        if (!current()) return nullptr;
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(state->entity.Get<CBaseEntity>());
        return monster && monster->Pool() == &session->Monsters() && session->Monsters().Borrowed(*monster) &&
            session->Monsters().Lease(*monster) == state->lease && monster->pev->deadflag == DEAD_NO &&
            monster->pev->health > 0 ? monster : nullptr;
    };
    auto* monster = liveBoss();
    if (!monster) return true;
    const float maximum = monster->pev->max_health;
    const bool initialized = session->Messages().SendBossHealthInit(state->appearance,
        BossTruncate(maximum), state->entityIndex, player->edict());
    player = current();
    if (!player) return false;
    if (!initialized) { error.message = "Boss joined-player health initialization failed"; return false; }
    monster = liveBoss();
    const bool sent = session->Messages().SendBossHealthRatio(state->appearance, monster ? monster->pev->health : 0,
        monster ? monster->pev->max_health : 0, state->entityIndex, player->edict());
    if (!current()) return false;
    if (!sent) error.message = "Boss joined-player health ratio failed";
    return sent;
}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLScenarioDamageServices::Pin(CSOLMonsterDamageCall& call) const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this ||
        !runtime->session || !call.Spawn().BelongsTo(runtime->session->Map()) || !call.Current()) return {};
    return runtime;
}

const bool& CCSOLScenarioDamageServices::DamageSuppressed() const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterDamageServices.get() != this || !runtime->session)
        return m_retiredDamageSuppressed;
    const auto* rules = dynamic_cast<CHalfLifeMultiplay*>(CSOL_ScenarioOwnerRules(runtime->session.get()));
    return rules ? rules->m_bRoundTerminating : m_retiredDamageSuppressed;
}

bool CCSOLScenarioDamageServices::WeaponEffectsBlocked21F2(CSOLMonsterDamageCall& call) const
{
    const auto runtime = Pin(call);
    if (!runtime) { call.Cancel(); return true; }
    auto* player = call.Player();
    if (!player) return false;
    const auto state = runtime->players ? runtime->players->Find(EHANDLE(player)) : nullptr;
    if (!state) throw std::runtime_error("scenario damage attacker profile is unavailable");
    return state->profile.weaponEffectsBlocked21F2 != 0;
}

bool CCSOLScenarioDamageServices::WeaponEffectsBlocked34F0(CSOLMonsterDamageCall& call) const
{
    const auto runtime = Pin(call);
    if (!runtime) { call.Cancel(); return true; }
    auto* player = call.Player();
    if (!player) return false;
    const auto state = runtime->players ? runtime->players->Find(EHANDLE(player)) : nullptr;
    if (!state) throw std::runtime_error("scenario damage attacker profile is unavailable");
    return state->profile.weaponEffectsBlocked34F0;
}

bool CCSOLScenarioDamageServices::RulesAfterAttackDamage(CSOLMonsterDamageCall& call,
    int capturedSource, csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    const auto keepAlive = runtime->monsterDamageServices;
    if (!call.Player()) return true;
    const auto players = runtime->players;
    if (!players) { error.message = "scenario damage player registry is unavailable"; return false; }
    const auto current = [&]
    { return Pin(call) == runtime && runtime->players == players; };
    try
    {
        const bool modified = players->ApplyAddonDamage(call, runtime, error);
        if (!current()) return call.Cancel();
        if (!modified || !error.message.empty()) return false;
        const bool recorded = players->RecordWeaponDamage(call, capturedSource, runtime, error);
        if (!current()) return call.Cancel();
        return recorded && error.message.empty();
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioDamageServices::BeforeHitReward(CSOLMonsterDamageCall& call,
    CSOLMonsterHitAccount& account, csol::scenario::ConfigError& error)
{
    account = {};
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    const auto keepAlive = runtime->monsterDamageServices;
    const auto session = runtime->session;
    // MP 11386B9A..11386C08 reads the current difficulty through 10823570.
    // Keep this hit's owner pinned; replay still invalidates it to cancel an
    // old callback, while the next hit resolves the replacement normally.
    account.rewards = session->HitRewards();
    if (!account.rewards) { error.message = "scenario hit reward owner is unavailable"; return false; }
    if (!call.Player()) return true;
    const auto players = runtime->players;
    const auto weapons = session->Weapons();
    if (!players || !weapons) { error.message = "scenario pre-hit owners are unavailable"; return false; }
    const EHANDLE actor(call.Player());
    const auto state = players->Find(actor);
    if (!state) { error.message = "scenario pre-hit player profile is unavailable"; return false; }
    const auto current = [&]
    { return Pin(call) == runtime && runtime->session == session && runtime->players == players &&
        session->Weapons() == weapons && players->Find(actor) == state; };
    const auto step = [&](bool result)
    { if (!current()) return call.Cancel(); return result && error.message.empty(); };
    try
    {
        if (!step(weapons->ApplyPartHitEffects(call, runtime, error)) ||
            !step(players->ApplyCriticalBuffDamage(call, runtime, error)) ||
            !step(weapons->ApplyAttributeDamage(call, runtime, error)) ||
            !step(CSOL_ConsumeMonsterDamageEffect(call, runtime, error)) ||
            !step(players->ApplyAttackBuffDamage(call, runtime, error))) return false;
        account.moneyExtractor = state->profile.moneyExtractor;
        if (account.moneyExtractor)
        {
            if (!runtime->room.moneyOption)
            { error.message = "scenario hit money extractor room option is unavailable"; return false; }
            account.roomOption = *runtime->room.moneyOption;
        }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioDamageServices::AfterHitReward(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    const auto keepAlive = runtime->monsterDamageServices;
    if (!call.Player()) return true; // Mode15's later1143DD80 has no side effects.
    const auto session = runtime->session;
    const auto weapons = session->Weapons();
    if (!weapons) { error.message = "scenario post-hit weapon directory is unavailable"; return false; }
    const auto current = [&]
    { return Pin(call) == runtime && runtime->session == session && session->Weapons() == weapons; };
    try
    {
        const bool notified = weapons->NotifyDamageBuff(call, runtime, error);
        if (!current()) return call.Cancel();
        if (!notified || !error.message.empty()) return false;
        const bool modified = weapons->ApplyEnhancementDamage(call, runtime, error);
        if (!current()) return call.Cancel();
        return modified && error.message.empty();
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioDamageServices::AfterHealthDamage(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return call.Cancel();
    const auto keepAlive = runtime->monsterDamageServices;
    const auto session = runtime->session;
    if (auto* player = call.Player())
    {
        const auto players = runtime->players;
        const auto weapons = session->Weapons();
        const auto deaths = runtime->deathServices;
        const auto tables = session->Map().Tables();
        if (!players || !weapons || !deaths || !tables || !tables->mapId || !deaths->CachedMapId())
        { error.message = "scenario post-health owners or map identity are unavailable"; return false; }
        const EHANDLE actor(player);
        const auto state = players->Find(actor);
        if (!state) { error.message = "scenario post-health player profile is unavailable"; return false; }
        const auto current = [&]
        { return Pin(call) == runtime && runtime->session == session && runtime->players == players &&
            session->Weapons() == weapons && runtime->deathServices == deaths &&
            session->Map().Tables() == tables && players->Find(actor) == state; };
        const auto step = [&](bool result)
        { if (!current()) return call.Cancel(); return result && error.message.empty(); };
        try
        {
            const int hitgroup = call.Current()->HostState().lastHitgroup;
            const float damage = call.damage;
            const int cachedMap = *deaths->CachedMapId(), currentMap = *tables->mapId;
            if (!step(players->FinishAddonDamage(call, runtime, error)) ||
                !step(players->RecordMonsterHeadHit(call, hitgroup, runtime, error)) ||
                !step(players->RecordMonsterKillChain(call, runtime, cachedMap, currentMap, error))) return false;
            bool boss{};
            if (!csol::scenario::MonsterStatisticsBoss(call.Current()->Type(), cachedMap, currentMap, boss, error)) return false;
            if (!step(deaths->GoldenHit(call, error))) return false;
            // MP108258A5..108258CD: after the Golden hit callback and before
            // the active skill. Only the resolved player's type32800 hit is
            // forwarded; this is not a generic Boss damage redirect.
            const auto elites = runtime->eliteManager;
            if (elites && call.Current()->Type()->id == 32800)
            {
                const EHANDLE victim(call.Current());
                if (!step(elites->ForwardFollowerDamage(victim, call.InflictorHandle(),
                    actor, damage, error))) return false;
                if (runtime->eliteManager != elites) return call.Cancel();
            }
            // Optional character abilities must not interrupt base combat after
            // health was committed. Accept only a completed, matching selection
            // with an explicitly unavailable adapter, never a lost/stale one.
            if (!state->activeSkill)
            {
                std::optional<csol::scenario::ClassModDefinition> selected;
                if (!step(players->CurrentClassMod(actor, 10, selected, error))) return false;
                const auto& prepared = state->activeSkillDefinition;
                const bool sameDefinition = selected.has_value() == prepared.has_value() &&
                    (!selected || (selected->item == prepared->item && selected->type == prepared->type &&
                        selected->subtype == prepared->subtype && selected->values == prepared->values));
                if (!state->activeSkillPrepared || state->activeSkillClass != state->classId ||
                    !sameDefinition || (selected && !state->activeSkillUnavailable) ||
                    (!selected && state->activeSkillUnavailable))
                { error.message = "scenario active ability initialization is absent or stale"; return false; }
            }
            if (!step(players->NotifyActiveSkillMonsterHit(call, runtime, error)) ||
                !step(players->RecordMonsterDamage(call, runtime, boss, damage, error)) ||
                !step(players->NotifyMonsterHitComponent(call, runtime, error)) ||
                !step(players->NotifyMonsterHitEvent(call, runtime, error)) ||
                !step(weapons->RecordPartBloodSucking(call, runtime, error)) ||
                !step(CSOL_ApplyMonsterMeleeImpulse(call, runtime, error))) return false;
            // The later statistics-Boss predicate is freshly queried, after
            // component/weapon callbacks; the earlier captured boss fed v1DC.
            if (!csol::scenario::MonsterStatisticsBoss(call.Current()->Type(), cachedMap, currentMap, boss, error)) return false;
            if (boss && !step(RecordBossParticipation(call, error))) return false;
            if (!step(players->NotifyClassModMonsterDamage(call, runtime, error))) return false;
        }
        catch (...)
        {
            if (!current()) return call.Cancel();
            throw;
        }
    }
    if (Pin(call) != runtime || runtime->session != session) return call.Cancel();
    const auto* type = call.Current()->Type();
    const auto* appearance = csol::scenario::FindMonsterAppearance(type->appearance);
    if (!appearance) { error.message = "scenario post-health appearance is unavailable"; return false; }
    if (type->id == 30300 || type->id == 30700 || type->id == 31000)
    { error.message = "scenario special survival consumer is not implemented"; return false; }
    if (appearance->healthSummary && type->id != 31805 && !RecordBossHealth(call, error)) return false;
    return true;
}
