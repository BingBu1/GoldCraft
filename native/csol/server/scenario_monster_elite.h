// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_ordinary.h"
#include "../scenario/elite.hpp"
#include <functional>

class CCSOLScenarioRuntime;
class CCSOLScenarioSession;

enum class CSOLEliteEntityVisual
{
    FireballTrail, FireballExplosion, FirewallSegment,
    FirewallPositiveSegment, FirewallNegativeSegment
};
using CSOLEliteVisualPublisher = std::function<bool(CCSOLScenarioSession&, const EHANDLE&,
    CSOLEliteEntityVisual, const Vector& offset, float duration, float radius,
    csol::scenario::ConfigError&)>;

// Factory/player authorities, not values inferred from total player count or
// mutable shared CSV rows. Each callback is followed by a lease/owner check.
struct CSOLEliteMonsterBindings
{
    std::function<bool(CSOLMonsterSpawnCall&, int&, csol::scenario::ConfigError&)> qualifiedPlayers;
    std::function<bool(CSOLMonsterSpawnCall&, CSOLMonsterSpawnState&,
        std::shared_ptr<const csol::scenario::MonsterType>, csol::scenario::ConfigError&)> replaceType;
    // Original 11384CF0 resolves the current scenario spawn group. A clone
    // must not inherit an arbitrary individual point from its primary.
    std::function<bool(CSOLMonsterSpawnCall&, csol::scenario::ConfigError&)> spawnFollower;
    // Original CMonster v50, including its context and recovery consumers.
    std::function<bool(CSOLMonsterSpawnCall&, float, const CSOLMonsterDeathContext&,
        csol::scenario::ConfigError&)> recoverHealth;
    // Original player v1D0 status53: duration, captured absolute speed,
    // value-9999, enabled1, source-1. This is not an additive StatBuff.
    std::function<bool(CSOLMonsterSpawnCall&, const EHANDLE&, float duration,
        float speed, csol::scenario::ConfigError&)> frostSlow;
    // Original20/4 fireball trail; TE explosion; 11493000 fire-wall sprites.
    // An absent publisher rejects creation, never creates an invisible hazard.
    CSOLEliteVisualPublisher publishEntityEffect;
    // Original D0 tail's scenario target/navigation gates. Until its actual
    // forced-repath flag and room target selector are connected, reject it.
    std::function<bool(CSOLMonsterSpawnCall&, CSOLOrdinaryMonsterState&,
        float& changedAt, int& previousArea, bool& relocate,
        csol::scenario::ConfigError&)> updateRecovery;
    // 115302F0 model FX carries different arguments for frost and pull.
    std::function<bool(CSOLMonsterSpawnCall&, int ability,
        csol::scenario::ConfigError&)> publishAreaEffect;
};

// MP10965760 actually derives from CBaseZombieImprov. Keep its common motion,
// navigation and damage owner, and override the distinct elite stages.
class CCSOLEliteMonsterBehavior final : public CCSOLOrdinaryMonsterBehavior
{
public:
    CCSOLEliteMonsterBehavior(std::shared_ptr<CCSOLOrdinaryMonsterServices> services,
        std::shared_ptr<CCSOLOrdinaryMonsterDamage> damage,
        std::weak_ptr<CCSOLScenarioRuntime> runtime,
        std::shared_ptr<const csol::scenario::EliteSettings> settings,
        CSOLEliteMonsterBindings bindings);
    // MP 11803454+114 -> 106129F0 returns true. Unlike the ordinary base,
    // an elite behavior must be recreated even after round reset clears Type.
    bool RecreateForSpawn() const override { return true; }
    bool ResetForSpawn(CSOLMonsterSpawnCall&, CSOLMonsterSpawnState&,
        csol::scenario::ConfigError&) override;
    bool BeforeDeath(CSOLMonsterDeathCall&, csol::scenario::ConfigError&) override;
    bool ResolvePreArmorDamage(CSOLMonsterDamageCall&, csol::scenario::ConfigError&) override;
    bool Follower() const;
    bool AbilityActive(int ability, float now) const;
    float WebSpeedDown() const;

protected:
    bool AcceptsSpawnType(const csol::scenario::MonsterType& type) const override
    { return type.id == 32800 && type.skill == 1000 && type.propensity == 0; }
    bool SkillStopsTree() const override { return false; }
    bool UpdateBehavior(CSOLMonsterSpawnCall&, const StatePtr&,
        CCSOLOrdinaryMonsterServices&) override;
    bool UseSkill(CSOLMonsterSpawnCall&, const StatePtr&,
        CCSOLOrdinaryMonsterServices&) override;

private:
    struct EliteState
    {
        StatePtr ordinary;
        csol::scenario::EliteAbilities abilities;
        std::shared_ptr<const csol::scenario::MonsterType> initializedType;
        struct Label { EHANDLE player; std::uint64_t generation{}, life{}; std::int32_t label{}; };
        std::vector<Label> webLabels;
        float lastDamage{}, lastRecovery{}, cloneAfter{}, healthRatio{1}, targetChangedAt{};
        int remainingClones{}, lastTarget{};
        bool follower{}, doubleSide{};
    };
    using EliteStatePtr = std::shared_ptr<EliteState>;
    std::shared_ptr<CCSOLScenarioRuntime> Pin(CSOLMonsterSpawnCall&, const EliteStatePtr&) const;
    bool InstallWeb(CSOLMonsterSpawnCall&, const EliteStatePtr&,
        const std::shared_ptr<CCSOLScenarioRuntime>&, csol::scenario::ConfigError&);
    bool ActivateAbility(CSOLMonsterSpawnCall&, const EliteStatePtr&,
        const std::shared_ptr<CCSOLScenarioRuntime>&, int ability,
        float cooldown, CCSOLOrdinaryMonsterServices&, csol::scenario::ConfigError&);
    bool AreaAbility(CSOLMonsterSpawnCall&, const EliteStatePtr&,
        const std::shared_ptr<CCSOLScenarioRuntime>&, int ability,
        CCSOLOrdinaryMonsterServices&, csol::scenario::ConfigError&);
    bool CreateAbilityEntity(CSOLMonsterSpawnCall&, const EliteStatePtr&,
        const std::shared_ptr<CCSOLScenarioRuntime>&, int ability,
        float damage, csol::scenario::ConfigError&);
    bool Teleport(CSOLMonsterSpawnCall&, const EliteStatePtr&,
        csol::scenario::ConfigError&);
    float Parameter(int ability, const char* name) const;
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<const csol::scenario::EliteSettings> m_settings;
    CSOLEliteMonsterBindings m_bindings;
    EliteStatePtr m_elite;
};
