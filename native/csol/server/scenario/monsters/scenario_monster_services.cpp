// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_services.h"
#include "scenario_monster_healing.h"
#include "scenario_monster_skills.h"
#include "../bosses/scenario_oberon.h"
#include "../bosses/scenario_zombi_boss.h"
#include "../../../scenario/bosses/zombi_boss.hpp"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include "../core/scenario_gameplay.h"
#include "../core/scenario_death_services.h"
#include "../core/scenario_player_runtime.h"
#include "../core/scenario_player_impulse.h"
#include "scenario_monster_bomb.h"
#include "scenario_monster_effects.h"
#include "scenario_elite_services.h"
#include "scenario_monster_elite.h"
#include "scenario_monster_recovery.h"
#include "../../scenario_character_skills/elite_visuals.hpp"
#include "../core/game_rules_gate.h"
#include "../../../scenario/monsters/frozen_death.hpp"
#include "../../../scenario/monsters/frozen_smoke.hpp"
#include "../../../scenario/core/map_support.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

CCSOLScenarioMonsterServices::CCSOLScenarioMonsterServices(
    std::weak_ptr<CCSOLScenarioRuntime> runtime, std::shared_ptr<CCSOLScenarioSession> session,
    std::shared_ptr<CCSOLScenarioDeathServices> deaths)
    : m_runtime(std::move(runtime)), m_session(std::move(session)), m_deaths(std::move(deaths)) {}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLScenarioMonsterServices::Pin() const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->monsterServices.get() != this ||
        runtime->session != m_session || runtime->deathServices != m_deaths ||
        !m_session || !m_session->Map().EntityAccessAllowed() || !m_deaths ||
        !m_deaths->Golden().EmptyPlan()) return {};
    if (runtime->gameplay != m_gameplay) return {};
    if (m_gameplay && runtime->monsterDamageServices != m_damage) return {};
    return runtime;
}

bool CCSOLScenarioMonsterServices::Current(CSOLMonsterSpawnCall& call) const
{
    return Pin() && m_gameplay && call.BelongsTo(m_session->Map()) && call.Current();
}

bool CCSOLScenarioMonsterServices::Assemble(std::shared_ptr<CCSOLMonsterDamageServices> damage,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime) return false;
    const auto* tables = m_session->Map().Tables();
    if (m_gameplay || !damage || runtime->monsterDamageServices != damage ||
        m_session->State() != CSOLScenarioSessionState::Precached ||
        m_session->ResourceScope() != CSOLScenarioResourceScope::Combat || !tables ||
        !csol::scenario::HasMapGameplay(tables->mapName, tables->scenarioType) || !tables->mapId || *tables->mapId <= 0)
    {
        error.message = "scenario monster assembly needs fresh combat resources, map ID and real damage services";
        return false;
    }
    auto gameplay = std::make_shared<CCSOLScenarioGameplay>(*m_session, *m_deaths, *this, *this,
        *tables->mapId, shared_from_this());
    m_damage = std::move(damage);
    m_gameplay = gameplay;
    runtime->gameplay = std::move(gameplay);
    return true;
}

std::unique_ptr<CCSOLMonsterSpawnBehavior> CCSOLScenarioMonsterServices::CreateOrdinary(
    CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return {};
    // These registries are installed by Session/PlayerRuntime::Activate, after
    // world precache. Capturing them in the provider constructor loses them.
    const auto rewards = m_session->HitRewards();
    const auto players = m_session->Players();
    const auto equipment = m_session->EquipmentState();
    const auto weapons = m_session->Weapons();
    if (!m_damage || !rewards || !players || !equipment || !weapons)
    {
        error.message = "scenario ordinary monster requires active damage/reward/player/equipment/weapon services";
        return {};
    }
    auto damage = std::make_shared<CCSOLOrdinaryMonsterDamage>(m_session->Map(), Deaths(),
        players, equipment, weapons, m_damage);
    return std::make_unique<CCSOLOrdinaryMonsterBehavior>(shared_from_this(), std::move(damage));
}

std::unique_ptr<CCSOLMonsterSpawnBehavior> CCSOLScenarioMonsterServices::CreateBoss(
    CSOLMonsterSpawnCall& call, std::int32_t recordId, csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return {};
    auto* monster = call.Current();
    if (monster->Type() && monster->Type()->skill >= 1000 && recordId == monster->TypeId() &&
        csol::scenario::zombi_boss::FactoryDefaultRecord(recordId))
    {
        // MP 113E5400 default branch 113E5496: CZombiBossImprov (117FE46C).
        const auto rewards = m_session->HitRewards();
        const auto players = m_session->Players();
        const auto equipment = m_session->EquipmentState();
        const auto weapons = m_session->Weapons();
        const auto skills = m_session->BossSkills();
        if (!m_damage || !rewards || !players || !equipment || !weapons || !skills)
        { error.message = "CZombiBossImprov requires active damage/reward/player/equipment/weapon/SK services"; return {}; }
        if (!m_zombiBoss)
        {
            m_zombiBoss = std::make_shared<CCSOLZombiBossServices>(runtime, shared_from_this());
            if (Pin() != runtime || !Current(call)) return {};
        }
        auto damage = std::make_shared<CCSOLOrdinaryMonsterDamage>(m_session->Map(), Deaths(),
            players, equipment, weapons, m_damage);
        return std::make_unique<CCSOLZombiBossBehavior>(m_zombiBoss, std::move(damage), skills);
    }
    if (recordId != 30200 || !monster->Type() || monster->Type()->skill != 1002)
    {
        error.message = "scenario boss behavior is not implemented for record " + std::to_string(recordId);
        return {};
    }
    const auto rewards = m_session->HitRewards();
    const auto players = m_session->Players();
    const auto equipment = m_session->EquipmentState();
    const auto weapons = m_session->Weapons();
    const auto skills = m_session->BossSkills();
    if (!m_damage || !rewards || !players || !equipment || !weapons || !skills)
    { error.message = "Oberon requires active damage/reward/player/equipment/weapon/SK services"; return {}; }
    if (!m_oberon)
    {
        auto services = std::make_shared<CCSOLOberonServices>(runtime, shared_from_this());
        if (!services->Initialize(error) || Pin() != runtime || !Current(call)) return {};
        m_oberon = std::move(services);
    }
    auto damage = std::make_shared<CCSOLOrdinaryMonsterDamage>(m_session->Map(), Deaths(),
        players, equipment, weapons, m_damage);
    return std::make_unique<CCSOLOberonBehavior>(m_oberon, std::move(damage), skills);
}

bool CCSOLScenarioMonsterServices::SupportsElite() const
{
    const auto runtime = Pin();
    return runtime && m_gameplay && m_damage && runtime->eliteManager &&
        runtime->eliteManager->Current() && runtime->eliteServices &&
        runtime->eliteServices->Current() && runtime->players && runtime->players->Active() &&
        m_session->EliteSettings() && m_session->Navigation();
}

std::unique_ptr<CCSOLMonsterSpawnBehavior> CCSOLScenarioMonsterServices::CreateElite(
    CSOLMonsterSpawnCall& call, bool follower, csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call) || !error.message.empty()) return {};
    const auto rewards = m_session->HitRewards();
    const auto players = m_session->Players();
    const auto equipment = m_session->EquipmentState();
    const auto weapons = m_session->Weapons();
    const auto settings = m_session->EliteSettings();
    if (!SupportsElite() || !rewards || !players || !equipment || !weapons ||
        follower != runtime->eliteManager->FollowerSpawn())
    { error.message = "scenario elite requires its current manager, player and combat services"; return {}; }

    const auto weak = weak_from_this();
    const std::weak_ptr<CCSOLScenarioEliteServices> elites = runtime->eliteServices;
    CSOLEliteMonsterBindings bindings;
    bindings.qualifiedPlayers = [elites](CSOLMonsterSpawnCall& spawn, int& count,
        csol::scenario::ConfigError& status)
    {
        const auto service = elites.lock();
        return service && service->QualifiedPlayers(spawn, count, status);
    };
    bindings.spawnFollower = [elites](CSOLMonsterSpawnCall& spawn, csol::scenario::ConfigError& status)
    {
        const auto service = elites.lock();
        return service && service->SpawnFollower(spawn, status);
    };
    bindings.updateRecovery = [elites](CSOLMonsterSpawnCall& spawn, CSOLOrdinaryMonsterState& ordinary,
        float& changedAt, int& previousArea, bool& relocate, csol::scenario::ConfigError& status)
    {
        const auto service = elites.lock();
        return service && service->UpdateRecovery(spawn, ordinary, changedAt, previousArea, relocate, status);
    };
    bindings.replaceType = [weak](CSOLMonsterSpawnCall& spawn, CSOLMonsterSpawnState& state,
        std::shared_ptr<const csol::scenario::MonsterType> type, csol::scenario::ConfigError& status)
    {
        const auto service = weak.lock();
        const auto owner = service ? service->Pin() : nullptr;
        if (!owner || !service->Current(spawn)) return false;
        const bool replaced = service->m_gameplay->Factory().ReplaceEliteSpawnType(
            spawn, state, std::move(type), status);
        return service->Pin() == owner && service->Current(spawn) && replaced;
    };
    bindings.recoverHealth = [weak](CSOLMonsterSpawnCall& spawn, float amount,
        const CSOLMonsterDeathContext& context, csol::scenario::ConfigError& status)
    {
        const auto service = weak.lock();
        const auto owner = service ? service->Pin() : nullptr;
        if (!owner || !service->Current(spawn)) return false;
        const bool recovered = CSOL_RecoverMonsterSelf(owner, spawn, amount, context, status);
        return service->Pin() == owner && service->Current(spawn) && recovered;
    };
    bindings.frostSlow = [weak](CSOLMonsterSpawnCall& spawn, const EHANDLE& target,
        float duration, float speed, csol::scenario::ConfigError& status)
    {
        const auto service = weak.lock();
        const auto owner = service ? service->Pin() : nullptr;
        if (!owner || !service->Current(spawn)) return false;
        const auto playerRuntime = owner->players;
        if (!playerRuntime)
        { status.message = "elite frost requires the current player registry"; return false; }
        bool applied{};
        const bool handled = playerRuntime->TryMonsterFrost(target, duration, speed, applied, status);
        // A genuine slow-immunity or shorter repeat is a handled contact.
        return service->Pin() == owner && service->Current(spawn) &&
            owner->players == playerRuntime && handled;
    };
    bindings.publishEntityEffect = csol::scenario::skills::PublishEliteEntityVisual;
    bindings.publishAreaEffect = csol::scenario::skills::PublishEliteAreaVisual;
    auto damage = std::make_shared<CCSOLOrdinaryMonsterDamage>(m_session->Map(), Deaths(),
        players, equipment, weapons, m_damage);
    return std::make_unique<CCSOLEliteMonsterBehavior>(shared_from_this(), std::move(damage),
        runtime, settings, std::move(bindings));
}

bool CCSOLScenarioMonsterServices::UpdateEliteSchedule(csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !m_gameplay || !error.message.empty()) return false;
    const auto elites = runtime->eliteServices;
    if (!elites)
    { error.message = "scenario elite schedule service is unavailable"; return false; }
    const bool updated = elites->Update(error);
    return Pin() == runtime && runtime->eliteServices == elites && updated;
}

std::span<const std::size_t> CCSOLScenarioMonsterServices::ProtectedSlots() const
{
    // Factory/ordinary callers already catch service exceptions. An unloaded
    // or unsupported Golden plan must not masquerade as an empty collection.
    if (!Pin() || !m_gameplay) throw std::runtime_error("scenario Golden membership is unavailable");
    return {};
}

bool CCSOLScenarioMonsterServices::ResetRoundDamage(csol::scenario::ConfigError& error)
{
    error = {};
    const auto runtime = Pin();
    if (!runtime || !m_gameplay || !m_damage) return false;
    const auto damage = m_damage;
    damage->ResetBossDamageState();
    return Pin() == runtime && m_damage == damage;
}

bool CCSOLScenarioMonsterServices::UpdateHealthSummary(float now, csol::scenario::ConfigError& error)
{
    error = {};
    const auto runtime = Pin();
    if (!runtime || !m_gameplay || !m_damage) return false;
    const auto damage = m_damage;
    const bool updated = damage->UpdateBossHealth(now, error);
    return Pin() == runtime && m_damage == damage && updated;
}

bool CCSOLScenarioMonsterServices::AttributesApplied(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    error = {};
    const auto runtime = Pin();
    if (!runtime || !Current(call) || !m_damage) return false;
    const auto damage = m_damage;
    const bool initialized = damage->InitializeBossHealth(call, error);
    return Pin() == runtime && Current(call) && m_damage == damage && initialized;
}

bool CCSOLScenarioMonsterServices::ClearSpawnEffects(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return false;
    const auto keepAlive = runtime->monsterServices;
    const auto effects = call.Current()->DamageEffects();
    if (!effects) { error.message = "scenario spawn has no effect owner"; return false; }
    struct Guard
    {
        const CCSOLScenarioMonsterServices& services;
        CSOLMonsterSpawnCall& call;
        const CCSOLMonsterEffects* effects;
    } context{*this, call, effects.get()};
    const CSOLMonsterEffectGuard guard{[](const void* value) -> CCSOLScenarioMonster*
    {
        const auto& state = *static_cast<const Guard*>(value);
        if (!state.services.Current(state.call)) return nullptr;
        auto* monster = state.call.Current();
        return monster->DamageEffects().get() == state.effects ? monster : nullptr;
    }, &context};
    // The factory has already reset the three host fields and owns the speed
    // ring stage immediately after this call. Do not duplicate either stage.
    return effects->ClearAll(guard) && guard.Current();
}

bool CCSOLScenarioMonsterServices::ResolveSpawnOverlap(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return false;
    const auto keepAlive = runtime->monsterServices;
    return m_gameplay->ResolveSpawnOverlap(call, {}, error, [](const void* value)
    {
        return static_cast<bool>(static_cast<const CCSOLScenarioMonsterServices*>(value)->Pin());
    }, this) && Current(call);
}

bool CCSOLScenarioMonsterServices::RulesMonsterSpawned(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    // Ver2 v328 notifies Golden. The actual loaded empty plan has no member
    // to update; Pin requires that specific service and its loaded state.
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return false;
    const auto elites = runtime->eliteServices;
    if (!elites)
    { error.message = "scenario spawn has no elite point registry"; return false; }
    // 113859AC records Create's requested point after the rule callback.
    // Do not substitute the TRG group or rescan a nearby spawn point.
    const bool recorded = elites->RecordSuccessfulSpawn(call, call.Current()->SpawnPointId(), error);
    return Pin() == runtime && runtime->eliteServices == elites && Current(call) && recorded;
}

bool CCSOLScenarioMonsterServices::BeforeSpawnClear(CCSOLScenarioMonster& monster)
{
    const auto runtime = Pin();
    if (!runtime || !m_gameplay || monster.Pool() != &m_session->Monsters()) return false;
    CSOLMonsterSpawnCall call(Map(), Pool(), Deaths(), monster);
    // Empty Golden membership makes its decrement/replan branch inapplicable.
    return Current(call);
}

bool CCSOLScenarioMonsterServices::RequestSpawnClearDeath(CCSOLScenarioMonster& monster)
{
    const auto runtime = Pin();
    if (!runtime || !m_gameplay || monster.Pool() != &m_session->Monsters()) return false;
    const auto keepAlive = runtime->monsterServices;
    CSOLMonsterSpawnCall call(Map(), Pool(), Deaths(), monster);
    if (!Current(call)) return false;
    CSOLMonsterDeathArguments arguments;
    arguments.suppressSound = true;
    const auto result = Deaths().Request(monster, arguments);
    // Successful death may park this lease. Do not require a live-health or
    // current-type check after it, and do not retry accepted deferred deaths.
    return Pin() == runtime && call.SameSession() && result != CSOLDeathRequestResult::Failed &&
        Deaths().Error().empty() && m_gameplay->Deaths().Error().message.empty();
}

CCSOLScenarioMap& CCSOLScenarioMonsterServices::Map() { return m_session->Map(); }
bool CCSOLScenarioMonsterServices::OwnerCurrent() const { return Pin() && m_gameplay; }
CCSOLMonsterPool& CCSOLScenarioMonsterServices::Pool() { return m_session->Monsters(); }
CCSOLMonsterDeathRequests& CCSOLScenarioMonsterServices::Deaths()
{
    if (!m_gameplay) throw std::logic_error("scenario death dispatcher is not assembled");
    return m_gameplay->Deaths().Requests();
}
CCSOLScenarioOccupancy& CCSOLScenarioMonsterServices::Occupancy()
{
    if (!m_gameplay) throw std::logic_error("scenario occupancy is not assembled");
    return m_gameplay->Occupancy();
}
const CCSOLScenarioMessages& CCSOLScenarioMonsterServices::Messages() const { return m_session->Messages(); }
std::shared_ptr<const CCSOLMonsterNavigation> CCSOLScenarioMonsterServices::PinNavigation()
{ return Pin() ? m_session->Navigation() : nullptr; }

const bool& CCSOLScenarioMonsterServices::DamageSuppressed() const
{
    if (!Pin()) return m_retiredDamageSuppressed;
    // SDK entries pin the registered rules through the whole engine callback.
    // This is the real termination bit; freeze/wave dispatch are independent.
    const auto* rules = dynamic_cast<CHalfLifeMultiplay*>(CSOL_ScenarioOwnerRules(m_session.get()));
    return rules ? rules->m_bRoundTerminating : m_retiredDamageSuppressed;
}

bool CCSOLScenarioMonsterServices::LogicStopped() const
{
    const auto runtime = Pin();
    return !runtime || runtime->monsterLogicStopped;
}

bool CCSOLScenarioMonsterServices::PlayerTargetExcluded(CSOLMonsterSpawnCall& call,
    const EHANDLE& player, bool& excluded, csol::scenario::ConfigError& error)
{
    excluded = false;
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return false;
    auto* target = dynamic_cast<CBasePlayer*>(player.Get<CBaseEntity>());
    if (!target || target->has_disconnected || (target->pev->flags & FL_KILLME))
    {
        // Ordinary target validation can still hold the old handle during
        // disconnect/removal. Drop that candidate without poisoning the AI.
        excluded = true;
        return true;
    }
    const auto players = runtime->players;
    const auto state = players ? players->Find(player) : nullptr;
    if (!state && runtime->players && runtime->players->WasRemoved(player))
    {
        excluded = true;
        return true;
    }
    if (!state) { error.message = "scenario target has no current player profile"; return false; }
    const auto current = [&]
    {
        return Current(call) && Pin() == runtime && runtime->players == players &&
            players->Find(player) == state;
    };
    bool active{};
    csol::scenario::ConfigError diagnostic;
    const bool queried = players->Feature3Active(player, active, diagnostic,
        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current);
    if (!Current(call) || Pin() != runtime) return false;
    if (!queried || !current())
    {
        if (!diagnostic.message.empty()) { error = std::move(diagnostic); return false; }
        // A target/ability may retire during a clone without retiring this
        // monster. Skip this candidate, not the entire AI: returning false
        // without a diagnostic would become a permanent behavior error.
        excluded = true;
        return true;
    }
    // MP IdleThink 1137F45C and Oberon's true v114 retain hidden targets for
    // Boss skills. Synthetic elite 32800 keeps the ordinary feature3 rule.
    const auto* type = call.Current()->Type();
    excluded = active && !(type && type->skill >= 1000 && type->id != 32800);
    return true;
}

bool CCSOLScenarioMonsterServices::UseSkill(CSOLMonsterSpawnCall& call,
    CSOLOrdinaryMonsterState& state, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto runtime = Pin();
    if (!runtime || !Current(call) || !gpGlobals) return false;
    const auto skill = call.Current()->Type()->skill;
    if (skill == 6 || skill == 8 || skill == 9 || skill == 10 || skill == 12)
    {
        const auto* spawn = m_gameplay->Factory().State(*call.Current());
        if (!spawn) { error.message = "scenario projectile skill spawn state is unavailable"; return false; }
        struct Owner
        {
            const CCSOLScenarioMonsterServices& services;
            const CCSOLScenarioRuntime* runtime;
            CSOLMonsterSpawnCall& call;
            const CSOLMonsterSpawnState* spawn;
        } owner{*this, runtime.get(), call, spawn};
        const CSOLMonsterSkillGuard guard{[](const void* context)
            {
                const auto& owner = *static_cast<const Owner*>(context);
                return owner.services.Pin().get() == owner.runtime && owner.services.Current(owner.call) &&
                    owner.services.m_gameplay->Factory().State(*owner.call.Current()) == owner.spawn;
            }, &owner};
        switch (skill)
        {
        case 6: return CSOL_UseMonsterZombieBomb(call, state, *spawn, runtime, DamageSuppressed(), guard, error);
        case 9: return CSOL_UseMonsterDash(call, state, *spawn, DamageSuppressed(), guard);
        case 10: return CSOL_UseMonsterPierce(call, state, *spawn, DamageSuppressed(), guard);
        case 12: return CSOL_UseMonsterLeap(call, state, *spawn, DamageSuppressed(), guard, error);
        default: return CSOL_UseMonsterDisarm(call, state, *spawn, runtime, guard, error);
        }
    }
    if (call.Current()->Type()->skill == 3)
    {
        const auto round = Map().RoundGeneration();
        if (m_trapRound != round) { m_trapRound = round; m_trapUntil = 0; }
        const float now = gpGlobals->time;
        if (m_trapUntil > 0 && now > m_trapUntil) m_trapUntil = 0;
        float duration = 99999.0f, cooldown = 5.0f;
        // MP113F0AF0: one manager slot, separate from trap lifetime. An
        // occupied slot is a normal retry after 5+jitter seconds.
        if (m_trapUntil == 0)
        {
            m_trapUntil = now + 9.0f;
            call.Current()->pev->maxspeed = 1.0f;
            if (!CSOL_CreateMonsterTrap(call, runtime, error) || !Current(call) || Pin() != runtime)
                return false;
            duration = 1.0f;
            cooldown = 8.0f;
        }
        const float interval = cooldown + state.jitter * 5.0f;
        state.skillActive = {duration, gpGlobals->time + duration, gpGlobals->time};
        state.skill = {interval, gpGlobals->time + interval, gpGlobals->time};
        return true;
    }
    if (call.Current()->Type()->skill == 4)
    {
        const auto round = Map().RoundGeneration();
        if (m_smokeRound != round) { m_smokeRound = round; m_smokeUntil = {}; }
        const float now = gpGlobals->time;
        // The original manager clears only strictly expired positive slots.
        for (auto& until : m_smokeUntil) if (until > 0 && now > until) until = 0;
        const auto slot = std::find(m_smokeUntil.begin(), m_smokeUntil.end(), 0.0f);
        float duration = 99999.0f, cooldown = 5.0f;
        if (slot != m_smokeUntil.end())
        {
            const auto event = m_session->SmokeEvent();
            if (!event || !g_engfuncs.pfnPlaybackEvent)
            { error.message = "scenario smoke event service is unavailable"; return false; }
            *slot = now + 20.0f;
            Vector origin = call.Current()->pev->origin;
            origin.z -= 30.0f;
            Vector angles(0, 0, 0);
            // MP113F0CCF mode15: no invoker, fparams=0, iparam2=1.
            g_engfuncs.pfnPlaybackEvent(0, nullptr, event, 0, origin, angles, 0, 0, 0, 1, 0, 0);
            if (!Current(call) || Pin() != runtime) return false;
            call.Current()->pev->maxspeed = 1.0f;
            duration = 1.0f;
            cooldown = 25.0f;
        }
        const float interval = cooldown + state.jitter * 5.0f;
        state.skillActive = {duration, gpGlobals->time + duration, gpGlobals->time};
        state.skill = {interval, gpGlobals->time + interval, gpGlobals->time};
        return true;
    }
    if (call.Current()->Type()->skill == 5)
    {
        struct Owner
        {
            const CCSOLScenarioMonsterServices* services;
            const CCSOLScenarioRuntime* runtime;
        } owner{this, runtime.get()};
        return CSOL_UseMonsterHealing(call, Pool(), Messages(), state,
            {[](const void* context)
            {
                const auto& owner = *static_cast<const Owner*>(context);
                return owner.services->Pin().get() == owner.runtime;
            }, &owner}, error);
    }
    if (call.Current()->Type()->skill == 7)
    {
        // MP113F0970 table entry 7 jumps straight to the shared 113F1014
        // tail: no entity, pose or speed write, only 99999 active and a
        // 5+jitter retry. The long jump itself runs in target update.
        const float interval = 5.0f + state.jitter * 5.0f;
        state.skillActive = {99999.0f, gpGlobals->time + 99999.0f, gpGlobals->time};
        state.skill = {interval, gpGlobals->time + interval, gpGlobals->time};
        return true;
    }
    if (call.Current()->Type()->skill == 11 || call.Current()->Type()->skill == 13)
    {
        // MP113F0970: table 113F1088 entry 11 is 113F1014, the same shared
        // tail as entry 7, and skill 13 exceeds the 1..12 table (ja 113F1014).
        // 113F03D0 special-cases only skill 9, so this is the whole activation.
        const float interval = 5.0f + state.jitter * 5.0f;
        state.skillActive = {99999.0f, gpGlobals->time + 99999.0f, gpGlobals->time};
        state.skill = {interval, gpGlobals->time + interval, gpGlobals->time};
        return true;
    }
    // Skills 1/2 execute inside OrdinaryBehavior. Keep unsupported producers
    // explicit until their real entities and effects have been implemented.
    error.message = "scenario ordinary skill activation is not implemented for skill " +
        std::to_string(call.Current()->Type()->skill);
    return false;
}

bool CCSOLScenarioMonsterServices::ExplodeOnFree(CSOLMonsterSpawnCall& call, float damage,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto runtime = Pin();
    if (!runtime || !Current(call)) return false;
    const auto* rules = dynamic_cast<CHalfLifeMultiplay*>(CSOL_ScenarioOwnerRules(m_session.get()));
    if (!rules)
    {
        error.message = "scenario Free explosion has no rules";
        CSOL_RejectGameRules(error.message.c_str());
        return false;
    }
    // MP1137D84B: root+4 is CGameRules::m_bFreezePeriod; RestartRound sets it
    // to 1 before cleanup, so only exactly 1 skips the explosion.
    if (rules->m_bFreezePeriod == TRUE) return true;
    struct Owner
    {
        const CCSOLScenarioMonsterServices& services;
        const CCSOLScenarioRuntime* runtime;
        CSOLMonsterSpawnCall& call;
    } owner{*this, runtime.get(), call};
    const CSOLMonsterSkillGuard guard{[](const void* context)
        {
            const auto& owner = *static_cast<const Owner*>(context);
            return owner.services.Pin().get() == owner.runtime && owner.services.Current(owner.call);
        }, &owner};
    // 1137D86B..D891: damage, 157.48 (431D7AE1) and force 5.0 (40A00000).
    const bool exploded = CSOL_ApplyMonsterFreeExplosion(call, damage, 157.48f, 5.0f, Messages(), guard, error);
    if (!exploded && !error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
    return exploded;
}

bool CCSOLScenarioMonsterServices::ApplyContactImpulse(CSOLMonsterSpawnCall& call,
    const EHANDLE& target, const Vector& requested, csol::scenario::ConfigError& error)
{
    const auto runtime = Pin();
    if (!runtime || !Current(call) || !error.message.empty()) return false;
    auto* receiver = target.Get<CBaseEntity>();
    if (!receiver || !receiver->pev || (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    if (!std::isfinite(requested.x) || !std::isfinite(requested.y) || !std::isfinite(requested.z))
    { error.message = "scenario contact impulse is not finite"; return false; }
    if (dynamic_cast<CBasePlayer*>(receiver))
    {
        bool applied{};
        const bool result = CSOL_ApplyScenarioPlayerImpulse(m_session, target, requested, applied, error);
        // A modifier may reject the target's impulse. This remains a normal
        // contact, so the caller still writes the charging monster's XY tail.
        return Pin() == runtime && Current(call) && result;
    }
    // Skill9's caller permits only players and Classify17. With weapon=-1,
    // original114ECF00 accepts the scenario rules gate and directly replaces
    // XYZ; host+1F8 and the weapon>=536 impulse accumulator are not consulted.
    auto* monster = dynamic_cast<CCSOLScenarioMonster*>(receiver);
    auto* pool = monster ? monster->Pool() : nullptr;
    const auto lease = pool ? pool->Lease(*monster) : 0;
    if (!monster || !pool || !lease)
    { error.message = "scenario contact impulse target has no registered monster lease"; return false; }
    if (Pin() != runtime || !Current(call)) return false;
    if (target.Get<CBaseEntity>() != receiver || monster->Pool() != pool || pool->Lease(*monster) != lease) return true;
    monster->pev->velocity = requested;
    return true;
}

bool CCSOLScenarioMonsterServices::FrozenDeath(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
#pragma clang fp contract(off)
    const auto runtime = Pin();
    const auto session = m_session;
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        return runtime && Pin() == runtime && call.BelongsTo(session->Map()) ? call.Current() : nullptr;
    };
    auto* monster = current();
    if (!monster || !error.message.empty()) return false;
    if (!session->Models().Ready() || monster->Pool() != &session->Monsters())
    { error.message = "frozen death requires this session's precached monster pool"; return false; }
    csol::frozen_death::IceEvent ice;
    csol::frozen_smoke::Event smoke;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        ice.center[axis] = monster->pev->origin[axis] +
            (monster->pev->mins[axis] + monster->pev->maxs[axis]) * .5f;
        ice.size[axis] = monster->pev->size[axis] * (axis == 2 ? 1.0f : 2.0f);
        smoke.center[axis] = ice.center[axis];
        smoke.absmin[axis] = monster->pev->absmin[axis];
        smoke.absmax[axis] = monster->pev->absmax[axis];
    }
    if (!csol::frozen_death::Valid(ice) || !csol::frozen_smoke::Valid(smoke))
    { error.message = "frozen death bounds are invalid"; return false; }
    const bool iceSent = session->Messages().SendFrozenIce(ice);
    if (!current()) return false;
    if (!iceSent) { error.message = "frozen death fragment message failed"; return false; }
    const bool smokeSent = session->Messages().SendFrozenSmoke(smoke);
    if (!current()) return false;
    if (!smokeSent) { error.message = "frozen death cloud message failed"; return false; }
    // Original113EDFA0 parks immediately after both effects. Keep the captured
    // death's lease/behavior alive so animation, money and map tails continue.
    const bool parked = session->Monsters().ParkForDeath(*current());
    if (!current()) return false;
    if (!parked) error.message = "frozen death could not park its monster slot";
    return parked;
}
