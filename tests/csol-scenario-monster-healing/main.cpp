// SPDX-License-Identifier: GPL-3.0-or-later
// Preparation for the matched SDK link: real Pool/SpawnCall/helper/messages.
// Controlled engine allocation/message callbacks are the only substitutes.
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_monster_healing.h"
#include "../../native/csol/server/scenario_monster_ordinary.h"
#include <cmath>

namespace
{
using namespace death_events_test;
struct Fixture
{
    EventEngine engine;
    Events events;
    CCSOLScenarioMap map;
    CCSOLMonsterPool pool;
    NoOwner deathOwner;
    CCSOLMonsterDeathRequests deaths{deathOwner};
    CCSOLScenarioMessages messages;
    std::deque<csol::scenario::MonsterType> types;
    CSOLOrdinaryMonsterState state;
    csol::scenario::ConfigError error;
    bool active{true};
    Fixture()
    {
        engine.Markers();
        Require(map.BeginMap("map_session_test") && map.CollectEntities() &&
            map.BeginRound(0,0,0,events), "healing uses real active map");
        Require(pool.BindEntityAccess(map.EntityAccess()) && pool.Initialize(),
            "pool and call share current map access");
        Require(messages.Register(), "real writer registered");
        engine.globals.time=10;state.jitter=.25f;engine.ClearCapture();
    }
    ~Fixture()
    {
        engine.onMessage={};engine.onQuery={};
        deaths.Reset();pool.Shutdown();map.EndMap();engine.DisposeAll();
        engine.DestroyWorld();pool.ForgetMap();
    }
    CCSOLScenarioMonster& Add(Vector position={},int priority=0)
    {
        auto& type=types.emplace_back();type.id=int(types.size());type.skill=5;
        type.priority=static_cast<std::int8_t>(priority);
        auto* monster=pool.Acquire(type,7,map.SpawnPoints().Points());
        Require(monster,"real pool lends healing target");
        monster->pev->health=10;monster->pev->max_health=100;monster->pev->origin=position;
        return *monster;
    }
    bool Run(CCSOLScenarioMonster& source)
    {
        CSOLMonsterSpawnCall call(map,pool,deaths,source);
        return CSOL_UseMonsterHealing(call,pool,messages,state,
            {[](const void* p){return static_cast<const Fixture*>(p)->active;},this},error);
    }
    void Reborrow(CCSOLScenarioMonster& monster)
    {
        while(pool.AvailableCount())
        {
            int priority=pool.CategoryCount(0)<39?0:pool.CategoryCount(1)<4?1:2;
            Add(Vector(1000,1000,1000),priority);
        }
        const auto* type=monster.Type();const auto lease=pool.Lease(monster);
        Require(pool.Free(monster),"callback returns selected lease");
        auto* replacement=pool.Acquire(*type,7,map.SpawnPoints().Points());
        Require(replacement==&monster&&pool.Lease(monster)!=lease,"same pointer acquires new lease");
        replacement->pev->health=17;
    }
};
void OrderAndBlock()
{
    Fixture f;auto& source=f.Add();
    source.SpeedState().impulseProtectionUntil=10.01;
    auto& second=f.Add();auto& third=f.Add();auto& fourth=f.Add();auto& fifth=f.Add();
    third.pev->health=90;fourth.pev->health=-10;fourth.pev->deadflag=DEAD_DEAD;
    fourth.pev->team=2;fourth.pev->solid=6;
    Check(f.Run(source),"skill executes with a blocked selected source");
    Check(source.pev->health==10&&second.pev->health==60&&third.pev->health==100&&fourth.pev->health==40,
        "heals first four slots with original health clamp and no team/life filtering");
    Check(fifth.pev->health==10&&f.engine.packets.size()==3,"blocked candidate consumes one of four slots");
    Check(source.pev->maxspeed==1,"skill sets source speed to one");
    Check(f.state.skillActive.interval==1&&f.state.skillActive.deadline==11&&f.state.skillActive.stamp==10,
        "active clock has one-second interval");
    Check(f.state.skill.interval==6.25f&&f.state.skill.deadline==16.25f&&f.state.skill.stamp==10,
        "cooldown is five plus jitter times five");
}
void RadiusAndDeadline()
{
    Fixture f;auto& source=f.Add();
    constexpr float radius=157.4799957275390625f;
    auto& boundary=f.Add(Vector(radius,0,0));
    auto& inside=f.Add(Vector(0,0,std::nextafter(radius,0.0f)));
    auto& above=f.Add(Vector(0,0,radius));
    auto& nonsolid=f.Add();nonsolid.pev->solid=SOLID_NOT;
    auto& diagonal=f.Add(Vector(120,120,0));
    source.SpeedState().impulseProtectionUntil=10;inside.SpeedState().impulseProtectionUntil=-1;
    Check(f.Run(source)&&source.pev->health==60&&inside.pev->health==60,"expired and negative deadlines permit healing");
    Check(boundary.pev->health==10&&above.pev->health==10&&nonsolid.pev->health==10&&diagonal.pev->health==10,
        "strict 3D squared radius and nonsolid exclusion match selector");
    Check(f.engine.packets.size()==2&&f.engine.packets[0].bytes.back()==13&&f.engine.packets[1].bytes.back()==9,
        "self and neighbor use different original effects");
}
void Replacement()
{
    {
        Fixture f;auto& source=f.Add();auto& target=f.Add();auto& later=f.Add();
        bool replaced{};const auto* oldPrivate=target.edict()->pvPrivateData;
        const int serial=target.edict()->serialnumber;
        f.engine.onMessage=[&](char kind)
        {
            if(kind!='e'||replaced)return;replaced=true;
            f.engine.ReplaceWithBase(target.edict());target.pev->health=77;
        };
        Check(f.Run(source)&&replaced&&target.pev->health==77&&later.pev->health==60,
            "same-serial wrong private type is skipped before subclass reads");
        Check(f.engine.packets.size()==2&&target.edict()->serialnumber==serial,"replacement gets no stale heal packet");
        f.engine.onMessage={};f.engine.DestroyPrivate(target.edict());
        target.edict()->pvPrivateData=const_cast<void*>(oldPrivate);
        std::erase(f.engine.orphanedPrivate,const_cast<void*>(oldPrivate));
    }
    {
        Fixture f;auto& source=f.Add();auto& target=f.Add();bool replaced{};
        f.engine.onMessage=[&](char kind)
        {if(kind=='e'&&!replaced){replaced=true;f.Reborrow(target);}};
        Check(f.Run(source)&&replaced&&target.pev->health==17&&f.engine.packets.size()==1,
            "old snapshot cannot heal a new lease at the same address");
    }
}
void Cancellation()
{
    {
        Fixture f;auto& source=f.Add();auto& target=f.Add();
        f.engine.onMessage=[&](char kind){if(kind=='e')f.active=false;};
        Check(!f.Run(source)&&source.pev->health==60&&target.pev->health==10,
            "owner retirement after complete message stops later healing");
        Check(f.error.message.empty()&&f.state.skill.deadline==-1&&f.state.skillActive.deadline==-1,
            "cancellation leaves cooldown clocks unchanged without failure diagnostic");
    }
    {
        Fixture f;auto& source=f.Add();auto& target=f.Add();
        f.engine.onMessage=[&](char kind){if(kind=='e')f.deaths.Reset();};
        Check(!f.Run(source)&&target.pev->health==10&&f.error.message.empty(),
            "request generation reset invalidates actual SpawnCall");
    }
    {
        Fixture f;auto& source=f.Add();
        g_engfuncs.pfnWriteByte=nullptr;
        Check(!f.Run(source)&&f.error.message.find("healing effect message failed")!=std::string::npos,
            "real writer failure is explicit after server health change");
        Check(source.pev->health==60&&f.state.skill.deadline==-1,"failed send does not invent successful clock tail");
    }
}
}
int main()
{
    try{OrderAndBlock();RadiusAndDeadline();Replacement();Cancellation();}
    catch(const std::exception& e){++death_events_test::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"monster-healing "<<death_events_test::checks<<" checks "<<death_events_test::failures<<" failures\n";
    return death_events_test::failures?1:0;
}
