// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../core/scenario_death_owner.h"
#include "../core/scenario_spawn_occupancy.h"
#include "../../../scenario/monsters/champions.hpp"

#include <list>
#include <map>

namespace csol::scenario { struct GoldenReplacement; }

// Runtime slot data belongs to the factory, not the immutable CSV record.
struct CSOLMonsterSpawnState : std::enable_shared_from_this<CSOLMonsterSpawnState>
{
    std::uint64_t lease{};
    const csol::scenario::MonsterType* type{};
    csol::scenario::MonsterAttributes attributes;
    csol::scenario::MonsterSequences sequences;
    std::int16_t skill{};
    std::uint8_t rowArgument{};
    float updateAt{}, animationAt{}, ambientAt{};
    bool shortDeath{};
    // Golden owns a private color-only type while this lease is active. The
    // source record belongs to the map/champion table. Reset releases these
    // records only after the pool has invalidated every remaining type borrow.
    const csol::scenario::MonsterType* goldenOriginal{};
    std::shared_ptr<const csol::scenario::MonsterType> goldenType;
};

// A path follower retains SpawnCall copies. Keep only a weak owner identity,
// never a stack callback or a strong service reference which would form a
// service -> pool -> behavior -> follower -> service cycle.
class CCSOLMonsterSpawnOwner
{
public:
    virtual ~CCSOLMonsterSpawnOwner() = default;
    virtual bool OwnerCurrent() const = 0; // No engine/plugin callbacks.
};

class CSOLMonsterSpawnCall
{
public:
    CSOLMonsterSpawnCall(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
        CCSOLMonsterDeathRequests& deaths, CCSOLScenarioMonster& monster,
        const CCSOLMonsterBehavior* expectedBehavior = nullptr,
        std::shared_ptr<const CCSOLMonsterSpawnOwner> owner = {});
    bool SameSession() const;
    bool BelongsTo(const CCSOLScenarioMap& map) const { return &m_map == &map; }
    // For cleaning up an entity just returned by an engine allocation after
    // this borrow/round was cancelled. Does not authorize gameplay callbacks.
    bool SameMap() const;
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
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::weak_ptr<const CCSOLMonsterSpawnOwner> m_owner;
    bool m_hasOwner{};
};

// Real ReAPI sequence-reset chain with session/lease checks around its two
// model queries. Also used by live attacks after a sequence changes.
bool CSOL_ResetMonsterSequenceInfo(CSOLMonsterSpawnCall& call,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
// Auxiliary scenario entities use the same real ReAPI chain. The resolver
// must check map/round and entity identity before returning the animated host.
bool CSOL_ResetScenarioSequenceInfo(CBaseAnimating& entity,
    CBaseAnimating* (*current)(const void*), const void* context);

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
    // Type32800 has its own behavior and follower initialization. Providers
    // without that capability must reject it before allocating a pool slot.
    virtual bool SupportsElite() const { return false; }
    virtual std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateElite(CSOLMonsterSpawnCall&,
        bool, csol::scenario::ConfigError& error)
    { error.message = "scenario elite behavior is unavailable"; return {}; }
    virtual bool UpdateEliteSchedule(csol::scenario::ConfigError& error)
    {
        if (!SupportsElite()) return true;
        error.message = "scenario elite schedule is unavailable";
        return false;
    }
    virtual std::span<const std::size_t> ProtectedSlots() const = 0;
    // Manager-owned damage state has an independent lifetime from behaviors.
    // Reset before round-start callbacks; update after the normal manager
    // frame, excluding its freeze and terminating/drain early returns.
    virtual bool ResetRoundDamage(csol::scenario::ConfigError& error) = 0;
    virtual bool UpdateHealthSummary(float now, csol::scenario::ConfigError& error) = 0;
    // Attributes are already written, but SET_MODEL and behavior reset have
    // not run yet. This is the original Boss health-summary initialization.
    virtual bool AttributesApplied(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error) = 0;
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
    float timeOrigin{};
    std::int16_t remaining{};
    // Original runtime type +58, distinct from CSV column18 (+59).
    bool useRecordIdentity{};
    // Created by ScheduleReplacement, shared only with this entry's borrows.
    std::shared_ptr<csol::scenario::ChampionBudget> budget;
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
        const CCSOLScenarioMessages& messages, std::shared_ptr<const CCSOLMonsterSpawnOwner> owner = {})
        : m_map(map), m_pool(pool), m_models(models), m_occupancy(occupancy),
          m_deaths(deaths), m_services(services), m_messages(messages), m_owner(owner), m_hasOwner(owner != nullptr) {}

    CSOLMonsterSpawnResult Spawn(int team, int requestedType, int point, int rowArgument);
    // MP11384CF0: team1/type32800/rowArgument1, with a distinct follower flag.
    // Pin the immutable synthetic record until every pool borrow is reset.
    // Output is empty on every non-Spawned result; no global last-spawn slot.
    CSOLMonsterSpawnResult SpawnElite(std::shared_ptr<const csol::scenario::MonsterType> type,
        int point, bool follower, EHANDLE& created);
    // Elite Initialize changes only this borrow's immutable type snapshot.
    // The verified behavior updates its three attributes separately; this
    // stage does not reapply round deltas, reset health, or reload the model.
    bool ReplaceEliteSpawnType(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& state,
        std::shared_ptr<const csol::scenario::MonsterType> type,
        csol::scenario::ConfigError& error);
    // RulesMonsterSpawned only, after overlap and the positive-health gate.
    // No engine callbacks, model reload, animation reset or immediate speed write.
    bool ApplyGoldenReplacement(CSOLMonsterSpawnCall& call,
        const csol::scenario::GoldenReplacement& replacement, csol::scenario::ConfigError& error);
    // Common-death tail, before Golden membership/clone is released. Restores
    // the original pointer only; current attributes and render state survive.
    bool RestoreGoldenType(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error);
    // The manager schedules these records; the wrapper updates next before
    // trying creation, and decrements remaining only for successful creation.
    bool ScheduleReplacement(CSOLMonsterReplacement replacement);
    // BeginRound reloads the exact difficulty rows in unsigned type-key order.
    // This is called after Reset, with no remaining borrows of the old table.
    // Zero preserves MP's absolute engine clock. Explicit same-map replay
    // uses its challenge origin; subsequent stages retain that same origin.
    bool ScheduleChampions(const csol::scenario::ChampionTable& table, std::uint16_t difficulty,
        float timeOrigin = 0);
    // Reset only after RestartRound/Shutdown has invalidated every live type.
    bool Reset();
    const CSOLMonsterSpawnState* State(const CCSOLScenarioMonster& monster) const;
    // Oberon's type9 invokes the real common lookup again, then replaces four
    // entries below half health. The original cache belongs to the type row:
    // publish to current borrows as well as subsequent spawns of that row.
    bool RefreshOberonSequences(CSOLMonsterSpawnCall& call);
    const csol::scenario::ConfigError& Error() const { return m_error; }

private:
    CSOLMonsterSpawnResult Create(int team, int requestedType, int point, int rowArgument,
        const csol::scenario::SpawnPoint& location, const csol::scenario::MonsterType& type,
        bool useRecordIdentity, const csol::scenario::RoundMonsterDeltas& delta,
        bool eliteFollower = false, EHANDLE* created = nullptr,
        std::shared_ptr<csol::scenario::ChampionLease> champion = {});
    bool FireSpawnTargets(CSOLMonsterSpawnCall& call, int recordId);
    bool ResolveSequences(CSOLMonsterSpawnCall& call, const csol::scenario::MonsterType& type,
        csol::scenario::MonsterSequences& output, bool refreshOberon = false);
    bool Fail(const char* message);

    CCSOLScenarioMap& m_map;
    CCSOLMonsterPool& m_pool;
    CCSOLMonsterModels& m_models;
    CCSOLScenarioOccupancy& m_occupancy;
    CCSOLMonsterDeathRequests& m_deaths;
    CCSOLMonsterFactoryServices& m_services;
    const CCSOLScenarioMessages& m_messages;
    std::weak_ptr<const CCSOLMonsterSpawnOwner> m_owner;
    bool m_hasOwner{};
    // Each borrow has its own state. Behaviors pin it through callback-driven
    // factory reset/reuse, including before initial timer publication finishes.
    std::array<std::shared_ptr<CSOLMonsterSpawnState>, CCSOLMonsterPool::Capacity> m_states{};
    std::map<const csol::scenario::MonsterType*, csol::scenario::MonsterSequences> m_sequences;
    std::list<CSOLMonsterReplacement> m_replacements;
    // Parked monsters and cached sequences still borrow these records.
    std::vector<std::shared_ptr<const csol::scenario::MonsterType>> m_runtimeTypes;
    const CSOLMonsterSpawnState* m_initializingState{};
    const CSOLMonsterSpawnState* m_ruleSpawnState{};
    csol::scenario::ConfigError m_error;
    bool m_busy{};
};
