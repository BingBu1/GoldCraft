// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_spawn_occupancy.h"
#include "../../native/csol/scenario/round_settings.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace spawn_test
{
inline int checks{}, failures{};
inline void Check(bool value, const char* name)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
inline void Require(bool value, const char* name)
{ Check(value, name); if (!value) throw std::runtime_error(name); }
inline std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
inline bool Equal(const Vector& a, const Vector& b)
{ return Bits(a.x)==Bits(b.x) && Bits(a.y)==Bits(b.y) && Bits(a.z)==Bits(b.z); }
extern std::function<void(edict_t*)> afterDispatch;
std::map<std::string, std::string> SyntheticResources();

struct Geometry
{
    std::vector<std::unique_ptr<CNavArea>> owned;
    std::list<CNavArea*> areas;
    void Add(Vector low, Vector high, float neZ=0, float swZ=0)
    {
        Vector ne(high.x,low.y,neZ), sw(low.x,high.y,swZ);
        owned.push_back(std::make_unique<CNavArea>(&low,&ne,&high,&sw));
        areas.push_back(owned.back().get());
    }
};

// The required death owner is an invocation/mutation observer, not a behavior
// implementation. It never pretends the missing reward/sound/event chain ran.
struct Deaths : CCSOLSpawnClearDeaths
{
    std::vector<std::size_t> before, requests;
    std::vector<std::uint64_t> requestLeases;
    std::function<void(CCSOLScenarioMonster&)> onBefore, onRequest;
    // Explicit boundary outcomes for occupancy/selector tests, not evidence
    // that a concrete death provider or monster death tail executed.
    bool prepareResult{true}, requestResult{true};
    bool BeforeSpawnClear(CCSOLScenarioMonster& monster) override
    { before.push_back(monster.Slot()); if(onBefore) onBefore(monster); return prepareResult; }
    bool RequestSpawnClearDeath(CCSOLScenarioMonster& monster) override
    {
        requests.push_back(monster.Slot()); requestLeases.push_back(monster.Pool()->Lease(monster));
        if(onRequest) onRequest(monster);
        return requestResult;
    }
    void Clear() { before.clear(); requests.clear(); requestLeases.clear(); }
};
struct OccupancyFixture : PoolFixture
{
    Geometry geometry;
    Deaths deaths;
    CCSOLScenarioOccupancy space{pool,deaths};
    explicit OccupancyFixture(bool initialize=true)
    {
        geometry.Add(Vector(-500,-500,0),Vector(500,500,0));
        if(initialize) Require(space.Initialize(geometry.areas),"initialize SDK occupancy");
    }
    CCSOLScenarioMonster& Monster(int priority, Vector origin=Vector(0,0,0), bool registerNow=true)
    {
        auto* monster=Acquire(priority); Require(monster!=nullptr,"borrow actual SDK monster");
        monster->pev->origin=origin; monster->pev->maxs=Vector(16,16,36);
        Require(space.ApplyPriority(*monster),"apply type attributes before grid");
        if(registerNow) Require(space.Register(*monster),"register actual monster in grid");
        return *monster;
    }
};

struct Events : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override {}
};
struct Space : CCSOLScenarioSpawnSpace
{
    bool ready{true}, refreshResult{true}, roomResult{true};
    int refreshes{};
    std::vector<Vector> origins;
    std::function<void()> onRefresh;
    std::function<void(const Vector&)> onRoom;
    bool Ready() const override { return ready; }
    bool RefreshOccupancy() override
    { ++refreshes; if(onRefresh) onRefresh(); return refreshResult; }
    bool MakeRoom(const Vector& origin) override
    { origins.push_back(origin); if(onRoom) onRoom(origin); return roomResult; }
};

struct SpawnEngine : PoolEngine
{
    inline static SpawnEngine* self{};
    std::map<std::string,std::string> files;
    std::set<void*> buffers;
    std::vector<CBasePlayer*> players;
    std::function<void()> onProxyCreate, onIndex;
    SpawnEngine();
    ~SpawnEngine();
    static unsigned char* Load(const char*,int*);
    static void FreeFile(void*);
    static edict_t* CreateAny(int);
    static edict_t* ClientIndex(int);
    edict_t* Raw(const char*,int);
    CBasePlayer& Client(int,Vector);
    CCSOLScenarioTrigger& Trigger(int,int,Vector,Vector);
    void Markers();
};

struct SelectionFixture
{
    SpawnEngine engine;
    CCSOLScenarioMap map;
    Events events;
    Space space;
    CCSOLScenarioPlayerSpawns selector{space};
    CBasePlayer* player{};
    std::int32_t offset{};
    csol::scenario::ConfigError error;
    SelectionFixture();
    CCSOLPlayerSpawn* Select() { return selector.Select(map,*player,offset,error); }
    void Points(std::initializer_list<csol::scenario::SpawnPoint> points)
    { map.SpawnPoints().Points().Clear(); for(const auto& point:points) map.SpawnPoints().Points().AppendPlayerPoint(point); }
};

inline csol::scenario::SpawnPoint Point(float x,int group=1,float z=7)
{ return {{x,0,z},{0,x,0},2,group,static_cast<std::size_t>(x+1000)}; }
void OccupancyCases();
void SelectorCases();
void ProxyCancellationNegative();
void ActualCases(const char* common,const char* pointPath,const char* volumePath,const char* navPath);
}
