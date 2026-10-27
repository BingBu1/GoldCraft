// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_requests.h"

#include <limits>
#include <new>
#include <utility>

bool CCSOLMonsterDeathRequests::Fail(const char* message)
{
    if (m_error.empty()) m_error = message;
    return false;
}

std::optional<std::uint64_t> CCSOLMonsterDeathRequests::NextOperation()
{
    if (!m_error.empty()) return {};
    if (m_lastOperation == std::numeric_limits<std::uint64_t>::max())
    {
        Fail("scenario death scope identity exhausted");
        return {};
    }
    return ++m_lastOperation;
}

bool CCSOLMonsterDeathRequests::Reset()
{
    // In-flight snapshots check generation before touching an old EHANDLE.
    m_local = {};
    m_executing = {};
    m_global.clear();
    m_timedOperation = 0;
    if (m_generation == std::numeric_limits<std::uint64_t>::max())
        return Fail("scenario death map identity exhausted");
    ++m_generation;
    m_error.clear();
    return true;
}

std::optional<CCSOLMonsterDeathRequests::Pending> CCSOLMonsterDeathRequests::Capture(
    CCSOLScenarioMonster& monster, const CSOLMonsterDeathArguments& arguments) const
{
    auto* pool = monster.Pool();
    if (!pool || pool->Busy() || !pool->Borrowed(monster) || !monster.Type() ||
        !monster.Behavior() || monster.pev->deadflag == DEAD_DEAD) return {};
    const auto lease = pool->Lease(monster);
    if (!lease) return {};
    return Pending{EntityHandle<CCSOLScenarioMonster>(&monster), pool, lease, m_generation, arguments};
}

CCSOLScenarioMonster* CCSOLMonsterDeathRequests::Resolve(const Pending& pending) const
{
    if (pending.generation != m_generation) return nullptr;
    auto* base = pending.monster.Get<CBaseEntity>();
    auto* monster = base ? dynamic_cast<CCSOLScenarioMonster*>(base) : nullptr;
    if (!monster || !pending.pool || monster->Pool() != pending.pool || !monster->Type()) return nullptr;
    if (pending.pool->Busy() || !pending.pool->Borrowed(*monster) ||
        pending.pool->Lease(*monster) != pending.lease || !monster->Behavior() ||
        monster->pev->deadflag == DEAD_DEAD) return nullptr;
    return monster;
}

std::optional<CCSOLMonsterDeathRequests::BehaviorScope>
CCSOLMonsterDeathRequests::BeginBehavior(CCSOLScenarioMonster& monster)
{
    auto pending = Capture(monster, {});
    if (!pending || !m_error.empty()) return {};
    auto& local = m_local[monster.Slot()];
    if (local.operation)
    {
        Fail("scenario monster behavior scope is already active");
        return {};
    }
    const auto operation = NextOperation();
    if (!operation) return {};
    local = {*operation, pending->lease, pending->pool, {}};
    return BehaviorScope{m_generation, *operation, monster.Slot()};
}

bool CCSOLMonsterDeathRequests::EndBehavior(const BehaviorScope& scope)
{
    if (scope.generation != m_generation) return false;
    if (scope.slot >= m_local.size() || !scope.operation ||
        m_local[scope.slot].operation != scope.operation)
        return Fail("scenario monster behavior scope does not match");
    auto pending = std::move(m_local[scope.slot].pending);
    m_local[scope.slot] = {};
    if (!m_error.empty()) return false;
    // Dispatch again rather than forcing immediate execution: the global
    // timed-event gate may still be active when the local AI callback exits.
    if (pending && Dispatch(std::move(*pending)) == CSOLDeathRequestResult::Failed) return false;
    return scope.generation == m_generation && m_error.empty();
}

std::optional<CCSOLMonsterDeathRequests::TimedScope> CCSOLMonsterDeathRequests::BeginTimedEvents()
{
    if (m_timedOperation)
    {
        Fail("scenario timed-event traversal is already active");
        return {};
    }
    const auto operation = NextOperation();
    if (!operation) return {};
    m_timedOperation = *operation;
    return TimedScope{m_generation, *operation};
}

bool CCSOLMonsterDeathRequests::EndTimedEvents(const TimedScope& scope)
{
    if (scope.generation != m_generation) return false;
    if (!scope.operation || scope.operation != m_timedOperation)
        return Fail("scenario timed-event traversal does not match");
    auto pending = std::move(m_global);
    m_global.clear();
    m_timedOperation = 0;
    const auto generation = m_generation;
    for (auto& request : pending)
    {
        if (generation != m_generation) return false;
        if (Dispatch(std::move(request)) == CSOLDeathRequestResult::Failed) return false;
    }
    return generation == m_generation && m_error.empty();
}

CSOLDeathRequestResult CCSOLMonsterDeathRequests::Request(CCSOLScenarioMonster& monster,
    const CSOLMonsterDeathArguments& arguments)
{
    if (!m_error.empty()) return CSOLDeathRequestResult::Failed;
    auto pending = Capture(monster, arguments);
    if (!pending) return CSOLDeathRequestResult::Ignored;
    return Dispatch(std::move(*pending));
}

CSOLDeathRequestResult CCSOLMonsterDeathRequests::Dispatch(Pending pending)
{
    if (!m_error.empty()) return CSOLDeathRequestResult::Failed;
    auto* monster = Resolve(pending);
    if (!monster) return CSOLDeathRequestResult::Ignored;
    auto& local = m_local[monster->Slot()];
    // Match the order in the original entry: local gate before global gate.
    if (local.operation)
    {
        if (local.pool != pending.pool || local.lease != pending.lease)
        {
            Fail("scenario monster was reborrowed during its behavior callback");
            return CSOLDeathRequestResult::Failed;
        }
        local.pending = std::move(pending);
        return CSOLDeathRequestResult::Deferred;
    }
    if (m_timedOperation)
    {
        try { m_global.push_back(std::move(pending)); }
        catch (const std::bad_alloc&)
        {
            Fail("scenario deferred death allocation failed");
            return CSOLDeathRequestResult::Failed;
        }
        return CSOLDeathRequestResult::Deferred;
    }
    // Notification/beforeDeath occur before deadflag is set. A hook can ask
    // for the same death again in that gap; it must not repeat any side effect.
    const auto slot = monster->Slot();
    const auto active = m_executing[slot];
    if (active.operation && active.pool == pending.pool && active.lease == pending.lease)
        return CSOLDeathRequestResult::Ignored;
    const auto operation = NextOperation();
    if (!operation) return CSOLDeathRequestResult::Failed;
    m_executing[slot] = {pending.pool, pending.lease, *operation};
    struct ExecutionGuard
    {
        Execution& record;
        const std::uint64_t& generation;
        std::uint64_t expectedGeneration, operation;
        ~ExecutionGuard()
        {
            if (generation == expectedGeneration && record.operation == operation) record = {};
        }
    } guard{m_executing[slot], m_generation, pending.generation, *operation};
    bool executed{};
    try
    {
        executed = m_executor.ExecuteDeath(*monster, pending.arguments);
    }
    catch (...)
    {
        // A partial death must never be retried after an exception: quota,
        // money or messages may already have been delivered. A reset cancels
        // the old transaction without contaminating the replacement session.
        if (pending.generation != m_generation) return CSOLDeathRequestResult::Ignored;
        Fail("scenario synchronous death execution threw an exception");
        return CSOLDeathRequestResult::Failed;
    }
    // A callback can reset the old map. Its return must not poison the error
    // state of a newly initialized session after the reset has completed.
    if (pending.generation != m_generation) return CSOLDeathRequestResult::Ignored;
    if (!m_error.empty()) return CSOLDeathRequestResult::Failed;
    if (!executed)
    {
        Fail("scenario synchronous death execution failed");
        return CSOLDeathRequestResult::Failed;
    }
    return CSOLDeathRequestResult::Executed;
}
