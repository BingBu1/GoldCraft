// SPDX-License-Identifier: GPL-3.0-or-later
#include <bit>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include "pool_fixture.hpp"

namespace
{
int checks{}, failures{};
void Check(bool value, const char* text)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << text << '\n'; } }
bool Parked(const CCSOLScenarioMonster& monster)
{
    return (monster.pev->effects & EF_NODRAW) && monster.pev->solid == SOLID_NOT &&
        monster.pev->movetype == MOVETYPE_NONE && monster.pev->health == 0 &&
        monster.pev->deadflag == DEAD_DEAD && !monster.m_pfnThink && !monster.m_pfnTouch;
}
void CheckCounts(const PoolFixture& fixture, std::size_t total, std::size_t p0, std::size_t p1, std::size_t p2)
{
    Check(fixture.pool.ActiveCount() == total && fixture.pool.AvailableCount() + total == 45,
        "borrowed plus idle slots remain exactly forty-five");
    Check(fixture.pool.CategoryCount(0) == p0 && fixture.pool.CategoryCount(1) == p1 &&
        fixture.pool.CategoryCount(2) == p2 && fixture.pool.CategoryCount(3) == 0,
        "priority counters match borrowed slots independently of category thresholds");
}
void Initialization()
{
    PoolFixture fixture(false);
    Check(fixture.pool.State() == CSOLMonsterPoolState::Empty && !fixture.pool.At(0), "pool begins empty");
    Check(fixture.pool.Initialize() && fixture.pool.State() == CSOLMonsterPoolState::Ready, "real exported monster objects initialize pool");
    Check(fixture.engine.creates == 45 && fixture.engine.exports == 45 && fixture.engine.dispatches == 45 &&
        fixture.engine.precaches.size() == 45 && fixture.engine.modelCalls.size() == 45,
        "exactly forty-five creations exports and initial Spawn calls, without double Spawn");
    std::set<const edict_t*> edicts;
    for (std::size_t i = 0; i < 45; ++i)
    {
        auto* monster = fixture.pool.At(i);
        Check(monster && monster->Slot() == i && monster->Pool() == &fixture.pool && !fixture.pool.Borrowed(*monster),
            "slot holds exact real SDK subclass with correct owner identity");
        if (!monster) continue;
        edicts.insert(monster->edict());
        Check(Parked(*monster) && !monster->Type() && !monster->Behavior(), "initialized entity is parked without type or invented behavior");
        Check(monster->Classify() == 17 && monster->pev->takedamage == DAMAGE_NO && monster->pev->skin == 0 &&
            monster->pev->gravity == 1 && monster->pev->max_health == 100 && monster->pev->view_ofs == Vector(0, 0, 12),
            "initial Spawn sets verified default state with explicit inactive damage adaptation");
        Check(monster->pev->velocity == g_vecZero && fixture.engine.modelCalls[i].edict == monster->edict() &&
            fixture.engine.modelCalls[i].path == "models/player/zombi_origin/zombi_origin.mdl",
            "initial model callback and velocity use actual production Spawn");
    }
    Check(edicts.size() == 45 && !fixture.pool.At(45), "all slot edicts are distinct and out-of-range lookup fails");
    Check(!fixture.pool.Initialize() && fixture.engine.creates == 45, "duplicate Initialize cannot append another pool");
    CheckCounts(fixture, 0, 0, 0, 0);
    Check(fixture.pool.Shutdown() && fixture.pool.State() == CSOLMonsterPoolState::Empty && fixture.engine.removes == 45,
        "Shutdown marks all owned live edicts for removal and clears pool");
    Check(fixture.pool.Shutdown() && fixture.engine.removes == 45, "repeat Shutdown does not remove twice");
}
void AllocationAndCapacity()
{
    {
        PoolFixture fixture;
        for (std::size_t i = 0; i < 39; ++i)
        {
            auto* monster = fixture.Acquire(0);
            Check(monster && monster->Slot() == i && monster->Type() == &fixture.types[0] && monster->TypeId() == 1,
                "ordinary acquisition follows initial FIFO and exact type identity");
            Check(monster && fixture.pool.Borrowed(*monster) && monster->pev->team == 1 &&
                monster->pev->takedamage == DAMAGE_NO && !monster->m_pfnThink && !monster->m_pfnTouch,
                "low-level acquisition remains inert until real factory installation");
        }
        Check(!fixture.Acquire(0), "ordinary category hard-rejects fortieth slot while idle slots remain");
        CheckCounts(fixture, 39, 39, 0, 0);
        for (std::size_t i = 39; i < 45; ++i) Check(fixture.Acquire(1)->Slot() == i, "priority one passes nominal four threshold until total pool full");
        CheckCounts(fixture, 45, 39, 6, 0);
        Check(fixture.engine.creates == 45 && fixture.engine.dispatches == 45 && fixture.engine.modelCalls.size() == 45,
            "acquisition never allocates edicts or repeats Spawn/model setup");
        Check(fixture.points.FindMonsterPoint(1, 7)->consumed == 0, "low-level Acquire does not secretly perform upstream quota consumption");
    }
    {
        PoolFixture fixture;
        for (std::size_t i = 0; i < 45; ++i) Check(fixture.Acquire(2)->Slot() == i, "priority two passes nominal two threshold through forty-five slots");
        CheckCounts(fixture, 45, 0, 0, 45);
        Check(!fixture.Acquire(2) && !fixture.Acquire(1) && !fixture.Acquire(0),
            "full pool without ordinary eviction candidate refuses every priority");
        CheckCounts(fixture, 45, 0, 0, 45);
    }
    for (std::int8_t priority : {std::int8_t(-1), std::int8_t(3)})
    {
        PoolFixture fixture; fixture.types[0].priority = priority;
        Check(!fixture.Acquire(0) && fixture.pool.State() == CSOLMonsterPoolState::Failed && !fixture.pool.Error().empty(),
            "unsupported priority fails before indexed counters or queue writes");
        CheckCounts(fixture, 0, 0, 0, 0);
    }
}
void FlagFields()
{
    PoolFixture fixture(false);
    fixture.engine.initialFlags = FL_FLY | FL_ONGROUND;
    fixture.engine.initialSpawnFlags = 0x1357;
    Check(fixture.pool.Initialize(), "flag-field fixture initializes");
    auto* initial = fixture.pool.At(0);
    Check(initial && initial->pev->flags == (FL_FLY | FL_ONGROUND | FL_MONSTER) &&
        initial->pev->spawnflags == 0x1357,
        "initial Spawn ORs FL_MONSTER into ordinary flags and leaves spawnflags independent");
    auto* borrowed = fixture.Acquire(0);
    Check(borrowed == initial && borrowed->pev->flags == FL_MONSTER && borrowed->pev->spawnflags == 0x1357,
        "pool reuse assigns ordinary flags FL_MONSTER without mapping CSO flags_mp into them");
    fixture.Fill(38); borrowed->pev->flags |= FL_FLY | FL_ONGROUND;
    fixture.pool.Free(*borrowed);
    Check(fixture.Acquire(2) == borrowed && borrowed->pev->flags == FL_MONSTER && borrowed->pev->spawnflags == 0x1357,
        "reuse of a previous behavior's slot clears ordinary transient flags only");
}
void SolidQuery()
{
    PoolFixture fixture; auto* monster = fixture.Acquire(0);
    Check(fixture.pool.HasSolidMonster(1) && !fixture.pool.HasSolidMonster(2), "query matches real pool entity type and solid");
    monster->pev->health = 0; monster->pev->deadflag = DEAD_DEAD; monster->pev->effects |= EF_NODRAW;
    Check(fixture.pool.HasSolidMonster(1), "event query adds no health deadflag or NODRAW filter");
    monster->pev->solid = SOLID_TRIGGER;
    Check(fixture.pool.HasSolidMonster(1), "any nonzero solid qualifies, not only slidebox");
    monster->pev->solid = SOLID_NOT;
    Check(!fixture.pool.HasSolidMonster(1), "zero solid suppresses immediately before pool return");
    auto* other = fixture.Acquire(0);
    Check(other && fixture.pool.HasSolidMonster(1), "second same-type solid entity keeps query true");
    Check(fixture.pool.Free(*other) && !fixture.pool.HasSolidMonster(1), "last solid instance removal suppresses query");
    fixture.pool.Free(*monster);
    Check(monster->TypeId() == 1 && !fixture.pool.HasSolidMonster(1), "parked residual type does not count");
    const int classname = Engine::AllocString("monster_entity");
    auto* unregistered = PoolEngine::CreatePoolEntity(classname); DispatchSpawn(unregistered);
    auto* extra = static_cast<CCSOLScenarioMonster*>(unregistered->pvPrivateData);
    Check(extra->pev->solid != 0 && !fixture.pool.HasSolidMonster(0), "same-class unregistered entity is not part of pool query");
}
void DeathAndFree()
{
    PoolFixture fixture; fixture.points.ConsumePointQuota(1, 7);
    auto* monster = fixture.Acquire(0); monster->pev->nextthink = 6;
    const auto* type = monster->Type();
    Check(fixture.pool.BeginDeath(*monster, 12, fixture.points), "death begins for a borrowed ordinary monster");
    Check(monster->pev->solid == SOLID_NOT && monster->pev->deadflag == DEAD_DEAD &&
        monster->pev->takedamage == DAMAGE_NO && !(monster->pev->effects & EF_NODRAW) &&
        monster->pev->nextthink == 12 && monster->m_pfnThink && !fixture.pool.HasSolidMonster(1),
        "death removes solid immediately and schedules real delayed FreeThink");
    CheckCounts(fixture, 1, 1, 0, 0);
    Check(fixture.points.FindMonsterPoint(1, 7)->consumed == 0 && !fixture.pool.BeginDeath(*monster, 13, fixture.points),
        "completed death return occurs once and repeated death is rejected");
    fixture.engine.globals.time = 12; monster->Think();
    Check(Parked(*monster) && monster->Type() == type && monster->TypeId() == 1 && monster->pev->nextthink == 12,
        "real SDK Think invokes Free preserving type and historical nextthink");
    CheckCounts(fixture, 0, 0, 0, 0);
    Check(fixture.points.FindMonsterPoint(1, 7)->consumed == 0 && fixture.pool.Free(*monster),
        "Free and repeated Free do not return quota again");
    CheckCounts(fixture, 0, 0, 0, 0);
    auto* boss = fixture.Acquire(2);
    Check(fixture.pool.BeginDeath(*boss, 15, fixture.points) && fixture.points.FindMonsterPoint(1, 7)->consumed == 0,
        "priority two death bypasses point quota return"); boss->Think();
    auto* unlimited = fixture.Acquire(0); fixture.points.RegisterMonsterPoint({{}, {}, 1, 7, 77}, 0);
    Check(fixture.pool.BeginDeath(*unlimited, 16, fixture.points) && fixture.points.FindMonsterPoint(1, 7)->consumed == 0xffffffffu,
        "unlimited point death still performs original decrement from zero");
    for (const float time : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        auto* fresh = fixture.Acquire(1);
        const auto count = fixture.points.FindMonsterPoint(1, 7)->consumed;
        Check(!fixture.pool.BeginDeath(*fresh, time, fixture.points) && fresh->pev->deadflag == DEAD_NO &&
            fixture.points.FindMonsterPoint(1, 7)->consumed == count, "invalid death time preserves state and quota");
    }
}
void EvictionAndRestart()
{
    {
        PoolFixture fixture; fixture.Fill();
        auto* dying = fixture.pool.At(0);
        Check(fixture.pool.BeginDeath(*dying, 9, fixture.points), "ordinary first slot is dying while pool remains full");
        Check(fixture.points.FindMonsterPoint(1, 7)->consumed == 0xffffffffu, "first death decrements original attempted-spawn counter");
        auto* replacement = fixture.Acquire(2);
        Check(replacement == dying && replacement->Slot() == 0 && replacement->TypeId() == 30200 &&
            !replacement->m_pfnThink && replacement->pev->takedamage == DAMAGE_NO,
            "full pool selects first ordinary slot even when dying, then reuses it inertly");
        Check(fixture.points.FindMonsterPoint(1, 7)->consumed == 0xfffffffeu,
            "original eviction path returns dying ordinary quota a second time");
        CheckCounts(fixture, 45, 38, 6, 1);
    }
    {
        PoolFixture fixture; fixture.Fill();
        const std::array<std::size_t, 2> protectedSlots{0, 1};
        Check(fixture.Acquire(2, 7, protectedSlots)->Slot() == 2, "protected slots skip to first unprotected ordinary slot");
        std::array<std::size_t, 45> all{};
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
        const auto quota = fixture.points.FindMonsterPoint(1, 7)->consumed;
        Check(!fixture.Acquire(1, 7, all) && fixture.points.FindMonsterPoint(1, 7)->consumed == quota,
            "all candidates protected refuses without untracked edict or victim quota change");
        CheckCounts(fixture, 45, 38, 6, 1);
    }
    {
        BehaviorStats stats; PoolFixture fixture; fixture.Fill();
        auto* seventh = fixture.pool.At(7); auto* second = fixture.pool.At(2);
        fixture.pool.Free(*seventh); fixture.pool.Free(*second);
        auto* dying = fixture.pool.At(0); fixture.pool.BeginDeath(*dying, 123, fixture.points);
        auto* probe = fixture.pool.At(1); Check(probe->InstallBehavior(std::make_unique<BehaviorProbe>(stats)), "install ownership probe before formal restart");
        for (std::size_t i = 0; i < 45; ++i) fixture.pool.At(i)->SetTouch(&CBaseEntity::Blocked);
        Check(fixture.pool.RestartRound(), "formal restart returns all remaining borrowed slots");
        CheckCounts(fixture, 0, 0, 0, 0);
        for (std::size_t i = 0; i < 45; ++i)
            Check(Parked(*fixture.pool.At(i)) && !fixture.pool.At(i)->Type(), "restart parks all and clears type Think and Touch");
        Check(dying->pev->nextthink == 123 && !dying->m_pfnThink && stats.kills == 0 && stats.destroyed == 0 && probe->Behavior(),
            "restart cancels pending Free by Think pointer, preserves behavior and emits no simulated death");
        dying->Think(); CheckCounts(fixture, 0, 0, 0, 0);
        Check(fixture.pool.RestartRound(), "repeat restart does not append parked slots twice");
        std::vector<std::size_t> wanted{7, 2};
        for (std::size_t i = 0; i < 45; ++i) if (i != 7 && i != 2) wanted.push_back(i);
        for (const auto slot : wanted) Check(fixture.Acquire(2)->Slot() == slot, "formal restart preserves old idle FIFO then appends borrowed slots in index order");
        CheckCounts(fixture, 45, 0, 0, 45);
        Check(fixture.engine.creates == 45 && fixture.engine.dispatches == 45, "restart and reuse never reinitialize engine objects");
    }
}
void FailureAndIdentity()
{
    for (const int at : {1, 7, 45})
    for (int fault = 0; fault < 10; ++fault)
    {
        PoolFixture fixture(false); fixture.engine.failAt = at;
        if (fault == 0) fixture.engine.createFault = Engine::CreateFailure::Null;
        else if (fault == 1) fixture.engine.createFault = Engine::CreateFailure::PrivateMissing;
        else if (fault == 2) fixture.engine.wrongPrivate = true;
        else fixture.engine.spawnFault = static_cast<Engine::SpawnFailure>(fault - 2);
        Check(!fixture.pool.Initialize() && fixture.pool.State() == CSOLMonsterPoolState::Failed && !fixture.pool.Error().empty(),
            "allocation or Spawn fault cannot publish a partial Ready pool");
        Check(fixture.pool.ActiveCount() == 0 && fixture.pool.AvailableCount() == 0 && !fixture.pool.At(0),
            "partial initialization failure clears previous pool records and queue");
        Check(!fixture.pool.Initialize(), "failed pool needs explicit shutdown before retry");
        fixture.engine.failAt = 0; fixture.engine.createFault = {}; fixture.engine.spawnFault = {}; fixture.engine.wrongPrivate = false;
        Check(fixture.pool.Shutdown() && fixture.pool.Initialize() && fixture.pool.AvailableCount() == 45,
            "explicit shutdown enables clean retry with forty-five newly owned slots");
    }
    for (int invalidation = 0; invalidation < 5; ++invalidation)
    {
        PoolFixture fixture; auto* monster = fixture.Acquire(0); auto* edict = monster->edict();
        if (invalidation == 0) ++edict->serialnumber;
        if (invalidation == 1) edict->free = TRUE;
        if (invalidation == 2) edict->v.flags |= FL_KILLME;
        if (invalidation == 3) edict->v.classname = Engine::AllocString("other_class");
        if (invalidation == 4) fixture.engine.ReplaceWithBase(edict);
        Check(!fixture.pool.At(0) && !fixture.pool.HasSolidMonster(1), "stale serial free kill classname or private type cannot pass At/event query");
        Check(!fixture.Acquire(1) && fixture.pool.State() == CSOLMonsterPoolState::Failed,
            "allocation validates all pool handles before queue mutation");
    }
}
void BehaviorOwnership()
{
    BehaviorStats first, second;
    PoolFixture fixture; auto* monster = fixture.Acquire(0);
    Check(!monster->InstallBehavior(nullptr) && monster->InstallBehavior(std::make_unique<BehaviorProbe>(first)),
        "behavior requires nonnull implementation on borrowed entity");
    Check(monster->pev->takedamage == DAMAGE_NO && !monster->m_pfnThink && !monster->m_pfnTouch,
        "installing ownership probe does not enable attacks or schedule fake AI");
    Check(!monster->TakeDamage(nullptr, nullptr, 1, 0) && !first.damageCalls, "inert acquisition refuses damage dispatch");
    monster->pev->takedamage = DAMAGE_AIM;
    Check(monster->TakeDamage(nullptr, nullptr, 1, 0) && first.damageCalls == 1, "explicitly enabled dispatch reaches installed probe only");
    monster->SetThink(&CCSOLScenarioMonster::BehaviorThink); monster->Think();
    Check(first.thinks == 1, "real SDK Think forwards to installed ownership probe");
    monster->Killed(nullptr, 0); Check(first.kills == 1, "Killed forwarding alone does not invent death rewards");
    Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(second)) && first.destroyed == 1,
        "behavior replacement destroys previous owned implementation once");
    fixture.pool.Free(*monster); monster->BehaviorThink(); monster->Killed(nullptr, 0);
    Check(!second.thinks && !second.kills && !monster->InstallBehavior(std::make_unique<BehaviorProbe>(first)),
        "parked behavior remains retained but inactive and cannot be reinstalled");
    Check(first.destroyed == 2 && second.destroyed == 0, "rejected candidate ownership is released but parked implementation stays");
    auto* edict = monster->edict(); const auto slot = monster->Slot(); fixture.engine.DestroyPrivate(edict);
    Check(second.destroyed == 1 && fixture.pool.State() == CSOLMonsterPoolState::Failed && !fixture.pool.At(slot),
        "real OnDestroy detaches owner and releases retained implementation exactly once");
}
void LifecycleReentryGaps()
{
    {
        BehaviorStats stats; PoolFixture fixture; auto* monster = fixture.Acquire(0); auto* edict = monster->edict();
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats)), "seed behavior for destructor private replacement");
        CBaseEntity* replacement{};
        stats.onDestroy = [&] { replacement = &fixture.engine.ReplaceWithBase(edict); };
        Check(fixture.pool.Shutdown() && stats.destroyed == 1, "shutdown releases installed behavior");
        stats.onDestroy = {};
        Check(replacement && !(replacement->pev->flags & FL_KILLME),
            "shutdown never removes a same-edict replacement installed by behavior destructor");
    }
    {
        BehaviorStats oldStats, newStats; PoolFixture fixture; auto* monster = fixture.Acquire(0);
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(oldStats)), "seed behavior for replacement reentry");
        bool nestedShutdown = true;
        oldStats.onDestroy = [&] { nestedShutdown = fixture.pool.Shutdown(); };
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(newStats)), "replacement ownership succeeds under guarded destruction");
        oldStats.onDestroy = {};
        Check(!nestedShutdown && fixture.pool.State() == CSOLMonsterPoolState::Ready && fixture.pool.Borrowed(*monster),
            "old behavior destructor cannot shut down pool during installation");
        Check(oldStats.destroyed == 1 && !newStats.destroyed && monster->Behavior(),
            "replacement remains owned exactly once after rejected destructive reentry");
    }
    {
        BehaviorStats stats; PoolFixture fixture; auto* monster = fixture.Acquire(0);
        monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats));
        monster->pev->flags |= FL_KILLME;
        Check(fixture.pool.Shutdown(), "shutdown handles entity already marked for deferred removal");
        Check(!monster->Pool() && !monster->Behavior() && stats.destroyed == 1,
            "pending-remove entity is detached and releases behavior before pool owner expires");
    }
}
void DestructionAndForwardingGuards()
{
    for (const bool duringPrecache : {false, true})
    for (const int action : {0, 1, 2})
    {
        PoolFixture fixture(false);
        auto invalidate = [&]
        {
            if (fixture.engine.creates != 7) return;
            auto* current = fixture.engine.edicts.back().get();
            if (action == 0) fixture.engine.DestroyPrivate(current);
            else if (action == 1) fixture.engine.ReplaceWithBase(current);
            else current->v.flags |= FL_KILLME;
        };
        if (duringPrecache) fixture.engine.onPrecache = invalidate;
        else fixture.engine.onModel = [&](edict_t*) { invalidate(); };
        Check(!fixture.pool.Initialize() && fixture.pool.State() == CSOLMonsterPoolState::Failed &&
            !fixture.pool.At(0) && fixture.pool.AvailableCount() == 0,
            "precache or model callback can destroy replace or retire its current entity without stale post-callback writes");
        Check(fixture.engine.modelCalls.size() == (duringPrecache ? 6u : 7u),
            "failed precache identity stops before model setup; model callback is the last Spawn operation");
    }
    for (const int allocation : {2, 7, 45})
    for (const bool realDestroy : {false, true})
    {
        PoolFixture fixture(false);
        fixture.engine.onModel = [&](edict_t*)
        {
            if (fixture.engine.creates != allocation) return;
            auto* earlier = fixture.pool.At(0);
            if (realDestroy) fixture.engine.DestroyPrivate(earlier->edict());
            else earlier->pev->flags |= FL_KILLME;
        };
        Check(!fixture.pool.Initialize() && fixture.pool.State() == CSOLMonsterPoolState::Failed &&
            !fixture.pool.Error().empty(),
            "later Spawn cannot publish Ready after an earlier slot is destroyed or marked for removal");
        fixture.engine.onModel = {};
        Check(!fixture.pool.At(0) && fixture.pool.ActiveCount() == 0 && fixture.pool.AvailableCount() == 0,
            "failed late validation removes every registered record and idle queue");
        Check(fixture.pool.Shutdown() && fixture.pool.Initialize(),
            "explicit shutdown recovers after prior-slot destruction during initialization");
    }
    {
        BehaviorStats stats; PoolFixture fixture; auto* monster = fixture.Acquire(0);
        Check(!fixture.pool.ForgetMap() && fixture.pool.State() == CSOLMonsterPoolState::Ready &&
            fixture.pool.Borrowed(*monster), "ForgetMap refuses to abandon live owners");
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats)), "seed repeated destruction probe");
        stats.onDestroy = [&] { monster->OnDestroy(); };
        monster->OnDestroy(); monster->OnDestroy();
        stats.onDestroy = {};
        Check(stats.destroyed == 1 && !monster->Pool() && !monster->Behavior() &&
            fixture.pool.State() == CSOLMonsterPoolState::Failed && !fixture.pool.At(0),
            "OnDestroy detaches before release and repeated or destructor-reentrant calls release only once");
        Check(!fixture.pool.ForgetMap(), "one destroyed slot cannot permit forgetting forty-four live owners");
        fixture.engine.DestroyWorld();
        Check(fixture.pool.ForgetMap() && fixture.pool.State() == CSOLMonsterPoolState::Empty,
            "all actual engine destruction notifications allow forgetting without stale edict access");
    }
    for (const bool replacement : {false, true})
    {
        BehaviorStats oldStats, newStats; PoolFixture fixture; auto* monster = fixture.Acquire(0);
        auto* edict = monster->edict();
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(oldStats)), "seed destructive replacement probe");
        CBaseEntity* other{};
        oldStats.onDestroy = [&]
        {
            if (replacement) other = &fixture.engine.ReplaceWithBase(edict);
            else fixture.engine.DestroyPrivate(edict);
        };
        Check(!monster->InstallBehavior(std::make_unique<BehaviorProbe>(newStats)),
            "InstallBehavior revalidates after destructor destroys or replaces the entity's private object");
        oldStats.onDestroy = {};
        Check(oldStats.destroyed == 1 && newStats.destroyed == 1 && !fixture.pool.At(0),
            "invalidated installation releases candidate once and never attaches it to replacement private data");
        if (replacement) Check(other && !(other->pev->flags & FL_KILLME), "replacement remains alive after rejected installation");
    }
    {
        BehaviorStats oldStats, newStats, peerStats; PoolFixture fixture;
        auto* monster = fixture.Acquire(0); auto* peer = fixture.Acquire(0);
        monster->InstallBehavior(std::make_unique<BehaviorProbe>(oldStats));
        peer->InstallBehavior(std::make_unique<BehaviorProbe>(peerStats)); peer->pev->takedamage = DAMAGE_AIM;
        oldStats.onDestroy = [&]
        {
            Check(fixture.pool.Busy(), "behavior replacement destructor runs inside pool operation guard");
            peer->BehaviorThink(); peer->Killed(nullptr, 0);
            Check(!peer->TakeDamage(nullptr, nullptr, 1, 0) && !fixture.pool.Free(*peer) &&
                !fixture.pool.BeginDeath(*peer, 9, fixture.points),
                "busy replacement suppresses forwarding and lifecycle mutation on another borrowed entity");
        };
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(newStats)), "guarded replacement completes");
        oldStats.onDestroy = {};
        Check(!peerStats.thinks && !peerStats.kills && !peerStats.damageCalls && fixture.pool.Borrowed(*peer),
            "no ownership-probe callback executes while the pool is busy");
        peer->BehaviorThink(); peer->Killed(nullptr, 0); peer->TakeDamage(nullptr, nullptr, 1, 0);
        Check(peerStats.thinks == 1 && peerStats.kills == 1 && peerStats.damageCalls == 1,
            "ordinary behavior forwarding resumes after the guarded operation exits");
    }
    {
        BehaviorStats stats; PoolEngine engine;
        auto pool = std::make_unique<CCSOLMonsterPool>();
        csol::scenario::MonsterType type{}; type.id = 1; type.priority = 0;
        csol::scenario::SpawnPoints points;
        Check(pool->Initialize(), "independent owner-lifetime fixture initializes");
        auto* monster = pool->Acquire(type, 7, points);
        monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats));
        monster->pev->flags |= FL_KILLME;
        pool.reset();
        Check(!monster->Pool() && !monster->Behavior() && stats.destroyed == 1,
            "pool destructor detaches a deferred-remove entity before owner storage expires");
        engine.DestroyWorld();
        Check(stats.destroyed == 1 && engine.edicts.empty(),
            "later engine destruction calls no expired pool owner and does not double-release behavior");
    }
}
void ExternallyHiddenBorrowedSlot()
{
    PoolFixture fixture; auto* monster = fixture.Acquire(0);
    monster->pev->effects |= EF_NODRAW;
    monster->pev->health = 123; monster->pev->nextthink = 12;
    monster->SetThink(&CCSOLScenarioMonster::BehaviorThink); monster->SetTouch(&CBaseEntity::Blocked);
    Check(fixture.pool.Free(*monster) && fixture.pool.Borrowed(*monster),
        "original NODRAW gate preserves a borrowed slot hidden externally rather than inventing a pool return");
    Check(monster->pev->solid == SOLID_SLIDEBOX && monster->pev->health == 123 &&
        monster->pev->nextthink == 12 && !monster->m_pfnThink && !monster->m_pfnTouch,
        "already-NODRAW Free clears callbacks only without resetting unrelated state");
    CheckCounts(fixture, 1, 1, 0, 0);
    Check(fixture.pool.Free(*monster) && fixture.pool.RestartRound() && fixture.pool.Borrowed(*monster) && !monster->Type(),
        "formal restart clears type but does not repair an externally hidden borrowed slot against original guard");
    CheckCounts(fixture, 1, 1, 0, 0);
    monster->pev->effects &= ~EF_NODRAW;
    Check(fixture.pool.Free(*monster) && Parked(*monster), "clearing external hidden flag permits the one actual return");
    CheckCounts(fixture, 0, 0, 0, 0);
}
void MapLifecycleAndGuards()
{
    PoolFixture fixture(false);
    auto deny = [&]
    {
        Check(fixture.pool.Busy() && !fixture.pool.Initialize() && !fixture.pool.Shutdown() &&
            !fixture.pool.ForgetMap() && !fixture.pool.RestartRound() && !fixture.Acquire(0),
            "engine callback cannot reenter any destructive pool lifecycle");
    };
    fixture.engine.onPrecache = deny;
    Check(fixture.pool.Initialize(), "initialization completes after rejected callback reentry");
    fixture.engine.onPrecache = {};
    fixture.Acquire(1); fixture.pool.BeginDeath(*fixture.pool.At(0), 10, fixture.points);
    fixture.engine.onRemove = [&](CBaseEntity&) { deny(); };
    Check(fixture.pool.Shutdown(), "shutdown completes after rejected removal callback reentry");
    fixture.engine.onRemove = {};
    fixture.engine.DestroyWorld();
    Check(fixture.pool.ForgetMap() && fixture.pool.State() == CSOLMonsterPoolState::Empty && fixture.pool.Error().empty(),
        "ForgetMap after engine destroys previous edicts dereferences no stale map handles");
    Check(fixture.pool.Initialize() && fixture.pool.AvailableCount() == 45 && fixture.engine.creates == 90,
        "same-name or different map session can initialize forty-five fresh identities");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--lifecycle-gaps") LifecycleReentryGaps();
        else
        {
            Initialization(); AllocationAndCapacity(); FlagFields(); SolidQuery(); DeathAndFree(); EvictionAndRestart();
            FailureAndIdentity(); BehaviorOwnership(); LifecycleReentryGaps(); DestructionAndForwardingGuards();
            ExternallyHiddenBorrowedSlot(); MapLifecycleAndGuards();
        }
    }
    catch (const std::exception& error) { std::cerr << "EXCEPTION: " << error.what() << '\n'; return 2; }
    std::cout << "monster_pool checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
