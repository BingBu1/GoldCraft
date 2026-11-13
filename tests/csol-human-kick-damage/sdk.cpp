// SPDX-License-Identifier: GPL-3.0-or-later
// Real GameRules/Session/Factory/TakeDamage/death owner; controlled engine only.
#include "../csol-scenario-teddy-bear/fixture_resources.hpp"
#include "../../native/csol/server/scenario/core/human_kick_damage.h"
#include "../../native/csol/server/scenario/core/scenario_gameplay.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_ordinary.h"
#include "../../native/csol/server/scenario/monsters/scenario_elite_services.h"
#include "../../native/csol/server/weapons/scenario_weapon_state.h"
#include "../../native/csol/server/weapons/weapons.h"
#include "../../native/csol/scenario/core/human_kick.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

extern "C" void worldspawn(entvars_t*);
std::shared_ptr<CCSOLScenarioRuntime> CSOL_TestScenarioRuntimeForAccess(const CSOLScenarioEntityAccess* p)
{ return CSOL_ScenarioRuntimeForAccess(p); }
namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using Result=CSOLHumanKickDamageResult;
struct Weapon final : CBasePlayerWeapon, CSOLWeaponMonsterKillCallbacks
{
    inline static int active{},source{};
    void ActiveMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*) override { ++active; }
    void SourceMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*) override { ++source; }
};
struct Case
{
    std::unique_ptr<Engine> storage=std::make_unique<Engine>();
    Engine& e=*storage;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    CBasePlayer* actor{};
    CCSOLScenarioMonster* target{};
    inline static std::map<std::string,trap_test::Fixture::Registration> registrations;
    explicit Case(int priority=0,float attack=1)
    {
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
        e.globals.mapname=Engine::String("zs_deadend");e.globals.time=100;e.Prepare("2");
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
        session=runtime->session;
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
        const auto spawned=runtime->gameplay->Factory().Spawn(TERRORIST,1,1,1);
        Require(spawned==CSOLMonsterSpawnResult::Spawned,runtime->gameplay->Factory().Error().message.c_str());
        for(std::size_t i=0;i<CCSOLMonsterPool::Capacity;++i)
        {auto* p=session->Monsters().At(i);if(p&&session->Monsters().Borrowed(*p)){target=p;break;}}
        Require(target&&dynamic_cast<const CCSOLOrdinaryMonsterBehavior*>(target->Behavior()),"actual factory installs ordinary behavior");
        Require(target->pev->deadflag==DEAD_NO&&target->pev->takedamage!=DAMAGE_NO,
            "actual factory target remains alive after player spawn-overlap checks");
        float ratio{};Require(CSOL_ReadScenarioDamageScale(session->Map(),ratio)&&ratio==1,"real rules damage ratio is readable");
        actor->pev->origin=target->pev->origin-Vector(3,4,0);
        target->pev->health=target->pev->max_health=1000;target->pev->velocity=Vector(9,8,7);
        Weapon::active=Weapon::source=0;
    }
    ~Case()
    {
        e.onOffset={};e.onIndexLookup={};e.onSound={};e.onOrigin={};e.onTrace={};
        e.Clear();session.reset();runtime.reset();
        for(const auto& [name,value]:e.cvars)if(value.registered)registrations[name]={value.registered,value.text};
    }
    Weapon& Held(int id)
    {
        auto* ed=e.Reserve(e.nextEntity++,"kick_weapon_probe");auto* weapon=GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<Weapon*>(&ed->v));
        weapon->m_iId=id;weapon->m_pPlayer=actor;actor->m_pActiveItem=weapon;return *weapon;
    }
    Result Kick(ConfigError& error,Vector direction=Vector(3,4,0),Vector velocity=Vector(0,0,0),float base=15)
    {return CSOL_HumanKickMonsterDamage(*target,*actor,runtime,base,direction,velocity,error,[](const void* context){const auto& c=*static_cast<const Case*>(context);return c.runtime&&!c.runtime->Closing();},this);}
};
void Damage()
{
    for(int id:{0,3,18,29,40,220})
    {
        Case c;auto& weapon=c.Held(id);ConfigError error;
        c.target->HostState().lastHitgroup=HITGROUP_HEAD;
        CSOLWeaponPartEffect part;part.id=17;part.kind=1;part.modes={1u<<15};part.parameters={99,0,0};
        Require(c.session->Weapons()->SetParts(weapon,{17,0})&&c.session->Weapons()->ReplacePartEffects({part}),"actual active weapon owns a large matching damage part");
        Require(c.Kick(error)==Result::Applied,error.message.c_str());
        Check(c.target->pev->health==985&&c.target->HostState().lastHitgroup==0,"actual TakeDamage applies base15 without active gun minimum/head multiplier");
        Check(c.actor->m_pActiveItem==&weapon&&weapon.m_iId==id&&!Weapon::active&&!Weapon::source,"kick never changes held identity or invokes weapon kill callbacks");
        const auto state=c.runtime->players->Find(EHANDLE(c.actor));
        Check(state&&state->ordinaryMonsterDamage==15&&state->monsterDamageHits==1,"real player damage statistics receive actual action damage");
    }
    {Case c(0,2);ConfigError error;Require(c.Kick(error)==Result::Applied,error.message.c_str());
        Check(c.target->pev->health==970,"actual configured Damage upgrade multiplies base15");}
    {Case c;c.target->pev->armorvalue=100;ConfigError error;Require(c.Kick(error)==Result::Applied,error.message.c_str());
        Check(c.target->pev->health==992.5f&&c.target->pev->armorvalue==96.25f,"actual armor stage consumes kick damage");}
    {Case c;c.actor->m_iTeam=TERRORIST;ConfigError error;Check(c.Kick(error)==Result::Ignored&&c.target->pev->health==1000&&c.target->pev->velocity==Vector(9,8,7),"same-team real damage rejection does not stagger");}
    {Case c;ConfigError error;Check(c.Kick(error,Vector(3,4,0),Vector(0,0,0),std::numeric_limits<float>::quiet_NaN())==Result::Failed&&c.target->pev->health==1000,"nonfinite input fails before damage");}
}
void Stagger()
{
    for(int priority:{0,1})for(bool coincident:{false,true})
    {
        Case c(priority);ConfigError error;
        Require(c.Kick(error,coincident?g_vecZero:Vector(3,4,0),Vector(3,4,12))==Result::Applied,error.message.c_str());
        const Vector expected=coincident?Vector(0,0,805.2f):Vector(303.12f,404.16f,300);
        const auto delta=c.target->pev->velocity-expected;
        Check(std::abs(delta.x)<.0001f&&std::abs(delta.y)<.0001f&&std::abs(delta.z)<.0001f,"original 3D normalize/500+.4speed/up300 including coincident direction");
        Check(CSOL_HumanKickMonsterStaggered(*c.target,100.999f)&&!CSOL_HumanKickMonsterStaggered(*c.target,101),"one-second stagger deadline is exclusive at equality");
        const auto before=c.target->pev->velocity;c.e.globals.time=100.1f;c.target->BehaviorThink();
        Check(c.target->pev->velocity==before,"actual ordinary Think preserves kick velocity during stagger");
        const auto lease=c.session->Monsters().Lease(*c.target);
        Require(c.session->Monsters().Free(*c.target),"actual pool returns staggered target");
        Check(!CSOL_HumanKickMonsterStaggered(*c.target,100.2f),"returned slot cannot retain stagger authority");
        const auto* type=c.session->Map().Tables()->monsterTypes.Find(1);
        auto* next=c.session->Monsters().Acquire(*type,1,c.session->Map().SpawnPoints().Points());
        Require(next,"real pool reborrows target");
        Check(c.session->Monsters().Lease(*next)!=lease&&!CSOL_HumanKickMonsterStaggered(*next,100.2f),"new lease cannot inherit stagger");
    }
}
void Death()
{
    for(bool deferred:{false,true})for(bool respawn:{false,true})
    {
        Case c;c.Held(WEAPON_KNIFE);c.target->pev->health=10;ConfigError error;
        auto& requests=c.runtime->gameplay->Deaths().Requests();
        auto scope=deferred?requests.BeginBehavior(*c.target):std::optional<CCSOLMonsterDeathRequests::BehaviorScope>{};
        Require(!deferred||scope.has_value(),"actual dispatcher opens deferred death scope");
        Require(c.Kick(error)==Result::Killed,error.message.c_str());
        if(respawn&&deferred)c.runtime->players->PlayerSpawnStarted(EHANDLE(c.actor));
        if(scope)Require(requests.EndBehavior(*scope),requests.Error().c_str());
        Check(c.target->pev->deadflag==DEAD_DEAD,"real ordinary death owner marks monster dead");
        const auto records=c.runtime->deathServices->Records();
        const auto count=std::count_if(records.begin(),records.end(),[](const auto& record){return record.kind==CSOLScenarioDeathRecord::Kind::HumanKickKill;});
        Check(count==(respawn&&deferred?0:1),"deferred death preserves typed kick attribution and revokes replaced actor life");
        Check(!Weapon::active&&!Weapon::source,"real death bypasses held weapon active/source kill callbacks");
        Check(sizeof(CSOLMonsterDeathContext)==32,"original death context remains exactly32 bytes");
    }
}
#include "owner_guard.inc"

void Reentry()
{
    Case c;c.Held(WEAPON_SCOUT);ConfigError error;bool nested{};
    c.e.onOffset=[&](const edict_t*)
    {
        if(nested)return;nested=true;
        // Same actor and victim, before the outer damage call is captured.
        // Native Scout has a225 minimum: only this nested ordinary hit gets it.
        c.target->TakeDamage(c.actor->pev,c.actor->pev,1,DMG_CLUB);
    };
    const auto result=c.Kick(error);c.e.onOffset={};
    Require(result==Result::Applied,error.message.c_str());
    Check(nested&&c.target->pev->health==760,"same-target nested damage retains its weapon path while outer kick remains15");
}
void Cancellation()
{
    for(int operation:{0,1,2})
    {
        Case c;ConfigError error;bool fired{};
        c.e.onOffset=[&](const edict_t*)
        {
            if(fired||c.target->pev->health==1000)return;fired=true;
            if(operation==0)c.runtime->players->PlayerSpawnStarted(EHANDLE(c.actor));
            else if(operation==1)c.runtime->RequestClose();
            else c.session->Monsters().Free(*c.target);
        };
        const auto result=c.Kick(error);c.e.onOffset={};
        Check(fired&&result==Result::Cancelled,"actual engine callback cancels actor-life/map/lease action");
    }
}
}
int main(int argc,char** argv)
{
    try
    {
        const std::string group=argc>1?argv[1]:"damage";
        if(group=="damage")Damage();else if(group=="stagger")Stagger();else if(group=="death")Death();else if(group=="cancel")Cancellation();else if(group=="reentry")Reentry();else if(group=="owner-guard")OwnerGuardCases();
        else throw std::runtime_error("unknown kick damage group");
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
