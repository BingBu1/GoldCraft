// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../csol-scenario-rules-session/resources.hpp"
#include "../../native/csol/server/scenario_session.h"

BotPhraseManager* TheBotPhrases{};
Place BotPhraseManager::NameToID(const char*) const
{ throw std::runtime_error("unexpected real Bot phrase lookup: this fixture supplies no place names"); }
extern "C" void zbstrigger(entvars_t*);
namespace nav_loader_test
{
namespace
{
struct SessionHost : PoolEngine
{
    Files files;
    std::function<void()> onEnumerate;
    inline static SessionHost* self{};
    std::size_t enumerations{};
    SessionHost()
    {
        self=this;globals.maxEntities=65536;files.contents=session_test::Resources();
        files.contents[difficulty_test::DefaultPath]=difficulty_test::Encrypt("header\r\n1,normal,1\r\n");
        files.contents[difficulty_test::DropPath]=difficulty_test::Encrypt(difficulty_test::TinyDrops);
        files.contents["maps/map_session_test.nav"]=Encode(LadderFile());
        g_engfuncs.pfnLoadFileForMe=Files::Load;g_engfuncs.pfnFreeFile=Files::Free;
        g_engfuncs.pfnGetFileSize=[](const char* path){Check(std::string_view(path)=="maps/map_session_test.bsp","session checks current map BSP identity");return 999;};
        g_engfuncs.pfnRegUserMsg=[](const char*,int){return 96;};
        g_engfuncs.pfnPrecacheSound=[](const char*){return 1;};
        g_engfuncs.pfnIsDedicatedServer=[](){return 1;};
        g_engfuncs.pfnFindEntityByString=[](edict_t*,const char* key,const char* value)->edict_t*
        {
            auto& s=*self;++s.enumerations;Check(std::string_view(key)=="classname"&&std::string_view(value)=="func_ladder","session ladder scan occurs at activation");
            const auto callback=s.onEnumerate;if(callback)callback();return nullptr;
        };
        g_engfuncs.pfnTraceLine=[](const float*,const float*,int,edict_t*,TraceResult*){throw std::runtime_error("empty session ladder scan must not trace");};
        g_engfuncs.pfnCreateNamedEntity=[](int name)->edict_t*
        {return std::strcmp(STRING(name),"monster_entity")==0?CreatePoolEntity(name):Engine::Create(name);};
    }
    ~SessionHost(){onEnumerate={};self=nullptr;}
    void Markers()
    {
        Player(101,1,2);Monster(201,1,5,1);
        for(int id:{1,2,30})
        {
            auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
            auto* raw=ed.get();indices[raw]=300+id;edicts.push_back(std::move(ed));
            raw->v.classname=AllocString("zbstrigger");raw->v.model=AllocString("*1");zbstrigger(&raw->v);
            auto* trigger=static_cast<CCSOLScenarioTrigger*>(raw->pvPrivateData);trigger->m_iId=id;trigger->Spawn();
            trigger->pev->mins=Vector(-5,-5,-5);trigger->pev->maxs=Vector(5,5,5);
        }
    }
};
}
void SessionCases()
{
    {
        SessionHost host;CCSOLScenarioSession session;
        Require(session.Precache("map_session_test",1),"real Session precaches existing resources plus NAV before monsters");
        auto old=session.Navigation();Require(old&&old->World().Ready(),"session retains ready NAV lease");
        std::weak_ptr<const CCSOLMonsterNavigation> weak=old;
        Check(host.enumerations==0&&session.State()==CSOLScenarioSessionState::Precached,"precache does not enumerate func_ladder before map activation");
        auto concrete=std::dynamic_pointer_cast<const CCSOLScenarioNavigation>(old);
        Check(concrete&&concrete->BspSizeMismatch(),"session continues through BSP mismatch diagnostic");concrete.reset();
        host.Markers();host.onEnumerate=[&]
        {Check(!session.Precache("map_session_test")&&!session.Deactivate()&&!session.Activate(),"actual ladder callback cannot reenter busy Session");};
        Require(session.Activate()&&session.State()==CSOLScenarioSessionState::Active,"real Session activates map and ladder graph");host.onEnumerate={};
        Check(host.enumerations==1,"one actual ladder enumeration at activation");
        Require(session.Deactivate(),"session invalidates NAV before pool and map release");
        Check(session.State()==CSOLScenarioSessionState::Empty&&!session.Navigation()&&!old->World().Ready()&&!weak.expired(),"external old lease survives session reset but is unusable for new queries");
        old.reset();Check(weak.expired(),"last external session NAV lease releases storage");
    }
    for(int failure=0;failure<3;++failure)
    {
        SessionHost host;CCSOLScenarioSession session;
        if(failure==0)host.files.contents.erase("maps/map_session_test.nav");
        if(failure==1)host.files.contents["maps/map_session_test.nav"]="broken";
        if(failure==2){NavigationFile empty;host.files.contents["maps/map_session_test.nav"]=Encode(empty);}
        const bool precached=session.Precache("map_session_test",1);
        if(failure<2)Check(!precached&&session.State()==CSOLScenarioSessionState::Failed&&host.creates==0,"missing or corrupt NAV prevents pool construction");
        else
        {
            Require(precached,"zero-area metadata can precache");host.Markers();
            Check(!session.Activate()&&session.State()==CSOLScenarioSessionState::Failed,"zero-area world cannot activate playable session");
        }
        Check(!session.Error().detail.message.empty(),"session NAV failure reason retained");
        Require(session.Deactivate()&&!session.Navigation(),"partial failed Session releases navigation and other resources");
    }
}
}
