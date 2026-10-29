// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_ordinary.h"
#include "scenario_monster_damage.h"

class CCSOLScenarioRuntime;
class CCSOLScenarioSession;
class CCSOLScenarioGameplay;
class CCSOLScenarioDeathServices;

// The ordinary behavior holds this provider weakly. A pinned provider retains
// the session, death service and gameplay managers until the entry unwinds;
// none of the reference accessors depends on a temporary Runtime lock.
class CCSOLScenarioMonsterServices final : public CCSOLMonsterFactoryServices,
    public CCSOLOrdinaryMonsterServices, public CCSOLSpawnClearDeaths,
    public std::enable_shared_from_this<CCSOLScenarioMonsterServices>
{
public:
    CCSOLScenarioMonsterServices(std::weak_ptr<CCSOLScenarioRuntime> runtime,
        std::shared_ptr<CCSOLScenarioSession> session,
        std::shared_ptr<CCSOLScenarioDeathServices> deaths);
    // World precache has already loaded the actual map ID and combat assets.
    // The caller must supply the real damage consumers, never a default null
    // damage owner. Player/equipment/reward registries resolve later, at spawn.
    bool Assemble(std::shared_ptr<CCSOLMonsterDamageServices> damage,
        csol::scenario::ConfigError& error);

    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateOrdinary(CSOLMonsterSpawnCall& call,
        csol::scenario::ConfigError& error) override;
    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateBoss(CSOLMonsterSpawnCall& call,
        std::int32_t recordId, csol::scenario::ConfigError& error) override;
    std::span<const std::size_t> ProtectedSlots() const override;
    bool ClearSpawnEffects(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) override;
    bool ResolveSpawnOverlap(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) override;
    bool RulesMonsterSpawned(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) override;
    bool BeforeSpawnClear(CCSOLScenarioMonster& monster) override;
    bool RequestSpawnClearDeath(CCSOLScenarioMonster& monster) override;

    bool OwnerCurrent() const override;
    CCSOLScenarioMap& Map() override;
    CCSOLMonsterPool& Pool() override;
    CCSOLMonsterDeathRequests& Deaths() override;
    CCSOLScenarioOccupancy& Occupancy() override;
    const CCSOLScenarioMessages& Messages() const override;
    std::shared_ptr<const CCSOLMonsterNavigation> PinNavigation() override;
    const bool& DamageSuppressed() const override;
    bool LogicStopped() const override;
    bool PlayerTargetExcluded(CSOLMonsterSpawnCall& call, const EHANDLE& player,
        bool& excluded, csol::scenario::ConfigError& error) override;
    bool UseSkill(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
        csol::scenario::ConfigError& error) override;
    bool ApplyContactImpulse(CSOLMonsterSpawnCall& call, const EHANDLE& target,
        const Vector& requested, csol::scenario::ConfigError& error) override;
    bool FrozenDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;

private:
    std::shared_ptr<CCSOLScenarioRuntime> Pin() const;
    bool Current(CSOLMonsterSpawnCall& call) const;
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<CCSOLScenarioDeathServices> m_deaths;
    std::shared_ptr<CCSOLScenarioGameplay> m_gameplay;
    std::shared_ptr<CCSOLMonsterDamageServices> m_damage;
    // Safe reference returned only after owner retirement; live state comes
    // directly from the registered rule's m_bRoundTerminating.
    const bool m_retiredDamageSuppressed{true};
};
