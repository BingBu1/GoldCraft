// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_active_skill.h"
#include "scenario_monster_damage.h"
#include "scenario_death_owner.h"
#include "../scenario_abilities.hpp"
#include "scenario_character_skills/catalog.hpp"
#include "scenario_character_skills/control.hpp"
#include "scenario_character_skills/host.hpp"
#include "scenario_character_skills/sdk_effects.hpp"
#include "scenario_character_skills/host_notifications.hpp"
#include "scenario_character_skills/area_damage.hpp"
#include "game_rules_gate.h"
#include "scenario_runtime.h"
#include <algorithm>
#include <cmath>

namespace
{
using namespace csol::scenario::skills;
class CharacterSkill final : public CSOLScenarioControlledSkill
{
public:
    CharacterSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
        const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
        const std::shared_ptr<CCSOLScenarioSession>& session,
        const csol::scenario::ClassModDefinition& definition, float now)
        : m_players(players), m_state(state), m_player(state->player),
          m_generation(state->generation), m_type(definition.subtype),
          m_session(session), m_traits(*FindSkill(definition.subtype)), m_definition(definition)
    {
        m_timers.cooldown = definition.values[0];
        m_timers.effects.duration = definition.values[1];
        m_timers.readyAt = now + m_timers.cooldown;
        m_effects = CreateSdkEffects(players, state, session, m_traits, this, m_damage);
    }

    bool Installed(csol::scenario::ConfigError& error) override
    {
        if (!Current()) return false;
        if (m_state.lock()->retiringCharacterSkillEffects) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        if (!m_host) m_host = CreateHost(m_players.lock(), m_state.lock(), m_session.lock(),
            m_traits, m_definition, this, m_timers, m_effects, error);
        const bool result = m_host && m_effects->Initialize(m_timers, error);
        return Current() && m_state.lock()->skillLifeSerial == life && Finish(result, error);
    }
    bool Rebuild(csol::scenario::ConfigError& error) override
    { return Current() && !m_state.lock()->retiringCharacterSkillEffects &&
        (m_disabled || (BindCurrent() && Finish(m_effects->Lifecycle30(m_timers, error), error))); }
    std::uint64_t RetirementBuffIdentity() const override
    {
        // Only the following-area skills revoke recipients at lifecycle38.
        // Timed grants (including type53 shields) survive their caster's exit.
        return (m_type == 24 || m_type == 25) && m_effects ? m_effects->OwnedBuffIdentity() : 0;
    }
    bool RetireEffects(csol::scenario::ConfigError& error) override
    {
        if (!Current() || !BindCurrent() || !m_effects) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        // Retirement must also run for a disabled capability. Finish() would
        // suppress its error and cannot substitute for releasing its targets.
        const bool result = m_effects->Lifecycle38(m_timers, error);
        return Current() && m_state.lock()->skillLifeSerial == life && result;
    }
    void CancelPendingEffects() override
    { m_timers.effects.pendingAt = -1; m_timers.effects.activeUntil = -1; m_nextGate = -1; }
    bool Disable(csol::scenario::ConfigError& error) override
    { return Finish(false, error); }
    bool TryActivate(std::shared_ptr<CCSOLScenarioRuntime> runtime, bool& activated,
        csol::scenario::ConfigError& error) override
    {
        activated = false;
        if (const auto state = m_state.lock(); state && state->retiringCharacterSkillEffects) return true;
        if (m_disabled) return SynchronizeGate(true, error);
        if (!Bind(std::move(runtime)) || !m_host) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        if (m_traits.automaticTrigger) return true;
        const auto result = csol::scenario::skills::Activate(m_traits, m_timers, *m_host, error);
        activated = result == ActivationResult::Started;
        if (!Current() || m_state.lock()->skillLifeSerial != life) return false;
        return Finish(result != ActivationResult::Failed && result != ActivationResult::Cancelled, error) &&
            SynchronizeGate(true, error);
    }
    bool Update(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        csol::scenario::ConfigError& error) override
    {
        if (const auto state = m_state.lock(); state && state->retiringCharacterSkillEffects) return true;
        if (m_disabled) return SynchronizeGate(false, error);
        if (!Bind(std::move(runtime)) || !m_host) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        bool result = csol::scenario::skills::Update(m_timers, *m_host, error);
        if (!Current() || m_state.lock()->skillLifeSerial != life) return false;
        if (result && m_type == 21) result = UpdateAutomaticElite(error);
        if (!Current() || m_state.lock()->skillLifeSerial != life) return false;
        return Finish(result, error) && SynchronizeGate(false, error);
    }
    bool IncomingDamage(std::shared_ptr<CCSOLScenarioRuntime> runtime, const EHANDLE& source,
        float& damage, csol::scenario::ConfigError& error) override
    {
        if (const auto state = m_state.lock(); state && state->retiringCharacterSkillEffects) return true;
        if (m_disabled) return true;
        if (!Bind(std::move(runtime)) || !m_effects) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        const bool result = m_effects->IncomingDamage(m_timers, source, damage, error);
        return Current() && m_state.lock()->skillLifeSerial == life && Finish(result, error);
    }
    bool BeforeProtectionDamage(std::shared_ptr<CCSOLScenarioRuntime> runtime, const EHANDLE& source,
        float& damage, csol::scenario::ConfigError& error) override
    {
        if (const auto state = m_state.lock(); state && state->retiringCharacterSkillEffects) return true;
        if (m_disabled) return true;
        if (!Bind(std::move(runtime)) || !m_effects) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        const bool result = m_effects->BeforeProtectionDamage(m_timers, source, damage, error);
        return Current() && m_state.lock()->skillLifeSerial == life && Finish(result, error);
    }

    int Type() const override { return m_type; }
    bool GateSnapshot(float now, csol::skillgate::Snapshot& output,
        csol::scenario::ConfigError& error) override
    {
        if (!Current() || !std::isfinite(now)) return false;
        if (m_disabled) { output = {}; return true; }
        if (!BindCurrent() || !m_host) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        bool ready{}, blocked{};
        if (!csol::scenario::skills::Ready(m_traits, m_timers, *m_host, ready, error, &blocked) ||
            !Current() || m_state.lock()->skillLifeSerial != life) return false;
        csol::skillgate::Snapshot candidate;
        candidate.subtype = static_cast<std::uint16_t>(m_type);
        if (!m_traits.automaticTrigger) candidate.flags |= csol::skillgate::Manual;
        // 106E4B30 retains pendingAt when action5 is cancelled; 106E1D10
        // still permits the next cast once its original cooldown expires.
        // A retained timestamp alone must not lock the client's activation key.
        bool preparing{};
        if (m_timers.effects.pendingAt >= 0 &&
            (!m_host->CharacterActionPlaying(preparing, error) || !Current() ||
                m_state.lock()->skillLifeSerial != life || !error.message.empty())) return false;
        if (preparing)
        {
            candidate.flags |= csol::skillgate::Preparing;
            candidate.prepareRemaining = std::max(0.0f, m_timers.effects.pendingAt - now);
        }
        if (m_timers.effects.activeUntil > now)
        {
            candidate.flags |= csol::skillgate::Active;
            candidate.activeRemaining = m_timers.effects.activeUntil - now;
        }
        if (blocked) candidate.flags |= csol::skillgate::Blocked;
        else if (ready && !preparing) candidate.flags |= csol::skillgate::Ready;
        output = candidate;
        return true;
    }
    bool HudSnapshot(float now, csol::abilities::Snapshot& output) const override
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        if (!players || !players->Active() || !state || state->generation != m_generation ||
            players->Find(m_player) != state || !std::isfinite(now) || m_timers.cooldown < 0) return false;
        output = {static_cast<unsigned>(m_type), m_timers.cooldown,
            std::clamp(m_timers.cooldown - (m_timers.readyAt - now), 0.0f, m_timers.cooldown), 0};
        if (m_disabled) output = {};
        return true;
    }
    bool Ready(CSOLMonsterDeathCall&, bool& ready, csol::scenario::ConfigError& error) override
    {
        ready = false;
        if (m_disabled) return Current();
        if (!BindCurrent() || !m_host) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        const bool result = csol::scenario::skills::Ready(m_traits, m_timers, *m_host, ready, error);
        return Current() && m_state.lock()->skillLifeSerial == life && Finish(result, error);
    }
    bool Activate(CSOLMonsterDeathCall&, csol::scenario::ConfigError& error) override
    {
        if (m_disabled) return Current();
        if (!BindCurrent() || !m_host) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        const auto result = csol::scenario::skills::Activate(m_traits, m_timers, *m_host, error);
        if (!Current() || m_state.lock()->skillLifeSerial != life) return false;
        return Finish(result != ActivationResult::Failed && result != ActivationResult::Cancelled, error);
    }
    bool ActiveForMonsterHit(CSOLMonsterDamageCall& call, bool& active,
        csol::scenario::ConfigError&) override
    {
        active = false;
        if (!Current(call)) return call.Cancel();
        if (!gpGlobals) return false;
        active = !m_disabled && m_timers.effects.activeUntil > gpGlobals->time;
        return true;
    }
    bool ActiveMonsterHit(CSOLMonsterDamageCall& call, csol::scenario::ConfigError& error) override
    {
        if (!Current(call)) return call.Cancel();
        if (m_disabled) return true;
        DamageAdapter lifetime(*this, call);
        const bool result = ApplyDragonBreathDamage(m_players.lock(), m_state.lock(), m_session.lock(),
            m_damage, call, error);
        return lifetime.Current() ? Finish(result, error) : call.Cancel();
    }
    bool AutoActivateOnBossDamage(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError&) override
    { enabled = !m_disabled && m_traits.automaticTrigger; return Current(call) || call.Cancel(); }
    bool ActivateOnBossDamage(CSOLMonsterDamageCall& call, csol::scenario::ConfigError& error) override
    {
        if (!Current(call)) return call.Cancel();
        if (m_disabled) return true;
        if (!BindCurrent() || !m_host) return false;
        DamageAdapter lifetime(*this, call);
        const auto result = csol::scenario::skills::Activate(m_traits, m_timers, *m_host, error);
        return lifetime.Current() ? Finish(result != ActivationResult::Failed &&
            result != ActivationResult::Cancelled, error) : call.Cancel();
    }
    bool BossDamaged(CSOLMonsterDamageCall& call, float amount,
        csol::scenario::ConfigError& error) override
    { return MonsterDamaged(call, amount, error); } // type29 v20 delegates v24.
    bool MonsterDamaged(CSOLMonsterDamageCall& call, float amount,
        csol::scenario::ConfigError& error) override
    {
        if (!Current(call)) return call.Cancel();
        if (m_disabled || m_traits.damageCallback == DamageCallback::Empty) return true;
        const auto players = m_players.lock();
        DamageAdapter host(*this, call);
        const bool result = m_traits.damageCallback == DamageCallback::Type28 ?
            ApplyLoveBombDamage(players, m_state.lock(), m_session.lock(), m_damage, host, call, amount, error) :
            ApplyDamageCallback(m_traits, m_damage, amount, call.context.bytes, host, error);
        return host.Current() ? Finish(result, error) : call.Cancel();
    }
    bool UsesDamageProgress(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError&) override
    { enabled = false; return Current(call) || call.Cancel(); } // v1C=105FFBE0.
    bool AdvanceDamageProgress(CSOLMonsterDamageCall&, csol::scenario::ConfigError& error) override
    { return Unsupported(error); }

private:
    bool SynchronizeGate(bool force, csol::scenario::ConfigError& error)
    {
        if (!Current() || !gpGlobals) return false;
        csol::skillgate::Snapshot gate;
        if (!GateSnapshot(gpGlobals->time, gate, error)) return false;
        const auto life = m_state.lock()->skillLifeSerial;
        // Publish state transitions immediately. Remaining durations only
        // need a bounded refresh; the client cannot turn expiry into Ready.
        if (!force && m_gateSent && gate.subtype == m_lastGate.subtype &&
            gate.flags == m_lastGate.flags && m_nextGate >= 0 &&
            (!(gate.flags & (csol::skillgate::Preparing | csol::skillgate::Active)) ||
                gpGlobals->time < m_nextGate)) return true;
        if (!PublishSkillGate(*m_session.lock(), m_player, gate, error) ||
            !Current() || m_state.lock()->skillLifeSerial != life) return false;
        m_lastGate = gate;
        m_gateSent = true;
        m_nextGate = gpGlobals->time + 0.25f;
        return true;
    }
    // A missing optional character effect disables this capability. It cannot
    // abort the monster's already-committed damage/death transaction.
    bool Finish(bool success, csol::scenario::ConfigError& error)
    {
        if (!Current()) return false;
        if (m_disabled) { error = {}; return true; }
        if (success && error.message.empty()) return true;
        m_disabled = true;
        CancelPendingEffects();
        const auto state = m_state.lock();
        state->activeSkillUnavailable = true;
        state->skillFailure = error.message.empty() ? "character effect provider failed" : error.message;
        error = {};
        if (auto* player = m_player.Get<CBasePlayer>()) CancelCharacterAction(*player, false);
        if (!Current()) return false;
        if (g_engfuncs.pfnAlertMessage)
            ALERT(at_console, "[CSOL] Character skill %d disabled: %s\n", m_type, state->skillFailure.c_str());
        return Current();
    }
    bool BindCurrent()
    {
        const auto session = m_session.lock();
        return session && Bind(CSOL_ScenarioRuntimeForAccess(session->Map().EntityAccess().get()));
    }
    class DamageAdapter final : public DamageHost
    {
    public:
        DamageAdapter(CharacterSkill& owner, CSOLMonsterDamageCall& call)
            : owner(owner), call(call), life(owner.m_state.lock()->skillLifeSerial) {}
        bool Current() const override
        { return owner.Current(call) && owner.m_state.lock()->skillLifeSerial == life; }
        bool Active(bool& value, csol::scenario::ConfigError&) override
        { if (!Current() || !gpGlobals) return false; value = owner.m_timers.effects.activeUntil > gpGlobals->time; return true; }
        bool Alive(bool& value, csol::scenario::ConfigError&) override
        { if (!Current()) return false; value = call.Player()->IsAlive(); return Current(); }
        bool SourceHasNonzeroOffset(bool& value, csol::scenario::ConfigError& error) override
        {
            value = false;
            if (!Current()) return false;
            auto* source = call.Inflictor();
            if (!source || !source->pev || !source->pev->pContainingEntity) return true;
            if (!g_engfuncs.pfnEntOffsetOfPEntity)
            { error.message = "character damage source offset API is unavailable"; return false; }
            value = OFFSET(source->pev->pContainingEntity) != 0;
            return Current();
        }
        bool SourceIsOwner(bool& value, csol::scenario::ConfigError&) override
        { if (!Current()) return false; value = call.Inflictor() == call.Player(); return true; }
        bool SourceWeapon(int& value, csol::scenario::ConfigError& error) override
        { return Current() && owner.m_players.lock()->ReadCharacterSkillSource(call, value, error); }
        bool StopCloaking(int key, csol::scenario::ConfigError& error) override
        { return Current() && owner.m_players.lock()->StopCharacterSkillCloaking(call, key, error); }
        bool InstalledBuff(int identity, bool& value, csol::scenario::ConfigError&) override
        {
            if (!Current()) return false;
            const auto state = owner.m_state.lock();
            value = std::any_of(state->buffs.begin(), state->buffs.end(),
                [identity](const auto& buff) { return identity >= 0 && buff.skillIdentity == static_cast<unsigned>(identity); });
            return true;
        }
        bool Parameter(DamageParameter parameter, float& value, csol::scenario::ConfigError&) override
        { if (!Current()) return false; value = DefaultDamageParameter(parameter); return true; }
        bool ReadHealth(float& value, float& maximum, csol::scenario::ConfigError&) override
        {
            if (!Current()) return false;
            value = call.Player()->pev->health;
            maximum = call.Player()->pev->max_health;
            return true;
        }
        bool WriteHealth(float value, csol::scenario::ConfigError&) override
        { if (!Current()) return false; call.Player()->pev->health = value; return true; }
        bool RecoverSelf(float amount, csol::scenario::ConfigError& error) override
        {
            if (!Current()) return false;
            const auto session = owner.m_session.lock();
            const auto runtime = CSOL_ScenarioRuntimeForAccess(session->Map().EntityAccess().get());
            if (!runtime) return call.Cancel();
            bool applied{};
            return owner.m_players.lock()->ApplyAttributeRecovery(call, runtime, amount, applied, error);
        }
    private:
        CharacterSkill& owner;
        CSOLMonsterDamageCall& call;
        const std::uint64_t life;
    };
    bool UpdateAutomaticElite(csol::scenario::ConfigError& error)
    {
        if (!Current() || !error.message.empty()) return false;
        if (m_updatingAutomatic) return true;
        if (!gpGlobals || !std::isfinite(gpGlobals->time))
        { error.message = "automatic elite skill clock is invalid"; return false; }
        //106E87A0 is the type21 v50 tail. It does not run while v10 is
        // active, and equality with the attempt deadline must still wait.
        if (m_timers.effects.activeUntil > gpGlobals->time ||
            !(gpGlobals->time > m_nextAutomatic)) return true;
        m_updatingAutomatic = true;
        struct Scope { bool& value; ~Scope() { value = false; } } scope{m_updatingAutomatic};
        const auto life = m_state.lock()->skillLifeSerial;
        const auto result = csol::scenario::skills::Activate(m_traits, m_timers, *m_host, error);
        if (!Current() || m_state.lock()->skillLifeSerial != life) return false;
        if (result == ActivationResult::Failed || result == ActivationResult::Cancelled) return false;
        // Read time after activation, including a normal readiness/target
        // refusal. This clock belongs to the object; Start does not reset it.
        if (!gpGlobals || !std::isfinite(gpGlobals->time + 1.0f))
        { error.message = "automatic elite skill deadline is invalid"; return false; }
        m_nextAutomatic = gpGlobals->time + 1.0f;
        return true;
    }

    bool Current() const
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        return players && players->Active() && state && state->generation == m_generation &&
            players->Find(m_player) == state && state->activeSkill.get() == this;
    }
    bool Bind(std::shared_ptr<CCSOLScenarioRuntime> runtime)
    {
        if (!Current() || !runtime || runtime->Closing() || runtime->players != m_players.lock() ||
            runtime->session != m_session.lock()) return false;
        m_effects->BindRuntime(std::move(runtime));
        return true;
    }
    bool Current(CSOLMonsterDamageCall& call) const
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        return Current() && players && players->Active() && state && !state->retiringCharacterSkillEffects && state->generation == m_generation &&
            players->Find(m_player) == state && call.Current() && call.Player() &&
            call.Player() == m_player.Get<CBasePlayer>();
    }
    static bool Unsupported(csol::scenario::ConfigError& error)
    { error.message = "scenario character skill activation effects are not implemented"; return false; }
    std::weak_ptr<CCSOLScenarioPlayerRuntime> m_players;
    std::weak_ptr<CSOLScenarioPlayerRuntimeState> m_state;
    EHANDLE m_player;
    std::uint64_t m_generation{};
    int m_type{};
    bool m_disabled{};
    std::weak_ptr<CCSOLScenarioSession> m_session;
    const SkillTraits& m_traits;
    csol::scenario::ClassModDefinition m_definition;
    ActivationState m_timers;
    DamageState m_damage;
    std::shared_ptr<SdkEffects> m_effects;
    std::shared_ptr<ActivationHost> m_host;
    csol::skillgate::Snapshot m_lastGate;
    float m_nextGate{-1};
    float m_nextAutomatic{};
    bool m_updatingAutomatic{};
    bool m_gateSent{};
};
}

std::shared_ptr<CSOLScenarioActiveSkill> CSOL_CreateScenarioActiveSkill(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const csol::scenario::ClassModDefinition& definition,
    float now, csol::scenario::ConfigError& error)
{
    if (definition.type != 10 || !FindSkill(definition.subtype) || !HasSdkEffects(definition.subtype)) return {};
    if (session && !SkillHostReady(*session)) return {};
    if (!players || !state || !session || !players->Active() || players->Find(state->player) != state ||
        !std::isfinite(now) || !std::isfinite(definition.values[0]) ||
        !std::isfinite(definition.values[1]) || !std::isfinite(now + definition.values[0]))
    { error.message = "scenario character skill initialization is invalid"; return {}; }
    return std::make_shared<CharacterSkill>(players, state, session, definition, now);
}
