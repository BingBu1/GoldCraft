// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_services.h"
#include "scenario_monster_healing.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_gameplay.h"
#include "scenario_death_services.h"
#include "scenario_player_runtime.h"
#include "scenario_monster_effects.h"
#include "game_rules_gate.h"

#include <algorithm>
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
        tables->mapName != "zs_deadend" || tables->scenarioType != 1 || !tables->mapId || *tables->mapId <= 0)
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
        rewards, players, equipment, weapons, m_damage);
    return std::make_unique<CCSOLOrdinaryMonsterBehavior>(shared_from_this(), std::move(damage));
}

std::unique_ptr<CCSOLMonsterSpawnBehavior> CCSOLScenarioMonsterServices::CreateBoss(
    CSOLMonsterSpawnCall& call, std::int32_t recordId, csol::scenario::ConfigError& error)
{
    if (Current(call))
        error.message = "scenario boss behavior is not implemented for record " + std::to_string(recordId);
    return {};
}

std::span<const std::size_t> CCSOLScenarioMonsterServices::ProtectedSlots() const
{
    // Factory/ordinary callers already catch service exceptions. An unloaded
    // or unsupported Golden plan must not masquerade as an empty collection.
    if (!Pin() || !m_gameplay) throw std::runtime_error("scenario Golden membership is unavailable");
    return {};
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
    csol::scenario::ConfigError&)
{
    // Ver2 v328 notifies Golden. The actual loaded empty plan has no member
    // to update; Pin requires that specific service and its loaded state.
    return Current(call);
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
    excluded = active;
    return true;
}

bool CCSOLScenarioMonsterServices::UseSkill(CSOLMonsterSpawnCall& call,
    CSOLOrdinaryMonsterState& state, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto runtime = Pin();
    if (!runtime || !Current(call) || !gpGlobals) return false;
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
    // Skills 1/2 execute inside OrdinaryBehavior. Keep unsupported producers
    // explicit until their real entities and effects have been implemented.
    error.message = "scenario ordinary skill activation is not implemented for skill " +
        std::to_string(call.Current()->Type()->skill);
    return false;
}

bool CCSOLScenarioMonsterServices::ApplyContactImpulse(CSOLMonsterSpawnCall& call,
    const EHANDLE&, const Vector&, csol::scenario::ConfigError& error)
{
    if (Current(call)) error.message = "scenario contact impulse requires the player effect multiplier service";
    return false;
}

bool CCSOLScenarioMonsterServices::FrozenDeath(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    if (Pin() && call.BelongsTo(m_session->Map()) && call.Current())
        error.message = "scenario frozen death requires the precached ice/smoke effect service";
    return false;
}
