// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../monsters/scenario_monster_ordinary.h"
#include "scenario_monster_boss_control.h"
#include "scenario_oberon_knife_effect.h"

class CCSOLScenarioRuntime;
class CCSOLScenarioMonsterServices;

// Owned by the map's monster service. Behaviors/control retain this weakly,
// so pool -> behavior never forms a cycle through the session's pool.
class CCSOLOberonServices final : public CCSOLBossControlServices
{
public:
    CCSOLOberonServices(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        std::shared_ptr<CCSOLScenarioMonsterServices> ordinary);
    bool Initialize(csol::scenario::ConfigError& error);
    bool OwnerCurrent() const override;
    bool RulesSkillStarted(CSOLMonsterSpawnCall&, csol::scenario::ConfigError&) override;
    const CCSOLScenarioMessages& BossMessages() const override;
    std::shared_ptr<CCSOLScenarioRuntime> Runtime() const;
    std::shared_ptr<CCSOLScenarioMonsterServices> Ordinary() const;
    std::shared_ptr<CCSOLOberonKnifeEffects> Knives() const { return m_knives; }
private:
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::weak_ptr<CCSOLScenarioMonsterServices> m_ordinary;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<CCSOLOberonKnifeEffects> m_knives;
};

class CCSOLOberonBehavior final : public CCSOLMonsterSpawnBehavior, public CCSOLMonsterDamageBehavior
{
public:
    CCSOLOberonBehavior(std::shared_ptr<CCSOLOberonServices> services,
        std::shared_ptr<CCSOLOrdinaryMonsterDamage> damage,
        std::shared_ptr<csol::scenario::BossSkills> skills);
    bool RecreateForSpawn() const override { return true; }
    bool ResetForSpawn(CSOLMonsterSpawnCall&, CSOLMonsterSpawnState&, csol::scenario::ConfigError&) override;
    void Think(CCSOLScenarioMonster&) override;
    std::string_view Failure() const noexcept override
    { return m_state ? std::string_view(m_state->error.message) : std::string_view{}; }
    BOOL TakeDamage(CCSOLScenarioMonster&, entvars_t*, entvars_t*, float, int) override;
    BOOL TakeDamageWithContext(CCSOLScenarioMonster&, entvars_t*, entvars_t*, float, int,
        const CSOLMonsterDeathContext&) override;
    void Killed(CCSOLScenarioMonster&, entvars_t*, int) override;
    CSOLMonsterDeathParameters DeathParameters() const override;
    bool BeforeDeath(CSOLMonsterDeathCall&, csol::scenario::ConfigError&) override;
    bool SelectDeathSequence(CSOLMonsterDeathCall&, int&, csol::scenario::ConfigError&) override;
    bool AdjustDeathDuration(CSOLMonsterDeathCall&, float&, csol::scenario::ConfigError&) override;
    bool PostDeath(CSOLMonsterDeathCall&, csol::scenario::ConfigError&) override;
    bool ResolveHitDamage(CSOLMonsterDamageCall&, int, csol::scenario::ConfigError&) override;
    bool DamageFlinch(CSOLMonsterSpawnCall&, csol::scenario::ConfigError&) override;
    // Original host1322 producers remain separate from ordinary damage.
    bool RequestInterrupt(CSOLMonsterSpawnCall&, std::int16_t cause);
private:
    struct State
    {
        std::shared_ptr<CSOLBossControlState> control;
        std::shared_ptr<const CCSOLMonsterNavigation> navigation;
        EHANDLE distantTarget;
        Vector previousOrigin{};
        float jitter{};
        std::int16_t interrupt{};
        bool thinking{};
        csol::scenario::ConfigError error;
    };
    using StatePtr = std::shared_ptr<State>;
    CCSOLScenarioMonster* Current(CSOLMonsterSpawnCall&, const StatePtr&) const;
    bool Step(CSOLMonsterSpawnCall&, const StatePtr&, bool, const char*);
    bool ValidateTarget(CSOLMonsterSpawnCall&, const StatePtr&, CCSOLOrdinaryMonsterServices&);
    bool SelectTarget(CSOLMonsterSpawnCall&, const StatePtr&, CCSOLOrdinaryMonsterServices&);
    bool MoveTo(CSOLMonsterSpawnCall&, const StatePtr&, const Vector&);
    bool Stop(CSOLMonsterSpawnCall&, const StatePtr&);
    bool FindPath(CSOLMonsterSpawnCall&, const StatePtr&);
    bool Effects(CSOLMonsterSpawnCall&, const StatePtr&, const std::shared_ptr<CCSOLScenarioRuntime>&);
    bool Landing(CSOLMonsterSpawnCall&, const StatePtr&, const std::shared_ptr<CCSOLScenarioRuntime>&);
    bool Debris(CSOLMonsterSpawnCall&, const StatePtr&, bool landing);
    bool UpdateBehavior(CSOLMonsterSpawnCall&, const StatePtr&, CCSOLOrdinaryMonsterServices&);
    bool UpdateStatusTick(CSOLMonsterSpawnCall&, bool&);
    bool UpdateAmbientAndEffects(CSOLMonsterSpawnCall&, const StatePtr&, CCSOLOrdinaryMonsterServices&);
    bool UpdatePosition(CSOLMonsterSpawnCall&, const StatePtr&, CCSOLOrdinaryMonsterServices&);
    std::weak_ptr<CCSOLOberonServices> m_services;
    std::shared_ptr<CCSOLOrdinaryMonsterDamage> m_damage;
    std::shared_ptr<csol::scenario::BossSkills> m_skills;
    std::shared_ptr<CCSOLMonsterBossControl> m_control;
    StatePtr m_state;
};
