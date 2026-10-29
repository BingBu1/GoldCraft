// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_melee.h"
#include "scenario_monster_position.h"
#include "scenario_navigation.h"

struct CSOLOrdinaryMonsterState
{
    CSOLMonsterPositionState position;
    CSOLMonsterMeleeClocks melee;
    CSOLMonsterInterval findTarget, findPath, skill, secondarySkill, skillPhase, skillRecovery;
    // Original +1DE0/+1DE4/+1DE8. Ordinary reset preserves this timer;
    // it is distinct from the host's separate enhancedUntil (+12D0).
    CSOLMonsterInterval skillActive;
    CSOLMonsterInterval knownGoodClock;
    CSOLMonsterInterval obstacleRecovery;
    EHANDLE target, distantTarget;
    Vector previousOrigin{}, knownGood{}, previousKnownGood{}, specialGoal{};
    Vector lastObstacleOrigin{};
    float deltaX{}, deltaY{}, motionRemainder{}, jitter{};
    float updateStamp{-1.0f}, secondaryUpdateAfter{-1.0f}, secondaryUpdateStamp{-1.0f};
    float secondaryUpdateInterval{}, skillCollisionDamage{};
    float lastActionAt{}, solidAfter{};
    int actionState{};
    bool knownGoodValid{}, previousKnownGoodValid{}, specialTarget{}, obstacleBlocked{};
    bool canSkill{}, canAttack{}, thinking{};
};

// These are actual rule/skill/effect integration points, not default-success
// substitutes. A session owns this provider and all the managers it returns.
// The behavior holds this provider weakly and pins it for each full entry.
// The provider must own the returned managers until that entry unwinds.
class CCSOLOrdinaryMonsterDamage;

class CCSOLOrdinaryMonsterServices : public CCSOLMonsterSpawnOwner
{
public:
    virtual ~CCSOLOrdinaryMonsterServices() = default;
    // Pure identity check, with no engine/plugin callbacks. A retained service
    // can outlive its registration; every behavior continuation must stop then.
    bool OwnerCurrent() const override = 0;
    virtual CCSOLScenarioMap& Map() = 0;
    virtual CCSOLMonsterPool& Pool() = 0;
    virtual CCSOLMonsterDeathRequests& Deaths() = 0;
    virtual CCSOLScenarioOccupancy& Occupancy() = 0;
    virtual const CCSOLScenarioMessages& Messages() const = 0;
    virtual std::shared_ptr<const CCSOLMonsterNavigation> PinNavigation() = 0;
    virtual std::span<const std::size_t> ProtectedSlots() const = 0;
    virtual const bool& DamageSuppressed() const = 0; // original root+309
    virtual bool LogicStopped() const = 0; // original manager+135A
    // Only the feature3 exclusion test. The owner implements all other
    // ordinary current-target checks, including flags equality and NODRAW.
    virtual bool PlayerTargetExcluded(CSOLMonsterSpawnCall& call, const EHANDLE& player,
        bool& excluded, csol::scenario::ConfigError& error) = 0;
    virtual bool UseSkill(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
        csol::scenario::ConfigError& error) = 0;
    // Original 114ECF00 with weapon/source/category/force = -1/null/-1/false.
    // Apply real rule/player-effect gates and interpolate the requested
    // velocity. A permitted rule rejection still returns true: the caller
    // must continue its own velocity tail. False means failure/cancellation.
    virtual bool ApplyContactImpulse(CSOLMonsterSpawnCall& call, const EHANDLE& target,
        const Vector& requested, csol::scenario::ConfigError& error) = 0;
    virtual bool FrozenDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
};

// Concrete ordinary CBaseZombieImprov owner. Factory installs this class for
// ordinary propensity0; the remaining required services must be supplied by
// real GameRules before the mode can be enabled.
class CCSOLOrdinaryMonsterBehavior final : public CCSOLMonsterSpawnBehavior
{
public:
    explicit CCSOLOrdinaryMonsterBehavior(std::shared_ptr<CCSOLOrdinaryMonsterServices> services,
        std::shared_ptr<CCSOLOrdinaryMonsterDamage> damage = {})
        : m_services(std::move(services)), m_damage(std::move(damage)) {}
    bool RecreateForSpawn() const override { return false; }
    bool ResetForSpawn(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& state,
        csol::scenario::ConfigError& error) override;
    void Think(CCSOLScenarioMonster& monster) override;
    void Touch(CCSOLScenarioMonster& monster, CBaseEntity* other) override;
    BOOL TakeDamage(CCSOLScenarioMonster& monster, entvars_t* inflictor,
        entvars_t* attacker, float damage, int damageType) override;
    BOOL TakeDamageWithContext(CCSOLScenarioMonster& monster, entvars_t* inflictor,
        entvars_t* attacker, float damage, int damageType, const CSOLMonsterDeathContext& context) override;
    void Killed(CCSOLScenarioMonster& monster, entvars_t* attacker, int gib) override;
    CSOLMonsterDeathParameters DeathParameters() const override;
    bool BeforeDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;
    bool SelectDeathSequence(CSOLMonsterDeathCall& call, int& sequence,
        csol::scenario::ConfigError& error) override;
    bool AdjustDeathDuration(CSOLMonsterDeathCall& call, float& seconds,
        csol::scenario::ConfigError& error) override;
    bool PostDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;

    bool MoveTo(CSOLMonsterSpawnCall& call, const Vector& goal);
    bool Stop(CSOLMonsterSpawnCall& call);
    bool Jump(CSOLMonsterSpawnCall& call);
    // Called by the damage owner only after the original frozen/appearance/
    // type flinch gates. This runs v164 and the accepted sequence/motion tail.
    bool Flinch(CSOLMonsterSpawnCall& call);
    // The owner calls this after sound and before the concrete DOT/gait pass.
    // The returned gate controls its 33 records, including player slot32.
    bool UpdateStatusTick(CSOLMonsterSpawnCall& call, bool& elapsed);
    const csol::scenario::ConfigError& Error() const { return m_state ? m_state->error : m_emptyError; }

private:
    struct State : CSOLOrdinaryMonsterState
    {
        std::shared_ptr<CSOLMonsterSpawnState> spawn;
        std::uint64_t lease{};
        std::shared_ptr<const CCSOLMonsterNavigation> navigation;
        csol::scenario::ConfigError error;
    };
    using StatePtr = std::shared_ptr<State>;
    CCSOLScenarioMonster* Current(CSOLMonsterSpawnCall& call, const StatePtr& state) const;
    bool MoveTo(CSOLMonsterSpawnCall& call, State& state, const Vector& goal);
    bool Stop(CSOLMonsterSpawnCall& call, State& state);
    bool ResetMotion(CSOLMonsterSpawnCall& call, State& state);
    bool ValidateTarget(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool SelectTarget(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool RunTree(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool UseSkill(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool UpdateSkill(CSOLMonsterSpawnCall& call, const StatePtr& state);
    bool UpdateAmbientAndEffects(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool CheckObstacles(CSOLMonsterSpawnCall& call, const StatePtr& state);
    bool FindPath(CSOLMonsterSpawnCall& call, const StatePtr& state, bool& acted);
    bool UpdateBehavior(CSOLMonsterSpawnCall& call, const StatePtr& state, CCSOLOrdinaryMonsterServices& services);
    bool Step(CSOLMonsterSpawnCall& call, const StatePtr& state, bool ok, const char* failure);
    std::weak_ptr<CCSOLOrdinaryMonsterServices> m_services;
    std::shared_ptr<CCSOLOrdinaryMonsterDamage> m_damage;
    StatePtr m_state;
    csol::scenario::ConfigError m_emptyError;
};
