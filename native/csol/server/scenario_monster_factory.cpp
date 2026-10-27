// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_factory.h"

#include <algorithm>
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
    CSOLMonsterSpawnCall& spawn;
    CBaseAnimating* expected;
    SequenceResetCall* previous;
    bool (*ownerValid)(const void*);
    const void* ownerContext;
    bool valid{true};
    static thread_local SequenceResetCall* active;
    SequenceResetCall(CSOLMonsterSpawnCall& call, CBaseAnimating* entity,
        bool (*guard)(const void*), const void* context)
        : spawn(call), expected(entity), previous(active), ownerValid(guard), ownerContext(context) { active = this; }
    ~SequenceResetCall() { active = previous; }
    CCSOLScenarioMonster* Current() const
    {
        return (!ownerValid || ownerValid(ownerContext)) ? spawn.Current() : nullptr;
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
    SequenceResetCall sequenceReset(call, current, ownerValid, ownerContext);
#ifdef REGAMEDLL_API
    g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(
        &CBaseAnimating::CSOL_ResetSpawnSequenceInfo, current);
#else
    current->CSOL_ResetSpawnSequenceInfo();
#endif
    return sequenceReset.valid && sequenceReset.Current();
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
    const CCSOLMonsterBehavior* expectedBehavior)
    : m_map(map), m_pool(pool), m_deaths(deaths), m_mapGeneration(map.SpawnPoints().Generation()),
      m_roundGeneration(map.RoundGeneration()), m_deathGeneration(deaths.Generation()),
      m_lease(pool.Lease(monster)), m_slot(monster.Slot()), m_entity(&monster),
      m_expectedBehavior(expectedBehavior)
{
}

bool CSOLMonsterSpawnCall::SameSession() const
{
    return m_mapGeneration == m_map.SpawnPoints().Generation() &&
        m_roundGeneration == m_map.RoundGeneration() && m_deathGeneration == m_deaths.Generation() &&
        m_map.State() == CSOLScenarioMapState::RoundActive;
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
    if (m_mapGeneration != m_map.SpawnPoints().Generation() ||
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
    m_error = {};
    return true;
}

bool CCSOLMonsterFactory::ScheduleReplacement(CSOLMonsterReplacement replacement)
{
    if (m_busy || !m_error.message.empty()) return false;
    if (!std::isfinite(replacement.start) || !std::isfinite(replacement.end) ||
        !std::isfinite(replacement.interval) || !std::isfinite(replacement.next) ||
        replacement.type.priority < 0 || replacement.type.priority > 2)
        return Fail("scenario replacement schedule is invalid");
    m_replacements.push_back(std::move(replacement));
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
    if (m_busy || !m_error.message.empty()) return Result::Failed;
    FactoryOperation operation(m_busy);
    if (m_map.State() != CSOLScenarioMapState::RoundActive || !m_map.Tables() ||
        m_map.Tables()->scenarioType != 1 || m_map.Tables()->mapName != "zs_deadend" ||
        !m_map.RoundIndex() || m_pool.State() != CSOLMonsterPoolState::Ready || !gpGlobals)
    {
        Fail("scenario factory requires the active zs_deadend subtype1 round");
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
        const float now = gpGlobals->time;
        for (auto& entry : m_replacements)
        {
            if (!(now > entry.start && entry.end > now && now > entry.next && entry.remaining > 0)) continue;
            const float next = now + entry.interval;
            if (!std::isfinite(next)) { Fail("scenario replacement deadline is not finite"); return Result::Failed; }
            entry.next = next;
            const auto result = Create(team, requestedType, point, rowArgument, location, entry.type,
                entry.useRecordIdentity, delta);
            if (result == Result::Spawned) { --entry.remaining; return result; }
            if (result == Result::Failed || result == Result::Cancelled) return result;
            // Ordinary inability to allocate keeps the new deadline/count,
            // falls back to the original type, and never tries another entry.
            break;
        }
    }
    return Create(team, requestedType, point, rowArgument, location, *type, false, delta);
}

bool CCSOLMonsterFactory::ResolveSequences(CSOLMonsterSpawnCall& call,
    const csol::scenario::MonsterType& type, csol::scenario::MonsterSequences& output)
{
    if (const auto found = m_sequences.find(&type); found != m_sequences.end())
    {
        output = found->second;
        return true;
    }
    struct Lookup
    {
        CSOLMonsterSpawnCall& call;
        bool valid{true};
    } lookup{call};
    csol::scenario::MonsterSequences sequences;
    const bool resolved = csol::scenario::ResolveMonsterSequences([](void* context, const char* name)
    {
        auto& state = *static_cast<Lookup*>(context);
        auto* monster = state.call.Current();
        if (!state.valid || !monster) { state.valid = false; return -1; }
        void* model = GET_MODEL_PTR(monster->edict());
        if (!state.call.Current()) { state.valid = false; return -1; }
        return ::LookupSequence(model, name);
    }, &lookup, sequences);
    if (!resolved || !lookup.valid || !call.Current()) return false;
    // The cache is specific to this actual type record and this loaded map.
    m_sequences.emplace(&type, sequences);
    output = sequences;
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
    bool useRecordIdentity, const csol::scenario::RoundMonsterDeltas& delta)
{
    using Result = CSOLMonsterSpawnResult;
    // Category2/4/5 and other maps have separate behavior contracts. Do not
    // install the ordinary class for one of them merely to make it visible.
    if (type.appearance == "elite_forceC" || type.appearance == "elite_forceT" ||
        type.id == 32800 || type.id == 32908 || !Finite(location.origin) || !Finite(location.angles) ||
        !Finite(type.color) || !RenderInteger(type.column21[0]) ||
        !RenderInteger(type.column21[1]) || !std::isfinite(type.column21[2]))
    {
        Fail("scenario spawn type needs an unavailable behavior category or has invalid render data");
        return Result::Failed;
    }
    const csol::scenario::MonsterType* previousType{};
    const auto mapGeneration = m_map.SpawnPoints().Generation();
    const auto roundGeneration = m_map.RoundGeneration();
    const auto deathGeneration = m_deaths.Generation();
    const auto sameSession = [&]
    {
        return mapGeneration == m_map.SpawnPoints().Generation() &&
            roundGeneration == m_map.RoundGeneration() && deathGeneration == m_deaths.Generation() &&
            m_map.State() == CSOLScenarioMapState::RoundActive;
    };
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
    if (!monster)
    {
        if (m_pool.State() == CSOLMonsterPoolState::Failed)
        { Fail(m_pool.Error().c_str()); return Result::Failed; }
        return Result::Skipped;
    }
    CSOLMonsterSpawnCall call(m_map, m_pool, m_deaths, *monster);
    m_states[monster->Slot()] = spawnState;
    auto& state = *spawnState;
    state.lease = m_pool.Lease(*monster);
    state.skill = type.skill;
    monster->pev->team = team;
    monster->m_typeId = useRecordIdentity || type.column20 > 0 ? type.id : requestedType;
    csol::scenario::ConfigError error;
    const auto valid = [&] { return call.Current() != nullptr; };
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
        if (!step(m_models.Apply(*monster, delta, state.attributes, error, [](const void* context)
            { return static_cast<const CSOLMonsterSpawnCall*>(context)->SameSession(); }, &call),
            "scenario spawn attributes/model/bounds failed")) return false;
        if (!step(m_occupancy.ApplyPriority(*call.Current()), "scenario spawn priority registration failed")) return false;
        if (!step(ResolveSequences(call, type, state.sequences), "scenario spawn sequence cache failed")) return false;
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
        }
        if (recreate)
        {
            auto behavior = type.skill >= 1000 ? m_services.CreateBoss(call, type.id, error) :
                m_services.CreateOrdinary(call, error);
            if (!step(behavior != nullptr, "scenario concrete behavior factory failed")) return false;
            if (!step(call.Current()->InstallBehavior(std::move(behavior)), "scenario behavior installation failed")) return false;
        }
        auto behavior = std::dynamic_pointer_cast<CCSOLMonsterSpawnBehavior>(call.Current()->RetainBehavior());
        if (!step(behavior != nullptr, "scenario spawn behavior is unavailable")) return false;
        if (!step(behavior->ResetForSpawn(call, state, error), "scenario behavior reset failed")) return false;
        call.Current()->m_behaviorReady = true;
        call.Current()->pev->takedamage = DAMAGE_YES;
        if (type.priority != 0 && !step(FireSpawnTargets(call, type.id), "scenario spawn target failed")) return false;
        auto* current = call.Current();
        current->pev->rendercolor = Vector(type.color.data());
        current->pev->rendermode = static_cast<int>(type.column21[0]);
        current->pev->renderfx = static_cast<int>(type.column21[1]);
        current->pev->renderamt = type.column21[2];
        current->HostState().nonlethalDamage = 0;
        current->HostState().damageScaleCount = 0;
        current->HostState().damageScale = 1.0f;
        if (!step(m_services.ClearSpawnEffects(call, error), "scenario spawn effect cleanup failed")) return false;
        if (!step(CSOL_ClearMonsterSpeedLimits(call, m_messages), "scenario spawn speed effect cleanup failed")) return false;
        call.Current()->HostState().statusUpdateAt = 0;
        SET_ORIGIN(call.Current()->edict(), location.origin.data());
        if (!step(true, "scenario changed during spawn origin placement")) return false;
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
        call.Current()->pev->nextthink = firstThink;
        if (!randomDeadline(state.updateAt) ||
            !randomDeadline(state.animationAt)) return false;
        state.ambientAt = (gpGlobals->time + 7.0f) + static_cast<float>(std::rand() % 15);
        current = call.Current();
        current->HostState().activeSkill = 0;
        current->pev->sequence = 1;
        current->pev->frame = 0;
        current->pev->framerate = 1;
        current->Animation().SynchronizeSequence(1);
        if (!step(CSOL_ResetMonsterSequenceInfo(call), "scenario guarded animation reset failed")) return false;
        if (!step(true, "scenario changed during initial animation setup")) return false;
        if (useRecordIdentity) call.Current()->pev->body = 1;
        state.rowArgument = static_cast<std::uint8_t>(rowArgument);
        if (!step(m_occupancy.Register(*call.Current(), true), "scenario spawn occupancy registration failed")) return false;
        if (!step(m_services.ResolveSpawnOverlap(call, error), "scenario spawn overlap resolution failed")) return false;
        if (call.Current()->pev->health > 0)
        {
            call.Current()->HostState().navigationExpired = false;
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
        return Result::Spawned;
    }
    // Never roll back attempted-spawn point quota; only return this pool lease.
    call.ReturnIncomplete();
    if (m_error.message.empty()) Fail("scenario monster creation was invalidated");
    return Result::Failed;
}
