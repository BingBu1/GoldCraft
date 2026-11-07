// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "sdk_effects.hpp"
#include "effects.hpp"
#include "area_expanded.hpp"
#include "smoke_zone.hpp"
#include "slow_wall.hpp"
#include "elite_manager.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_entity_lookup.h"
#include <cmath>

namespace csol::scenario::skills
{
namespace
{
std::uint64_t nextSkillIdentity = 1;

class Effects final : public SdkEffects
{
public:
    Effects(std::shared_ptr<CCSOLScenarioPlayerRuntime> players,
        std::shared_ptr<CSOLScenarioPlayerRuntimeState> state,
        std::shared_ptr<CCSOLScenarioSession> session, const SkillTraits& traits,
        const CSOLScenarioActiveSkill* owner, DamageState& damage)
        : m_players(players), m_state(state), m_session(session), m_traits(traits), m_owner(owner),
          m_life(state->skillLifeSerial), m_damage(damage) {}
    bool Current() const override
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        const auto session = m_session.lock();
        return players && players->Active() && state && session &&
            session->Map().EntityAccessAllowed() && players->Find(state->player) == state &&
            state->activeSkill.get() == m_owner && state->skillLifeSerial == m_life;
    }
    std::uint64_t OwnedBuffIdentity() const override { return m_identity; }
    void BindRuntime(std::shared_ptr<CCSOLScenarioRuntime> runtime) override
    { m_runtime = std::move(runtime); if (const auto state = m_state.lock()) m_life = state->skillLifeSerial; }
    bool Initialize(ActivationState&, ConfigError& error) override
    {
        if (!Current()) return false;
        //22/23's original Initialize is empty. Their local cloak handles
        // still need a host identity; this is not an invented MP initializer.
        if ((m_traits.subtype == 22 || m_traits.subtype == 23) && !m_identity)
        {
            if (!nextSkillIdentity) return Missing(error, "character skill identity exhausted");
            m_identity = nextSkillIdentity++;
        }
        if (m_traits.emptyInitialize3C) return true;
        if (m_traits.subtype == 14 || m_traits.subtype == 24 || m_traits.subtype == 25 ||
            m_traits.subtype == 26 || m_traits.subtype == 27 || m_traits.subtype == 29 ||
            m_traits.subtype == 30 || m_traits.subtype == 31 || m_traits.subtype == 37 || m_traits.subtype == 38 || m_traits.subtype == 39 || m_traits.subtype == 40 ||
            m_traits.subtype == 41 || m_traits.subtype == 42 || m_traits.subtype == 43 || m_traits.subtype == 44 || m_traits.subtype == 46 || m_traits.subtype == 49 ||
            m_traits.subtype == 50 || m_traits.subtype == 51 || m_traits.subtype == 52 ||
            m_traits.subtype == 53 || m_traits.subtype == 54 || m_traits.subtype == 55)
        {
            if (!m_identity)
            {
                if (!nextSkillIdentity) return Missing(error, "character skill identity exhausted");
                m_identity = nextSkillIdentity++;
            }
            return true;
        }
        return Missing(error, "character skill initialization has no concrete effect");
    }
    bool Lifecycle30(ActivationState&, ConfigError& error) override
    {
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::Rebuild, error);
        //22/23 v30 is106E3AA0: resolve/validate the caster, without
        //restarting the independent smoke entity or its cloak lifetime.
        if (m_traits.subtype == 22 || m_traits.subtype == 23) return Current();
        return Empty(m_traits.emptyLifecycle30, error);
    }
    bool Lifecycle34(ActivationState&, ConfigError& error) override
    {
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::RemoveVisual, error);
        if (m_traits.subtype == 36) return RemoveReflectionVisual(error);
        return Empty(m_traits.emptyLifecycle34, error);
    }
    bool Lifecycle38(ActivationState&, ConfigError& error) override
    {
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::Retire, error);
        if (m_traits.subtype == 36) return RemoveReflectionVisual(error);
        return Empty(m_traits.emptyLifecycle38, error);
    }
    bool Notify28(ActivationState&, ConfigError& error) override
    { return Empty(m_traits.emptyNotification28, error); }
    bool Notify2C(ActivationState&, ConfigError& error) override
    { return Empty(m_traits.emptyNotification2C, error); }
    bool BeforeProtectionDamage(ActivationState& timers, const EHANDLE& source,
        float& damage, ConfigError& error) override
    {
        if (!Current()) return false;
        if (m_traits.subtype != 51 && m_traits.subtype != 52)
            return Empty(m_traits.emptyNotification2C, error);
        if (!gpGlobals) return Missing(error, "character guard damage clock is unavailable");
        if (!(timers.effects.activeUntil > gpGlobals->time)) return true;
        auto* entity = source.Get<CBaseEntity>();
        const auto state = m_state.lock();
        if (!entity || !entity->pev || entity == state->player.Get<CBasePlayer>()) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
            return Missing(error, "character guard damage source API is unavailable");
        const int offset = OFFSET(entity->edict());
        if (!Current() || source.Get<CBaseEntity>() != entity) return false;
        if (!offset) return true;
        //106E3EC0 adds the pre-protection damage without a positive-only
        // filter. It is a different stage from49/50's v28 callback.
        const float accumulated = m_guardDamage + damage;
        if (!std::isfinite(accumulated)) return Missing(error, "character guard damage is not finite");
        m_guardDamage = accumulated;
        return true;
    }
    bool IncomingDamage(ActivationState& timers, const EHANDLE& source, float& damage, ConfigError& error) override
    {
        if (!Current()) return false;
        if (m_traits.subtype != 36 && m_traits.subtype != 49 && m_traits.subtype != 50)
            return Empty(m_traits.emptyNotification28, error);
        if (!gpGlobals) return Missing(error, "character incoming-damage clock is unavailable");
        if (!(timers.effects.activeUntil > gpGlobals->time)) return true;
        if (m_traits.subtype != 36 && (m_incomingUpgraded || m_upgradingIncoming)) return true;
        auto* entity = source.Get<CBaseEntity>();
        const auto state = m_state.lock();
        if (!entity || !entity->pev || entity == state->player.Get<CBasePlayer>()) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity) return Missing(error, "character incoming-damage source API is unavailable");
        const int offset = OFFSET(entity->edict());
        if (!Current() || source.Get<CBaseEntity>() != entity) return false;
        if (!offset) return true;
        if (m_traits.subtype == 36)
        {
            const float accumulated = m_reflection.accumulated + damage;
            if (!std::isfinite(accumulated)) return Missing(error, "character reflection damage is not finite");
            m_reflection.accumulated = accumulated;
            return true;
        }
        m_upgradingIncoming = true;
        struct Scope { bool& value; ~Scope() { value = false; } } scope{m_upgradingIncoming};
        if (!UpgradeIncomingSkill(m_traits.subtype, m_players.lock(), state, m_session.lock(),
            m_identity, error) || !Current()) return false;
        m_incomingUpgraded = true;
        return true;
    }
    bool Prepare(ActivationState& state, bool& prepared, ConfigError& error) override
    {
        prepared = false;
        if (!Current()) return false;
        if (m_traits.subtype == 24 || m_traits.subtype == 25 || m_traits.subtype == 36)
        {
            state.delay = .5f;
            state.actionConstraint = m_traits.subtype == 36 ? state.delay + .1f : 1.53f;
            prepared = PlaySkillVoice(*m_session.lock(), m_state.lock()->player, m_traits.subtype, error);
            return prepared && Current();
        }
        if (m_traits.subtype == 10 || m_traits.subtype == 11 || m_traits.subtype == 13)
            return PrepareOtherSkillEffects(m_traits.subtype, m_players.lock(), m_state.lock(),
                m_session.lock(), state, prepared, error) && Current();
        if (m_traits.subtype == 21)
        {
            const auto runtime = m_runtime.lock();
            if (!runtime || !runtime->eliteManager)
                return Missing(error, "elite skill manager is unavailable");
            return PrepareEliteSkill21(m_players.lock(), m_state.lock(), m_session.lock(),
                runtime->eliteManager, state, prepared, error) && Current();
        }
        // Type54's original preparation leaves both constructor timers
        // unchanged: its effects start without an artificial half-second delay.
        if (m_traits.subtype == 54)
        {
            prepared = PrepareSkillVisual(m_session.lock(), m_state.lock()->player, 54, error);
            return prepared && Current();
        }
        if (m_traits.subtype == 22 || m_traits.subtype == 23)
        {
            state.delay = 1; //106F3240 leaves the original constraint3 intact.
            prepared = PrepareSkillVisual(m_session.lock(), m_state.lock()->player, m_traits.subtype, error);
            return prepared && Current();
        }
        state.delay = 0.5f; // 106FFE70 is the verified fldz extension.
        if (m_traits.subtype == 14 || m_traits.subtype == 20) state.actionConstraint = 0.5f;
        else if (m_traits.subtype == 26 || m_traits.subtype == 27)
            state.actionConstraint = static_cast<float>(static_cast<double>(state.delay) + .1);
        else if (m_traits.subtype == 28 || m_traits.subtype == 29 || m_traits.subtype == 30 || m_traits.subtype == 31 ||
            m_traits.subtype == 32 || m_traits.subtype == 33 || m_traits.subtype == 37 ||
            m_traits.subtype == 38 || m_traits.subtype == 39 || m_traits.subtype == 40 || m_traits.subtype == 41 || m_traits.subtype == 42 ||
            m_traits.subtype == 43 || m_traits.subtype == 44 || m_traits.subtype == 45 || m_traits.subtype == 46 ||
            m_traits.subtype == 47 || m_traits.subtype == 48 || m_traits.subtype == 49 || m_traits.subtype == 50 ||
            m_traits.subtype == 51 || m_traits.subtype == 52 || m_traits.subtype == 53 || m_traits.subtype == 55)
            state.actionConstraint = state.delay + 0.1f;
        else if (m_traits.subtype != 19 && m_traits.subtype != 34)
            state.actionConstraint = 3;
        const auto player = m_state.lock()->player;
        const auto session = m_session.lock();
        if (!PrepareSkillVisual(session, player, m_traits.subtype, error) || !Current()) return false;
        if (m_traits.subtype == 46) m_damage.accumulated46 = 0; //106F4A00 resets after its sound.
        prepared = true;
        return true;
    }
    bool Start(ActivationState& timers, ConfigError& error) override
    {
        if (!Current()) return false;
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        const auto session = m_session.lock();
        //21 performs its execution in Prepare. Its v44 is10611B70
        //(ret0); the deferred Start must not disable a successful cast.
        if (m_traits.subtype == 21) return true;
        if (m_traits.subtype == 14 || m_traits.subtype == 26 || m_traits.subtype == 27 || m_traits.subtype == 30)
            return StartLegacyShieldSkill(m_traits.subtype, players, state, session, timers,
                m_identity, m_shieldAttack, error) && Current();
        if (m_traits.subtype == 17)
            return StartLegacyShieldSkill(17, players, state, session, timers,
                0, m_shieldAttack, error) && Current() &&
                StartSlowWall17(players, state, session, timers, error) && Current();
        if (m_traits.subtype == 32 || m_traits.subtype == 33)
            return StartGroupSkill(m_traits.subtype, players, state, session, timers, error) && Current();
        if (m_traits.subtype == 37)
            return StartDrainArea37(players, state, session, error) && Current() &&
                StartDrainSupport37(players, state, session, timers, m_identity, error) && Current();
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::Start, error);
        if (m_traits.subtype == 36)
            return StartReflectionSkill36(players, state, session, timers, m_reflection, error) && Current();
        if (m_traits.subtype == 18)
            return StartCrisisSkill18(players, state, session, timers, error) && Current();
        if (m_traits.subtype == 22 || m_traits.subtype == 23)
            return StartSmokeCloakSkill(m_traits.subtype, players, state, session, m_identity, error) && Current();
        if (m_traits.subtype == 55)
            return StartLotusSkill(players, state, session, timers, m_lotus, m_identity, error) && Current();
        if (m_traits.subtype == 51 || m_traits.subtype == 52)
            return StartGuardSkill(m_traits.subtype, players, state, session, timers,
                m_identity, m_guardDamage, error) && Current();
        if (m_traits.subtype == 53)
            return StartDragonShield53(players, state, session, m_identity, error) && Current();
        if (HasOtherSkillEffects(m_traits.subtype) || m_traits.subtype == 46)
            return StartOtherSkillEffects(m_traits.subtype, players, state, session, timers, m_damage,
                error, m_identity, &m_incomingUpgraded) && Current();
        if (m_traits.subtype == 29 || m_traits.subtype == 39)
            return players->StartCloakSkill(state->player, m_traits.subtype == 29 ? 12 : 13, m_identity, error) && Current();
        if (m_traits.subtype != 1 && m_traits.subtype != 5)
            return Missing(error, "character skill area effect is unavailable");
        if (!gpGlobals || !g_engfuncs.pfnEntOffsetOfPEntity)
            return Missing(error, "character skill player enumeration is unavailable");
        const float duration = timers.effects.duration;
        for (int index = 1; index <= gpGlobals->maxClients; ++index)
        {
            auto* entity = CSOL_ScenarioPlayerEntity(index, session->Map().EntityAccess());
            if (!Current()) return false;
            auto* target = dynamic_cast<CBasePlayer*>(entity);
            if (!target || !target->pev || target->has_disconnected ||
                (target->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
            const EHANDLE handle(target);
            const auto targetState = players->Find(handle);
            const auto targetLife = targetState ? targetState->skillLifeSerial : 0;
            const auto targetCurrent = [&]
            { return Current() && handle.Get<CBasePlayer>() == target && players->Find(handle) == targetState &&
                (!targetState || targetState->skillLifeSerial == targetLife); };
            const int offset = OFFSET(target->edict());
            if (!targetCurrent()) return false;
            if (!offset) continue;
            const bool alive = target->IsAlive();
            if (!targetCurrent()) return false;
            if (!alive) continue;
            if (!targetState) return Missing(error, "character skill target is not prepared");
            const auto delta = target->pev->origin - state->player.Get<CBasePlayer>()->pev->origin;
            const float distance = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
            if (m_traits.subtype == 1 ? !(distance < 40000) : distance > 40000) continue;
            if (m_traits.subtype == 1)
            {
                if (!players->InstallSkillHealing(handle, duration, 1, 0.05f, error) || !targetCurrent()) return false;
                if (!PublishHealingDuration(*session, handle, duration, error) || !targetCurrent()) return false;
                if (!players->InstallSkillLabel(handle, 13, 0, duration))
                    return Missing(error, "character healing label could not be installed");
            }
            else
            {
                CSOLScenarioStatBuff buff;
                buff.group = 3;
                buff.source = 10;
                buff.subsource = 5;
                buff.expires = duration < 0 ? -1 : gpGlobals->time + duration;
                buff.infiniteAmmo = true;
                buff.extraClip = 2;
                const auto token = players->InstallStatBuff(handle, std::move(buff));
                if (!targetCurrent()) return false;
                if (!token) return Missing(error, "character ammo stat buff could not be installed");
                if (!players->InstallSkillLabel(handle, 15, 0, duration, token))
                    return Missing(error, "character ammo label could not be installed");
                if (!players->RefreshSkillAmmo(handle, true, false, error) || !targetCurrent()) return false;
                if (!PublishAmmoDuration(*session, handle, duration, error) || !targetCurrent()) return false;
            }
        }
        return Current();
    }
    bool Active(ActivationState&, ConfigError& error) override
    {
        if (m_traits.subtype == 32 || m_traits.subtype == 33)
        {
            if (!Current() || !gpGlobals) return false;
            // Both constructors initialize this deadline once; Start leaves
            // it intact. A delayed frame performs one pulse, without catch-up.
            if (m_groupNextPulse > gpGlobals->time) return true;
            m_groupNextPulse = gpGlobals->time + .8f;
            return ActiveDrainAreaSkill32_33(m_traits.subtype, m_players.lock(),
                m_state.lock(), m_session.lock(), error) && Current();
        }
        if (m_traits.subtype == 30)
            return Current() && ActiveShieldAttack30(m_players.lock(), m_state.lock(),
                m_session.lock(), m_identity, m_shieldAttack, error) && Current();
        if (m_traits.subtype == 36)
            return Current() && ActiveReflectionSkill36(m_players.lock(), m_state.lock(), m_session.lock(),
                m_reflection, error) && Current();
        if (m_traits.subtype == 55)
            return Current() && ActiveLotusSkill(m_players.lock(), m_state.lock(), m_session.lock(), m_lotus, error) && Current();
        return Empty(m_traits.emptyActive48, error);
    }
    bool End(ActivationState&, ConfigError& error) override
    {
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::End, error);
        if (m_traits.subtype == 36) return RemoveReflectionVisual(error);
        if (m_traits.subtype == 46)
        {
            if (!Current()) return false;
            const auto players = m_players.lock();
            const auto state = m_state.lock();
            std::vector<std::uint64_t> tokens;
            for (const auto& buff : state->buffs)
                if (buff.skillIdentity == m_identity) tokens.push_back(buff.token);
            for (const auto token : tokens)
                if (!players->RemoveStatBuff(state->player, token) || !Current()) return false;
            if (!EndExpandedAreaSkill46(players, state, m_session.lock(),
                m_damage.accumulated46, error) || !Current()) return false;
            return PublishStoredAttackSkillEffect(*m_session.lock(), state->player, error) && Current();
        }
        if (m_traits.subtype == 51)
            return Current() && EndGuardBurst51(m_players.lock(), m_state.lock(),
                m_session.lock(), m_guardDamage, error) && Current() &&
                PublishDragonShieldRetirementEffect(*m_session.lock(), m_state.lock()->player, error) && Current();
        if (m_traits.subtype == 52)
            return Current() && EndGuardRecovery52(m_players.lock(), m_state.lock(),
                m_session.lock(), m_guardDamage, error) && Current();
        // Original106E3AA0 only validates the caster. The shield has its own
        // seven-second lifetime and retires through feature21's callback.
        if (m_traits.subtype == 53) return Current();
        if (m_traits.subtype == 38)
            return Current() && EndRallySkill38(m_players.lock(), m_state.lock(), m_session.lock(), error) && Current();
        if (m_traits.subtype == 45)
            return Current() && EndShieldSkill45(m_players.lock(), m_state.lock(), m_session.lock(), error) && Current();
        if (m_traits.subtype == 43 || m_traits.subtype == 44)
            return Current() && EndEscortSkill(m_players.lock(), m_state.lock(), m_session.lock(), error) && Current();
        return Empty(m_traits.emptyEnd4C, error);
    }
    bool Update(ActivationState&, ConfigError& error) override
    {
        //21's automatic Activate belongs to CharacterSkill::Update, where
        // the real ActivationHost is available. This is only its SDK tail.
        if (m_traits.subtype == 21) return Current();
        if (m_traits.subtype == 24 || m_traits.subtype == 25) return Aura(AuraStage::Update, error);
        return Empty(m_traits.emptyUpdate50, error);
    }
    bool Voice(ConfigError& error) override
    {
        if (!Current()) return false;
        return PlayActivationRadio(*m_session.lock(), m_state.lock()->player, error) && Current();
    }
    bool FrozenWrathBlocks(bool& blocked, ConfigError& error) override
    {
        blocked = false;
        if (!Current()) return false;
        bool mapFeature{};
        if (!SkillMapHasFrozenWrath(*m_session.lock(), mapFeature, error) || !Current()) return false;
        if (!mapFeature) return true;
        return Missing(error, "FrozenWrath player state is not installed");
    }
    bool TargetAvailable(bool& available, ConfigError& error) override
    {
        available = false;
        if (!Current()) return false;
        if (m_traits.subtype != 21)
            return Missing(error, "character skill requires its scenario target provider");
        const auto runtime = m_runtime.lock();
        if (!runtime || !runtime->eliteManager || !runtime->eliteManager->Current())
            return Missing(error, "elite skill manager is unavailable");
        available = runtime->eliteManager->Primary() != nullptr;
        return Current();
    }
    bool CooldownReduction(float& reduction, ConfigError& error) override
    {
        if (!Current()) return false;
        if (!gpGlobals) return Missing(error, "character skill cooldown clock is unavailable");
        reduction = m_players.lock()->SkillCooldownReduction(m_state.lock()->player, gpGlobals->time);
        return true;
    }
    bool UiDelay(float& delay, ConfigError&) override
    { if (!Current()) return false; delay = ActivationUiDelay(); return true; }
private:
    bool RemoveReflectionVisual(ConfigError& error)
    {
        return Current() && PublishReflectionSkillEffect(*m_session.lock(), m_state.lock()->player,
            m_reflection.effectId, false, error) && Current();
    }
    bool Aura(AuraStage stage, ConfigError& error)
    {
        return Current() && RunAuraSkill(m_traits.subtype, stage, m_players.lock(), m_state.lock(),
            m_session.lock(), m_aura, m_identity, error) && Current();
    }
    static bool Missing(ConfigError& error, const char* text) { error.message = text; return false; }
    bool Empty(bool originalEmpty, ConfigError& error)
    { return Current() && (originalEmpty || Missing(error, "character skill nonempty callback is unavailable")); }
    std::weak_ptr<CCSOLScenarioPlayerRuntime> m_players;
    std::weak_ptr<CSOLScenarioPlayerRuntimeState> m_state;
    std::weak_ptr<CCSOLScenarioSession> m_session;
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    const SkillTraits& m_traits;
    const CSOLScenarioActiveSkill* m_owner{};
    std::uint64_t m_life{};
    std::uint64_t m_identity{};
    bool m_incomingUpgraded{}, m_upgradingIncoming{};
    DamageState& m_damage;
    float m_guardDamage{};
    float m_groupNextPulse{};
    LotusState m_lotus;
    AuraState m_aura;
    ReflectionState m_reflection;
    ShieldAttackState m_shieldAttack;
};
}

bool HasSdkEffects(int subtype) noexcept
{
    return subtype == 1 || subtype == 5 || subtype == 14 || subtype == 17 || subtype == 18 ||
        subtype == 22 || subtype == 23 || subtype == 26 || subtype == 27 ||
        subtype == 29 || subtype == 30 || subtype == 32 || subtype == 33 ||
        subtype == 39 || subtype == 55 ||
        HasOtherSkillEffects(subtype);
}

std::shared_ptr<SdkEffects> CreateSdkEffects(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const SkillTraits& traits, const CSOLScenarioActiveSkill* owner, DamageState& damage)
{
    if (!HasSdkEffects(traits.subtype)) return {};
    return std::make_shared<Effects>(players, state, session, traits, owner, damage);
}
}
