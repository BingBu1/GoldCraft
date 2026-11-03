// SPDX-License-Identifier: GPL-3.0-or-later
// The two included methods are extracted unchanged from current production
// GameRules and the pinned ReGameDLL timer accessor by prepare.py.
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
struct Globals { float time{}; } globals, *gpGlobals=&globals;
enum class Phase { Playing, Failed, Advancing, Completing, FreePlay };
struct Tables { int scenarioType{}; };
struct Map { ::Tables tables; const ::Tables* Tables() const {return &tables;} };
struct Session { ::Map map; auto& Map(){return map;} };
struct Runtime { std::shared_ptr<Session> session=std::make_shared<Session>(); };
struct Rules
{
    bool ready=true,started=true,m_bFreezePeriod=false,m_bRoundTerminating=false,m_completionLocked=false;
    int m_aliveCt=1,m_iRoundTimeSecs=180,goals{};
    float m_fRoundStartTime=1000;
    Phase m_phase=Phase::Playing;
    std::shared_ptr<Runtime> m_runtime=std::make_shared<Runtime>();
    bool Ready()const{return ready;}
    bool IsGameStarted()const{return started;}
    void GoalReached(){++goals;m_completionLocked=true;m_phase=Phase::Advancing;}
#include "actual_timeout.inc"
};
int main()
{
    int checks{},failures{};
    auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}};
    for(int gate=0;gate<13;++gate)
    {
        Rules rules;globals.time=1180;
        if(gate==0)rules.ready=false;
        if(gate==1)rules.started=false;
        if(gate==2)rules.m_phase=Phase::Failed;
        if(gate==3)rules.m_bFreezePeriod=true;
        if(gate==4)rules.m_bRoundTerminating=true;
        if(gate==5)rules.m_completionLocked=true;
        if(gate==6)rules.m_aliveCt=0;
        if(gate==7)rules.m_runtime->session->map.tables.scenarioType=1;
        if(gate==8)rules.m_runtime->session->map.tables.scenarioType=5;
        if(gate==9)globals.time=std::numeric_limits<float>::quiet_NaN();
        if(gate==10)rules.m_fRoundStartTime=std::numeric_limits<float>::infinity();
        if(gate==11)gpGlobals=nullptr;
        if(gate==12)rules.m_phase=Phase::FreePlay;
        rules.CheckSurvivalTimeout();gpGlobals=&globals;
        check(rules.goals==0,"closed lifecycle cannot report survival success");
    }
    for(float start:{0.0f,20.0f,1000.0f,5000.0f})
    {
        Rules rules;rules.m_fRoundStartTime=start;
        globals.time=start+179.99f;rules.CheckSurvivalTimeout();
        check(rules.goals==0,"no completion before the native deadline");
        globals.time=start+180;rules.CheckSurvivalTimeout();
        check(rules.goals==1,"exact native180 second boundary completes");
        globals.time+=1;rules.CheckSurvivalTimeout();
        check(rules.goals==1,"completion phase prevents duplicate score/cleanup");
    }
    Rules rule;rule.m_fRoundStartTime=1020;globals.time=1180;
    rule.CheckSurvivalTimeout();check(rule.goals==0,"20 second preparation does not consume survival duration");
    globals.time=1200;rule.CheckSurvivalTimeout();check(rule.goals==1,"deadline follows actual thaw time");
    std::printf("survival actual timeout methods: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
