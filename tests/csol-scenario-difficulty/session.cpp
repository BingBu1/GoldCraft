// SPDX-License-Identifier: GPL-3.0-or-later
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_session.h"
#include "../../native/csol/server/game_mode.h"
#include "../csol-scenario-rules-session/resources.hpp"
#include "support.hpp"
using namespace difficulty_test;
using namespace csol::scenario;
extern "C" void zbstrigger(entvars_t*);
namespace
{
std::map<std::string,cvar_t*> registeredVars;
std::string requestedText;
int registrations{};
void Register(cvar_t* cvar)
{
    ++registrations;Require(cvar&&cvar->name&&cvar->string,"real CVAR declaration");
    Check(std::string_view(cvar->string)=="1"&&(cvar->flags&FCVAR_SERVER),"registered modes and difficulty default one server flag");
    Require(registeredVars.emplace(cvar->name,cvar).second,"each CVAR registered once");
}
void Request(const char* text)
{
    auto* c=registeredVars.at("csol_scenario_difficulty");requestedText=text?text:"";
    c->string=text?requestedText.data():nullptr;
}
struct EngineForSession:PoolEngine
{
    inline static EngineForSession* self{};
    Files files;
    EngineForSession()
    {
        self=this;files.contents=session_test::Resources();
        files.contents[DefaultPath]=Encrypt("h,n,v\r\n1,first,2\r\n2,second,3\r\n");
        files.contents[DropPath]=Encrypt(TinyDrops);
        globals.maxEntities=65536;
        g_engfuncs.pfnLoadFileForMe=Files::Load;g_engfuncs.pfnFreeFile=Files::Free;
        g_engfuncs.pfnCVarRegister=Register;
        g_engfuncs.pfnRegUserMsg=[](const char* name,int size) {
            Check(std::string_view(name)=="MPToCL"&&size==-1,"real registration name and variable size");
            self->files.order.push_back("message");return 96;
        };
        g_engfuncs.pfnPrecacheSound=[](const char*){return 1;};
        g_engfuncs.pfnIsDedicatedServer=[](){return 1;};
        g_engfuncs.pfnCreateNamedEntity=[](int name)->edict_t* {
            if(std::strcmp(STRING(name),"monster_entity")==0)return CreatePoolEntity(name);
            return Engine::Create(name);
        };
        onPrecache=[&]{files.order.push_back("model");};
        onCreate=[&]{files.order.push_back("pool");};
    }
    ~EngineForSession(){onCreate={};onPrecache={};self=nullptr;}
    void Markers()
    {
        Player(101,1,2);Monster(201,1,5,1);
        for(int id:{1,2,30}) {
            auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
            auto* raw=ed.get();indices[raw]=300+id;edicts.push_back(std::move(ed));
            raw->v.classname=AllocString("zbstrigger");raw->v.model=AllocString("*1");zbstrigger(&raw->v);
            auto* trigger=static_cast<CCSOLScenarioTrigger*>(raw->pvPrivateData);trigger->m_iId=id;trigger->Spawn();
            trigger->pev->mins=Vector(-5,-5,-5);trigger->pev->maxs=Vector(5,5,5);
        }
    }
};
struct Events:TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    std::function<void()> callback;
    void WaveActivated(std::uint16_t)override{if(callback)callback();}
    void BeforeSpatialWaveEnd(std::uint16_t)override{if(callback)callback();}
    void WavesChanged(std::span<const std::uint16_t>)override{if(callback)callback();}
    void GoalReached()override{if(callback)callback();}
};
std::string ScalingTable()
{
    std::string out="header\r\n";
    for(int row=1;row<=2;++row) {
        for(int col=0;col<18;++col){if(col)out+=',';out+=col==0?std::to_string(row):col==1?"name":(col==10||col==13||col==16)?std::to_string(row+1):"1";}
        out+="\r\n";
    }
    return out;
}
void Cvars()
{
    EngineForSession e;std::uint16_t out=77;
    Check(!CSOL_RequestedScenarioDifficulty(out)&&out==77,"unregistered CVAR cannot invent difficulty");
    CSOL_InitGameModes();CSOL_InitGameModes();
    Check(registrations==2&&registeredVars.contains("csol_game_mode")&&registeredVars.contains("csol_scenario_difficulty"),"init registers exactly two named CVARs once");
    Require(CSOL_RequestedScenarioDifficulty(out),"initial difficulty readable");Check(out==1,"original room initial value one");
    for(const auto& p:std::vector<std::pair<const char*,int>>{{"1",1},{"2",2},{"65535",65535},{"0007",7}}) {
        Request(p.first);out=77;Check(CSOL_RequestedScenarioDifficulty(out)&&out==p.second,"complete valid uint16 selection");
    }
    for(const char* text:std::array<const char*,13>{nullptr,"","0","65536","4294967296","-1","+1","1.0","1x"," 1","1 ","nan","inf"}) {
        Request(text);out=77;Check(!CSOL_RequestedScenarioDifficulty(out)&&out==77,"invalid selection rejected without changing output");
    }
    Request("1");
}
void Session()
{
    EngineForSession e;CSOL_InitGameModes();Request("2");
    e.files.contents[DefaultPath]=Encrypt(ScalingTable());
    CCSOLScenarioSession session;Events events;std::uint16_t selected{};
    Require(CSOL_RequestedScenarioDifficulty(selected)&&selected==2,"capture actual CVAR for session");
    int callbacks{};
    auto callback=[&](std::string_view) {
        ++callbacks;Request("1");const auto before=e.files.order.size();
        Check(!session.Precache("other",1)&&!session.Activate()&&!session.Deactivate()&&!session.BeginRound(0,0,0,events),"all session mutations denied inside file load/release");
        Check(e.files.order.size()==before,"reentry does not reach another file callback");
    };
    e.files.onLoad=callback;e.files.onFree=callback;
    Require(session.Precache("map_session_test",selected),"real changed Session precaches map drops difficulty models message pool");
    e.files.onLoad={};e.files.onFree={};
    Check(callbacks>10&&session.Difficulty().SelectedRow()==2&&session.DropRules().difficulty==2,"load callback CVAR edits do not split snapshot between tables");
    Check(session.DropRules().Find(1).size()==2&&session.DropRules().Find(1)[1].rewardId==12,"session drop merge used selected two");
    auto drop=std::find(e.files.order.begin(),e.files.order.end(),std::string("load:")+DropPath);
    auto difficulty=std::find(e.files.order.begin(),e.files.order.end(),std::string("load:")+MapPath);
    auto release=std::find(e.files.order.begin(),e.files.order.end(),std::string("free:")+DefaultPath);
    auto model=std::find(e.files.order.begin(),e.files.order.end(),"model");
    auto message=std::find(e.files.order.begin(),e.files.order.end(),"message");
    auto pool=std::find(e.files.order.begin(),e.files.order.end(),"pool");
    Check(drop<difficulty&&difficulty<release&&release<model&&model<message&&message<pool,"real preload order commits resources before models and pool");
    Check(!session.BeginRound(0,0,0,events),"precache alone cannot start round");
    e.Markers();Require(session.Activate(),"actual map entity collection");
    const auto* tables=session.Map().Tables();Require(tables&&!tables->monsterTypes.byId.empty(),"actual type table loaded");
    const auto* type=tables->monsterTypes.Find(tables->monsterTypes.byId.begin()->first);
    Require(type&&type->priority>=0&&type->priority<=2&&type->health>0&&type->health<1000000,"known fixture has a scalable small positive type");
    const auto health=type->health;
    const auto attack=type->attackDamage;const auto raw=tables->monsterTypes.source.sourceBytes;
    int roundCallbacks{};
    events.callback=[&]{
        ++roundCallbacks;
        Check(type->health==health*3,"round callback sees already scaled types");
        Check(!session.BeginRound(0,0,0,events)&&!session.Deactivate(),"round event cannot reenter owner");
    };
    Require(session.BeginRound(0,0,0,events),"session applies multiplier before real first round");
    Check(type==tables->monsterTypes.Find(type->Key())&&type->health==health*3&&type->attackDamage==attack&&tables->monsterTypes.source.sourceBytes==raw,"in-place type scaling preserves pointer raw source and attack");
    const auto once=type->health;
    Require(session.BeginRound(0,1,0,events),"next formal round uses existing scaled types");
    Check(type->health==once&&session.Difficulty().SelectedRow()==2,"repeat round neither doubles multiplier nor refreshes pending CVAR");
    Check(roundCallbacks>0,"actual TriggerRound invoked its event observer");
    events.callback={};Require(session.Deactivate(),"session teardown");
    Check(session.Difficulty().State()==DifficultyState::Empty&&session.DropRules().rows.empty()&&!session.Map().Tables(),"teardown clears both tables and map");
    Require(CSOL_RequestedScenarioDifficulty(selected)&&selected==1,"pending one applies on next complete session");
    Require(session.Precache("map_session_test",selected),"new complete session uses new selection");
    Check(session.Difficulty().SelectedRow()==1&&session.DropRules().difficulty==1,"new session pair matches pending setting");
    Require(session.Deactivate(),"final explicit detach before fixture destruction");
}
void Failures()
{
    for(int kind=0;kind<5;++kind) {
        EngineForSession e;CCSOLScenarioSession s;
        if(kind==0)e.files.contents.erase(DefaultPath);
        if(kind==1)e.files.contents[DefaultPath]="bad";
        if(kind==2)e.files.contents[DropPath]="bad";
        const std::uint16_t selected=kind==3?0:kind==4?3:1;
        Check(!s.Precache("map_session_test",selected)&&s.State()==CSOLScenarioSessionState::Failed,"missing corrupt or unavailable selected resources fail session");
        Check(!s.Activate()&&e.creates==0&&!s.Error().detail.message.empty(),"failed resource stage cannot create pool or activate gameplay");
        Require(s.Deactivate(),"failed partial session cleans owned resources");
        Check(!s.Map().Tables()&&s.Difficulty().State()==DifficultyState::Empty&&s.DropRules().rows.empty(),"failure cleanup removes stale difficulty and drops");
    }
}
}
int main(int argc,char** argv)
{
    try {
        Require(argc==2,"one session group required");const std::string mode=argv[1];
        if(mode=="--cvar")Cvars();else if(mode=="--session"){Session();Failures();}
        else throw std::runtime_error("unknown session group");
    }catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"difficulty-session "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
