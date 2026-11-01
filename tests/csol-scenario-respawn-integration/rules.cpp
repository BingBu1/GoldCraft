// SPDX-License-Identifier: GPL-3.0-or-later
// Unchanged ScenarioCheckWinConditions; controlled player enumeration/session
// and real one-shot state transition. No full rules SDK or live game claim.
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
unsigned checks{},sent{},rejects{};bool sendResult=true;
std::function<void()> onSend;
void Check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void CSOL_RejectGameRules(const char*){++rejects;}
struct Messages {bool SendRoundOutcome(bool success)const{Check(!success,"failure uses original outcome false");++sent;if(onSend)onSend();return sendResult;}};
struct Session {auto Messages()const->::Messages{return{};}};
struct Runtime {std::shared_ptr<Session> session=std::make_shared<Session>();struct {int alive{},total{};bool valid{};}ctCounts;};
struct CBasePlayer {bool spawnable=true;};
struct Global {float time=100;}globals,*gpGlobals=&globals;
enum class Phase {Waiting,Starting,Playing,Failed};
struct Rules {
    std::shared_ptr<Runtime> m_runtime=std::make_shared<Runtime>();
    Phase m_phase=Phase::Playing;bool m_completing{},m_applyingFinish{},m_bFreezePeriod{},m_bCompleteReset{},m_bRoundTerminating{};
    int m_aliveCt{},m_iNumSpawnableCT{},alive{},dead=1;float m_flRestartRoundTime=55;
    bool ready=true,selected=true,owns=true;std::array<CBasePlayer,1> players{};
    bool SelectionReady()const{return selected;}bool Ready()const{return ready;}bool OwnsRules()const{return owns;}
    void InitializePlayerCounts(int& aliveT,int& aliveCt,int& deadT,int& deadCt){aliveT=deadT=0;aliveCt=alive;deadCt=dead;}
    template<class F>void ForPlayers(F f){for(auto& p:players)f(p);}bool Spawnable(const CBasePlayer& p)const{return p.spawnable;}
#include "actual_rules.inc"
};
void Reset(){sent=rejects=0;sendResult=true;onSend={};}
int main(){try{
    Reset();Rules rules;rules.ScenarioCheckWinConditions();Check(sent==1&&rules.m_phase==Phase::Failed&&rules.m_bRoundTerminating&&rules.m_flRestartRoundTime==0,"all-dead transition sends one failure");
    for(int i=0;i<100;++i)rules.ScenarioCheckWinConditions();Check(sent==1,"later Think cannot reset failure fade");
    Reset();Rules reentered;onSend=[&]{Check(reentered.m_phase==Phase::Failed,"phase commits before send callback");reentered.ScenarioCheckWinConditions();};
    reentered.ScenarioCheckWinConditions();Check(sent==1,"send callback reentry cannot duplicate overlay");
    Reset();Rules failed;sendResult=false;failed.ScenarioCheckWinConditions();Check(sent==1&&rejects==1,"current failed send rejects rules accurately");
    for(int invalid=0;invalid<4;++invalid){Reset();Rules changed;sendResult=false;onSend=[&]{
        if(invalid==0)changed.ready=false;if(invalid==1)changed.m_runtime=std::make_shared<Runtime>();
        if(invalid==2)changed.m_runtime->session=std::make_shared<Session>();if(invalid==3)changed.m_phase=Phase::Playing;};
        changed.ScenarioCheckWinConditions();Check(sent==1&&rejects==0,"retired/replaced phase does not reject a new owner");}
    for(int gate=0;gate<6;++gate){Reset();Rules gated;if(gate==0)gated.selected=false;if(gate==1)gated.m_completing=true;if(gate==2)gated.m_applyingFinish=true;
        if(gate==3)gated.m_bFreezePeriod=true;if(gate==4)gated.alive=1;if(gate==5)gated.ready=false;
        gated.ScenarioCheckWinConditions();Check(sent==0,"preconditions prevent premature round failure");}
    Reset();Rules waiting;waiting.m_phase=Phase::Waiting;waiting.ScenarioCheckWinConditions();Check(sent==0&&waiting.m_phase==Phase::Starting&&waiting.m_flRestartRoundTime==103,"first spawn still enters starting phase normally");
    std::cout<<"respawn failure transition checks="<<checks<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<checks<<": "<<error.what()<<'\n';return 1;}}
