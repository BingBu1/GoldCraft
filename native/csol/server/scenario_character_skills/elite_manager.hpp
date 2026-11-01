// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "control.hpp"
#include "../scenario_death_requests.h"
#include <map>
#include <memory>
#include <set>
#include <vector>

class CCSOLScenarioRuntime;
class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
struct CSOLScenarioPlayerRuntimeState;

// The original CEliteMonsterMgr owns one main target, a separate follower
// list and the damage/execution participation set. It is not a nearest-Boss selector.
// Synthetic type32800 spawning/behavior is a separate factory capability.
class CCSOLScenarioEliteManager final
{
public:
    //10141C50 constructs setting11EF5578 from float bits42200000.
    static constexpr float OriginalSpawnInterval = 40.0f;
    explicit CCSOLScenarioEliteManager(std::weak_ptr<CCSOLScenarioRuntime> runtime);
    bool Current() const;
    // This intentionally permits a dead main target, like10969830. The
    // consumer, not the getter, owns any alive test.
    CCSOLScenarioMonster* Primary() const;
    bool FollowerSpawn() const;
    bool Disabled() const;
    bool WebActive() const;
    bool SetWebActive(bool active);
    bool SetPrimary(const EHANDLE& target, csol::scenario::ConfigError& error);
    bool AddFollower(const EHANDLE& target, csol::scenario::ConfigError& error);
    //11384CF0: group<=0 is an ordinary no-op. A primary spawn kills the old
    // primary first; a follower spawn brackets the factory call with byte135.
    // The factory retains the immutable synthetic type for every borrowed slot.
    bool Spawn(bool follower, int pointGroup,
        std::shared_ptr<const csol::scenario::MonsterType> type,
        csol::scenario::ConfigError& error);
    bool PrimaryDied(const EHANDLE& victim, const EHANDLE& attacker,
        csol::scenario::ConfigError& error);
    bool ForwardFollowerDamage(const EHANDLE& target, const EHANDLE& inflictor,
        const EHANDLE& attacker, float damage, csol::scenario::ConfigError& error);
    bool FlushFollowers(csol::scenario::ConfigError& error);
    struct SpawnClock
    {
        bool enabled{}; // Original room root+27C, supplied by its real owner.
        float elapsed{}; //108235D0, not an assumed time since this manager reset.
        float fragRateReference{}; // Original rules+320, must be provided.
    };
    // Dispatch the actual zs_elite_spawn command path. Success means the
    // command was dispatched, not that a pool slot necessarily spawned.
    using SpawnEvent = bool (*)(void*, csol::scenario::ConfigError&);
    bool UpdateSpawnSchedule(const SpawnClock& clock, SpawnEvent event, void* context,
        csol::scenario::ConfigError& error);
    // Call after formal BeginRound has advanced Map.RoundGeneration. This
    // only resets records; it never kills or returns an entity. Original
    //10969D00 preserves credit keys and the two spawn counters.
    // spawnInterval is the resolved original setting, not an invented cvar.
    // Can rebuild empty records after Invalidate only if this same manager is
    // still registered in a nonclosing runtime with live entity access.
    bool ResetRound(float now, float spawnInterval, csol::scenario::ConfigError& error);
    // Retire before runtime/session teardown. No allocation, engine access or deaths.
    void Invalidate();
    bool SetDisabled(bool disabled, bool killPrimary, csol::scenario::ConfigError& error);
    //109663FC records every positive hit participant; type21 records the
    // same identity before an execution. Both share original manager+12C.
    bool RecordExecution(std::int32_t localIdentity, csol::scenario::ConfigError& error);
    std::int32_t ExecutionCount(std::int32_t localIdentity) const;
    bool Kill(const EHANDLE& target, const CSOLMonsterDeathArguments& arguments,
        csol::scenario::ConfigError& error);

private:
    struct Target
    {
        EHANDLE handle;
        CCSOLMonsterPool* pool{};
        std::uint64_t lease{};
        Target() = default;
        explicit Target(CCSOLScenarioMonster& monster);
        CCSOLScenarioMonster* Resolve() const;
    };
    struct State
    {
        std::shared_ptr<const CSOLScenarioEntityAccess> access;
        std::uint64_t round{}, revision{};
        Target primary;
        std::vector<Target> followers;
        // Host-local authoritative player generations. These are explicitly
        // not CSO's account oid and never come from client userinfo.
        std::set<std::int32_t> executions;
        float lastSpawnCheck{};
        int failedSpawns{}, successfulSpawns{};
        bool pendingCleanup{}, spawnGate{}, scheduleGate{}, followerSpawn{}, disabled{}, webActive{};
    };
    struct Call;
    bool ClearFollowers(Call& call, csol::scenario::ConfigError& error, bool requireType = false);
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<State> m_state;
    // Original rules+32C, separate from manager+12C's pending key set.
    // Same map/session lifetime; ordinary round reset does not erase it.
    std::map<std::int32_t, std::int32_t> m_executionCounts;
};

namespace csol::scenario::skills
{
bool PrepareEliteSkill21(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const std::shared_ptr<CCSOLScenarioEliteManager>& elites,
    const ActivationState& timers, bool& prepared, ConfigError& error);
}
