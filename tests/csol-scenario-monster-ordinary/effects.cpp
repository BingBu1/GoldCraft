// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "../../native/csol/server/scenario_monster_effects.h"
#include "hostage/hostage.h"

namespace ordinary_test
{
namespace
{
CSOLMonsterEffectGuard Guard(CSOLMonsterSpawnCall& call)
{
    return {[](const void* value) { return static_cast<const CSOLMonsterSpawnCall*>(value)->Current(); }, &call};
}
CSOLMonsterEffectGuard Guard(CSOLMonsterDeathCall& call)
{
    return {[](const void* value) { return static_cast<const CSOLMonsterDeathCall*>(value)->Current(); }, &call};
}
bool Empty(const CSOLMonsterDamageEffect& effect)
{
    return !effect.source.Get<CBaseEntity>() && effect.count == 0 && effect.damage == 0 && effect.flag == 0 &&
        effect.hitgroup == -1 && effect.localOffset == Vector(0,0,0) && effect.sourceId == 0 && !effect.notify;
}
struct Lifetime
{
    std::function<void()> onDestroy;
    ~Lifetime() { if (onDestroy) onDestroy(); }
};
struct ObservationScope
{
    bool& enabled;
    ~ObservationScope() { enabled = false; }
};
}
void DamageEffectCases()
{
    {
        Fixture f; auto& monster = f.Spawn(); f.engine.globals.maxClients = 32;
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        Require(bool(effects), "real spawned host owns DOT state");
        for (std::size_t slot = 0; slot < CCSOLMonsterEffects::Slots; ++slot)
            Check(Empty(effects->At(slot)), "all thirty-three records start empty with hitgroup minus one");
        auto& first = f.engine.Client(1); auto& last = f.engine.Client(32); auto& outside = f.engine.Client(33);
        Check(!effects->Install(guard, nullptr, 3, 2, 0, 100, -1, {}, {}), "null source rejected");
        Check(!effects->Install(guard, outside.pev, 3, 2, 0, 100, -1, {}, {}), "source beyond player32 rejected");
        f.engine.indexReplies[first.edict()] = 0;
        Check(!effects->Install(guard, first.pev, 3, 2, 0, 100, -1, {}, {}), "source index zero rejected");
        f.engine.indexReplies.erase(first.edict());
        monster.pev->deadflag = DEAD_DEAD;
        Check(!effects->Install(guard, first.pev, 3, 2, 0, 100, -1, {}, {}), "DEAD_DEAD host rejects installation");
        monster.pev->deadflag = DEAD_DYING; monster.pev->health = 0;
        Require(effects->Install(guard, first.pev, 3, 2, 7, 100, -1, Vector(1,2,3), {}),
            "zero health host still accepts source unless deadflag is DEAD_DEAD");
        Require(effects->Install(guard, last.pev, 4, 5, 9, 200, 2, Vector(4,5,6), {}), "player32 installs in owned record");
        Check(effects->At(1).source.Get<CBaseEntity>() == &first && effects->At(32).source.Get<CBaseEntity>() == &last &&
            effects->At(1).flag == 7 && effects->At(32).flag == 9 && Empty(effects->At(0)),
            "source index is direct one-to-thirty-two addressing and byte flag remains opaque");
    }
    {
        int notifications{}, destroyed{}; std::vector<std::string> order;
        Fixture f; auto& monster = f.Spawn(); auto& player = f.engine.Client(1);
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        monster.pev->angles.y = 90;
        auto token = std::make_shared<Lifetime>(); std::weak_ptr<Lifetime> weak = token;
        token->onDestroy = [&]
        {
            ++destroyed; order.emplace_back("destroy");
            Check(Empty(effects->At(1)), "old callback destruction observes cleared scalar record");
        };
        Require(effects->Install(guard, player.pev, 7, 3, 11, 725, 1, Vector(10,0,3), [&, token]
        {
            ++notifications; order.emplace_back("notify");
            Check(effects->At(1).count == 3 && effects->At(1).sourceId == 725,
                "replacement notification sees old record before scalar reset");
        }), "install owns callback beyond argument lifetime");
        token.reset();
        Check(!weak.expired() && Near(effects->At(1).localOffset.x, 0) &&
            Near(effects->At(1).localOffset.y, -10) && effects->At(1).localOffset.z == 3,
            "owned callback survives caller release and world offset rotates by negative host yaw");
        Require(effects->Install(guard, player.pev, 9, 2, 0, 726, -1, {}, {}), "replace same source record");
        Check(order == std::vector<std::string>({"notify","destroy"}) && notifications == 1 && destroyed == 1 && weak.expired(),
            "replace notifies then destroys old callback exactly once");
        Check(effects->At(1).count == 2 && effects->At(1).damage == 9 && effects->At(1).sourceId == 726,
            "replacement publishes new values only after old callback destruction");
    }
    {
        int destroyed{}, notified{};
        Fixture f; auto& monster = f.Spawn(); auto call = f.Call(); auto effects = monster.DamageEffects();
        auto token = std::make_shared<Lifetime>(); token->onDestroy = [&] { ++destroyed; };
        Check(!effects->Install(Guard(call), nullptr, 1, 1, 0, 1, -1, {},
            [token = std::move(token), &notified] { ++notified; }), "rejected install does not retain incoming callback");
        Check(destroyed == 1 && notified == 0, "reject destroys owned argument without notification");
    }
    for (const auto flag : {0, 1, 2}) for (float health : {1.0f, .5f, 0.0f, -1.0f})
    {
        Fixture f; auto& monster = f.Spawn(); auto& first = f.engine.Client(1); auto& last = f.engine.Client(32);
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        Require(effects->Install(guard, last.pev, 1, 1, static_cast<std::uint8_t>(flag), 725, -1, {}, {}), "install predicate source32");
        monster.pev->health = health; bool exhausted{};
        Require(effects->ClearSource(guard, first.pev, exhausted), "clear other source evaluates remaining flags");
        Check(exhausted == ((flag == 0 && health == 1) || (flag == 1 && health <= 0)),
            "clear-source result uses exact opaque-flag and health predicates including player32");
        bool queryOnly{};
        Require(effects->ClearSource(guard, nullptr, queryOnly), "null source requests predicate without removal");
        Check(queryOnly == exhausted && effects->At(32).count == 1, "null-source query preserves installed record");
    }
    {
        int notified{};
        Fixture f; auto& monster = f.Spawn(); f.engine.globals.maxClients = 32;
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        for (int index = 1; index <= 32; ++index)
        {
            auto& player = f.engine.Client(index);
            Require(effects->Install(guard, player.pev, 1, index, 0, 100 + index, -1, {}, [&] { ++notified; }),
                "install every actual player index");
        }
        Require(f.Reset(), "ordinary reset preserves host effect ownership");
        Check(monster.DamageEffects() == effects && effects->At(32).count == 32 && notified == 0,
            "same-lease ordinary reset does not reconstruct or notify DOT records");
        Require(effects->ClearAll(guard), "clear-all uses host guard and all thirty-three records");
        Check(notified == 32, "clear-all notifies every installed source including source32 once");
        for (std::size_t index = 0; index < CCSOLMonsterEffects::Slots; ++index)
            Check(Empty(effects->At(index)), "clear-all resets every scalar and releases each owned callback");
    }
    {
        bool recursiveAccepted{}; int notified{};
        Fixture f; auto& monster = f.Spawn(); auto& player = f.engine.Client(1);
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        Require(effects->Install(guard, player.pev, 1, 1, 0, 725, -1, {}, [&]
        {
            ++notified;
            recursiveAccepted = effects->Install(guard, player.pev, 2, 2, 0, 726, -1, {}, {});
        }), "install callback that attempts same-slot mutation");
        Require(effects->ClearAll(guard), "clear completes after same-slot replacement during notification");
        Check(recursiveAccepted && notified == 1 && Empty(effects->At(1)),
            "same-slot replacement is allowed and current record clears without recursively invoking the same callback");
    }
    for (bool earlyPark : {false, true})
    {
        int notified{};
        Fixture f; auto& monster = f.Spawn(); auto& player = f.engine.Client(32);
        auto call = f.Call(); auto effects = monster.DamageEffects();
        Require(effects->Install(Guard(call), player.pev, 1, 2, 0, 725, -1, {}, [&] { ++notified; }),
            "install source32 before pool return");
        Require(earlyPark ? f.pool.ParkForDeath(monster) : f.pool.Free(monster), "actual ordinary or early pool return");
        Check(notified == 0 && effects->At(32).count == 2, "plain Free and early ParkForDeath do not universally clear DOT");
        Require(f.pool.RestartRound(), "destination round reset clears retained effects before reuse");
        Check(notified == 1 && Empty(effects->At(32)), "actual round lifecycle prevents source32 carrying into a new round");
    }
    {
        int notified{};
        Fixture f; auto& monster = f.Spawn(); auto call = f.Call(); auto effects = monster.DamageEffects();
        for (int index : {1,32})
        {
            auto& player = f.engine.Client(index);
            Require(effects->Install(Guard(call), player.pev, 1, 2, 0, 725, -1, {}, [&] { ++notified; }),
                "install first and final source before real death owner");
        }
        CSOLMonsterDeathArguments death; death.suppressSound = true;
        Require(f.deathOwner->Requests().Request(monster, death) == CSOLDeathRequestResult::Executed,
            "actual ordinary death dispatcher runs synchronous owner");
        Check(notified == 2 && Empty(effects->At(1)) && Empty(effects->At(32)),
            "actual death owner reaches real effect clear for source1 and source32");
    }
    {
        int notified{}, destroyed{};
        Fixture f; auto& monster = f.Spawn(); auto& player = f.engine.Client(1);
        auto call = f.Call(); auto effects = monster.DamageEffects();
        auto token = std::make_shared<Lifetime>(); token->onDestroy = [&] { ++destroyed; };
        Require(effects->Install(Guard(call), player.pev, 1, 1, 0, 725, -1, {}, [&, token] { ++notified; }),
            "install callback before raw private-state teardown");
        token.reset();
        f.ReleaseMap();
        Check(!call.Current() && !effects->ClearAll(Guard(call)), "old session guard cannot clear detached entity");
        effects.reset();
        Check(notified == 0 && destroyed == 1, "plain state destruction destroys callback without synthetic notification");
    }
    {
        bool observing = true; std::vector<std::string> order;
        Fixture f; ObservationScope observationScope{observing};
        auto& monster = f.Spawn(); f.engine.globals.maxClients = 32;
        auto spawn = f.Call(); auto effects = monster.DamageEffects();
        auto& host = monster.HostState();
        host.nonlethalDamage = 37.5f; host.damageScaleCount = 7; host.damageScale = 2.5f;
        host.statusUpdateAt = 345;
        const auto checkReset = [&]
        {
            Check(host.nonlethalDamage == 0 && host.damageScaleCount == 0 && host.damageScale == 1,
                "death callback and destruction observe all three host modifiers already reset");
        };
        for (int index : {1, 32})
        {
            auto& player = f.engine.Client(index);
            auto token = std::make_shared<Lifetime>();
            token->onDestroy = [&, index, checkReset, effectState = effects.get()]
            {
                if (!observing) return;
                checkReset(); order.push_back("destroy" + std::to_string(index));
                Check(Empty(effectState->At(index)), "death-owned callback destruction follows its scalar slot reset");
            };
            Require(effects->Install(Guard(spawn), player.pev, 6, 3, 0, 725, -1, {}, [&, index, token, checkReset]
            {
                if (!observing) return;
                checkReset(); order.push_back("notify" + std::to_string(index));
            }), "install both ends of death effect traversal with owned destructor observers");
        }
        CSOLMonsterDeathArguments arguments;
        CSOLMonsterDeathCall death(f.map, f.deathOwner->Requests(), monster, arguments);
        Require(death.Current() == &monster, "actual death transaction captures live map request generation and lease");
        monster.pev->deadflag = DEAD_DEAD; // The real death tail runs after MarkDead.
        Require(CSOL_ClearMonsterEffects(death), "actual death helper clears effects after the death flag is set");
        Check(order == std::vector<std::string>({"notify1", "destroy1", "notify32", "destroy32"}),
            "actual helper resets modifiers before ordered notifications and callback destruction through source32");
        Check(host.statusUpdateAt == 345, "death modifier reset preserves the unrelated status clock");
        for (std::size_t index = 0; index < CCSOLMonsterEffects::Slots; ++index)
            Check(Empty(effects->At(index)), "actual death helper completed all thirty-three effect records");
    }
    for (bool revokeMap : {false, true})
    {
        Fixture f; auto& monster = f.Spawn();
        CSOLMonsterDeathArguments arguments;
        CSOLMonsterDeathCall death(f.map, f.deathOwner->Requests(), monster, arguments);
        Require(death.Current() == &monster, "stale death helper case captures an actual valid transaction");
        if (revokeMap) f.map.RevokeEntityAccess();
        else f.Reborrow(monster); // Existing fixture uses real Free/Acquire and a new lease at the same address.
        auto& host = monster.HostState();
        host.nonlethalDamage = 81; host.damageScaleCount = 11; host.damageScale = 7;
        Check(!death.Current() && !CSOL_ClearMonsterEffects(death),
            "revoked map permission or old same-address lease rejects the actual death helper");
        Check(host.nonlethalDamage == 81 && host.damageScaleCount == 11 && host.damageScale == 7,
            "invalid death call cannot reset retained storage or replacement lease modifiers");
    }
    for (bool cancelInDestructor : {false, true})
    {
        bool observing = true, cancelled{}; int notified{}, laterNotifications{};
        Fixture f; ObservationScope observationScope{observing};
        auto& monster = f.Spawn(); f.engine.globals.maxClients = 32;
        auto& first = f.engine.Client(1); auto& last = f.engine.Client(32);
        auto spawn = f.Call(); auto effects = monster.DamageEffects();
        auto& host = monster.HostState();
        host.nonlethalDamage = 42; host.damageScaleCount = 9; host.damageScale = 3;
        const auto cancel = [&]
        {
            if (!observing || cancelled) return;
            Check(host.nonlethalDamage == 0 && host.damageScaleCount == 0 && host.damageScale == 1,
                "notification or destructor sees reset modifiers before replacing the lease");
            cancelled = true;
            f.Reborrow(monster);
            host.nonlethalDamage = 91; host.damageScaleCount = 13; host.damageScale = 8;
        };
        auto token = std::make_shared<Lifetime>();
        token->onDestroy = [&, cancel] { if (cancelInDestructor) cancel(); };
        Require(effects->Install(Guard(spawn), first.pev, 5, 2, 0, 725, -1, {}, [&, token, cancel]
        {
            if (!observing) return;
            ++notified;
            if (!cancelInDestructor) cancel();
        }), "first death record can replace the actual lease during notification or owned destruction");
        token.reset();
        Require(effects->Install(Guard(spawn), last.pev, 6, 17, 0, 726, -1, {}, [&] { ++laterNotifications; }),
            "later record detects traversal after replacement lease is published");
        CSOLMonsterDeathArguments arguments;
        CSOLMonsterDeathCall death(f.map, f.deathOwner->Requests(), monster, arguments);
        Require(death.Current() == &monster, "cancellation test enters through actual death transaction");
        Check(!CSOL_ClearMonsterEffects(death) && cancelled && !death.Current(),
            "actual death helper propagates ClearAll cancellation from both callback boundaries");
        Check(host.nonlethalDamage == 91 && host.damageScaleCount == 13 && host.damageScale == 8,
            "cancelled old death tail never resets replacement lease modifiers after ClearAll returns");
        Check(notified == 1 && laterNotifications == 0 && effects->At(32).count == 17 &&
            effects->At(32).sourceId == 726 && bool(effects->At(32).notify),
            "cancelled old death traversal does not notify or clear the later record retained by the new lease");
        Check(cancelInDestructor ? Empty(effects->At(1)) : effects->At(1).count == 2 && bool(effects->At(1).notify),
            "notification cancellation stops before scalar reset while destruction cancellation follows that reset");
        // Release observers while their stack captures still exist. This is a
        // separate new-lease cleanup; it is not part of the cancelled old call.
        CSOLMonsterDeathCall replacement(f.map, f.deathOwner->Requests(), monster, arguments);
        Require(replacement.Current() == &monster && effects->ClearAll(Guard(replacement)),
            "replacement lease owns its independent observer cleanup");
    }
    {
        int notified{};
        Fixture f; auto& monster = f.Spawn(); auto& player = f.engine.Client(1);
        auto call = f.Call(); const auto guard = Guard(call); auto effects = monster.DamageEffects();
        cv_hostage_stop.value = 1; f.SpawnState().animationAt = 10000; f.SpawnState().updateAt = 0;
        monster.HostState().statusUpdateAt = 101;
        Require(effects->Install(guard, player.pev, 1, 0, 0, 725, -1, {}, [&] { ++notified; }), "install due cleanup without a damage receiver");
        f.Tick(100); f.Tick(100.5f);
        Check(notified == 0 && bool(effects->At(1).notify), "actual ordinary owner does not traverse effects before one-second gate");
        f.Tick(101);
        Check(notified == 1 && Empty(effects->At(1)) && monster.HostState().statusUpdateAt == 102,
            "equal deadline executes real owned cleanup and advances status once");
        Require(effects->Install(guard, player.pev, 1, 0, 0, 725, -1, {}, [&] { ++notified; }), "install another cleanup before time jump");
        f.Tick(110);
        Check(notified == 2 && monster.HostState().statusUpdateAt == 111, "owner does not replay missed per-second cleanup ticks");
    }
}
}
