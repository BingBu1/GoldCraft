// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "hostage/hostage.h"

namespace ordinary_test
{
void OwnerClockCases()
{
    {
        Fixture f(true); auto& monster = f.Spawn(); auto& spawn = f.SpawnState();
        Check(Near(monster.pev->nextthink, 20.15f) && Near(spawn.updateAt, 20.15f) && Near(spawn.animationAt, 20.15f),
            "owner retains three independently assigned factory deadlines");
        f.log.clear(); const auto initial = spawn.animationAt;
        f.Tick(initial);
        Check(f.eventCalls.empty() && Near(monster.HostState().statusUpdateAt, initial + 1),
            "animation uses strict greater while D0 accepts equality");
        Check(spawn.animationAt == initial && Near(spawn.updateAt, initial + .1f),
            "D0 advances the actual factory clock without advancing animation clock");
        f.log.clear(); const float next = std::nextafter(initial, 100.f);
        f.PrepareEvent(next);
        f.Tick(next);
        Check(f.eventCalls.size() == 1 && Near(monster.HostState().statusUpdateAt, initial + 1) &&
            Near(spawn.animationAt, next + Frame), "intervening 30Hz tick advances animation but not D0");
        Check(Near(monster.pev->nextthink, next + Frame), "each owner Think schedules the next 30Hz tick");
        Check(f.Behavior().Error().message.empty(), "ordinary scheduling has no owner error");
        auto& state = f.CaptureState();
        Check(!state.thinking && !state.position.updating, "scope flags clear after actual state-observation tick");
    }
    {
        Fixture f(true); auto& monster = f.Spawn(); auto& spawn = f.SpawnState();
        spawn.animationAt = spawn.updateAt = 0;
        f.PrepareEvent(21);
        monster.SpeedState().impulse = Vector2D(123, 0);
        monster.SpeedState().impulseUntil = 50;
        monster.pev->velocity = Vector(0, 0, 7);
        bool animationSawOld{}, d0SawApplied{}, d0SawOldStatus{};
        const float status = monster.HostState().statusUpdateAt;
        f.onEvent = [&] { animationSawOld = monster.pev->velocity.x == 0; };
        f.services->onStopped = [&]
        {
            d0SawApplied = Near(monster.pev->velocity.x, 123);
            d0SawOldStatus = monster.HostState().statusUpdateAt == status;
        };
        f.Tick(21);
        Check(animationSawOld && d0SawApplied, "actual Think order is animation then D4 velocity then D0");
        Check(d0SawOldStatus && monster.HostState().statusUpdateAt == 22,
            "real status/effects tail follows D0");
    }
    {
        Fixture f(true); auto& monster = f.Spawn(); auto& spawn = f.SpawnState();
        spawn.animationAt = spawn.updateAt = 0;
        f.PrepareEvent(21);
        monster.HostState().frozenRemaining = Frame * 2;
        monster.pev->velocity = Vector(71, 72, 73);
        const auto before = monster.pev->velocity;
        f.Tick(21);
        Check(Near(monster.HostState().frozenRemaining, Frame) && Near(monster.m_flLastEventCheck, 21.1f) &&
            monster.pev->framerate == 0 && f.eventCalls.empty(),
            "frozen dispatch retains real fixed .1 event clock while zero framerate prevents event advancement");
        Check(monster.pev->velocity == before && monster.HostState().statusUpdateAt == 22 && f.services->d0Calls == 0,
            "frozen D4 and D0 suppressed while real due status tail still runs");
        const int queries = f.modelQueries;
        f.Tick(21 + Frame);
        Check(monster.HostState().frozenRemaining == 0 && f.modelQueries > queries,
            "thaw calls actual guarded sequence reset before motion resumes");
        Check(monster.pev->rendercolor == Vector(255,255,255), "thaw restores white before later D0 render restore");
    }
    {
        Fixture f; auto& monster = f.Spawn();
        f.SpawnState().animationAt = f.SpawnState().updateAt = 0;
        cv_hostage_stop.value = 1;
        monster.SpeedState().impulse = Vector2D(123, 0); monster.SpeedState().impulseUntil = 50;
        f.Tick(21);
        Check(Near(monster.pev->velocity.x, 123) && f.services->d0Calls == 0 && monster.HostState().statusUpdateAt == 22,
            "hostage_stop suppresses D0 without suppressing D4 or real status effects");
    }
    {
        Fixture f; auto& monster = f.Spawn(); monster.HostState().navigationExpired = true;
        const float next = monster.pev->nextthink;
        const float status = monster.HostState().statusUpdateAt;
        f.Tick(21);
        Check(monster.pev->deadflag == DEAD_DEAD && monster.pev->solid == SOLID_NOT,
            "expired unprotected ordinary navigation reaches real death owner");
        Check(monster.pev->nextthink != 21 + Frame && monster.pev->nextthink != next,
            "death's return deadline is not overwritten by idle nextthink");
        Check(f.eventCalls.empty() && f.services->d0Calls == 0 && monster.HostState().statusUpdateAt == status,
            "navigation-expired death precedes all idle tick services");
    }
    {
        Fixture f; auto& monster = f.Spawn(); monster.HostState().navigationExpired = true;
        f.services->protectedSlots.push_back(monster.Slot());
        f.Tick(21);
        Check(monster.pev->deadflag == DEAD_NO && Near(monster.pev->nextthink, 21 + Frame),
            "protected navigation-expired slot continues ordinary scheduling");
    }
    {
        Fixture f; auto& monster = f.Spawn();
        auto* state = &f.CaptureState();
        state->position.motion.moveType = 1;
        monster.pev->velocity = Vector(5,6,7);
        auto call = f.Call();
        Require(f.Behavior().Stop(call), "Stop uses owner MoveTo and actual entity velocity");
        Check(monster.pev->velocity == Vector(0,0,0) && state->position.motion.moveType == 0,
            "Stop clears three-dimensional velocity and selects still movement");
        state->previousOrigin = Vector(11,12,13);
        state->melee.attack = {.8f, 123, 122.2f};
        state->position.jumpUntil = 31; state->position.obstacleAfter = 32; state->position.routeTick = 17;
        state->position.destination = Vector(1000,0,0);
        Require(f.Behavior().MoveTo(call, Vector(1100,0,0)), "nearby destination accepted");
        Check(state->position.destination == Vector(1000,0,0), "far old destination retained for nearby goal change");
        Require(f.Behavior().MoveTo(call, Vector(1400,0,0)), "distant goal change accepted");
        Check(state->position.destination == Vector(1400,0,0), "larger goal change resets destination");
        Require(f.Reset(), "ordinary same-lease reset preserves original histories");
        const auto& reset = f.CaptureState(110);
        Check(reset.melee.attack.deadline == 123 && reset.position.jumpUntil == 31 &&
            reset.position.obstacleAfter == 32 && reset.position.routeTick == 17,
            "Reset retains attack jump obstacle and route histories");
        Check(reset.position.destination.x == -99999,
            "Reset invalidates destination sentinel while preserving runtime histories");
    }
}

void OwnerCancellationCases()
{
    {
        Fixture f; f.Spawn(); f.services->navigation->world.Reset();
        auto call = f.Call(); ConfigError error;
        Check(!f.Behavior().ResetForSpawn(call, f.SpawnState(), error) && !error.message.empty(),
            "nonzero generation after NAV Reset is not an initialized world");
    }
    {
        Fixture f(true); auto& monster = f.Spawn(); auto& spawn = f.SpawnState(); spawn.animationAt = spawn.updateAt = 0;
        f.PrepareEvent(21);
        const float status = monster.HostState().statusUpdateAt;
        f.onEvent = [&]
        {
            f.onEvent = {};
            Require(f.Reset(), "same-lease reset from animation callback succeeds");
            spawn.animationAt = 900;
            spawn.updateAt = 901;
        };
        f.Tick(21);
        Check(spawn.animationAt == 900 && spawn.updateAt == 901 && monster.HostState().statusUpdateAt == status,
            "old animation callback cannot overwrite replacement-state factory deadlines");
        Check(f.Behavior().Error().message.empty(), "cancelled old callback cannot poison new state error");
        f.Tick(22);
        Check(Near(f.Monster().pev->nextthink, 22 + Frame), "replacement state not stuck in old thinking scope");
    }
    {
        Fixture f(true); auto& monster = f.Spawn(); f.SpawnState().animationAt = f.SpawnState().updateAt = 0;
        f.PrepareEvent(21);
        f.onEvent = [&] { f.Monster().BehaviorThink(); };
        f.Tick(21);
        Check(f.eventCalls.size() == 1 && f.services->d0Calls == 1 && monster.HostState().statusUpdateAt == 22,
            "same-state nested Think rejected without duplicating animation or D0");
    }
    {
        Fixture f; auto& monster = f.Spawn();
        auto* old = &f.CaptureState();
        f.services->onStopped = [&]
        {
            f.services->onStopped = {};
            Require(old->thinking, "outer D0 state remains live on callback stack");
            Require(f.Reset(), "same-lease reset from D0 service succeeds");
            old->actionState = 123; // must be safe while outer StatePtr is alive
        };
        const float status = monster.HostState().statusUpdateAt;
        f.Tick(101);
        Check(monster.HostState().statusUpdateAt == status && f.Behavior().Error().message.empty(),
            "D0 state replacement cancels old tail without dangling state or error");
        const auto& replacement = f.CaptureState(110);
        Check(replacement.actionState == 0 && !replacement.thinking,
            "old-state writes do not reach replacement state");
    }
    {
        Fixture f(true); auto& monster = f.Spawn();
        auto retained = monster.RetainBehavior();
        f.SpawnState().animationAt = f.SpawnState().updateAt = 0;
        f.PrepareEvent(21);
        f.onEvent = [&] { f.ReleaseMap(); };
        f.Tick(21);
        Check(f.pool.State() == CSOLMonsterPoolState::Empty && f.services->d0Calls == 0,
            "map teardown from animation callback cancels old owner before D4 or D0");
        Check(static_cast<CCSOLOrdinaryMonsterBehavior*>(retained.get())->Error().message.empty(),
            "retained old behavior does not report new-map failure");
    }
    {
        Fixture f; f.Spawn(); auto* state = &f.CaptureState();
        state->position.motion.jumpGoal = Vector(100, 0, 0);
        f.HookAnimation(); bool rejected{};
        f.onAnimationHook = [&](Hook* chain, CBaseAnimating* monster)
        { rejected = !f.Reset(); chain->callNext(monster); };
        auto call = f.Call();
        Check(f.Behavior().Jump(call) && rejected && !state->position.startingJump,
            "Reset during valid Jump transaction rejected without cancelling outer jump");
    }
    {
        Fixture f; auto& monster = f.Spawn(); auto* state = &f.CaptureState();
        state->position.motion.jumpGoal = Vector(100, 0, 0);
        const auto retained = f.services;
        const auto replacement = std::make_shared<Services>(f);
        const auto lease = f.pool.Lease(monster);
        const auto lock = monster.Animation().LockedUntil();
        const int queries = f.modelQueries;
        int hookCalls{};
        f.HookAnimation();
        f.onAnimationHook = [&](Hook* chain, CBaseAnimating* current)
        {
            ++hookCalls; f.services = replacement;
            // Keep the old service and the real monster lease alive. Only
            // registration changes before the actual SDK helper is entered.
            monster.m_flFrameRate = 777; monster.pev->animtime = 333;
            chain->callNext(current);
        };
        auto call = f.Call();
        Check(!f.Behavior().Jump(call) && hookCalls == 1 && !state->position.startingJump,
            "Jump cancels when animation hook replaces a still-retained service");
        Check(call.Current() == &monster && f.pool.Lease(monster) == lease && !retained->OwnerCurrent() &&
            replacement->OwnerCurrent(), "cancellation comes from registration identity, not map teardown or slot reuse");
        Check(f.modelQueries == queries && monster.Animation().LockedUntil() == lock &&
            monster.m_flFrameRate == 777 && monster.pev->animtime == 333,
            "bound SpawnCall prevents later model queries, SDK clock writes and Jump LockUntil");
        Check(f.Behavior().Error().message.empty(), "retired Jump does not poison the retained behavior error");
    }
    for (int boundary : {0, 1, 2})
    {
        Fixture f; auto& monster = f.Spawn();
        auto* state = &f.CaptureState();
        auto* edict = f.engine.Raw("ordinary_damage_receiver", 500);
        auto* victim = new(&edict->v) actions_test::Victim;
        victim->pev = &edict->v; victim->pev->health = 100; victim->pev->origin = Vector(50,0,0);
        victim->pev->mins = Vector(-16,-16,-36); victim->pev->maxs = Vector(16,16,36);
        f.engine.privateOwners[edict] = [](void* p) { static_cast<actions_test::Victim*>(p)->~Victim(); std::free(p); };
        state->target = victim;
        state->melee.attack.deadline = 0;
        f.SpawnState().animationAt = 1000;
        monster.pev->angles.y = 0;
        const int queries = f.modelQueries;
        const float status = monster.HostState().statusUpdateAt;
        bool reset{};
        auto replace = [&]
        {
            Require(f.Reset(), "same-lease reset inside melee sequence reset");
            monster.pev->framerate = 17;
            monster.m_flFrameRate = 777;
            monster.pev->animtime = 333;
            reset = true;
        };
        if (!boundary)
        {
            f.HookAnimation();
            f.onAnimationHook = [&](Hook* chain, CBaseAnimating* current) { replace(); chain->callNext(current); };
        }
        else f.onModel = [&] { if (f.modelQueries - queries == boundary) replace(); };
        f.Tick(101);
        Check(reset && victim->damage.empty() && monster.HostState().statusUpdateAt == status,
            "same-lease animation reset cancels old melee before damage or D0 tail");
        Check(f.modelQueries - queries == boundary && monster.m_flFrameRate == 777 &&
            monster.pev->framerate == 17 && monster.pev->animtime == 333,
            "guard checks hook entry and both actual GET_MODEL_PTR returns before stale SDK writes");
        Check(f.Behavior().Error().message.empty(), "old melee cancellation leaves replacement error clear");
    }
    {
        Fixture f; auto& monster = f.Spawn();
        auto retained = monster.RetainBehavior();
        std::weak_ptr<CSOLMonsterSpawnState> spawn = f.SpawnState().shared_from_this();
        f.ReleaseMap(); f.factory.reset();
        Check(!spawn.expired(), "retained behavior keeps factory spawn storage alive after factory destruction");
        const auto parameters = static_cast<CCSOLOrdinaryMonsterBehavior*>(retained.get())->DeathParameters();
        Check(parameters.slotSkill == 0, "retained state remains readable after factory destruction");
        retained.reset();
        Check(spawn.expired(), "last old behavior release frees detached spawn storage");
    }
    {
        Fixture f(true); f.Spawn();
        bool destroyed{}, liveInside{};
        f.services->navigation->onDestroy = [&] { destroyed = true; };
        std::weak_ptr<Navigation> old = f.services->navigation;
        f.SpawnState().animationAt = f.SpawnState().updateAt = 0;
        f.PrepareEvent(21);
        f.onEvent = [&]
        {
            f.onEvent = {};
            f.services->navigation->world.Reset();
            f.services->navigation = std::make_shared<Navigation>();
            Require(f.Reset(), "NAV replacement publishes new ordinary state");
            liveInside = !old.expired() && !destroyed;
        };
        f.Tick(21);
        Check(liveInside && destroyed && old.expired(),
            "old NAV area storage pinned through old callback then released after its StatePtr unwinds");
    }
    for (int cancelMap : {0, 1})
    {
        Fixture f; auto& monster = f.Spawn();
        auto* state = &f.CaptureState();
        state->melee.avoidance.deadline = 1000;
        auto call = f.Call();
        Require(f.Behavior().MoveTo(call, Vector(1000, 0, 0)), "D4 death case sets actual destination");
        state->position.routeTick = 1;
        f.SpawnState().animationAt = 1000;
        const int d0Calls = f.services->d0Calls;
        bool deferred{}, aliveInside{};
        f.onTrace = [&]
        {
            f.onTrace = {};
            CSOLMonsterDeathArguments arguments; arguments.suppressSound = true;
            deferred = f.deathOwner->Requests().Request(monster, arguments) == CSOLDeathRequestResult::Deferred;
            aliveInside = monster.pev->deadflag == DEAD_NO;
            f.log.emplace_back("queued-death");
            if (cancelMap) f.ReleaseMap();
        };
        f.Tick(101);
        Check(deferred && aliveInside, "D4 trace queues death without changing deadflag inside guarded callback");
        if (cancelMap)
        {
            Check(f.pool.State() == CSOLMonsterPoolState::Empty && f.services->d0Calls == d0Calls,
                "map reset cancels pending D4 death without replay or D0 tail");
            Check(f.deathOwner->Requests().Error().empty(), "cancelled old scope does not poison new dispatcher");
        }
        else
        {
            Check(monster.pev->deadflag == DEAD_DEAD, "D4 last deferred death replays during same Think");
            const auto queued = std::find(f.log.begin(), f.log.end(), "queued-death");
            const auto dead = std::find(f.log.begin(), f.log.end(), "death-rules");
            Check(queued != f.log.end() && dead != f.log.end() && queued < dead,
                "actual death owner runs only after guarded D4 callback returns");
            Check(f.deathOwner->Requests().Error().empty(), "D4 death gate closed after same-tick replay");
        }
    }
    {
        BehaviorStats replacementStats;
        Fixture f; auto& monster = f.Spawn();
        auto* state = &f.CaptureState();
        auto call = f.Call();
        Require(f.Behavior().MoveTo(call, Vector(1000,0,0)), "owner replacement sets destination");
        state->position.routeTick = 1;
        state->melee.avoidance.deadline = 1000;
        f.SpawnState().animationAt = 1000;
        auto retained = monster.RetainBehavior();
        const float status = monster.HostState().statusUpdateAt;
        bool replaced{};
        f.onTrace = [&]
        {
            f.onTrace = {};
            Require(monster.InstallBehavior(std::make_unique<BehaviorProbe>(replacementStats)), "trace replaces behavior on same entity lease");
            monster.pev->velocity = Vector(777,778,779);
            replaced = true;
        };
        f.Tick(101);
        Check(replaced && monster.pev->velocity == Vector(777,778,779) && monster.HostState().statusUpdateAt == status,
            "bound SpawnCall cancels F4 helper before old owner writes replacement velocity or D0");
        Check(static_cast<CCSOLOrdinaryMonsterBehavior*>(retained.get())->Error().message.empty() &&
            f.deathOwner->Requests().Error().empty(), "owner replacement cancels without poisoning dispatcher or retained owner");
    }
}
}
