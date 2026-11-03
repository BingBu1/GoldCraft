// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_factory.h"
#include "../scenario/map_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace
{
struct FactoryOperation
{
    bool& busy;
    explicit FactoryOperation(bool& value) : busy(value) { busy = true; }
    ~FactoryOperation() { busy = false; }
};
bool ExcludesReplacement(std::string_view appearance)
{
    return appearance == "zombieclimber" || appearance == "flydrone" ||
        appearance == "elite_forceC" || appearance == "elite_forceT";
}
bool Finite(const std::array<float, 3>& vector)
{
    return std::all_of(vector.begin(), vector.end(), [](float value) { return std::isfinite(value); });
}
bool RenderInteger(float value)
{
    return std::isfinite(value) && static_cast<double>(value) >= -2147483648.0 &&
        static_cast<double>(value) < 2147483648.0;
}

struct SequenceResetCall
{
    CBaseAnimating* expected;
    SequenceResetCall* previous;
    CBaseAnimating* (*resolve)(const void*);
    const void* context;
    bool valid{true};
    static thread_local SequenceResetCall* active;
    SequenceResetCall(CBaseAnimating* entity,
        CBaseAnimating* (*resolver)(const void*), const void* value)
        : expected(entity), previous(active), resolve(resolver), context(value) { active = this; }
    ~SequenceResetCall() { active = previous; }
    CBaseAnimating* Current() const
    {
        auto* current = resolve ? resolve(context) : nullptr;
        return current == expected ? current : nullptr;
    }
};
thread_local SequenceResetCall* SequenceResetCall::active{};
}

bool CSOL_ResetMonsterSequenceInfo(CSOLMonsterSpawnCall& call,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    auto* current = call.Current();
    if (!current) return false;
    if (ownerValid && !ownerValid(ownerContext)) return false;
    current = call.Current();
    if (!current) return false;
    struct Context
    {
        CSOLMonsterSpawnCall& call;
        bool (*ownerValid)(const void*);
        const void* ownerContext;
    } context{call, ownerValid, ownerContext};
    return CSOL_ResetScenarioSequenceInfo(*current, [](const void* value) -> CBaseAnimating*
    {
        const auto& state = *static_cast<const Context*>(value);
        return (!state.ownerValid || state.ownerValid(state.ownerContext)) ? state.call.Current() : nullptr;
    }, &context);
}

bool CSOL_ResetScenarioSequenceInfo(CBaseAnimating& entity,
    CBaseAnimating* (*resolve)(const void*), const void* context)
{
    if (!resolve || resolve(context) != &entity) return false;
    SequenceResetCall sequenceReset(&entity, resolve, context);
#ifdef REGAMEDLL_API
    g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(
        &CBaseAnimating::CSOL_ResetSpawnSequenceInfo, &entity);
#else
    entity.CSOL_ResetSpawnSequenceInfo();
#endif
    return sequenceReset.valid && sequenceReset.Current() == &entity;
}

void CBaseAnimating::CSOL_ResetSpawnSequenceInfo()
{
    auto* scope = SequenceResetCall::active;
    // A hook may remove this entity before invoking the original function.
    // Compare the saved address without reading through it, then resolve the
    // captured session/lease before every engine-model query and SDK read.
    if (!scope) return;
    auto* current = scope->Current();
    if (scope->expected != this || current != this ||
        !g_engfuncs.pfnGetModelPtr)
    { scope->valid = false; return; }
    void* model = GET_MODEL_PTR(current->edict());
    current = scope->Current();
    if (!current) { scope->valid = false; return; }
    ::GetSequenceInfo(model, current->pev, &current->m_flFrameRate, &current->m_flGroundSpeed);
    model = GET_MODEL_PTR(current->edict());
    current = scope->Current();
    if (!current) { scope->valid = false; return; }
    current->m_fSequenceLoops = (::GetSequenceFlags(model, current->pev) & STUDIO_LOOPING) != 0;
    current->pev->animtime = gpGlobals->time;
    current->pev->framerate = 1;
    current->m_fSequenceFinished = FALSE;
    current->m_flLastEventCheck = gpGlobals->time;
}

CSOLMonsterSpawnCall::CSOLMonsterSpawnCall(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
    CCSOLMonsterDeathRequests& deaths, CCSOLScenarioMonster& monster,
    const CCSOLMonsterBehavior* expectedBehavior, std::shared_ptr<const CCSOLMonsterSpawnOwner> owner)
    : m_map(map), m_pool(pool), m_deaths(deaths), m_mapGeneration(map.SpawnPoints().Generation()),
      m_roundGeneration(map.RoundGeneration()), m_deathGeneration(deaths.Generation()),
      m_lease(pool.Lease(monster)), m_slot(monster.Slot()), m_entity(&monster),
      m_expectedBehavior(expectedBehavior), m_access(map.EntityAccess()), m_owner(owner), m_hasOwner(owner != nullptr)
{
}

bool CSOLMonsterSpawnCall::SameSession() const
{
    if (m_hasOwner)
    {
        const auto owner = m_owner.lock();
        if (!owner || !owner->OwnerCurrent()) return false;
    }
    return m_access->Allowed() && m_map.EntityAccess() == m_access && m_pool.EntityAccessAllowed() &&
        m_mapGeneration == m_map.SpawnPoints().Generation() &&
        m_roundGeneration == m_map.RoundGeneration() && m_deathGeneration == m_deaths.Generation() &&
        m_map.State() == CSOLScenarioMapState::RoundActive;
}

bool CSOLMonsterSpawnCall::SameMap() const
{
    return m_access->Allowed() && m_map.EntityAccess() == m_access && m_pool.EntityAccessAllowed() &&
        m_mapGeneration == m_map.SpawnPoints().Generation();
}

CCSOLScenarioMonster* CSOLMonsterSpawnCall::Current() const
{
    if (!SameSession() || !m_deaths.Error().empty()) return nullptr;
    auto* base = m_entity.Get<CBaseEntity>();
    auto* monster = base ? dynamic_cast<CCSOLScenarioMonster*>(base) : nullptr;
    if (!monster || monster->Pool() != &m_pool || !monster->Type() ||
        (m_expectedBehavior && monster->Behavior() != m_expectedBehavior) ||
        m_pool.State() != CSOLMonsterPoolState::Ready ||
        m_pool.Busy() || m_pool.Lease(*monster) != m_lease) return nullptr;
    return monster;
}

void CSOLMonsterSpawnCall::ReturnIncomplete() const
{
    // A dispatcher-only reset can cancel this call without destroying the
    // pool. Resolve the pool's current slot rather than an old-map handle.
    // Never reclaim a replacement lease, or touch a pool during map shutdown.
    if (!SameMap() ||
        m_roundGeneration != m_map.RoundGeneration() ||
        m_pool.State() != CSOLMonsterPoolState::Ready || m_pool.Busy()) return;
    auto* monster = m_pool.At(m_slot);
    if (monster && m_pool.Lease(*monster) == m_lease) m_pool.Free(*monster);
}

bool CCSOLMonsterFactory::Fail(const char* message)
{
    if (m_error.message.empty()) m_error.message = message;
    return false;
}

bool CCSOLMonsterFactory::Reset()
{
    if (m_busy || m_pool.ActiveCount() || m_pool.State() == CSOLMonsterPoolState::Failed) return false;
    // Parked slots retain type pointers and behaviors. RestartRound clears the
    // pointers; Shutdown detaches them. Neither an empty active count nor
    // NODRAW alone makes replacement/cache storage safe to release.
    if (m_pool.State() == CSOLMonsterPoolState::Ready)
        for (std::size_t i = 0; i < CCSOLMonsterPool::Capacity; ++i)
            if (const auto* monster = m_pool.At(i); !monster || monster->Type()) return false;
    m_states = {};
    m_sequences.clear();
    m_replacements.clear();
    m_runtimeTypes.clear();
    m_error = {};
    return true;
}

bool CCSOLMonsterFactory::ScheduleReplacement(CSOLMonsterReplacement replacement)
{
    if (m_busy || !m_error.message.empty()) return false;
    if (!std::isfinite(replacement.start) || !std::isfinite(replacement.end) ||
        !std::isfinite(replacement.interval) || !std::isfinite(replacement.next) ||
        !std::isfinite(replacement.timeOrigin) ||
        replacement.type.priority < 0 || replacement.type.priority > 2)
        return Fail("scenario replacement schedule is invalid");
    replacement.budget = std::make_shared<csol::scenario::ChampionBudget>();
    replacement.type.champion = replacement.useRecordIdentity;
    replacement.budget->available = replacement.remaining;
    m_replacements.push_back(std::move(replacement));
    return true;
}

bool CCSOLMonsterFactory::ScheduleChampions(const csol::scenario::ChampionTable& table,
    std::uint16_t difficulty, float timeOrigin)
{
    if (m_busy || !m_error.message.empty() || !m_replacements.empty() || m_pool.ActiveCount())
        return Fail("scenario champions must be installed after the old round is reset");
    if (!std::isfinite(timeOrigin)) return Fail("scenario champion clock origin is invalid");
    std::list<CSOLMonsterReplacement> candidate;
    for (const auto& [key, index] : table.Select(difficulty))
    {
        const auto& row = table.rows[index];
        if (!std::isfinite(row.start) || !std::isfinite(row.end) || !std::isfinite(row.interval) ||
            row.type.priority < 0 || row.type.priority > 2)
            return Fail("scenario champion schedule is invalid");
        CSOLMonsterReplacement entry;
        entry.type = row.type;
        entry.type.champion = true;
        entry.timeOrigin = timeOrigin;
        entry.start = entry.next = row.start;
        entry.end = row.end;
        entry.interval = row.interval;
        entry.remaining = row.maximum;
        entry.useRecordIdentity = true;
        entry.budget = std::make_shared<csol::scenario::ChampionBudget>();
        entry.budget->available = row.maximum;
        candidate.push_back(std::move(entry));
    }
    m_replacements = std::move(candidate);
    return true;
}

const CSOLMonsterSpawnState* CCSOLMonsterFactory::State(const CCSOLScenarioMonster& monster) const
{
    if (monster.Pool() != &m_pool || monster.Slot() >= m_states.size()) return nullptr;
    const auto& state = m_states[monster.Slot()];
    return state && state->lease && m_pool.Lease(monster) == state->lease ? state.get() : nullptr;
}

CSOLMonsterSpawnResult CCSOLMonsterFactory::Spawn(int team, int requestedType, int point, int rowArgument)
{
    using Result = CSOLMonsterSpawnResult;
    const auto owner = m_owner.lock();
    if (m_hasOwner && (!owner || !owner->OwnerCurrent())) return Result::Cancelled;
    if (m_busy || !m_error.message.empty()) return Result::Failed;
    FactoryOperation operation(m_busy);
    if (m_map.State() != CSOLScenarioMapState::RoundActive || !m_map.Tables() ||
        !csol::scenario::HasMapGameplay(m_map.Tables()->mapName, m_map.Tables()->scenarioType) ||
        !m_map.RoundIndex() || m_pool.State() != CSOLMonsterPoolState::Ready || !gpGlobals)
    {
        Fail("scenario factory requires an active implemented map round");
        return Result::Failed;
    }
    // The original wrapper selects the point before it looks up a type, and
    // consumes attempted-spawn quota before checking pool category capacity.
    auto& points = m_map.SpawnPoints().Points();
    const auto* selected = points.FindMonsterPoint(team, point);
    if (!selected) return Result::Skipped;
    const auto location = selected->point;
    const auto* type = m_map.Tables()->monsterTypes.Find(requestedType);
    if (!type) return Result::Skipped;
    if (type->priority != 2 && !points.ConsumePointQuota(team, point)) return Result::Skipped;
    if (type->priority == 0 && m_pool.CategoryCount(0) >= CCSOLMonsterPool::CategoryLimits[0])
        return Result::Skipped;
    csol::scenario::RoundMonsterDeltas delta;
    if (!csol::scenario::ReadRoundMonsterDeltas(m_map.Tables()->rounds, *m_map.RoundIndex(), delta, m_error))
        return Result::Failed;

    if (type->priority == 0 && !ExcludesReplacement(type->appearance))
    {
        for (auto& entry : m_replacements)
        {
            const float now = gpGlobals->time - entry.timeOrigin;
            if (!std::isfinite(now)) { Fail("scenario champion clock is not finite"); return Result::Failed; }
            if (!(now > entry.start && entry.end > now && now > entry.next && entry.budget->available > 0)) continue;
            const float next = now + entry.interval;
            if (!std::isfinite(next)) { Fail("scenario replacement deadline is not finite"); return Result::Failed; }
            entry.next = next;
            auto lease = std::make_shared<csol::scenario::ChampionLease>(entry.budget);
            const auto result = Create(team, requestedType, point, rowArgument, location, entry.type,
                entry.useRecordIdentity, delta, false, nullptr, lease);
            if (result == Result::Spawned) { lease->Commit(); return result; }
            if (result == Result::Failed || result == Result::Cancelled) return result;
            // Ordinary inability to allocate keeps the new deadline/count,
            // falls back to the original type, and never tries another entry.
            break;
        }
    }
    return Create(team, requestedType, point, rowArgument, location, *type, false, delta);
}

CSOLMonsterSpawnResult CCSOLMonsterFactory::SpawnElite(
    std::shared_ptr<const csol::scenario::MonsterType> type, int point,
    bool follower, EHANDLE& created)
{
    using Result = CSOLMonsterSpawnResult;
    created = nullptr;
    const auto owner = m_owner.lock();
    if (m_hasOwner && (!owner || !owner->OwnerCurrent())) return Result::Cancelled;
    if (m_busy || !m_error.message.empty()) return Result::Failed;
    FactoryOperation operation(m_busy);
    if (m_map.State() != CSOLScenarioMapState::RoundActive || !m_map.Tables() ||
        !csol::scenario::HasMapGameplay(m_map.Tables()->mapName, m_map.Tables()->scenarioType) ||
        !m_map.RoundIndex() || m_pool.State() != CSOLMonsterPoolState::Ready || !gpGlobals)
    {
        Fail("scenario elite factory requires an active implemented map round");
        return Result::Failed;
    }
    if (point <= 0) return Result::Skipped;
    auto& points = m_map.SpawnPoints().Points();
    const auto* selected = points.FindMonsterPoint(1, point);
    if (!selected) return Result::Skipped;
    const auto location = selected->point;
    if (!type || type->id != 32800 || type->priority != 1 || type->skill != 1000 ||
        type->appearance != "zbs_elite_01")
    {
        Fail("scenario elite factory requires the original synthetic type32800");
        return Result::Failed;
    }
    // Preserve the common wrapper's attempted-spawn quota ordering.
    if (!points.ConsumePointQuota(1, point)) return Result::Skipped;
    csol::scenario::RoundMonsterDeltas delta;
    if (!csol::scenario::ReadRoundMonsterDeltas(m_map.Tables()->rounds,
        *m_map.RoundIndex(), delta, m_error)) return Result::Failed;
    // Retain a row only once. Do not copy it per spawn: the sequence cache is
    // keyed by this actual record identity, as in the ordinary factory.
    if (std::none_of(m_runtimeTypes.begin(), m_runtimeTypes.end(),
        [&](const auto& record) { return record.get() == type.get(); }))
    {
        try { m_runtimeTypes.push_back(type); }
        catch (...) { Fail("scenario elite type retention failed"); return Result::Failed; }
    }
    return Create(1, 32800, point, 1, location, *type, false, delta, follower, &created);
}

bool CCSOLMonsterFactory::ReplaceEliteSpawnType(CSOLMonsterSpawnCall& call,
    CSOLMonsterSpawnState& state, std::shared_ptr<const csol::scenario::MonsterType> type,
    csol::scenario::ConfigError& error)
{
    error = {};
    // This is an initialization operation within Create, not a way to replace
    // the type of a live, dead, or subsequently reused pool slot.
    if (!call.BelongsTo(m_map)) return false;
    auto* monster = call.Current();
    if (!monster) return false;
    if (!m_busy || m_initializingState != &state || !m_error.message.empty() ||
        monster->Pool() != &m_pool || !m_pool.Borrowed(*monster) ||
        State(*monster) != &state || state.type != monster->Type() ||
        monster->pev->deadflag != DEAD_NO ||
        monster->pev->takedamage != DAMAGE_NO)
    {
        error.message = "elite type replacement requires the current initializing borrow";
        return false;
    }
    const auto* previous = monster->Type();
    if (!type || previous->id != 32800 || type->id != 32800 ||
        previous->priority != 1 || type->priority != 1 ||
        previous->skill != 1000 || type->skill != 1000 ||
        previous->appearance != "zbs_elite_01" || type->appearance != previous->appearance ||
        type->propensity != previous->propensity || type->Key() != previous->Key() ||
        !std::isfinite(type->attackInterval))
    {
        error.message = "elite initialization cannot replace its model or behavior identity";
        return false;
    }
    // Both parked slots and sequence-cache keys can outlive the behavior.
    // Retain before publishing either pointer; allocation failure is atomic.
    if (std::none_of(m_runtimeTypes.begin(), m_runtimeTypes.end(),
        [&](const auto& record) { return record.get() == type.get(); }))
    {
        try { m_runtimeTypes.push_back(type); }
        catch (...) { error.message = "elite initialized type retention failed"; return false; }
    }
    monster->m_type = type.get();
    // ResetForSpawn specializes the elite record after the attribute stage
    // registered its initial type. Rebind that same borrowed lease before
    // post-origin registration; keep Register's exact type/lease checks.
    // ApplyPriority has no callbacks and only writes after all checks pass.
    if (!m_occupancy.ApplyPriority(*monster))
    {
        monster->m_type = previous;
        error.message = "elite initialized type occupancy binding failed";
        return false;
    }
    state.type = type.get();
    return true;
}

bool CCSOLMonsterFactory::ResolveSequences(CSOLMonsterSpawnCall& call,
    const csol::scenario::MonsterType& type, csol::scenario::MonsterSequences& output, bool refreshOberon)
{
    if (const auto found = m_sequences.find(&type); !refreshOberon && found != m_sequences.end())
    {
        output = found->second;
        return true;
    }
    struct Lookup
    {
        CSOLMonsterSpawnCall& call;
        const csol::scenario::MonsterType* type;
        bool valid{true};
    } lookup{call, &type};
    csol::scenario::MonsterSequences sequences;
    const auto query = [](void* context, const char* name)
    {
        auto& state = *static_cast<Lookup*>(context);
        auto* monster = state.call.Current();
        if (!state.valid || !monster || monster->Type() != state.type) { state.valid = false; return -1; }
        void* model = GET_MODEL_PTR(monster->edict());
        monster = state.call.Current();
        if (!monster || monster->Type() != state.type) { state.valid = false; return -1; }
        return ::LookupSequence(model, name);
    };
    const bool resolved = csol::scenario::ResolveMonsterSequences(query, &lookup, sequences);
    if (!resolved || !lookup.valid || !call.Current()) return false;
    // MP113D6A40 reads health only AFTER the common sequence queries. Retain
    // the original four separate lookups, including both knife_walk calls.
    if (refreshOberon && call.Current()->pev->health / call.Current()->pev->max_health < 0.5f)
    {
        using Sequence = csol::scenario::MonsterSequence;
        for (const auto& entry : {std::pair{Sequence::Idle, "knife_idle"},
            std::pair{Sequence::Walk, "knife_walk"}, std::pair{Sequence::Run, "knife_walk"},
            std::pair{Sequence::Jump, "knife_idle"}})
        {
            const auto value = query(&lookup, entry.second);
            if (!lookup.valid || !call.Current()) return false;
            sequences[entry.first] = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
        }
    }
    // The cache is specific to this actual type record and this loaded map.
    m_sequences.insert_or_assign(&type, sequences);
    output = sequences;
    return true;
}

bool CCSOLMonsterFactory::RefreshOberonSequences(CSOLMonsterSpawnCall& call)
{
    if (m_busy || !call.BelongsTo(m_map) || !call.Current()) return false;
    auto* monster = call.Current();
    const auto* type = monster->Type();
    if (!type || type->id != 30200 || !State(*monster) || !g_engfuncs.pfnGetModelPtr)
        return Fail("Oberon animation refresh requires its factory-owned type row");
    FactoryOperation operation(m_busy);
    csol::scenario::MonsterSequences sequences;
    if (!ResolveSequences(call, *type, sequences, true))
        return call.Current() ? Fail("Oberon animation refresh was interrupted") : false;
    for (const auto& state : m_states)
        if (state && state->type == type) state->sequences = sequences;
    return true;
}

bool CCSOLMonsterFactory::FireSpawnTargets(CSOLMonsterSpawnCall& call, int recordId)
{
    if (!g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnIndexOfEdict)
        return Fail("scenario spawn target service is unavailable");
    char name[40];
    const int length = std::snprintf(name, sizeof(name), "mm_zb_spawn_%d", recordId);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name))
        return Fail("scenario spawn target name is too long");
    edict_t* previous = nullptr;
    int previousIndex{};
    static_assert(USE_SET == 2);
    for (;;)
    {
        if (!call.Current()) return false;
        auto* found = FIND_ENTITY_BY_STRING(previous, "targetname", name);
        if (!call.Current()) return false;
        if (!found) return true;
        const int index = ENTINDEX(found);
        if (!call.Current()) return false;
        if (!index) return true;
        if (!gpGlobals || index <= previousIndex || index >= gpGlobals->maxEntities || found->free)
            return Fail("scenario spawn target traversal order is invalid");
        auto* entity = CBaseEntity::Instance(found);
        if (!entity) return true;
        entity->Use(nullptr, nullptr, USE_SET, 0.0f);
        if (!call.Current()) return false;
        previous = found;
        previousIndex = index;
    }
}

CSOLMonsterSpawnResult CCSOLMonsterFactory::Create(int team, int requestedType, int point,
    int rowArgument, const csol::scenario::SpawnPoint& location, const csol::scenario::MonsterType& type,
    bool useRecordIdentity, const csol::scenario::RoundMonsterDeltas& delta,
    bool eliteFollower, EHANDLE* created, std::shared_ptr<csol::scenario::ChampionLease> champion)
{
    using Result = CSOLMonsterSpawnResult;
    // Category2/4/5 and other maps have separate behavior contracts. Do not
    // install the ordinary class for one of them merely to make it visible.
    if (type.appearance == "elite_forceC" || type.appearance == "elite_forceT" ||
        type.id == 32908 || !Finite(location.origin) || !Finite(location.angles) ||
        !Finite(type.color) || !RenderInteger(type.column21[0]) ||
        !RenderInteger(type.column21[1]) || !std::isfinite(type.column21[2]))
    {
        Fail("scenario spawn type needs an unavailable behavior category or has invalid render data");
        return Result::Failed;
    }
    const csol::scenario::MonsterType* previousType{};
    const auto owner = m_owner.lock();
    if (m_hasOwner && (!owner || !owner->OwnerCurrent())) return Result::Cancelled;
    const auto mapGeneration = m_map.SpawnPoints().Generation();
    const auto roundGeneration = m_map.RoundGeneration();
    const auto deathGeneration = m_deaths.Generation();
    const auto access = m_map.EntityAccess();
    const auto sameSession = [&]
    {
        return access && access->Allowed() && m_map.EntityAccess() == access && m_pool.EntityAccessAllowed() &&
            (!owner || owner->OwnerCurrent()) && mapGeneration == m_map.SpawnPoints().Generation() &&
            roundGeneration == m_map.RoundGeneration() && deathGeneration == m_deaths.Generation() &&
            m_map.State() == CSOLScenarioMapState::RoundActive;
    };
    if (type.id == 32800)
    {
        bool supported{};
        try { supported = m_services.SupportsElite(); }
        catch (...)
        {
            if (!sameSession()) return Result::Cancelled;
            Fail("scenario elite capability query threw an exception");
            return Result::Failed;
        }
        if (!sameSession()) return Result::Cancelled;
        if (!supported)
        { Fail("scenario elite behavior is unavailable"); return Result::Failed; }
    }
    std::span<const std::size_t> protectedSlots;
    try { protectedSlots = m_services.ProtectedSlots(); }
    catch (...)
    {
        if (!sameSession()) return Result::Cancelled;
        Fail("scenario protected-slot query threw an exception");
        return Result::Failed;
    }
    if (!sameSession()) return Result::Cancelled;
    std::shared_ptr<CSOLMonsterSpawnState> spawnState;
    try { spawnState = std::make_shared<CSOLMonsterSpawnState>(); }
    catch (...) { Fail("scenario spawn state allocation failed"); return Result::Failed; }
    auto* monster = m_pool.Acquire(type, point, m_map.SpawnPoints().Points(),
        protectedSlots, &previousType);
    // Acquire can invoke effect cleanup while evicting an old slot. Its new
    // SpawnCall must not bless a generation installed by that callback.
    if (!sameSession())
    {
        if (monster && access && access->Allowed() && m_map.EntityAccess() == access &&
            m_pool.EntityAccessAllowed() && mapGeneration == m_map.SpawnPoints().Generation() &&
            roundGeneration == m_map.RoundGeneration())
        {
            CSOLMonsterSpawnCall cleanup(m_map, m_pool, m_deaths, *monster, nullptr, owner);
            cleanup.ReturnIncomplete();
        }
        return Result::Cancelled;
    }
    if (!monster)
    {
        if (m_pool.State() == CSOLMonsterPoolState::Failed)
        { Fail(m_pool.Error().c_str()); return Result::Failed; }
        return Result::Skipped;
    }
    CSOLMonsterSpawnCall call(m_map, m_pool, m_deaths, *monster, nullptr, owner);
    if (!call.SameSession())
    {
        call.ReturnIncomplete();
        return Result::Cancelled;
    }
    if (!call.Current())
    {
        call.ReturnIncomplete();
        Fail("scenario allocated monster is unavailable before spawn initialization");
        return Result::Failed;
    }
    // Acquire returns the previous type for behavior selection below. A
    // parked Golden clone belongs to the old state, so retain that state
    // until selection has finished even after publishing this new lease.
    const auto previousSpawnState = m_states[monster->Slot()];
    if (previousSpawnState && previousSpawnState->goldenType)
        m_sequences.erase(previousSpawnState->goldenType.get());
    m_states[monster->Slot()] = spawnState;
    auto& state = *spawnState;
    struct InitializingScope
    {
        const CSOLMonsterSpawnState*& current;
        InitializingScope(const CSOLMonsterSpawnState*& target, const CSOLMonsterSpawnState& state)
            : current(target) { current = &state; }
        ~InitializingScope() { current = nullptr; }
    } initializing(m_initializingState, state);
    state.lease = m_pool.Lease(*monster);
    state.type = &type;
    state.skill = type.skill;
    monster->pev->team = team;
    monster->m_championLease = std::move(champion);
    monster->m_typeId = useRecordIdentity || type.column20 > 0 ? type.id : requestedType;
    csol::scenario::ConfigError error;
    const auto valid = [&] { return call.Current() != nullptr; };
    // A real synchronous death owns FreeThink and its animation/deadline.
    // Health alone is insufficient: callbacks may set zero without dispatch.
    const auto died = [&]
    { const auto* current = call.Current(); return current && current->pev->deadflag == DEAD_DEAD; };
    const auto step = [&](bool ok, const char* reason)
    {
        if (!call.SameSession()) return false;
        if (ok && valid() && m_error.message.empty()) return true;
        if (m_error.message.empty()) m_error = error.message.empty() ? csol::scenario::ConfigError{0, reason} : error;
        return false;
    };
    float firstThink{};
    const auto run = [&]() -> bool
    {
        if (!g_engfuncs.pfnSetModel || !g_engfuncs.pfnSetSize || !g_engfuncs.pfnGetModelPtr ||
            !g_engfuncs.pfnSetOrigin || !g_engfuncs.pfnRandomFloat)
            return step(false, "scenario spawn engine services are unavailable");
        auto initial = monster->RetainBehavior();
        struct AttributeContext
        {
            CCSOLMonsterFactoryServices& services;
            CSOLMonsterSpawnCall& call;
        } attributes{m_services, call};
        if (!step(m_models.Apply(*monster, delta, state.attributes, error, [](const void* context)
            { return static_cast<const CSOLMonsterSpawnCall*>(context)->SameSession(); }, &call,
            [](void* context, csol::scenario::ConfigError& status)
            {
                auto& value = *static_cast<AttributeContext*>(context);
                return value.call.Current() && value.services.AttributesApplied(value.call, status);
            }, &attributes),
            "scenario spawn attributes/model/bounds failed")) return false;
        if (died()) return true;
        if (!step(m_occupancy.ApplyPriority(*call.Current()), "scenario spawn priority registration failed")) return false;
        if (died()) return true;
        if (!step(ResolveSequences(call, type, state.sequences), "scenario spawn sequence cache failed")) return false;
        if (died()) return true;
        call.Current()->pev->weaponmodel = 0;
        auto previous = std::dynamic_pointer_cast<CCSOLMonsterSpawnBehavior>(call.Current()->RetainBehavior());
        const bool differentCategory = previousType &&
            (previousType->appearance == "elite_forceC" || previousType->appearance == "elite_forceT" ||
             previousType->id == 32800 || previousType->id == 32908);
        bool recreate = type.skill >= 1000 || !previous || differentCategory;
        if (!recreate)
        {
            recreate = previous->RecreateForSpawn();
            if (!step(true, "scenario changed while selecting spawn behavior")) return false;
            if (died()) return true;
        }
        if (recreate)
        {
            auto behavior = type.id == 32800 ? m_services.CreateElite(call, eliteFollower, error) :
                type.skill >= 1000 ? m_services.CreateBoss(call, type.id, error) :
                m_services.CreateOrdinary(call, error);
            if (!step(behavior != nullptr, "scenario concrete behavior factory failed")) return false;
            if (died()) return true;
            if (!step(call.Current()->InstallBehavior(std::move(behavior)), "scenario behavior installation failed")) return false;
            if (died()) return true;
        }
        auto behavior = std::dynamic_pointer_cast<CCSOLMonsterSpawnBehavior>(call.Current()->RetainBehavior());
        if (!step(behavior != nullptr, "scenario spawn behavior is unavailable")) return false;
        if (!step(behavior->ResetForSpawn(call, state, error), "scenario behavior reset failed")) return false;
        if (died()) return true;
        call.Current()->m_behaviorReady = true;
        call.Current()->pev->takedamage = DAMAGE_YES;
        if (type.priority != 0 && !step(FireSpawnTargets(call, type.id), "scenario spawn target failed")) return false;
        if (died()) return true;
        auto* current = call.Current();
        current->pev->rendercolor = Vector(type.color.data());
        current->pev->rendermode = static_cast<int>(type.column21[0]);
        current->pev->renderfx = static_cast<int>(type.column21[1]);
        current->pev->renderamt = type.column21[2];
        current->HostState().nonlethalDamage = 0;
        current->HostState().damageScaleCount = 0;
        current->HostState().damageScale = 1.0f;
        if (!step(m_services.ClearSpawnEffects(call, error), "scenario spawn effect cleanup failed")) return false;
        if (died()) return true;
        if (!step(CSOL_ClearMonsterSpeedLimits(call, m_messages), "scenario spawn speed effect cleanup failed")) return false;
        if (died()) return true;
        call.Current()->HostState().statusUpdateAt = 0;
        SET_ORIGIN(call.Current()->edict(), location.origin.data());
        if (!step(true, "scenario changed during spawn origin placement")) return false;
        if (died()) return true;
        call.Current()->pev->angles = Vector(location.angles.data());
        call.Current()->SetThink(&CCSOLScenarioMonster::BehaviorThink);
        const auto randomDeadline = [&](float& output)
        {
            const float now = gpGlobals->time;
            const float offset = RANDOM_FLOAT(0.1f, 0.2f);
            if (!step(true, "scenario changed during initial timer RNG") || !gpGlobals) return false;
            output = now + offset;
            return step(std::isfinite(output), "scenario initial deadline is not finite");
        };
        if (!randomDeadline(firstThink)) return false;
        if (died()) return true;
        call.Current()->pev->nextthink = firstThink;
        if (!randomDeadline(state.updateAt)) return false;
        if (died()) return true;
        if (!randomDeadline(state.animationAt)) return false;
        if (died()) return true;
        state.ambientAt = (gpGlobals->time + 7.0f) + static_cast<float>(std::rand() % 15);
        current = call.Current();
        current->HostState().activeSkill = 0;
        current->pev->sequence = 1;
        current->pev->frame = 0;
        current->pev->framerate = 1;
        current->Animation().SynchronizeSequence(1);
        const bool animationReset = CSOL_ResetMonsterSequenceInfo(call, [](const void* context)
        {
            const auto* current = static_cast<const CSOLMonsterSpawnCall*>(context)->Current();
            return current && current->pev->deadflag != DEAD_DEAD;
        }, &call);
        if (died()) return true;
        if (!step(animationReset, "scenario guarded animation reset failed")) return false;
        if (!step(true, "scenario changed during initial animation setup")) return false;
        call.Current()->HostState().transformed = false;
        if (useRecordIdentity) call.Current()->pev->body = 1;
        state.rowArgument = static_cast<std::uint8_t>(rowArgument);
        if (!step(m_occupancy.Register(*call.Current(), true), "scenario spawn occupancy registration failed")) return false;
        if (died()) return true;
        if (!step(m_services.ResolveSpawnOverlap(call, error), "scenario spawn overlap resolution failed")) return false;
        if (call.Current()->pev->health > 0)
        {
            // MP 1138594A..11385982: use the CURRENT type, after overlap
            // callbacks. Failed/skipped creation must not enable progress.
            if (!step(m_map.EnableMonsterClearPoint(call.Current()->Type()->clearPoint),
                "scenario spawn clear-point activation failed")) return false;
            call.Current()->HostState().navigationExpired = false;
            InitializingScope rulesSpawn(m_ruleSpawnState, state);
            if (!step(m_services.RulesMonsterSpawned(call, error), "scenario spawn rules callback failed")) return false;
        }
        return true;
    };
    bool completed{};
    try { completed = run(); }
    catch (...)
    {
        if (call.SameSession()) Fail("scenario monster creation threw an exception");
    }
    // Behavior releases can run callbacks. Resolve only after their scope ends.
    if (!call.SameSession()) { call.ReturnIncomplete(); return Result::Cancelled; }
    if (completed && valid() && m_error.message.empty())
    {
        // Targets can kill this same lease. The original still returns it and
        // only skips the positive-health rules callback; do not reject it here
        // or restore damage after a legitimate death changed takedamage.
        if (created) *created = call.Current();
        return Result::Spawned;
    }
    // Never roll back attempted-spawn point quota; only return this pool lease.
    call.ReturnIncomplete();
    if (m_error.message.empty()) Fail("scenario monster creation was invalidated");
    return Result::Failed;
}
