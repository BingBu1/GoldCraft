// SPDX-License-Identifier: GPL-3.0-or-later
// Unchanged production rules methods, with controlled owner/engine boundaries.
// Adapter startup is covered separately by startup.cpp; the real vote core is
// used here to advance deadlines without constructing a game process.
#include "../../native/csol/scenario/core/map_catalog.hpp"
#include "../../native/csol/scenario/core/map_support.hpp"
#include "../../native/csol/scenario/core/map_vote.hpp"
#include "../../native/csol/server/game_mode.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using namespace csol::scenario;
using namespace csol::scenario::map_vote;
unsigned checks{}, failures{}, starts{}, updates{}, cancels{}, changes{}, warnings{};
void Check(bool value,const char* label)
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
std::filesystem::path resourceRoot;
std::function<void()> startHook, updateHook, loadHook, releaseHook, validHook, changeHook;
bool startAvailable{true}, mapAvailable{true}, policyValid{true};
CSOLScenarioFinishPolicy requestedPolicy;
std::vector<std::string> changedMaps;
Choice suppliedChoice{98,"zs_deadend"};
struct Globals {float time{10},frametime{.01f};} globals,*gpGlobals=&globals;
Vote ballot;std::uint32_t generation{};
bool CSOL_StartScenarioMapVote()
{
    ++starts;if(startHook){auto h=std::move(startHook);h();}
    const std::array choices{suppliedChoice};
    return startAvailable &&
        ballot.Begin(++generation,choices,"zs_lostcity",globals.time,20);
}
std::optional<Choice> CSOL_UpdateScenarioMapVote()
{++updates;if(updateHook){auto h=std::move(updateHook);h();}return ballot.Finish(globals.time,0);}
bool CSOL_ScenarioMapVoteActive(){return ballot.State()==csol::scenario::map_vote::Phase::Open;}
void CSOL_CancelScenarioMapVote(){++cancels;ballot.Cancel();}
bool CSOL_RequestedScenarioDifficulty(std::uint16_t& value){value=1;return true;}
bool CSOL_RequestedScenarioFinishPolicy(CSOLScenarioFinishPolicy& value){value=requestedPolicy;return policyValid;}
void CSOL_RejectGameRules(const char*){++warnings;}
constexpr int HUD_PRINTTALK=3,at_console=0,FALSE=0,DEAD_NO=0;
void UTIL_ClientPrintAll(int,const char*){++warnings;}
void Alert(int,const char*,...){++warnings;}
unsigned char* Load(const char* name,int* length)
{
    if(loadHook){auto h=std::move(loadHook);h();}
    std::ifstream f(resourceRoot/name,std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(f)),{});
    *length=int(value.size());if(value.empty())return nullptr;
    auto* data=static_cast<unsigned char*>(std::malloc(value.size()));std::memcpy(data,value.data(),value.size());return data;
}
void Release(void* data)
{std::free(data);if(releaseHook){auto h=std::move(releaseHook);h();}}
int MapValid(char* name)
{if(validHook){auto h=std::move(validHook);h();}return mapAvailable&&std::filesystem::is_regular_file(resourceRoot/"maps"/(std::string(name)+".bsp"));}
void Change(char* name,char*)
{++changes;changedMaps.emplace_back(name);if(changeHook){auto h=std::move(changeHook);h();}}
struct Engine {
    unsigned char* (*pfnLoadFileForMe)(const char*,int*){Load};void (*pfnFreeFile)(void*){Release};
    int (*pfnIsMapValid)(char*){MapValid};void (*pfnChangeLevel)(char*,char*){Change};
    void (*pfnAlertMessage)(int,const char*,...){Alert};
} g_engfuncs;
#define IS_MAP_VALID(name) g_engfuncs.pfnIsMapValid(name)
#define CHANGE_LEVEL(name,landmark) g_engfuncs.pfnChangeLevel(name,landmark)
#define ALERT(...) g_engfuncs.pfnAlertMessage(__VA_ARGS__)
struct Access {bool allowed{true};bool Allowed()const{return allowed;}};
struct Tables {std::optional<int> mapId{74};int scenarioType{};};
struct Map {
    Tables table;bool tableAvailable{true};std::uint64_t round{1};std::shared_ptr<Access> access=std::make_shared<Access>();
    auto EntityAccess()const{return access;}std::uint64_t RoundGeneration()const{return round;}
    const ::Tables* Tables()const{return tableAvailable?&table:nullptr;}
};
struct Session {
    ::Map map;bool busy{};bool Busy()const{return busy;}::Map& Map(){return map;}
    std::unique_ptr<int> PrepareReplaySettings(std::uint16_t,ConfigError&){return std::make_unique<int>(1);}
};
struct Gameplay {
    bool busy{};unsigned ticks{};bool Busy()const{return busy;}
    template<class T>bool Update(T&,float,int,T&){++ticks;return true;}
};
struct Runtime {
    bool closing{};std::shared_ptr<Session> session=std::make_shared<Session>();
    std::shared_ptr<Gameplay> gameplay=std::make_shared<Gameplay>();
    bool SetClearPointProtection(bool){return true;}
};
class CCSOLScenarioGameplay {public:static bool SupportsMap(std::string_view map,int subtype){return HasMapGameplay(map,subtype);}};
struct CBaseEntity {};
struct CBasePlayer : CBaseEntity {
    struct Vars{int deadflag{};} vars,*pev{&vars};bool m_bCanShoot{};
    void RoundRespawn(){}void ResetMaxSpeed(){}void SyncRoundTimer(){}
};
struct EHANDLE {
    CBasePlayer* player;EHANDLE(CBasePlayer* p):player(p){}
    template<class T>T* Get()const{return static_cast<T*>(player);}
};
struct Gate {
    enum class Phase {Loading,Selecting,Waiting,Starting,Playing,Advancing,Completing,Failed,Complete,FreePlay};
    enum class AdminMapVote {Idle,Requested,Open};
    std::shared_ptr<Runtime> m_runtime=std::make_shared<Runtime>();
    AdminMapVote m_adminMapVote{AdminMapVote::Idle};Phase m_phase{Phase::Playing};
    bool owner{true},m_bGameOver{},m_mapVoteStarted{},m_restarting{},m_completing{},m_applyingFinish{},m_itemsPrepared{};
    bool m_bRoundTerminating{},m_bFreezePeriod{},m_bGameStarted{true};
    float m_flRestartRoundTime{},m_freezeUntil{},m_fRoundStartTime{},m_fRoundStartTimeReal{};
    int m_aliveCt{},m_iRoundTime{},m_iRoundTimeSecs{};unsigned restarts{},winChecks{};
    std::unique_ptr<int> m_replaySettings;
    struct Voice {void Update(float){}}m_VoiceGameMgr;
    struct Music {void Thaw(float){}}m_musicCountdown;
    std::function<void()> winHook;
    bool SelectionReady()const{return owner&&!m_runtime->closing&&m_runtime->session&&m_runtime->session->Map().access->Allowed();}
    bool Ready()const{return SelectionReady()&&m_runtime->gameplay;}
    bool PublishPlayerClips(){return true;}bool PublishTeamKills(){return true;}
    bool IsGameStarted(){return m_bGameStarted;}
    void RestartRound(){++restarts;m_replaySettings.reset();m_phase=Phase::Playing;}
    void CheckWinConditions(){++winChecks;if(winHook){auto h=std::move(winHook);h();}}
    void CheckSurvivalTimeout(){}void FailGameplay(){owner=false;}void UpdateMusic(){}
    bool Spawnable(CBasePlayer&){return true;}bool PreparePlayer(CBasePlayer&){return true;}
    template<class F>void ForPlayers(F&&){}
#include "actual_rules.inc"
};
void Reset()
{
    starts=updates=cancels=changes=warnings=0;globals={10,.01f};ballot.Reset();generation=0;changedMaps.clear();
    startHook={};updateHook={};loadHook={};releaseHook={};validHook={};changeHook={};startAvailable=mapAvailable=policyValid=true;
    requestedPolicy={};requestedPolicy.action=CSOLScenarioFinishAction::Vote;g_engfuncs={};
    suppliedChoice={98,"zs_deadend"};
}
void NormalLifecycle()
{
    Reset();Gate gate;
    Check(!gate.RequestMapVote(98),"stale mapId cannot queue an administrator vote");
    Check(gate.RequestMapVote(74)&&starts==0,"administrator request queues without file/message callbacks");
    Check(!gate.RequestMapVote(74),"duplicate request rejected before frame");
    gate.ScenarioThink();
    Check(starts==1&&updates==1&&CSOL_ScenarioMapVoteActive()&&gate.m_adminMapVote==Gate::AdminMapVote::Open,"actual Playing frame starts and updates queued ballot");
    Check(gate.m_runtime->gameplay->ticks==1,"normal gameplay keeps running during administrator vote");
    Check(!gate.RequestMapVote(74),"active vote rejects another request");
    globals.time=29.99f;gate.ScenarioThink();Check(changes==0,"Playing frames retain fixed ballot deadline");
    globals.time=30;gate.ScenarioThink();
    Check(changes==1&&changedMaps==std::vector<std::string>{"zs_deadend"}&&gate.m_bGameOver,"deadline winner reaches validated ChangeLevel exactly once");
    gate.ScenarioThink();gate.ScenarioThink();Check(changes==1&&starts==1,"later frames cannot replay consumed winner");
    Check(!gate.RequestMapVote(74),"game-over rules reject another request");
}
void ReplayPriority()
{
    for(bool open:{false,true}) {
        Reset();Gate gate;Check(gate.RequestMapVote(74),"setup queued vote before replay");
        if(open)gate.ScenarioThink();
        Check(gate.RequestReplay(74),"actual Replay API accepts before frame");globals.time=40;gate.ScenarioThink();
        Check(gate.restarts==1&&cancels==1&&changes==0&&gate.m_adminMapVote==Gate::AdminMapVote::Idle,"actual frame gives Replay priority and cancels queued/open vote");
        Check(!CSOL_ScenarioMapVoteActive(),"Replay cancellation leaves no live ballot");
    }
    Reset();Gate gate;gate.m_replaySettings=std::make_unique<int>(1);
    Check(!gate.RequestMapVote(74),"pending Replay prevents new administrator vote");
}
void CallbackCancellation()
{
    for(unsigned stage=0;stage<5;++stage)for(unsigned reason=0;reason<6;++reason) {
        Reset();Gate gate;Check(gate.RequestMapVote(74),"queue callback cancellation case");
        if(stage)gate.ScenarioThink();
        const auto cancel=[&gate,reason](){
            if(reason==0)gate.owner=false;
            if(reason==1)gate.m_runtime->session=std::make_shared<Session>();
            if(reason==2)++gate.m_runtime->session->Map().round;
            if(reason==3)gate.m_runtime->session->Map().access=std::make_shared<Access>();
            if(reason==4)gate.m_runtime=std::make_shared<Runtime>();
            if(reason==5)gate.m_runtime->gameplay=std::make_shared<Gameplay>();
        };
        if(stage==0)startHook=cancel;
        if(stage==1)updateHook=cancel;
        if(stage==2)loadHook=cancel;
        if(stage==3)releaseHook=cancel;
        if(stage==4)validHook=cancel;
        globals.time=30;gate.ScenarioThink();
        Check(changes==0,"owner/session/round/access/runtime/gameplay change at start/update/catalog/release/existence callback prevents winner commit");
        Check(!gate.m_applyingFinish,"callback invalidation releases finish guard");
    }
    for(unsigned stage=0;stage<5;++stage) {
        Reset();Gate gate;Check(gate.RequestMapVote(74),"queue reentrant Replay case");if(stage)gate.ScenarioThink();
        bool accepted=true;const auto replay=[&](){accepted=gate.RequestReplay(74);};
        if(stage==0)startHook=replay;if(stage==1)updateHook=replay;if(stage==2)loadHook=replay;if(stage==3)releaseHook=replay;if(stage==4)validHook=replay;
        globals.time=30;gate.ScenarioThink();
        Check(!accepted&&!gate.m_replaySettings,"actual Replay request is rejected inside protected vote callback");
        Check(changes==(stage?1u:0u),"rejected reentrant Replay does not mutate the accepted vote or forge a new pending replay");
    }
}
void AutomaticOverlap()
{
    Reset();Gate gate;Check(gate.RequestMapVote(74),"queue before automatic completion deadline");
    gate.m_phase=Gate::Phase::Completing;gate.m_flRestartRoundTime=1;gate.ScenarioThink();
    Check(starts==1&&!gate.m_mapVoteStarted&&gate.m_adminMapVote==Gate::AdminMapVote::Open,"Completing deadline does not start a second automatic ballot while administrator vote owns one");
    globals.time=30;gate.ScenarioThink();Check(changes==1&&starts==1,"administrator result wins completion overlap once");
    Reset();Gate automatic;automatic.m_phase=Gate::Phase::Completing;automatic.m_flRestartRoundTime=1;
    automatic.ScenarioThink();Check(starts==1&&automatic.m_mapVoteStarted,"actual automatic completion still starts a vote");
    Check(!automatic.RequestMapVote(74),"automatic ballot excludes administrator duplicate");
    globals.time=30;automatic.ScenarioThink();Check(changes==1,"automatic ballot winner reuses validated ChangeScenarioMap");
    automatic.ScenarioThink();Check(changes==1,"automatic terminal path cannot issue duplicate ChangeLevel");
}
void Rejections()
{
    for(unsigned reason=0;reason<7;++reason) {
        Reset();Gate gate;
        if(reason==0)gate.owner=false;if(reason==1)gate.m_runtime->gameplay->busy=true;
        if(reason==2)gate.m_runtime->session->busy=true;if(reason==3)gate.m_restarting=true;
        if(reason==4)gate.m_completing=true;if(reason==5)gate.m_applyingFinish=true;
        if(reason==6)gate.m_runtime->session->Map().tableAvailable=false;
        Check(!gate.RequestMapVote(74)&&starts==0,"unavailable/busy/reentrant rules cannot queue or start");
    }
    Reset();Gate failed;startAvailable=false;Check(failed.RequestMapVote(74),"unavailable resources discovered at safe frame");failed.ScenarioThink();
    Check(starts==1&&warnings==1&&changes==0&&failed.m_adminMapVote==Gate::AdminMapVote::Idle,"startup failure reports once and releases administrator state");
    Reset();Gate lost;Check(lost.RequestMapVote(74),"queue removed-map case");lost.ScenarioThink();globals.time=30;mapAvailable=false;lost.ScenarioThink();
    Check(changes==0&&warnings==1&&!lost.m_bGameOver,"winner BSP revalidation failure keeps running map");
    Reset();Gate nested;Check(nested.RequestMapVote(74),"queue nested change callback");nested.ScenarioThink();globals.time=30;changeHook=[&](){nested.ScenarioThink();};nested.ScenarioThink();
    Check(changes==1,"ChangeLevel callback reentry cannot resubmit winner");
}
void CurrentMapReload()
{
    for(bool automatic:{false,true}) {
        Reset();Gate gate;suppliedChoice={74,"zs_lostcity"};
        if(automatic){gate.m_phase=Gate::Phase::Completing;gate.m_flRestartRoundTime=1;}
        else Check(gate.RequestMapVote(74),"administrator queues vote containing the current map");
        gate.ScenarioThink();
        Check(CSOL_ScenarioMapVoteActive(),"actual rules start a ballot whose only choice is the current map");
        Check(ballot.Connect(1,101)&&ballot.Cast(1,101,generation,74,globals.time)==Ballot::Accepted,
            "current map receives a vote through the real core");
        globals.time=30;gate.ScenarioThink();
        Check(changes==1&&changedMaps==std::vector<std::string>{"zs_lostcity"}&&gate.m_bGameOver,
            "current-map winner reaches actual ChangeScenarioMap and requests same-name ChangeLevel");
        Check(gate.restarts==0&&gate.m_phase==Gate::Phase::Complete,
            "current-map victory does not continue old gameplay or use in-place Replay");
        gate.ScenarioThink();Check(changes==1,"same-map ChangeLevel request is consumed exactly once");
    }
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;resourceRoot=argv[1];
    NormalLifecycle();ReplayPriority();CallbackCancellation();AutomaticOverlap();Rejections();CurrentMapReload();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
