// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>
#include <stdexcept>
#include "../csol-scenario-point-manager/engine_fixture.h"
#include "../../native/csol/server/scenario_monster_pool.h"

extern "C" void monster_entity(entvars_t*);

// Controlled engine allocation/services only. Export, RTTI, entity methods,
// pool ownership and SDK EHANDLE/Think dispatch remain production code.
struct PoolEngine : Engine
{
    inline static PoolEngine* current{};
    struct ModelCall { edict_t* edict; std::string path; };
    std::vector<ModelCall> modelCalls;
    std::vector<std::string> precaches;
    int exports{};
    int initialFlags{}, initialSpawnFlags{};
    int failAt{};
    CreateFailure createFault{};
    SpawnFailure spawnFault{};
    bool wrongPrivate{};
    std::function<void(edict_t*)> onModel;
    std::function<void()> onCreate;
    std::function<void()> onPrecache;
    PoolEngine()
    {
        current = this; globals.maxClients = 4;
        g_engfuncs.pfnCreateNamedEntity = CreatePoolEntity;
        g_engfuncs.pfnPrecacheModel = Precache;
        g_engfuncs.pfnSetModel = SetModel;
    }
    ~PoolEngine() { DestroyWorld(); current = nullptr; }
    static int Precache(const char* model)
    {
        current->precaches.emplace_back(model);
        if (current->onPrecache) current->onPrecache();
        return static_cast<int>(current->precaches.size());
    }
    static void SetModel(edict_t* edict, const char* model)
    {
        current->modelCalls.push_back({edict, model});
        edict->v.model = AllocString(model); edict->v.modelindex = 1;
        if (current->onModel) current->onModel(edict);
    }
    static edict_t* CreatePoolEntity(int classname)
    {
        auto& engine = *current; ++engine.creates; engine.calls.emplace_back("pool-create");
        if (engine.onCreate) engine.onCreate();
        const bool fault = engine.failAt == engine.creates;
        engine.spawnFailure = fault ? engine.spawnFault : SpawnFailure::None;
        if (fault && engine.createFault == CreateFailure::Null) return nullptr;
        auto edict = std::make_unique<edict_t>(); edict->serialnumber = 1;
        edict->v.pContainingEntity = edict.get(); edict->v.classname = classname;
        edict->v.flags = engine.initialFlags; edict->v.spawnflags = engine.initialSpawnFlags;
        edict->v.skin = -5; edict->v.velocity = Vector(99, 98, 97);
        auto* raw = edict.get(); engine.indices[raw] = 10000 + engine.creates;
        engine.edicts.push_back(std::move(edict));
        if (fault && engine.wrongPrivate) GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&raw->v));
        else { monster_entity(&raw->v); ++engine.exports; }
        if (fault && engine.createFault == CreateFailure::PrivateMissing)
        { engine.orphanedPrivate.push_back(raw->pvPrivateData); raw->pvPrivateData = nullptr; }
        // Do not invoke Spawn here. DispatchSpawn owns the one initial call.
        return raw;
    }
    void DestroyPrivate(edict_t* edict)
    {
        auto* base = static_cast<CBaseEntity*>(edict->pvPrivateData);
        if (!base) return;
        auto* wrapper = base->m_pEntity;
        base->OnDestroy();
        if (auto* monster = dynamic_cast<CCSOLScenarioMonster*>(base)) monster->~CCSOLScenarioMonster();
        std::erase(orphanedPrivate, base);
        delete wrapper; std::free(base);
        if (edict->pvPrivateData == base) edict->pvPrivateData = nullptr;
    }
    void DestroyWorld()
    {
        // Keep edict storage live until all OnDestroy calls have run, matching
        // the engine callback lifetime; then free the old map's edict array.
        for (auto& edict : edicts) DestroyPrivate(edict.get());
        for (void* memory : orphanedPrivate)
        {
            auto* base = static_cast<CBaseEntity*>(memory);
            if (!base) continue;
            auto* wrapper = base->m_pEntity;
            base->OnDestroy();
            if (auto* monster = dynamic_cast<CCSOLScenarioMonster*>(base)) monster->~CCSOLScenarioMonster();
            delete wrapper; std::free(base);
        }
        orphanedPrivate.clear(); ClearEdicts();
    }
};

struct BehaviorStats
{
    int destroyed{}, thinks{}, damageCalls{}, kills{};
    std::function<void()> onDestroy;
    std::function<void(CCSOLScenarioMonster&)> onThink, onKilled, onDamage;
};
// Ownership/forwarding probe only. No movement, damage calculation, rewards,
// animations or simulated AI; it must never count as behavior implementation.
struct BehaviorProbe : CCSOLMonsterBehavior
{
    BehaviorStats& stats;
    explicit BehaviorProbe(BehaviorStats& value) : stats(value) {}
    ~BehaviorProbe() override { ++stats.destroyed; if (stats.onDestroy) stats.onDestroy(); }
    void Think(CCSOLScenarioMonster& monster) override
    { ++stats.thinks; if (stats.onThink) stats.onThink(monster); }
    BOOL TakeDamage(CCSOLScenarioMonster& monster, entvars_t*, entvars_t*, float, int) override
    { ++stats.damageCalls; if (stats.onDamage) stats.onDamage(monster); return TRUE; }
    void Killed(CCSOLScenarioMonster& monster, entvars_t*, int) override
    { ++stats.kills; if (stats.onKilled) stats.onKilled(monster); }
};

struct PoolFixture
{
    PoolEngine engine;
    CCSOLMonsterPool pool;
    std::array<csol::scenario::MonsterType, 3> types;
    csol::scenario::SpawnPoints points;
    explicit PoolFixture(bool initialize = true)
    {
        for (int priority = 0; priority < 3; ++priority)
        { types[priority].id = priority == 0 ? 1 : priority == 1 ? 100 : 30200; types[priority].priority = static_cast<std::int8_t>(priority); }
        points.RegisterMonsterPoint({{}, {}, 1, 7, 77}, 1000);
        if (initialize && !pool.Initialize()) throw std::runtime_error("pool fixture initialization failed");
    }
    ~PoolFixture()
    {
        engine.onCreate = {}; engine.onModel = {}; engine.onPrecache = {}; engine.onRemove = {};
        pool.Shutdown();
        // A test may deliberately invalidate an EHANDLE. Run the remaining
        // engine destruction while its former owner still exists.
        engine.DestroyWorld(); pool.ForgetMap();
    }
    CCSOLScenarioMonster* Acquire(std::size_t priority, int point = 7,
        std::span<const std::size_t> protectedSlots = {})
    { return pool.Acquire(types.at(priority), point, points, protectedSlots); }
    void Fill(std::size_t ordinary = 39)
    {
        for (std::size_t i = 0; i < ordinary; ++i) if (!Acquire(0)) throw std::runtime_error("ordinary fill failed");
        while (pool.ActiveCount() < pool.Capacity) if (!Acquire(1)) throw std::runtime_error("priority fill failed");
    }
};
