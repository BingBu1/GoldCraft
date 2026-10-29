// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include "precompiled.h"
#include "../../native/csol/server/scenario_player_scan.h"

using namespace csol::scenario;
namespace
{
int checks{},failures{};
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
struct Engine
{
    static Engine* active;
    globalvars_t globals{};
    struct Owned{std::unique_ptr<edict_t> ed;CBaseEntity* entity;bool player;};
    std::vector<Owned> owned;
    std::map<int,edict_t*> slots;
    std::vector<int> lookups;
    std::function<void()> onIndex;
    Engine(int clients=4)
    {
        active=this;gpGlobals=&globals;globals.maxClients=clients;g_engfuncs={};
        g_engfuncs.pfnPvAllocEntPrivateData=Allocate;g_engfuncs.pfnPEntityOfEntIndex=Index;
    }
    ~Engine(){Clear();active=nullptr;gpGlobals=nullptr;g_engfuncs={};}
    void Clear()
    {
        for(auto& o:owned)
        {
            if(o.player)static_cast<CBasePlayer*>(o.entity)->~CBasePlayer();else o.entity->~CBaseEntity();
            std::free(o.entity);
        }
        owned.clear();slots.clear();
    }
    static void* Allocate(edict_t* ed,int32 size)
    {if(ed->pvPrivateData||size<=0)throw std::runtime_error("bad fixture private allocation");return ed->pvPrivateData=std::calloc(1,static_cast<std::size_t>(size));}
    static edict_t* Index(int slot)
    {
        auto& e=*active;e.lookups.push_back(slot);Check(slot>=1&&slot<=e.globals.maxClients,"production looks up one valid current client slot");
        if(e.onIndex)e.onIndex();const auto found=e.slots.find(slot);return found==e.slots.end()?nullptr:found->second;
    }
    CBaseEntity& Add(int slot,bool player=true)
    {
        auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
        auto* raw=ed.get();CBaseEntity* entity=player?static_cast<CBaseEntity*>(new(&raw->v) CBasePlayer):new(&raw->v) CBaseEntity;
        entity->pev=&raw->v;entity->m_pEntity=nullptr;entity->pev->health=100;entity->pev->deadflag=DEAD_NO;
        entity->pev->origin=Vector(0,0,0);slots[slot]=raw;owned.push_back({std::move(ed),entity,player});
        Check((dynamic_cast<CBasePlayer*>(entity)!=nullptr)==player,"real SDK object constructor and RTTI distinguish player/base");
        return *entity;
    }
    CBasePlayer& Player(int slot){return *static_cast<CBasePlayer*>(&Add(slot));}
};
Engine* Engine::active{};
struct Actions:TriggerActions
{
    // Explicit observation state; Current performs no engine/plugin callback.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Current() const override { return observerValid && observerError.message.empty(); }
    int goals{},refresh{};std::vector<std::uint16_t> waves;std::function<void()> callback;
    void ActivateWave(std::uint16_t id,float)override{waves.push_back(id);if(callback)callback();}
    bool EndWave(std::uint16_t,bool)override{return false;}
    void RefreshWaves()override{++refresh;}
    void GoalReached()override{++goals;if(callback)callback();}
};
void Install(TriggerProgram& program,bool wave=false,int ratio=100)
{
    TriggerRuleTable table;TriggerRule rule;rule.group=1;rule.trigger=100;rule.command=wave?'w':'g';
    rule.cooldown=-2;rule.destinationRatio=static_cast<std::int8_t>(ratio);if(wave)rule.waves={42};
    table.rows.push_back(rule);table.groups[1]={0};TriggerVolumes volumes;volumes.Register({{-10,-20,-30},{10,20,30},100,0});
    ConfigError error;Check(program.Install(table,volumes,1,error),"real trigger program fixture installs");
}
void Cycle()
{
    Engine engine(4);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program);
    engine.Player(2).m_iTeam=TERRORIST;engine.Player(3).m_iTeam=CT;engine.Player(4).m_iTeam=SPECTATOR;
    program.ExecuteTrigger(100,0,10,actions);program.ExecuteTrigger(100,0,10,actions);
    Check(program.Arrivals()==2&&scan.NextPlayerSlot()==1,"preexisting arrival count and initial slot known");
    Check(scan.Update(true,program,0,3,actions).valid&&program.Arrivals()==0&&scan.NextPlayerSlot()==2,"empty first client slot resets scan counter and advances");
    scan.Update(true,program,0,3,actions);Check(program.Arrivals()==1&&scan.NextPlayerSlot()==3&&actions.goals==0,"second slot contributes one arrival only");
    scan.Update(false,program,0,3,actions);Check(program.Arrivals()==1&&scan.NextPlayerSlot()==3&&engine.lookups==std::vector<int>{1,2},"disabled scanner neither advances nor clears cycle or queries engine");
    scan.Update(true,program,0,3,actions);Check(program.Arrivals()==2&&scan.NextPlayerSlot()==4,"third slot CT contributes next arrival");
    auto result=scan.Update(true,program,0,3,actions);
    Check(result.goalReached&&program.Arrivals()==3&&scan.NextPlayerSlot()==1&&actions.goals==1,"spectator team has no independent filter and completes shared threshold");
    Check(engine.lookups==std::vector<int>{1,2,3,4},"one exact client slot per enabled update");
    scan.Update(true,program,0,3,actions);Check(program.Arrivals()==0&&scan.NextPlayerSlot()==2&&actions.goals==1,"next cycle clears arrivals even if slot1 remains empty");
}
void Admission()
{
    struct Case{const char* label;int dead;float health;std::uint32_t flags;TeamName team;bool expected;};
    const Case cases[]{
        {"CT alive",DEAD_NO,100,0,CT,true},{"T alive",DEAD_NO,100,0,TERRORIST,true},
        {"unassigned alive",DEAD_NO,100,0,UNASSIGNED,true},{"spectator alive",DEAD_NO,100,FL_SPECTATOR,SPECTATOR,true},
        {"sentinel exact",DEAD_NO,100,0x80000000u,CT,false},{"sentinel combined",DEAD_NO,100,0x80000001u,CT,true},
        {"killflag only",DEAD_NO,100,FL_KILLME,CT,true},{"dead dying",DEAD_DYING,100,0,CT,false},
        {"dead dead",DEAD_DEAD,100,0,CT,false},{"dead nonzero",-1,100,0,CT,false},
        {"health zero",DEAD_NO,0,0,CT,false},{"health negative",DEAD_NO,-1,0,CT,false},
        {"health nan",DEAD_NO,std::numeric_limits<float>::quiet_NaN(),0,CT,false},
        {"health positiveinf",DEAD_NO,std::numeric_limits<float>::infinity(),0,CT,true},
        {"health minpositive",DEAD_NO,std::numeric_limits<float>::min(),0,CT,true}};
    for(const auto& test:cases)
    {
        Engine engine(1);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program);
        auto& player=engine.Player(1);player.pev->deadflag=test.dead;player.pev->health=test.health;
        player.pev->flags=std::bit_cast<int>(test.flags);player.m_iTeam=test.team;player.pev->iuser1=3;
        auto result=scan.Update(true,program,0,1,actions);
        Check(result.valid&&result.goalReached==test.expected&&program.Arrivals()==(test.expected?1u:0u),test.label);
        Check(engine.lookups==std::vector<int>{1}&&scan.NextPlayerSlot()==1,"single-client scan wraps each tick");
    }
    for(int kind=0;kind<4;++kind)
    {
        Engine engine(1);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program);
        auto& entity=engine.Add(1,kind!=0);
        if(kind==1)entity.edict()->free=TRUE;
        if(kind==2)entity.edict()->pvPrivateData=nullptr;
        if(kind==3)entity.pev=nullptr;
        Check(scan.Update(true,program,0,1,actions).valid&&program.Arrivals()==0&&actions.goals==0,"nonplayer free private-null and pev-null slot rejected safely");
    }
}
void PositionAndFreshSlots()
{
    Engine engine(2);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program);
    auto& first=engine.Player(1);first.pev->origin=Vector(10,0,0);first.pev->view_ofs=Vector(-10,0,0);
    auto& second=engine.Player(2);second.pev->origin=Vector(0,0,0);second.pev->view_ofs=Vector(10000,10000,10000);
    scan.Update(true,program,0,10,actions);Check(program.Arrivals()==0,"scanner sends raw origin, strict face excluded despite view-offset moving inside");
    scan.Update(true,program,0,10,actions);Check(program.Arrivals()==1,"interior origin accepted despite view-offset outside");
    first.pev->origin=Vector(0,0,0);engine.slots.erase(1);scan.Update(true,program,0,10,actions);
    Check(program.Arrivals()==0,"slot lookup does not retain prior player pointer after disconnect");
    auto& replacement=engine.Player(2);replacement.pev->deadflag=DEAD_DEAD;
    scan.Update(true,program,0,10,actions);Check(program.Arrivals()==0,"replaced edict in slot is freshly checked rather than old alive object");
}
void GuardsAndCapacity()
{
    Engine engine(4);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program);
    engine.Player(1);engine.Player(2);engine.Player(3);engine.Player(4);
    auto* globals=gpGlobals;gpGlobals=nullptr;Check(!scan.Update(true,program,0,10,actions).valid&&scan.NextPlayerSlot()==1,"missing globals rejects without cursor change");gpGlobals=globals;
    for(float time:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        Check(!scan.Update(true,program,time,10,actions).valid&&scan.NextPlayerSlot()==1&&engine.lookups.empty(),"nonfinite elapsed rejects before slot lookup or cycle clear");
    for(int capacity:{0,-1}){engine.globals.maxClients=capacity;Check(!scan.Update(true,program,0,10,actions).valid&&scan.NextPlayerSlot()==1,"nonpositive capacity rejected without modulo or lookup");}
    engine.globals.maxClients=4;scan.Update(true,program,0,10,actions);scan.Update(true,program,0,10,actions);scan.Update(true,program,0,10,actions);
    Check(scan.NextPlayerSlot()==4&&program.Arrivals()==3,"cursor near old capacity end");
    engine.globals.maxClients=2;scan.Update(true,program,0,10,actions);
    Check(engine.lookups.back()==1&&scan.NextPlayerSlot()==2&&program.Arrivals()==1,"capacity shrink resets out-of-range cursor and starts new cycle before lookup");
    engine.globals.maxClients=4;scan.Update(true,program,0,10,actions);
    Check(engine.lookups.back()==2&&scan.NextPlayerSlot()==3&&program.Arrivals()==2,"capacity increase retains valid current cursor");
    engine.Clear();scan.BeginMap();program.BeginMap();
    Check(scan.NextPlayerSlot()==1&&program.Arrivals()==0,"same-map lifecycle explicit reset clears cursor/program after old players freed");
    engine.Player(1);Install(program);scan.Update(true,program,0,1,actions);
    Check(engine.lookups.back()==1&&scan.NextPlayerSlot()==2&&program.Arrivals()==1,"fresh map uses newly allocated slot1 only");
    scan.BeginMap();scan.BeginMap();Check(scan.NextPlayerSlot()==1,"repeated scanner BeginMap safe");
}
void CallbackCancellation()
{
    for(bool diagnostic:{false,true})for(bool duringIndex:{false,true})
    {
        Engine engine(3);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;
        Install(program,true);engine.Player(1);
        auto cancel=[&]{actions.observerValid=false;if(diagnostic)actions.observerError={4,"scan owner unavailable"};};
        if(duringIndex)engine.onIndex=cancel;else actions.callback=cancel;
        const auto result=scan.Update(true,program,0,2,actions);
        Check(!result.valid&&scan.NextPlayerSlot()==1&&!scan.IsScanning()&&engine.lookups==std::vector<int>{1},
            "INDEXENT or trigger callback cancellation leaves visited slot cursor unadvanced and releases scan guard");
        Check(actions.waves==(duringIndex?std::vector<std::uint16_t>{}:std::vector<std::uint16_t>{42})&&
            actions.refresh==0&&program.UsedTriggers().empty(),
            "cancelled scan retains only any completed activation and never reaches summary or used suffix");
        engine.onIndex={};actions.callback={};actions.observerValid=true;actions.observerError={};
        Check(scan.Update(true,program,0,2,actions).valid&&scan.NextPlayerSlot()==2&&
            engine.lookups==std::vector<int>({1,1}), "a new valid scan revisits the cancelled slot before advancing");
    }
    {
        Engine engine(3);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;
        Install(program,false,50);engine.Player(1);
        actions.callback=[&]{actions.observerValid=false;};
        const auto result=scan.Update(true,program,0,2,actions);
        Check(!result.valid&&!result.goalReached&&scan.NextPlayerSlot()==1&&program.Arrivals()==1&&actions.goals==1,
            "goal callback cancellation keeps arrival prefix but reports no completed goal and does not advance slot");
    }
}
void Reentry()
{
    Engine engine(3);CCSOLTriggerPlayerScan scan;TriggerProgram program;Actions actions;Install(program,true);engine.Player(1);engine.Player(2);
    int nested{};actions.callback=[&]
    {
        ++nested;const auto before=engine.lookups.size();Check(!scan.Update(true,program,0,2,actions).valid&&engine.lookups.size()==before,"action callback scanner reentry cannot query another slot");
        scan.BeginMap();Check(scan.NextPlayerSlot()==1,"BeginMap during scan does not reset or advance in-flight cursor");
    };
    auto r=scan.Update(true,program,0,2,actions);Check(r.valid&&r.waveCommands==1&&nested==1&&scan.NextPlayerSlot()==2&&engine.lookups==std::vector<int>{1},"outer scan advances once after rejected callback reentry");
    actions.callback={};engine.onIndex=[&]
    {
        const auto before=engine.lookups.size();Check(!scan.Update(true,program,0,2,actions).valid&&engine.lookups.size()==before,"INDEXENT service callback cannot recursively advance scanner");
    };
    r=scan.Update(true,program,0,2,actions);Check(r.waveCommands==1&&scan.NextPlayerSlot()==3&&engine.lookups==std::vector<int>{1,2},"second slot remains functional after reentry attempts");
    engine.onIndex={};
}
}
int main()
{
    try{Cycle();Admission();PositionAndFreshSlots();GuardsAndCapacity();CallbackCancellation();Reentry();}
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::cout<<"player scan checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
