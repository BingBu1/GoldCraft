// SPDX-License-Identifier: GPL-3.0-or-later
#include "csol/server/scenario_bgm_trigger.h"
#include "csol/server/scenario_music.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace fixture {
int checks{},fades{},plays{},rejected{},precaches{};bool current=true;
std::vector<CBaseEntity*> entities;
auto access=std::make_shared<CSOLScenarioEntityAccess>();
auto music=std::make_shared<CCSOLScenarioMusic>(access);
std::string last;bool looping{};
void Check(bool ok,const std::source_location where=std::source_location::current())
{++checks;if(!ok)throw std::runtime_error("trigger BGM line "+std::to_string(where.line()));}
}
class EHANDLE {
public:EHANDLE(CBaseEntity* value):p(value){}template<class T>T* Get()const{return p&&p->alive?dynamic_cast<T*>(p):nullptr;}
private:CBaseEntity* p{};
};
class CSOLScenarioSdkScope {
public:bool Current()const{return fixture::current;}bool Scenario()const{return fixture::current;}
std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess()const{return fixture::access;}
};
bool FStrEq(const char* a,const char* b){return !std::strcmp(a,b);}
constexpr int TRUE=1,SOLID_NOT=0;
void CSOL_RejectGameRules(const char*){++fixture::rejected;}
std::shared_ptr<CCSOLScenarioMusic> CSOL_ScenarioMusic(const CSOLScenarioEntityAccess*){return fixture::music;}
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* previous,const char*)
{
    bool take=!previous;
    for(auto* entity:fixture::entities){if(take)return entity;if(entity==previous)take=true;}
    return nullptr;
}
bool CCSOLScenarioMusic::PrecacheFile(std::string_view name,csol::scenario::ResourceError& error)
{++fixture::precaches;if(!csol::music::ResourcePath(name)){error.detail.message="invalid";return false;}return true;}
bool CCSOLScenarioMusic::Play(std::string_view name,bool loop)
{++fixture::plays;fixture::last=name;fixture::looping=loop;++m_state.revision;return true;}
bool CCSOLScenarioMusic::Fade(){++fixture::fades;++m_state.revision;return true;}
#include "actual_trigger.inc"
void Configure(CCSOLTriggerBGM& trigger,const char* filename,const char* loop="1")
{
    KeyValueData key{"filename",filename};trigger.KeyValue(&key);fixture::Check(key.fHandled);
    key={"loop",loop};trigger.KeyValue(&key);fixture::Check(key.fHandled);trigger.Spawn();
}
int main()
{
    using namespace fixture;
    try{
        CCSOLTriggerBGM a,b,empty,invalid;
        entities={&a,&b,&empty,&invalid};Configure(a,"Scenario_Normal.mp3");Configure(b,"Scenario_Rush.mp3","0");
        Check(precaches==2);Check(a.pev->solid==1);
        a.PlayUse(nullptr,nullptr,USE_OFF,0);Check(plays==1&&fades==0&&looping);
        a.PlayUse(nullptr,nullptr,USE_ON,0);Check(plays==1&&fades==1);
        a.PlayUse(nullptr,nullptr,USE_SET,0);Check(plays==2&&fades==1);
        b.PlayUse(nullptr,nullptr,USE_TOGGLE,0);Check(plays==3&&last=="Scenario_Rush.mp3"&&!looping);
        a.OnDestroy();Check(fades==1); // New active entity owns the stream.
        b.OnDestroy();Check(fades==2);
        Configure(empty,"");Check(precaches==2&&rejected==0);empty.PlayUse(nullptr,nullptr,USE_ON,0);Check(plays==3);
        Configure(invalid,"../escape.mp3");Check(rejected==1);invalid.PlayUse(nullptr,nullptr,USE_ON,0);Check(plays==3);
        CCSOLTriggerBGM stage;entities={&stage};stage.pev->spawnflags=2;Configure(stage,"Scenario_Normal.mp3");Check(stage.pev->solid==SOLID_NOT);
        stage.PlayUse(nullptr,nullptr,USE_ON,0);Check(plays==4);
        music->Play("Scenario_Rush.mp3",true);stage.OnDestroy();Check(fades==2); // Later stage survives old trigger removal.
        current=false;CCSOLTriggerBGM ordinary;Configure(ordinary,"Scenario_Normal.mp3");ordinary.PlayUse(nullptr,nullptr,USE_ON,0);Check(plays==5);
        std::cout<<"PASS BGM actual trigger "<<checks<<" checks; engine and music boundaries are fixtures\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
