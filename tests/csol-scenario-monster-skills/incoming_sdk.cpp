// SPDX-License-Identifier: GPL-3.0-or-later
// Whole SDK objects. Controlled engine/filesystem; no replacement production
// damage, equipment, player authority, rules, Spawn or bomb implementation.
#include "trap_fixture.hpp"
#include "../../native/csol/server/scenario_incoming_damage.h"
#include "../../native/csol/server/scenario_zombie_bomb.h"
#include <bit>
#include <cmath>
#include <limits>

#pragma clang fp contract(off)
namespace {
using namespace sdk_test;
int assertions{}, failed{};
void Expect(bool value,const char* label){++assertions;if(!value){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
bool Bits(float a,float b){return std::bit_cast<unsigned>(a)==std::bit_cast<unsigned>(b);}
struct Fixture {
    Engine& e;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    trap_test::NoDeath executor;
    CCSOLMonsterDeathRequests deaths{executor};
    trap_test::Waves waves;
    csol::scenario::MonsterType type;
    CCSOLScenarioMonster* monster{};
    CBasePlayer* player{};
    EHANDLE bomb;
    explicit Fixture(Engine& engine,int priority=0,bool champion=false,int id=24,
        const char* appearance="zombi_origin"):e(engine) {
        e.files=trap_test::Resources(0);e.globals.time=100;
        e.files["maps/ZBS.json"]=R"({"ZBS_ClueItem":{"Type":{"BLAST_RESIST":11,"ZOMBIE_SUICIDE_BOMB_RESIST":12,"ZOMBIE_BOMB_RESIST":13},"OperatorType":{"MUL":1,"ADD":3},"ClueItems":[{"itemID":9001,"type":"BLAST_RESIST","value":0.5,"resourceName":"","operatorType":"MUL"},{"itemID":9002,"type":"ZOMBIE_SUICIDE_BOMB_RESIST","value":3,"resourceName":"","operatorType":"ADD"},{"itemID":9003,"type":"ZOMBIE_BOMB_RESIST","value":0.25,"resourceName":"","operatorType":"MUL"}]}})";
        std::string difficulty="header\r\n";
        for(int i=0;i<64;++i){if(i)difficulty+=',';difficulty+=i==9?"2":i==12?"3":i==15?"4":"1";}
        e.files["maps/ZBS_Difficulty_Default.cso"]=difficulty_test::Encrypt(difficulty+"\r\n");
        for(const char* path:{CSOLZombieBombModel,csol::monster_effects::ZombieBombSprite,"models/grenade.mdl",CSOLZombieBombEvent})e.files[path]="fixture";
        e.Prepare("2");g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"registered actual scenario rules");
        {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());Require(scope.Current()&&runtime,"actual runtime registry");}
        session=runtime->session;
        const bool loaded=session->Precache("map_session_test",1,CSOLScenarioResourceScope::Selection);
        if(!loaded)std::cerr<<"PRECACHE "<<session->Error().path<<' '<<session->Error().detail.message<<'\n';
        Require(loaded,"actual Selection resource parsing");
        for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int number:{1,2,30}){
            if(std::strcmp(name,"zbstrigger")&&number!=1)continue;
            auto* ed=Engine::Create(Engine::String(name));auto* base=CBaseEntity::Instance(ed);
            if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(base)){p->m_iId=1;p->m_iTeam=2;}
            if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(base)){p->m_iId=7;p->m_iTeam=1;p->m_iMaxSpawn=1000;}
            if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(base)){p->m_iId=number;ed->v.model=Engine::String("*1");}
            Require(e.api.pfnSpawn(ed)==0,"actual marker dispatch");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        }
        Require(session->Activate(),"actual session activation");
        Require(session->Monsters().Initialize(),"independent actual pool initialization");
        Require(session->Map().BeginRound(0,100,0,waves),"independent map round identity");
        players=runtime->players=std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(),"actual player authority and equipment activation");
        type.id=id;type.priority=priority;type.champion=champion;type.appearance=appearance;
        monster=session->Monsters().Acquire(type,7,session->Map().SpawnPoints().Points());
        Require(monster,"actual typed pool lease");monster->pev->health=100;monster->pev->team=TERRORIST;
        auto* ed=e.Reserve(1,"player");player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;ed->v.armorvalue=50;
        player->m_iTeam=CT;player->m_iJoiningState=JOINED;player->m_iNumSpawns=1;player->m_bJustConnected=false;player->has_disconnected=false;
        Require(players->SetProfile(*player,{}),"actual player registry generation");
        csol::scenario::EquipmentLoadout loadout;loadout.carriedItems={9001,9002,9003};
        Require(session->EquipmentState()->BindPlayer(*player,0,std::move(loadout)),"actual equipment inventory with three distinguishable effects");
        LinkUserMessages();
    }
    ~Fixture(){e.onOffset={};e.onIndexLookup={};e.onOrigin={};e.onSize={};e.onSound={};deaths.Reset();e.Clear();players.reset();session.reset();runtime.reset();}
    CCSOLZombieBomb& Bomb(){
        if(!bomb.Get()){
            const int first=e.nextEntity;CSOLMonsterSpawnCall call(session->Map(),session->Monsters(),deaths,*monster);csol::scenario::ConfigError error;
            Require(CCSOLZombieBomb::Create(call,runtime,Vector(1,2,3),Vector(4,5,6),40,error),"actual independent zombie bomb creation");
            for(int i=first;i<e.nextEntity;++i)if(auto* b=dynamic_cast<CCSOLZombieBomb*>(CBaseEntity::Instance(Engine::At(i))))bomb=b;
        }
        auto* result=bomb.Get<CCSOLZombieBomb>();Require(result,"actual zombie bomb instance");return *result;
    }
    bool Apply(float& amount,int bits,entvars_t* inflictor=nullptr,entvars_t* attacker=nullptr){
        return CSOL_ScenarioApplyMonsterIncomingDamage(*player,attacker?attacker:monster->pev,amount,bits,inflictor);
    }
};
void Values(Engine& e){
    for(int priority:{0,1,2})for(bool champion:{false,true})for(int id:{24,32800}){
        Fixture f(e,priority,champion,id);float amount=40;
        const float factor=!champion&&id!=32800&&priority>=0&&priority<=2?float(priority+2):1;
        Expect(f.Apply(amount,DMG_GENERIC)&&Bits(amount,40*factor),"actual type priority/champion/id gates exact difficulty factor");
    }
    for(const char* appearance:{"zombi_origin","boomer_zombi_host","boomer_zombi_origin"})for(int bits:{DMG_GENERIC,DMG_BLAST,DMG_EXPLOSION,DMG_BLAST|DMG_EXPLOSION})for(bool bomb:{false,true}){
        Fixture f(e,0,false,24,appearance);float amount=40;
        entvars_t* source=bomb?f.Bomb().pev:f.monster->pev;
        float expected=40;if(bits&DMG_EXPLOSION){expected*=.5f;if(std::strcmp(appearance,"zombi_origin"))expected+=3;}
        if(bomb)expected*=.25f;expected*=2;
        Expect(f.Apply(amount,bits,source)&&Bits(amount,expected),"actual inflictor resistance is independent of explosion-bit resistance and applied before difficulty once");
    }
    {Fixture f(e);float amount=40;Expect(f.Apply(amount,DMG_EXPLOSION,f.player->pev,f.player->pev)&&amount==40,"nonmonster attacker keeps original value");}
    for(float input:{0.f,-3.f,std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        Fixture f(e);float amount=input;const bool expected=std::isfinite(input*2);
        Expect(f.Apply(amount,DMG_GENERIC)==expected&&(expected?Bits(amount,input*2):Bits(amount,input)),"nonfinite result rejects without partial candidate writeback");
    }
}
void Identity(Engine& e){
    for(int mode=0;mode<5;++mode){
        Fixture f(e);auto& bomb=f.Bomb();float amount=40;
        if(mode==0)bomb.pev->dmg=998;
        if(mode==1)bomb.pev->flags|=FL_KILLME;
        if(mode==2){auto* other=f.session->Monsters().Acquire(f.type,7,f.session->Map().SpawnPoints().Points());Require(other,"second monster lease");Expect(!f.Apply(amount,DMG_BLAST,bomb.pev,other->pev)&&amount==40,"valid bomb cannot credit different monster");continue;}
        if(mode==3){Require(f.session->Monsters().Free(*f.monster),"return creator pool slot");}
        if(mode==4){
            const auto lease=f.session->Monsters().Lease(*f.monster);
            Require(f.session->Monsters().Free(*f.monster),"return before reuse");
            CCSOLScenarioMonster* fresh{};
            for(std::size_t i=0;i<CCSOLMonsterPool::Capacity;++i){
                fresh=f.session->Monsters().Acquire(f.type,7,f.session->Map().SpawnPoints().Points());Require(fresh,"reacquire lease");
                if(fresh==f.monster)break;
                Require(f.session->Monsters().Free(*fresh),"rotate idle pool queue");
            }
            Require(fresh==f.monster&&f.session->Monsters().Lease(*fresh)!=lease,"same creator address has a genuinely new lease");
        }
        Expect(!f.Apply(amount,DMG_BLAST,bomb.pev)&&amount==40,"invalid bomb marker or retired/reused creator rejects unchanged damage");
    }
    {Fixture f(e);auto* raw=Engine::Create(Engine::String("csol_zombiebomb"));raw->v.dmg=999;float amount=40;Expect(!f.Apply(amount,DMG_BLAST,&raw->v)&&amount==40,"classname and marker alone do not create ready authenticated bomb");}
}
void Entries(Engine& e){
    for(int bits:{DMG_GENERIC,DMG_BULLET,DMG_BLAST,DMG_EXPLOSION})for(bool bomb:{false,true}){
        Fixture f(e);entvars_t* source=bomb?f.Bomb().pev:f.monster->pev;
        Require(f.players->SetPlayerClearPointProtection(true),"real protection stops native armor/health after incoming stage");
        float amount=40;float expected=(bits&DMG_EXPLOSION)?20:40;if(bomb)expected*=.25f;expected*=2;
        Expect(!f.player->TakeDamage_OrigFunc(source,f.monster->pev,amount,bits)&&Bits(amount,expected)&&f.player->pev->health==100&&f.player->pev->armorvalue==50,
            "complete real TakeDamage ordinary/explosion branches pass actual inflictor and transform exactly once before protection");
    }
}
struct StopSpawn{};
void Spawn(Engine& e){
    Fixture f(e);const auto state=f.players->Find(EHANDLE(f.player));Require(state!=nullptr,"spawn state observed");
    const auto native=state->nativeSpawnSerial,life=state->skillLifeSerial;
    CSOL_ScenarioPlayerSpawnStarted(*f.player);
    Expect(state->nativeSpawnSerial==native+1&&state->skillLifeSerial==life,"actual public SpawnStarted routes to owned native serial");
    const auto previous=g_engfuncs.pfnSetPhysicsKeyValue;
    g_engfuncs.pfnSetPhysicsKeyValue=[](const edict_t*,const char*,const char*){throw StopSpawn{};};
    bool stopped=false;try{f.player->Spawn();}catch(const StopSpawn&){stopped=true;}
    g_engfuncs.pfnSetPhysicsKeyValue=previous;
    Expect(stopped&&state->nativeSpawnSerial==native+2,"complete native Spawn increments serial before first physics callback (tail intentionally stopped)");
    f.player->m_bJustConnected=true;f.player->m_iJoiningState=PICKINGTEAM;f.player->Spawn();
    Expect(state->nativeSpawnSerial==native+2,"native picking-team early return does not claim a spawn start");
}
struct DamageContext {Fixture& fixture;int operation{},before{},after{};float finalDamage{};};
DamageContext* damageContext{};
BOOL DamageHook(IReGameHook_CBasePlayer_TakeDamage* chain,CBasePlayer* player,
    entvars_t* inflictor,entvars_t* attacker,float& damage,int bits){
    auto& c=*damageContext;auto& f=c.fixture;++c.before;
    if(c.operation==0)player->has_disconnected=true;
    if(c.operation==1)f.players->Remove(EHANDLE(player));
    if(c.operation==2)f.runtime->players.reset();
    if(c.operation==3)Require(f.session->Monsters().Free(*f.monster),"actual ReAPI pre hook returns source lease");
    if(c.operation==4)inflictor->dmg=998;
    const auto result=chain->callNext(player,inflictor,attacker,damage,bits);
    ++c.after;c.finalDamage=damage;return result;
}
struct DamageHookScope {
    explicit DamageHookScope(DamageContext& context){Require(!damageContext,"one real ReAPI damage hook");damageContext=&context;g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(DamageHook,192);}
    ~DamageHookScope(){g_ReGameHookchains.m_CBasePlayer_TakeDamage.unregisterHook(DamageHook);damageContext=nullptr;}
};
void Reentry(Engine& e){
    {
        Fixture f(e);auto& bomb=f.Bomb();float amount=40;const enginefuncs_t saved=g_engfuncs;
        // The current SDK uses direct ENT/GET_PRIVATE/EHANDLE reads. Prove
        // there is no engine callback inside this synchronous transform;
        // do not manufacture an OFFSET hook which the real method never calls.
        try{
#include "../csol-scenario-rules-session/engine_edge_defaults.inc"
            Expect(f.Apply(amount,DMG_BLAST,bomb.pev)&&amount==20,"complete incoming method succeeds with every engine function trapped");
        }catch(...){g_engfuncs=saved;throw;}
        g_engfuncs=saved;
        const auto state=f.players->Find(EHANDLE(f.player));const auto serial=state->nativeSpawnSerial;
        CSOL_ScenarioPlayerSpawnStarted(*f.player);amount=40;
        Expect(state->nativeSpawnSerial==serial+1&&f.Apply(amount,DMG_BLAST,bomb.pev)&&amount==20,
            "a newly entered synchronous hit after SpawnStarted takes a fresh state snapshot");
    }
    for(int bits:{DMG_GENERIC,DMG_BLAST})for(int operation=0;operation<5;++operation){
        Fixture f(e);auto& bomb=f.Bomb();
        Require(f.players->SetPlayerClearPointProtection(true),"real later protection keeps native tail outside lifecycle test");
        DamageContext context{f,operation};DamageHookScope hook(context);
        const bool accepted=f.player->TakeDamage(bomb.pev,f.monster->pev,40,bits)!=FALSE;
        Expect(context.before==1&&context.after==1,"actual public TakeDamage dispatch invokes ReAPI pre and post exactly once");
        Expect(!accepted&&context.finalDamage==40&&f.player->pev->health==100&&f.player->pev->armorvalue==50,
            "real original entry rejects disconnected/removed authority/returned owner/invalid bomb changed by pre hook before any incoming damage write");
    }
}
}
int main(int argc,char** argv){
    try{auto e=std::make_unique<Engine>();const std::string group=argc>1?argv[1]:"values";
        if(group=="values")Values(*e);else if(group=="identity")Identity(*e);else if(group=="entries")Entries(*e);else if(group=="spawn")Spawn(*e);else if(group=="reentry")Reentry(*e);else throw std::runtime_error("unknown incoming SDK group");
        e.reset();std::cout<<"behavior="<<assertions<<" failed="<<failed<<" sdk="<<checks<<" sdkFailed="<<failures<<'\n';return failed||failures?1:0;
    }catch(const std::exception& ex){std::cerr<<"EXCEPTION "<<ex.what()<<" behavior="<<assertions<<" failed="<<failed<<" sdk="<<checks<<" sdkFailed="<<failures<<'\n';return 2;}
}
