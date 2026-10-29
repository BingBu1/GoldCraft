// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_pool.h"

#include <optional>
#include <vector>

// Preserve the original 32-byte context without inventing names for unknown
// fields. Default padding is zero; no uninitialized source-stack bytes travel.
struct CSOLMonsterDeathContext
{
    std::array<std::uint8_t, 32> bytes{
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};
static_assert(sizeof(CSOLMonsterDeathContext) == 32);

struct CSOLMonsterDeathArguments
{
    EHANDLE inflictor;
    EHANDLE attacker;
    bool suppressSound{};
    CSOLMonsterDeathContext context;
};

class CCSOLMonsterDeathExecutor
{
public:
    virtual ~CCSOLMonsterDeathExecutor() = default;
    // The actual synchronous owner runs notification, behavior, state, rewards
    // and map tails. This interface has no successful no-op implementation.
    virtual bool ExecuteDeath(CCSOLScenarioMonster& monster,
        const CSOLMonsterDeathArguments& arguments) = 0;
};

enum class CSOLDeathRequestResult { Ignored, Executed, Deferred, Failed };

// One dispatcher per scenario session. Keep this owner alive until every
// behavior/event scope has returned; Reset before map edict memory is released.
class CCSOLMonsterDeathRequests
{
public:
    explicit CCSOLMonsterDeathRequests(CCSOLMonsterDeathExecutor& executor) : m_executor(executor) {}
    CCSOLMonsterDeathRequests(const CCSOLMonsterDeathRequests&) = delete;
    CCSOLMonsterDeathRequests& operator=(const CCSOLMonsterDeathRequests&) = delete;

    struct BehaviorScope
    {
        std::uint64_t generation{}, operation{};
        std::size_t slot{};
    };
    struct TimedScope { std::uint64_t generation{}, operation{}; };
    // Call around the actual ordinary AI callback (only when it runs). Start
    // drops the previous pending callable; End lowers the gate then replays
    // only its last request, in this same tick.
    // End returns false if replay resets the map, including its last request.
    // It never writes an old scope's failure into the new map's error state.
    std::optional<BehaviorScope> BeginBehavior(CCSOLScenarioMonster& monster);
    bool EndBehavior(const BehaviorScope& scope);

    // Call around the actual timed-event traversal. End detaches the FIFO
    // snapshot and lowers the gate BEFORE replay; newly nested traversals own
    // a separate queue, so old requests are neither lost nor replayed twice.
    // Like EndBehavior, completion requires the same map generation.
    std::optional<TimedScope> BeginTimedEvents();
    bool EndTimedEvents(const TimedScope& scope);
    CSOLDeathRequestResult Request(CCSOLScenarioMonster& monster,
        const CSOLMonsterDeathArguments& arguments);

    bool Reset();
    std::uint64_t Generation() const { return m_generation; }
    const std::string& Error() const { return m_error; }

private:
    struct Pending
    {
        EntityHandle<CCSOLScenarioMonster> monster;
        CCSOLMonsterPool* pool{};
        std::uint64_t lease{}, generation{};
        CSOLMonsterDeathArguments arguments;
        std::shared_ptr<const CSOLScenarioEntityAccess> access;
    };
    struct Local
    {
        std::uint64_t operation{}, lease{};
        CCSOLMonsterPool* pool{};
        std::optional<Pending> pending;
    };
    struct Execution
    {
        CCSOLMonsterPool* pool{};
        std::uint64_t lease{}, operation{};
    };
    std::optional<Pending> Capture(CCSOLScenarioMonster& monster,
        const CSOLMonsterDeathArguments& arguments) const;
    CCSOLScenarioMonster* Resolve(const Pending& pending) const;
    CSOLDeathRequestResult Dispatch(Pending pending);
    std::optional<std::uint64_t> NextOperation();
    bool Fail(const char* message);

    CCSOLMonsterDeathExecutor& m_executor;
    std::array<Local, CCSOLMonsterPool::Capacity> m_local{};
    std::array<Execution, CCSOLMonsterPool::Capacity> m_executing{};
    std::vector<Pending> m_global;
    std::uint64_t m_generation{1}, m_lastOperation{}, m_timedOperation{};
    std::string m_error;
};
