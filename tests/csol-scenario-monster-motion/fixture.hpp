// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_monster_motion.h"
#include <bit>
#include <cmath>
#include <memory>
#include <optional>

namespace motion_test
{
using death_events_test::Check;
using death_events_test::Require;
using R = CSOLMonsterMotionResult;
inline bool Equal(const Vector& a,const Vector& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
inline bool Near(float a,float b,float tolerance=.0001f){return std::abs(a-b)<=tolerance;}
struct NoClear final : CCSOLSpawnClearDeaths
{
    bool BeforeSpawnClear(CCSOLScenarioMonster&) override {throw std::runtime_error("unexpected spawn clear");}
    bool RequestSpawnClearDeath(CCSOLScenarioMonster&) override {throw std::runtime_error("unexpected spawn death");}
};
struct Fixture : death_events_test::Fixture
{
    inline static Fixture* active{};
    NoClear clear;
    CCSOLScenarioOccupancy occupancy{pool,clear};
    CCSOLScenarioNavWorld world;
    std::vector<std::unique_ptr<CNavArea>> storage;
    std::vector<CNavArea*> areas;
    CCSOLScenarioMonster* monster{};
    std::optional<CSOLMonsterSpawnCall> call;
    csol::scenario::MonsterAppearance appearance;
    std::function<void()> onTrace,onModelIndex;
    int traces{},modelIndexCalls{},modelIndex{501};
    std::vector<std::string> modelNames;
    Fixture();
    ~Fixture();
    CNavArea* Flat(float x,float y,float half=40);
    void Space();
    void Register(CCSOLScenarioMonster&);
    CCSOLScenarioMonster& Add(const Vector&,int priority=0);
    void Route(CCSOLMonsterPathFollower&);
    static void Trace(const float*,const float*,int,edict_t*,TraceResult*);
    static int ModelIndex(const char*);
};
void SpeedCases();
void NeighborCases();
void VelocityCases();
void CancellationCases();
void CleanupCases();
}
