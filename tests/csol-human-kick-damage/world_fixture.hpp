// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Actual world/ServerActivate/round owners, extracted from the verified damage
// fixture; no automatic monster spawn and no synthetic Ready override.
#include "../csol-scenario-teddy-bear/fixture_resources.hpp"
#include "../../native/csol/server/scenario/core/scenario_gameplay.h"
#include "../../native/csol/server/scenario/monsters/scenario_elite_services.h"
#include "../../native/csol/scenario/core/human_kick.hpp"
extern "C" void worldspawn(entvars_t*);
namespace kick_world_test
{
using namespace sdk_test;
using csol::scenario::ConfigError;
struct WorldFixture
{
    std::unique_ptr<Engine> storage=std::make_unique<Engine>();
    Engine& e=*storage;
    Engine& engine=e;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    CBasePlayer* actor{};
    inline static std::map<std::string,trap_test::Fixture::Registration> registrations;
    using ResourceSetup = void (*)(std::map<std::string, std::string>&);
    explicit WorldFixture(ResourceSetup setup = nullptr)
    {
        constexpr int priority=0;constexpr float attack=1;
        for(const auto& [name,entry]:registrations)
        {e.Var(name.c_str()).registered=entry.variable;e.Set(name.c_str(),entry.value.c_str());}
        e.files=trap_test::Resources(0);teddy_sdk_test::GroundFixtureResources(e.files);
        using difficulty_test::Encrypt;
        e.files["maps/zbs_zs_deadend.cso"]=Encrypt(
            "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,0,0,0,0,0,0,0,0,0,0,1\r\n");
        e.files["maps/ZBS_ZT_zs_deadend.cso"]=Encrypt(
            "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
            "1,zombi_origin,1,1,1,100,0,100,1,"+std::to_string(priority)+",0,255 255 255,0,1,0,0,0,1\r\n");
        std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
        for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,"+std::to_string(attack)+",1,1,1\r\n";
        e.files["resource/ZBSVER2/ZBS_neededexp.cso"]=Encrypt(levels);
        e.files[csol::scenario::human_kick::FootModel]="fixture";
        e.files[std::string("sound/")+csol::scenario::human_kick::SwingSound]="fixture";
        for(const auto* sound:csol::scenario::human_kick::HitSounds)e.files[std::string("sound/")+sound]="fixture";
        e.files["sound/vox/man/sprint_end_default.wav"]="explicit recovery sound existence fixture";
        e.files["sound/vox/woman/sprint_end_default.wav"]="explicit recovery sound existence fixture";
        for(const char* music:{"Zsh_nightbgm.mp3","Play_BGM_Rex.mp3","BGM_Rex_loop.mp3","BGM_zombie_attack.mp3"})
            e.files[std::string("bgm/")+music]="explicit challenge music existence fixture";
        // Same parsed cold-input shape as csol-oberon-chain/resources.hpp and
        // csol-consumables-host: valid empty mode/radio/action directories.
        e.files["resource/ModeEvent/modeevent.csv"]=";id,name,event\r\n";
        e.files["sound/sentences.json"]=R"({"MRAD_BUFFCLASS_SKILL":{}})";
        std::array<std::string,35> actionColumns;
        actionColumns[0]="model_id";actionColumns[8]="wa05";actionColumns[18]="seq05";
        actionColumns[24]="frame01";actionColumns[34]="vmodelIndex";
        std::string actionHeader;
        for(std::size_t i=0;i<actionColumns.size();++i){if(i)actionHeader+=',';actionHeader+=actionColumns[i];}
        e.files["classes2/emotion.csv"]=actionHeader+"\r\n";
        e.files["resource/ZBSVER2/ZBS_class.cso"]=Encrypt(
            "id,hp,damage,speed,armor,ammo,order\r\n0,20,20,20,20,20,hdsam\r\n");
        e.files["maps/ZBS_DropItem.cso"]=Encrypt("id,diff,effect,p,owner,count,remove,reward\r\n");
        if (setup) setup(e.files);
        e.globals.mapname=Engine::String("zs_deadend");e.globals.time=100;e.Prepare("2");
        g_engfuncs.pfnGetInfoKeyBuffer=[](edict_t*) -> char* {return const_cast<char*>("controlled-userinfo");};
        e.onTrace=[](edict_t*,TraceResult& trace){trace.flFraction=.5f;trace.vecEndPos=Vector(0,0,0);};
        gmsgCurWeapon=0;
        auto* world=e.Reserve(0,"worldspawn");worldspawn(&world->v);
        Require(e.api.pfnSpawn(world)==0&&!CSOL_RulesRejected(),"actual world installs and precaches Combat rules");
        for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int id:{1,2,30})
        {
            if(std::strcmp(name,"zbstrigger")&&id!=1)continue;
            auto* ed=Engine::Create(Engine::String(name));
            if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=CT;}
            if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=TERRORIST;p->m_iMaxSpawn=5;}
            if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))){p->m_iId=id;ed->v.model=Engine::String("*1");}
            Require(e.api.pfnSpawn(ed)==0,"real map marker Spawn");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        }
        e.api.pfnServerActivate(e.edicts.data(),e.nextEntity,e.globals.maxClients);
        Require(!CSOL_RulesRejected(),"actual ServerActivate enters Ready Combat");
        {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());}
        Require(runtime&&runtime->gameplay&&runtime->gameplay->Active(),"real rules registry owns active gameplay");
        session=runtime->session;players=runtime->players;
        Require(runtime->gameplay->BeginRound(0,e.globals.time,0,*runtime->waveEvents),"actual gameplay begins an authored round");
        // The component enters the same real owner initialization sequence as
        // GameRules::RestartRound, without claiming its player-spawn/UI tail.
        ConfigError roundError;
        Require(runtime->eliteManager&&runtime->eliteManager->ResetRound(e.globals.time,
            CCSOLScenarioEliteManager::OriginalSpawnInterval,roundError),roundError.message.c_str());
        Require(runtime->eliteServices&&runtime->eliteServices->BeginRound(e.globals.time,roundError),roundError.message.c_str());
        runtime->ctCounts={1,1,true};
        auto* ed=e.Reserve(1,"player");actor=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT|FL_FAKECLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;
        ed->v.origin=Vector(1000,1000,128); // Keep actual spawn-overlap death policy outside this damage fixture.
        actor->m_iTeam=CT;actor->m_iNumSpawns=1;actor->m_bJustConnected=false;actor->m_iJoiningState=JOINED;actor->has_disconnected=false;
        Require(CSOL_EnsureScenarioBotCharacter(actor,session->Map().EntityAccess()),"real character selects an authored upgrade class");
        ed->v.flags=FL_CLIENT;
        CSOLScenarioPlayerProfile profile;profile.equipment.classModsComplete=true;
        Require(runtime->players->SetProfile(*actor,std::move(profile)),"actual profile registers actor");
        Require(runtime->players->Prepare(*actor),runtime->players->Error().message.c_str());
        // The resource helper installs a complete controlled studio at precache;
        // use the same named sequence descriptors for factory's runtime query.
        teddy_sdk_test::FixtureResources(e.files);
        g_engfuncs.pfnRandomLong=[](int32 low,int32)->int32{return low;};
        float ratio{};Require(CSOL_ReadScenarioDamageScale(session->Map(),ratio)&&ratio==1,"real rules damage ratio is readable");
    }
    ~WorldFixture()
    {
        e.onOffset={};e.onIndexLookup={};e.onSound={};e.onOrigin={};e.onTrace={};
        e.Clear();session.reset();runtime.reset();
        for(const auto& [name,value]:e.cvars)if(value.registered)registrations[name]={value.registered,value.text};
    }
    CBasePlayer& Player(){return *actor;}
};
}
