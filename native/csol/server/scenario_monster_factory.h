// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_owner.h"
#include "scenario_spawn_occupancy.h"

#include <list>
#include <map>

// Runtime slot data belongs to the factory, not the immutable CSV record.
struct CSOLMonsterSpawnState : std::enable_shared_from_this<CSOLMonsterSpawnState>
{
    std::uint64_t lease{};
    csol::scenario::MonsterAttributes attributes;
    csol::scenario::MonsterSequences sequences;
    std::int16_t skill{};
    std::uint8_t rowArgument{};
    float updateAt{}, animationAt{}, ambientAt{};
    bool shortDeath{};
};

class CSOLMonsterSpawnCall
{
public:
    CSOLMonsterSpawnCall(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
        CCSOLMonsterDeathRequests& deaths, CCSOLScenarioMonster& monster,
        const CCSOLMonsterBehavior* expectedBehavior = nullptr);
    bool SameSession() const;
    CCSOLScenarioMonster* Current() const;
    void ReturnIncomplete() const;

private:
    CCSOLScenarioMap& m_map;
    CCSOLMonsterPool& m_pool;
    CCSOLMonsterDeathRequests& m_deaths;
    std::uint64_t m_mapGeneration{}, m_roundGeneration{}, m_deathGeneration{}, m_lease{};
    std::size_t m_slot{};
    EntityHandle<CCSOLScenarioMonster> m_entity;
    // Optional identity only, never dereferenced. Unlike a stack callback,
    // this remains safe when the path follower retains a copy of the call.
    const CCSOLMonsterBehavior* m_expectedBehavior{};
};

// Real ReAPI sequence-reset chain with session/lease checks around its two
// model queries. Also used by live attacks after a sequence changes.
bool CSOL_ResetMonsterSequenceInfo(CSOLMonsterSpawnCall& call,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);

class CCSOLMonsterSpawnBehavior : public CCSOLMonsterDeathBehavior
{
public:
    // Original category0 ordinary behavior can be reused; Oberon requests
    // recreation even when the next category is also zero.
    virtual bool RecreateForSpawn() const = 0;
    virtual bool ResetForSpawn(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& state,
        csol::scenario::ConfigError& error) = 0;
};

// GameRules provides real behavior and auxiliary managers. No default service
// accepts a spawn with missing AI, overlap handling or on-spawn rule effects.
class CCSOLMonsterFactoryServices
{
public:
    virtual ~CCSOLMonsterFactoryServices() = default;
    virtual std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateOrdinary(CSOLMonsterSpawnCall& call,
        csol::scenario::ConfigError& error) = 0;
    virtual std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateBoss(CSOLMonsterSpawnCall& call,
        std::int32_t recordId, csol::scenario::ConfigError& error) = 0;
    virtual std::span<const std::size_t> ProtectedSlots() const = 0;
    // Other per-spawn effects. The factory owns the following speed-ring
    // invalidation/notification stage before SET_ORIGIN.
    virtual bool ClearSpawnEffects(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) = 0;
    virtual bool ResolveSpawnOverlap(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) = 0;
    virtual bool RulesMonsterSpawned(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) = 0;
};

struct CSOLMonsterReplacement
{
    csol::scenario::MonsterType type;
    float start{}, end{}, interval{}, next{};
    std::int16_t remaining{};
    // Original runtime type +58, distinct from CSV column18 (+59).
    bool useRecordIdentity{};
};

enum class CSOLMonsterSpawnResult { Spawned, Skipped, Cancelled, Failed };

// Actual mode15/map98 creation path; the final factory service must supply the
// real ordinary/Boss classes before rules enable this map. Replacement records
// and per-type sequence caches remain alive until all borrowed slots are reset.
class CCSOLMonsterFactory
{
public:
    CCSOLMonsterFactory(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
        CCSOLMonsterModels& models, CCSOLScenarioOccupancy& occupancy,
        CCSOLMonsterDeathRequests& deaths, CCSOLMonsterFactoryServices& services,
        const CCSOLScenarioMessages& messages)
        : m_map(map), m_pool(pool), m_models(models), m_occupancy(occupancy),
          m_deaths(deaths), m_services(services), m_messages(messages) {}

    CSOLMonsterSpawnResult Spawn(int team, int requestedType, int point, int rowArgument);
    // The manager schedules these records; the wrapper updates next before
    // trying creation, and decrements remaining only for successful creation.
    bool ScheduleReplacement(CSOLMonsterReplacement replacement);
    // Reset only after RestartRound/Shutdown has invalidated every live type.
    bool Reset();
    const CSOLMonsterSpawnState* State(const CCSOLScenarioMonster& monster) const;
    const csol::scenario::ConfigError& Error() const { return m_error; }

private:
    CSOLMonsterSpawnResult Create(int team, int requestedType, int point, int rowArgument,
        const csol::scenario::SpawnPoint& location, const csol::scenario::MonsterType& type,
        bool useRecordIdentity, const csol::scenario::RoundMonsterDeltas& delta);
    bool FireSpawnTargets(CSOLMonsterSpawnCall& call, int recordId);
    bool ResolveSequences(CSOLMonsterSpawnCall& call, const csol::scenario::MonsterType& type,
        csol::scenario::MonsterSequences& output);
    bool Fail(const char* message);

    CCSOLScenarioMap& m_map;
    CCSOLMonsterPool& m_pool;
    CCSOLMonsterModels& m_models;
    CCSOLScenarioOccupancy& m_occupancy;
    CCSOLMonsterDeathRequests& m_deaths;
    CCSOLMonsterFactoryServices& m_services;
    const CCSOLScenarioMessages& m_messages;
    // Each borrow has its own state. Behaviors pin it through callback-driven
    // factory reset/reuse, including before initial timer publication finishes.
    std::array<std::shared_ptr<CSOLMonsterSpawnState>, CCSOLMonsterPool::Capacity> m_states{};
    std::map<const csol::scenario::MonsterType*, csol::scenario::MonsterSequences> m_sequences;
    std::list<CSOLMonsterReplacement> m_replacements;
    csol::scenario::ConfigError m_error;
    bool m_busy{};
};
