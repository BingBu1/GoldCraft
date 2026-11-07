// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Complete matched SDK objects. Only the engine/file boundaries are controlled.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../csol-scenario-navigation-loader/support.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_monster_recovery.h"
#include "../../native/csol/server/scenario/monsters/scenario_elite_services.h"
#include "../../native/csol/server/scenario/core/scenario_runtime.h"
#include "../../native/csol/server/scenario/core/scenario_gameplay.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_services.h"
#include "../../native/csol/server/scenario/core/scenario_death_services.h"
#include "../../native/csol/server/scenario/core/scenario_spawn_points.h"
#include "../../native/csol/server/scenario/core/scenario_trigger.h"
#include "../../native/csol/server/scenario/core/scenario_player_runtime.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_resources.h"
#include "../../native/csol/server/characters.h"
#include "../../native/csol/scenario/core/scenario_kill.hpp"
#include "../../native/csol/scenario/core/scenario_healing.hpp"
#include "../../native/csol/scenario/core/scenario_smoke.hpp"
#include "../../native/csol/scenario/core/scenario_upgrades.hpp"
#include "../../native/csol/scenario/core/scenario_abilities.hpp"
#include "../../native/csol/scenario/core/scenario_round_hud.hpp"
#include "../../native/csol/scenario/monsters/scenario_monster_effects.hpp"
#include <deque>
#include <limits>

namespace monster_recovery_test
{
using namespace sdk_test;
using csol::scenario::ConfigError;

inline std::map<std::string,std::string> Resources()
{
    using difficulty_test::Encrypt;
    std::map<std::string,std::string> files;
    for(const auto& [original,bytes]:session_test::Resources())
    {
        std::string name=original;
        if(const auto offset=name.find("map_session_test");offset!=std::string::npos)
            name.replace(offset,std::strlen("map_session_test"),"zs_deadend");
        files.emplace(std::move(name),bytes);
    }
    std::vector<std::string> head(103),row(103,"0");
    head[0]=";map_id";head[1]="map_name";head[2]="map_filename";head[58]="SubType";head[102]="ZSmaxDifficulty";
    row[0]="98";row[1]="fixture";row[2]="zs_deadend";row[58]="1";row[102]="7";
    std::string catalog;
    for(const auto& fields:{head,row})
    {for(std::size_t i=0;i<fields.size();++i){if(i)catalog+=',';catalog+=fields[i];}catalog+="\r\n";}
    files["resource/maplist.csv"]=catalog;
    files["maps/zbs_zs_deadend.cso"]=Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nRewardRound,0\r\n"
        "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
        "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,0,0,0,0,0,0,0,0,0,0,1\r\n");
    files["maps/ZBS_ZT_zs_deadend.cso"]=Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,0,1\r\n");
    files["maps/ZBS_DropItem.cso"]=Encrypt("id,diff,effect,p,owner,count,remove,reward\r\n");
    std::string difficulty="header\r\n";
    for(int i=0;i<64;++i){if(i)difficulty+=',';difficulty+='1';}
    files["maps/ZBS_Difficulty_Default.cso"]=Encrypt(difficulty+"\r\n");
    std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,1,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"]=Encrypt(levels);
    files["resource/ZBSVER2/ZBS_class.cso"]=Encrypt("id,hp,damage,speed,armor,ammo,order\r\n1,20,20,20,20,20,hdsam\r\n");
    files["resource/buff/BuffWeapon.cso"]=Encrypt("header\r\n");
    files["resource/FloatingDamage/FloatingDamageInfo.jsoo"]=Encrypt(R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})");
    // One real difficulty record is required by Session's synthesized32800.
    files["maps/ZBS.json"]=R"({"zbs_add_attribute":{"restrictWeaponDrop":{"default":{"Difficulty":7}}},"ZBS_Elite":{"Properties":[{"difficulty":1}],"Colors":[255,255,255],"Skills":[{},{},{},{},{},{},{},{},{},{},{},{},{},{}]}})";
    files["resource/item.csv"]=";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    files["resource/defaultitems.csv"]="Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"]=Encrypt("id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"]=Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    csol::scenario::NavigationFile nav;nav.areas={nav_loader_test::Area(1,0,0,0,4096)};
    files["maps/zs_deadend.nav"]=nav_loader_test::Encode(nav);
    // Explicit existence files; the controlled engine does not decode assets.
    for(const auto& item:csol::scenario_kill::Resources)files[item.path]="fixture";
    for(const auto& item:csol::upgrades::Resources)files[item.path]="fixture";
    for(const auto& item:csol::scenario_hud::Resources)files[item.path]="fixture";
    for(const auto* path:csol::abilities::DownloadResources)files[path]="fixture";
    for(const auto* path:csol::healing::Sprites)files[path]="fixture";
    for(const auto* path:{csol::upgrades::FontPath,csol::upgrades::DigitSpritePath,
        csol::scenario_hud::LocalizationPath,csol::scenario_hud::FontPath,
        CSOLScenarioInitialMonsterModel,csol::smoke::Sprite,csol::smoke::EventPath,
        CSOLMonsterTrapModel,CSOLMonsterDisarmModel,CSOLMonsterDisarmTrail,
        csol::monster_effects::ImpactSprite,"sprites/320_pain.spr","sprites/640_pain.spr"})files[path]="fixture";
    files[csol::scenario_hud::MessagePath]=Encrypt("id,unused,duration,text,image,sound\r\n");
    for(const auto* sound:{csol::upgrades::SoundPath,"vox/roundclear.wav","zombi/Heavy_newTrap.wav",
        "zombi/deimos_skill_hit.wav","zombi/Zombi_Trapped.wav","zombi/Zombi_Trapped_Female.wav",
        csol::monster_effects::ImpactSound})files[std::string("sound/")+sound]="fixture";
    const auto* elite=csol::scenario::FindMonsterAppearance("zbs_elite_01");
    Require(elite!=nullptr,"real appearance catalog contains synthesized elite");
    files[std::string(elite->model)]="fixture-elite-model";
    files["bgm/Scenario_Ready.mp3"]="fixture-bgm";
    return files;
}

struct Waves final:csol::scenario::TriggerWaveEvents
{
    mutable std::function<bool(ConfigError&)> onCheck;
    std::function<void()> onChanged;
    int changes{},activated{};
    bool Check(ConfigError& e)const override{return !onCheck||onCheck(e);}
    void WavesChanged(std::span<const std::uint16_t>)override{++changes;if(onChanged)onChanged();}
    void WaveActivated(std::uint16_t)override{++activated;throw std::runtime_error("BeginRound must not activate a wave");}
    void BeforeSpatialWaveEnd(std::uint16_t)override{throw std::runtime_error("unexpected wave end");}
    void GoalReached()override{throw std::runtime_error("unexpected goal");}
};

struct Fixture
{
    inline static Fixture* active{};
    struct Registration{cvar_t* variable;std::string value;};
    inline static std::map<std::string,Registration> registrations;
    std::unique_ptr<Engine> storage{std::make_unique<Engine>()};
    Engine& engine{*storage};
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioEliteServices> elites;
    CHalfLifeMultiplay* rules{};
    CCSOLScenarioMonster* monster{};
    std::deque<csol::scenario::MonsterType> extraTypes;
    struct Model{studiohdr_t header{};std::array<mstudioseqdesc_t,40> sequence{};} model;
    std::function<void(int)> onWorld;
    std::function<void()> onDedicated;
    std::vector<std::string> trace;
    int worldQueries{},offsets{},messages{};
    bool loadOk{},activated{};

    explicit Fixture(std::function<void(std::map<std::string,std::string>&)> configure={},
        bool startRound=true,bool expectLoad=true, bool selectionOnly=false)
    {
        Require(!active,"one complete SDK recovery fixture");active=this;
        for(const auto& [name,r]:registrations){engine.Var(name.c_str()).registered=r.variable;engine.Set(name.c_str(),r.value.c_str());}
        engine.files=Resources();if(configure)configure(engine.files);
        engine.globals.mapname=Engine::String("zs_deadend");engine.globals.time=10;
        engine.Set("csol_scenario_difficulty","1");engine.Set("csol_scenario_short_final_delay","0");engine.Prepare("2");
        gmsgCurWeapon=0;LinkUserMessages();
        g_pGameRules=InstallGameRules();rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
        Require(rules&&!CSOL_RulesRejected(),"actual scenario rules installed");
        {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());Require(scope.Current()&&runtime,"runtime obtained through actual public registry");}
        session=runtime->session;
        // Exercise the enabled notification path explicitly. This fixture
        // does not assert the room constructor's production default.
        runtime->room.floatingDamageDisplay=1;
        // Keep all incidental archives inside this isolated process's cwd.
        g_engfuncs.pfnGetGameDir=[](char* path){std::strcpy(path,".");};
        engine.onTrace=[](edict_t*,TraceResult& t){t.flFraction=.5f;t.vecEndPos=Vector(0,0,0);};
        g_engfuncs.pfnPlaybackEvent=[](int,const edict_t*,unsigned short,float,float*,float*,float,float,int,int,int,int){};
        g_engfuncs.pfnIsDedicatedServer=[](){auto action=active->onDedicated;if(action)action();return 1;};
        model.header.length=sizeof(model);model.header.numseq=static_cast<int>(csol::scenario::MonsterSequenceCount);
        model.header.seqindex=offsetof(Model,sequence);
        const auto names=csol::scenario::MonsterSequenceNames();
        for(std::size_t i=0;i<names.size();++i){auto& s=model.sequence[i];std::snprintf(s.label,sizeof(s.label),"%s",names[i].data());s.fps=10;s.numframes=11;s.flags=STUDIO_LOOPING;}
        g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return &active->model;};
        if(selectionOnly)loadOk=session->Precache("zs_deadend",1,CSOLScenarioResourceScope::Selection);
        else{CSOL_PrecacheGameRules();loadOk=!CSOL_RulesRejected()&&session->State()==CSOLScenarioSessionState::Precached;}
        if(expectLoad&&!loadOk)std::cerr<<"SESSION "<<session->Error().path<<" "<<session->Error().detail.message<<" RULES "<<CSOL_RulesRejectionReason()<<'\n';
        if(expectLoad)Require(loadOk,"actual Session accepted explicit resources");
        if(!loadOk||selectionOnly)return;
        for(const auto* name:{"playerspawn","zombiespawn","zbstrigger"})
            for(int id:{1,2,7,30})
            {
                if(std::strcmp(name,"playerspawn")==0&&id!=1)continue;
                if(std::strcmp(name,"zombiespawn")==0&&id!=1&&id!=7)continue;
                if(std::strcmp(name,"zbstrigger")==0&&id==7)continue;
                auto* ed=Engine::Create(Engine::String(name));
                if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=2;}
                if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=1;p->m_iMaxSpawn=50;}
                if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))){p->m_iId=id;ed->v.model=Engine::String("*1");}
                Require(engine.api.pfnSpawn(ed)==0,"markers dispatched by actual SDK");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
            }
        activated=CSOL_ActivateGameRules();Require(activated&&!CSOL_RulesRejected(),"actual GameRules activates services");
        elites=runtime->eliteServices;Require(bool(elites),"actual activation installs elite service");
        if(startRound)Restart();
        g_engfuncs.pfnPEntityOfEntOffset=[](int offset)->edict_t*
        {
            auto& f=*active;++f.worldQueries;f.trace.push_back("world:"+std::to_string(offset));
            auto action=f.onWorld;if(action)action(offset);
            return Engine::At(offset/static_cast<int>(sizeof(edict_t)));
        };
        engine.onOffset=[this](const edict_t*){++offsets;trace.emplace_back("offset");};
        g_engfuncs.pfnMessageBegin=[](int,int,const float*,edict_t*){++active->messages;};
    }
    void Restart()
    {
        const auto generation=session->Map().RoundGeneration();
        Require(CSOL_ScenarioRestartRound(*rules),"actual scenario RestartRound hook consumes request");
        Require(!CSOL_RulesRejected()&&session->Map().RoundGeneration()==generation+1&&elites->Current(),"formal restart initializes new round and elite service exactly once");
    }
    CCSOLScenarioMonster& Borrow(int priority=0,int point=1)
    {
        const auto* type=session->Map().Tables()->monsterTypes.Find(1);Require(type!=nullptr,"actual CSV has ordinary type");
        if(priority){extraTypes.push_back(*type);extraTypes.back().priority=static_cast<std::int8_t>(priority);type=&extraTypes.back();}
        auto* value=session->Monsters().Acquire(*type,point,session->Map().SpawnPoints().Points());Require(value!=nullptr,"actual pool lends a monster");
        value->pev->takedamage=DAMAGE_YES;value->pev->health=40;value->pev->max_health=100;monster=value;return *value;
    }
    CSOLMonsterSpawnCall Call(CCSOLScenarioMonster& value)
    {return CSOLMonsterSpawnCall(session->Map(),session->Monsters(),runtime->gameplay->Deaths().Requests(),value);}
    bool Recover(CCSOLScenarioMonster& value,float amount,ConfigError& error)
    {auto call=Call(value);return CSOL_RecoverMonsterSelf(runtime,call,amount,{},error);}
    void Reborrow(CCSOLScenarioMonster& target)
    {
        auto& pool=session->Monsters();
        while(pool.AvailableCount())Borrow(pool.CategoryCount(0)<39?0:pool.CategoryCount(1)<4?1:2);
        const auto* type=target.Type();const auto lease=pool.Lease(target);
        Require(pool.Free(target),"callback returns old lease");
        auto* next=pool.Acquire(*type,1,session->Map().SpawnPoints().Points());
        Require(next==&target&&pool.Lease(*next)!=lease,"real pool reuses same pointer with different lease");next->pev->health=17;
    }
    CBasePlayer& Player(int index,bool configured)
    {
        auto* ed=engine.Reserve(index,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;p->m_iTeam=CT;p->m_iNumSpawns=1;p->has_disconnected=false;
        CSOLScenarioPlayerProfile profile;profile.equipment.classModsComplete=true;
        Require(runtime->players->SetProfile(*p,profile),"actual player profile bound");
        if(configured)Require(session->PrepareEquippedPlayer(*p,1,0,false,profile.equipment),"actual class1 upgrade record configured");
        return *p;
    }
    ~Fixture()
    {
        onWorld={};onDedicated={};engine.onOffset={};engine.onIndexLookup={};engine.onModel={};engine.onSize={};
        for(const auto& [name,v]:engine.cvars)if(v.registered)registrations.insert_or_assign(name,Registration{v.registered,v.text});
        try{engine.Clear();}catch(const std::exception& e){++failures;std::cerr<<"CLEANUP "<<e.what()<<'\n';}
        runtime.reset();session.reset();elites.reset();extraTypes.clear();active=nullptr;
    }
};

// Restores an intentionally inconsistent entvars/source only after the call.
struct ContainingScope
{
    entvars_t& vars;edict_t* original;
    explicit ContainingScope(entvars_t& v):vars(v),original(v.pContainingEntity){v.pContainingEntity=nullptr;}
    ~ContainingScope(){vars.pContainingEntity=original;}
};
}
