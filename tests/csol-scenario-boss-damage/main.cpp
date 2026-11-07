// SPDX-License-Identifier: GPL-3.0-or-later
// Execute extracted production Boss methods. Entity lookup, runtime lifecycle,
// network writer and appearance directory are explicit SDK boundaries.
#include "../../native/csol/scenario/core/config.hpp"
#include "../../native/csol/scenario/monsters/monster_damage.hpp"
#include "../../native/csol/scenario/core/map_support.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_monster_damage_behavior.h"
#include "../../native/csol/server/scenario/core/scenario_entity_access.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using csol::scenario::ConfigError;
struct CBaseEntity;
struct edict_t { int index{},serial{1}; bool free{}; CBaseEntity* entity{}; };
struct Vars { CBaseEntity* owner{}; float health{100},max_health{100},armorvalue{}; int flags{},team{},deadflag{}; const char* classname{"monster"}; };
using entvars_t=Vars;
struct CBaseEntity
{
    edict_t ed; Vars vars,*pev=&vars;
    CBaseEntity(){ed.entity=this;vars.owner=this;}
    virtual ~CBaseEntity()=default;
    edict_t* edict(){return &ed;}
    virtual int Classify(){return 0;}
    static CBaseEntity* Instance(Vars* vars){return vars?vars->owner:nullptr;}
};
struct Weapon:CBaseEntity{int m_iId{};};
struct CBasePlayer:CBaseEntity
{
    Weapon* m_pActiveItem{};mutable std::function<void()> playerTypeCallback;
    bool has_disconnected{};
    int IsPlayer()const{if(playerTypeCallback)playerTypeCallback();return !(pev->flags&(1<<26));}
};
struct EHANDLE
{
    edict_t* value{};int serial{};
    EHANDLE()=default;EHANDLE(CBaseEntity* p){*this=p;}
    EHANDLE& operator=(CBaseEntity* p){value=p?p->edict():nullptr;serial=value?value->serial:0;return *this;}
    edict_t* Get()const{return value&&!value->free&&value->serial==serial?value:nullptr;}
    template<class T>T* Get()const{return Get()?dynamic_cast<T*>(Get()->entity):nullptr;}
    int GetSerialNumber()const{return serial;}
};
struct Type { int id{30200},skill{1002};const char* appearance{"boss"}; };
struct BehaviorBase{virtual ~BehaviorBase()=default;};
struct MonsterPool;
struct CCSOLScenarioMonster:CBaseEntity
{
    struct Host{int lastHitgroup{},damageScaleCount{};float damageScale{1},frozenRemaining{};bool flinchEnabled{true};}host;Type type;
    std::shared_ptr<BehaviorBase> behavior;
    MonsterPool* pool{};std::uint64_t lease{1};bool borrowed{true};
    Type* Type(){return &type;} Host& HostState(){return host;}
    int TypeId()const{return type.id;}
    auto RetainBehavior(){return behavior;}
    auto* Pool(){return pool;}
};
struct MonsterPool
{
    bool Borrowed(CCSOLScenarioMonster& monster)const{return monster.borrowed;}
    std::uint64_t Lease(CCSOLScenarioMonster& monster)const{return monster.lease;}
};
struct Map
{
    struct Table{std::string mapName{"zs_deadend"};int scenarioType{1};}table;
    std::uint64_t round{1};std::uint64_t RoundGeneration()const{return round;}
    std::shared_ptr<CSOLScenarioEntityAccess> access=std::make_shared<CSOLScenarioEntityAccess>();
    std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess(){return access;}
    auto* Tables(){return &table;}
};
struct CSOLMonsterSpawnCall
{
    Map* map{};CCSOLScenarioMonster* monster{};bool valid{true};
    bool BelongsTo(const Map& m)const{return map==&m;}
    CCSOLScenarioMonster* Current()const{return valid?monster:nullptr;}
    bool SameSession()const{return valid;}
};
struct CSOLMonsterDeathContext{std::array<std::uint8_t,32> bytes{};};
struct CSOLMonsterDamageCall
{
    CSOLMonsterSpawnCall spawn;CBasePlayer* player{};CBaseEntity* source{};
    bool cancelled{};float damage{10},original{};int bits{};CSOLMonsterDeathContext context;
    const bool* ownerActive{};bool(*ownerValid)(const void*){};const void* ownerContext{};
    BehaviorBase* behavior{};
    CSOLMonsterDamageCall()=default;
    CSOLMonsterDamageCall(CSOLMonsterSpawnCall& s,EHANDLE src,EHANDLE actor,float amount,int type,
        CSOLMonsterDeathContext c,const bool& active,bool(*valid)(const void*),const void* owner)
        :spawn(s),player(actor.Get<CBasePlayer>()),source(src.Get<CBaseEntity>()),damage(amount),original(amount),
        bits(type),context(c),ownerActive(&active),ownerValid(valid),ownerContext(owner),behavior(s.Current()->behavior.get()){}
    CCSOLScenarioMonster* Current()const
    {return cancelled||(ownerActive&&(!*ownerActive||!ownerValid(ownerContext)||!spawn.Current()||spawn.Current()->behavior.get()!=behavior))?nullptr:spawn.Current();}
    CBasePlayer* Player()const{return player;}
    CBaseEntity* Inflictor()const{return source;}
    CSOLMonsterSpawnCall& Spawn(){return spawn;}
    bool Cancel(){cancelled=true;return false;}
    CBaseEntity* Attacker()const{return player;}
    float OriginalDamage()const{return original;}
};
struct CSOLWeaponDamageSource{virtual int DamageWeaponId()const=0;};
struct Globals{int maxClients{32};} globals;
Globals* gpGlobals=&globals;
constexpr int FL_KILLME=0x40000000;
constexpr int DEAD_NO=0;
std::function<void()> indexHook;
int ENTINDEX(edict_t* ed){const int result=ed->index;if(indexHook)indexHook();return result;}
#define STRING(x) (x)
std::array<CBaseEntity*,33> clients;
CBaseEntity* CSOL_ScenarioPlayerEntity(int index,std::shared_ptr<const CSOLScenarioEntityAccess>){auto* p=index>=1&&index<=32?clients[index]:nullptr;if(indexHook)indexHook();return p;}
namespace csol::scenario
{
struct Appearance{bool healthSummary{};int index{};};
const Appearance* FindMonsterAppearance(const char* name)
{static Appearance boss{true,18},ordinary{false,0};return std::strcmp(name,"missing")==0?nullptr:std::strcmp(name,"boss")==0?&boss:&ordinary;}
}

struct Packet { int dest{},id{},recipient{};std::vector<std::pair<char,double>> fields; };
std::vector<Packet> packets;
std::function<void()> packetHook;
std::function<void()> beginHook;
struct Engine{bool pfnMessageBegin{true},pfnMessageEnd{true},pfnWriteByte{true},pfnWriteShort{true},pfnWriteLong{true},pfnWriteCoord{true};}g_engfuncs;
constexpr int MSG_ONE=1,MSG_ALL=2;
void Begin(int dest,int id,const void* =nullptr,edict_t* ed=nullptr){packets.push_back({dest,id,ed?ed->index:0,{}});if(beginHook)beginHook();}
void Write(char kind,double value){packets.back().fields.emplace_back(kind,value);}
void End(){if(packetHook)packetHook();}
#define MESSAGE_BEGIN(...) Begin(__VA_ARGS__)
#define WRITE_BYTE(x) Write('b',static_cast<std::uint8_t>(x))
#define WRITE_SHORT(x) Write('s',static_cast<std::int16_t>(x))
#define WRITE_LONG(x) Write('l',static_cast<std::int32_t>(x))
#define WRITE_COORD(x) Write('c',x)
#define MESSAGE_END() End()
#include "actual_message_scope.inc"
class CCSOLScenarioMessages
{
public:
    int m_messageId{190};
    bool SendBossParticipation(edict_t*,int,int,std::int32_t,int)const;
    bool SendBossHealthInit(int,std::int32_t,int,edict_t* =nullptr)const;
    bool SendBossHealthRatio(int,float,float,int,edict_t* =nullptr)const;
};
#include "actual_messages.inc"
struct Session
{
    ::Map map;CCSOLScenarioMessages messages;MonsterPool pool;
    auto& Map(){return map;} const auto& Messages(){return messages;}
    auto& Monsters(){return pool;}
};
class CCSOLScenarioDamageServices;
struct CCSOLScenarioRuntime
{
    bool closing{};std::shared_ptr<Session> session=std::make_shared<Session>();
    std::shared_ptr<CCSOLScenarioDamageServices> monsterDamageServices;
    bool Closing()const{return closing;}
};
class CCSOLScenarioDamageServices
{
public:
    explicit CCSOLScenarioDamageServices(std::weak_ptr<CCSOLScenarioRuntime>);
    void ResetBossDamageState();void RemoveBossParticipant(const EHANDLE&);
    bool RecordBossParticipation(CSOLMonsterDamageCall&,ConfigError&);
    bool InitializeBossHealth(CSOLMonsterSpawnCall&,ConfigError&);
    bool RecordBossHealth(CSOLMonsterDamageCall&,ConfigError&);
    bool UpdateBossHealth(float,ConfigError&);
    bool ClearRetiredBossHealth(ConfigError&);
    bool ReplayBossHealth(const EHANDLE&,ConfigError&);
    struct BossState;
    std::shared_ptr<CCSOLScenarioRuntime> Pin(CSOLMonsterDamageCall&)const;
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<BossState> m_boss;
};
#include "actual_boss_methods.inc"
#include "actual_prearmor.inc"
#include "actual_oberon_hit.inc"
using BOOL=int;
constexpr int TRUE=1,FALSE=0;
enum class CSOLMonsterRewardResult{Credited,Cancelled,Failed};
enum class CSOLDeathRequestResult{Executed,Failed};
struct CSOLMonsterDeathArguments{EHANDLE inflictor,attacker;CSOLMonsterDeathContext context;};
struct PipelineRewards;
struct CSOLMonsterHitAccount{std::shared_ptr<PipelineRewards> rewards;bool moneyExtractor{};int roomOption{};};
namespace csol::scenario{enum class UpgradeStat{Damage};}
std::vector<std::string> pipelineStages;
std::function<void(const char*,CSOLMonsterDamageCall&)> pipelineHook;
void PipelineStage(const char* name,CSOLMonsterDamageCall& call)
{pipelineStages.emplace_back(name);if(pipelineHook)pipelineHook(name,call);}
struct PipelineUpgrade
{
    bool Current()const{return true;}
    bool StatValue(csol::scenario::UpgradeStat,float& value){value=1;return true;}
    int experience{};bool AddExperience(int value){experience+=value;return true;}
};
struct PipelinePlayers
{
    std::shared_ptr<PipelineUpgrade> value=std::make_shared<PipelineUpgrade>();
    auto Upgrades(const EHANDLE&){return value;}
};
struct PipelineEquipment{bool ModifyCreatureDamage(const EHANDLE&,bool,float&){return true;}};
struct PipelineWeapons
{
    bool Current()const{return true;}
    bool ApplyItemMultiplier(CSOLMonsterDamageCall& c,int,bool,ConfigError&){PipelineStage("item",c);return true;}
    bool ApplyPartDamage(CSOLMonsterDamageCall& c,bool,bool,ConfigError&){PipelineStage("parts",c);return true;}
};
struct PipelineRewards
{
    float damage{};ConfigError error;
    CSOLMonsterRewardResult Credit(CSOLMonsterSpawnCall&,const EHANDLE&,const EHANDLE&,float value,bool,int,bool(*)(const void*),const void*)
    {damage=value;pipelineStages.emplace_back("reward");return CSOLMonsterRewardResult::Credited;}
    const ConfigError& Error()const{return error;}
};
struct PipelineServices
{
    std::shared_ptr<PipelineRewards> rewards=std::make_shared<PipelineRewards>();
    bool suppressed{};
    bool DamageSuppressed()const{return suppressed;}
    bool WeaponEffectsBlocked21F2(CSOLMonsterDamageCall&)const{return false;}
    bool WeaponEffectsBlocked34F0(CSOLMonsterDamageCall&)const{return false;}
    bool RulesAfterAttackDamage(CSOLMonsterDamageCall& c,int,ConfigError&){PipelineStage("rules",c);return true;}
    bool BeforeHitReward(CSOLMonsterDamageCall& c,CSOLMonsterHitAccount& account,ConfigError&){account.rewards=rewards;PipelineStage("before-reward",c);return true;}
    bool AfterHitReward(CSOLMonsterDamageCall& c,ConfigError&){PipelineStage("after-reward",c);return true;}
    bool AfterHealthDamage(CSOLMonsterDamageCall& c,ConfigError&){PipelineStage("after-health",c);return true;}
};
struct PipelineDeaths
{
    int calls{};float health{};
    CSOLDeathRequestResult Request(CCSOLScenarioMonster& m,const CSOLMonsterDeathArguments&)
    {++calls;health=m.pev->health;pipelineStages.emplace_back("death");return CSOLDeathRequestResult::Executed;}
    std::string Error()const{return "unused";}
};
class CCSOLOrdinaryMonsterDamage
{
public:
    Map& m_map;PipelineDeaths& m_deaths;bool m_valid{true};
    std::weak_ptr<PipelineServices> m_services;
    std::shared_ptr<PipelinePlayers> m_players=std::make_shared<PipelinePlayers>();
    std::shared_ptr<PipelineEquipment> m_equipment=std::make_shared<PipelineEquipment>();
    std::shared_ptr<PipelineWeapons> m_weapons=std::make_shared<PipelineWeapons>();
    BOOL Apply(CSOLMonsterSpawnCall&,entvars_t*,entvars_t*,float,int,ConfigError&,bool(*)(const void*),const void*,CSOLMonsterDeathContext={});
};
CBaseEntity* Live(const EHANDLE& h){return h.Get<CBaseEntity>();}
int Team(CBaseEntity& e){return e.pev->team;}
int CSOL_DamageWeaponId(CBaseEntity*,CBasePlayer*){return 144;}
bool CSOL_ReadScenarioDamageScale(Map&,float& value){value=1;return true;}
bool ApplyOrdinaryKnockback(CSOLMonsterDamageCall& c,const EHANDLE&,ConfigError&){PipelineStage("knockback",c);return true;}
#include "actual_apply.inc"
struct TestBoss:BehaviorBase,CCSOLMonsterDamageBehavior
{
    int activeSkill{7},interruptState{-1},flinches{};float deadline{18};
    bool ResolveHitDamage(CSOLMonsterDamageCall& c,int id,ConfigError& error)override
    {PipelineStage("behavior",c);return c.Current()&&CSOL_ResolveOberonHitDamage(c,id,error);}
    bool DamageFlinch(CSOLMonsterSpawnCall&,ConfigError&)override{++flinches;return true;}
};
int checks{};
void Check(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL "<<checks<<" "<<message<<'\n';std::exit(1);}}
struct Fixture
{
    std::shared_ptr<CCSOLScenarioRuntime> runtime=std::make_shared<CCSOLScenarioRuntime>();
    std::shared_ptr<CCSOLScenarioDamageServices> services=std::make_shared<CCSOLScenarioDamageServices>(runtime);
    CCSOLScenarioMonster monster;std::array<CBasePlayer,32> players;
    ConfigError error;CSOLMonsterDamageCall call;
    Fixture()
    {
        packets.clear();packetHook={};beginHook={};indexHook={};clients={};g_engfuncs={};pipelineStages.clear();pipelineHook={};
        runtime->monsterDamageServices=services;monster.ed.index=64;
        monster.pool=&runtime->session->pool;
        for(int i=0;i<32;++i){players[i].ed.index=i+1;clients[i+1]=&players[i];}
        call.spawn={&runtime->session->map,&monster};call.player=&players[0];
    }
    ~Fixture(){packetHook={};beginHook={};indexHook={};pipelineHook={};}
};
struct Rocket:CBaseEntity,CSOLWeaponDamageSource,CSOLAt4RocketDamageState
{
    int id{133};bool alternate{};mutable std::function<void()> callback,variantCallback;
    Rocket(){pev->classname="at4rocket";}
    int DamageWeaponId()const override{if(callback)callback();return id;}
    bool AlternateBossDamage()const override{if(variantCallback)variantCallback();return alternate;}
};
struct Missile:CBaseEntity,CSOLBlockMissileDamageSource
{
    float multiplier{2.5f};mutable std::function<void()> callback;
    Missile(){pev->classname="block_missile";}
    bool OberonDamageMultiplier(CSOLMonsterDamageCall&,float& value,ConfigError&)const override
    {value=multiplier;if(callback)callback();return true;}
};
int main()
{
    {
        Fixture f;Rocket rocket;f.monster.behavior=std::make_shared<TestBoss>();
        f.monster.pev->team=1;f.players[0].pev->team=2;
        f.monster.pev->health=1000;f.monster.pev->armorvalue=40;f.monster.host.lastHitgroup=1;
        PipelineDeaths deaths;auto services=std::make_shared<PipelineServices>();
        CCSOLOrdinaryMonsterDamage damage{f.runtime->session->map,deaths};damage.m_services=services;
        const auto owner=[](const void*){return true;};
        Check(damage.Apply(f.call.spawn,rocket.pev,f.players[0].pev,20,2,f.error,owner,nullptr)==TRUE,
            "whole production Apply accepts a separate Boss behavior");
        Check(f.monster.pev->health==900&&f.monster.pev->armorvalue==0&&services->rewards->damage==100,
            "Boss multipliers flow through shared armor reward and health stages");
        Check(pipelineStages==std::vector<std::string>{"item","rules","behavior","parts","before-reward","reward","after-reward","after-health","knockback"},
            "whole shared pipeline preserves exact stage order");
        const auto boss=std::dynamic_pointer_cast<TestBoss>(f.monster.behavior);
        Check(boss->activeSkill==7&&boss->interruptState==-1&&boss->deadline==18&&boss->flinches==0,
            "ordinary hit preserves Boss action interrupt state and deadline");
        f.monster.pev->health=100;pipelineStages.clear();
        Check(damage.Apply(f.call.spawn,rocket.pev,f.players[0].pev,20,2,f.error,owner,nullptr)==FALSE&&deaths.calls==1,
            "lethal Boss hit reaches the shared death dispatcher");
        Check(services->rewards->damage==180&&deaths.health==0&&pipelineStages.back()=="death",
            "hit reward sees pre-clamp damage while death sees clamped health");
    }
    for(const char* boundary:{"item","rules","behavior","parts","before-reward","after-reward","after-health"})
    {
        Fixture f;f.monster.behavior=std::make_shared<TestBoss>();f.monster.pev->team=1;f.players[0].pev->team=2;
        PipelineDeaths deaths;auto services=std::make_shared<PipelineServices>();
        CCSOLOrdinaryMonsterDamage damage{f.runtime->session->map,deaths};damage.m_services=services;
        pipelineHook=[&](const char* stage,CSOLMonsterDamageCall&){if(std::strcmp(stage,boundary)==0)f.monster.behavior=std::make_shared<TestBoss>();};
        Check(!damage.Apply(f.call.spawn,f.players[0].pev,f.players[0].pev,100,2,f.error,[](const void*){return true;},nullptr)&&f.error.message.empty(),
            "behavior replacement cancels whole Apply without persistent failure");
        Check(deaths.calls==0&&pipelineStages.back()==boundary,"replaced Boss cannot continue rewards or death after callback");
    }
    for(int source:{133,275,446,134})for(int group:{0,1,3,6,8,9})for(bool alternate:{false,true})
    {
        Fixture f;Rocket rocket;rocket.id=source;rocket.alternate=alternate;f.call.source=&rocket;f.monster.host.lastHitgroup=group;
        float expected{};Check(csol::scenario::ResolveOrdinaryMonsterDamage(10,group,0,expected),"base hitgroup reference");
        Check(CSOL_ResolveOberonHitDamage(f.call,0,f.error),"real Oberon modifier accepts typed rocket");
        const float multiplier=source==134?1:source==446?(alternate?3.5f:7.0f):3.0f;
        Check(f.call.damage==expected*multiplier,"Boss source multiplier follows shared hitgroup stage and original profile selection");
    }
    {
        Fixture f;f.call.source=f.call.player;f.call.damage=1;f.monster.host.lastHitgroup=1;
        Check(CSOL_ResolveOberonHitDamage(f.call,18,f.error)&&f.call.damage==1035,"direct AWP floor precedes head multiplier");
        Missile missile;f.call.source=&missile;f.call.damage=20;
        Check(CSOL_ResolveOberonHitDamage(f.call,0,f.error)&&f.call.damage==150,"block missile uses real provider parameter");
        missile.callback=[&]{f.call.Cancel();};f.call.damage=20;
        Check(!CSOL_ResolveOberonHitDamage(f.call,0,f.error)&&f.error.message.empty(),"source callback cancellation stops modifier");
    }
    {
        Fixture f;CBaseEntity forged;forged.pev->classname="at4rocket";f.call.source=&forged;
        Check(!CSOL_ResolveOberonHitDamage(f.call,0,f.error)&&!f.error.message.empty(),"classname cannot fabricate AT4 field layout");
    }
    {
        Fixture f;Rocket rocket;rocket.callback=[&]{f.call.Cancel();};f.call.source=&rocket;
        Check(!CSOL_ResolveOberonHitDamage(f.call,0,f.error)&&f.call.damage==10,"rocket callback retirement preserves unmultiplied prefix");
    }
    {
        Fixture f;Rocket rocket;rocket.id=446;rocket.variantCallback=[&]{f.call.Cancel();};f.call.source=&rocket;
        Check(!CSOL_ResolveOberonHitDamage(f.call,0,f.error)&&f.call.damage==10&&f.error.message.empty(),
            "AT4 variant callback retirement stops before source multiplier");
    }
    {
        Fixture f;f.call.damage=300;
        Check(f.services->RecordBossParticipation(f.call,f.error),"production contribution records hit");
        Check(f.services->m_boss->total==300&&f.services->m_boss->hits==1,"shared total and hit counter");
        Check(packets.size()==4&&packets[0].dest==MSG_ALL&&packets[3].recipient==2,"top three plus original first polled slot2");
        Check(packets[0].fields==std::vector<std::pair<char,double>>{{'b',76},{'s',0},{'b',1},{'l',3},{'b',100}},"original contribution wire values");
        for(int i=1;i<4;++i){f.call.player=&f.players[i];f.call.damage=300*(i+1);Check(f.services->RecordBossParticipation(f.call,f.error),"multiple participants");}
        packets.clear();f.call.player=&f.players[0];f.call.damage=1;
        Check(f.services->RecordBossParticipation(f.call,f.error)&&packets.size()==5,"non-top-three attacker gets personal packet plus poll");
        Check(packets[3].recipient==1&&packets[3].fields[1].second==3,"personal zero-based rank");
        f.services->RemoveBossParticipant(EHANDLE(&f.players[3]));
        Check(f.services->m_boss->total==1801&&f.services->m_boss->participants[4].damage==0,"retired participant subtracts its accumulated damage");
        ++f.runtime->session->map.round;f.call.damage=10;packets.clear();
        Check(f.services->RecordBossParticipation(f.call,f.error)&&f.services->m_boss->total==10,"new round cannot inherit old contribution");
        Check(f.services->m_boss->nextHealth==99999.0f,"formal round reset suspends health summary until the next Boss spawn");
    }
    {
        Fixture f;f.call.damage=0;
        Check(f.services->RecordBossParticipation(f.call,f.error)&&f.services->m_boss->hits==1,"zero damage still counts and sends");
        Check(packets[0].fields[4].second==0,"0/0 percentage keeps original indefinite low byte");
        packets.clear();f.call.player->pev->flags=static_cast<int>(0x80000000u);
        Check(f.services->RecordBossParticipation(f.call,f.error)&&packets.empty(),"exact retired-player flags skip contribution");
    }
    {
        Fixture f;f.players[1].playerTypeCallback=[&]{f.services->ResetBossDamageState();};
        Check(!f.services->RecordBossParticipation(f.call,f.error)&&f.call.cancelled&&f.error.message.empty(),
            "polled IsPlayer callback retirement cancels the old contribution owner");
        Check(packets.size()==3&&f.services->m_boss->total==0,"no private rank packet after poll owner reset");
    }
    {
        Fixture f;f.players[1].pev->flags=1<<26;
        Check(f.services->RecordBossParticipation(f.call,f.error)&&packets.size()==3,
            "polled entity must confirm the original player predicate");
    }
    for(int boundary=0;boundary<4;++boundary)
    {
        Fixture f;int messages{};
        packetHook=[&]{if(++messages==boundary+1)f.services->ResetBossDamageState();};
        Check(!f.services->RecordBossParticipation(f.call,f.error)&&f.call.cancelled&&f.error.message.empty(),"message reset cancels retired contribution owner");
        Check(packets.size()==boundary+1,"no further packets after contribution reset");
    }
    {
        Fixture f;f.monster.pev->health=f.monster.pev->max_health=1000;
        Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"Boss spawn initializes actual health owner");
        Check(packets.size()==2&&packets[0].fields[1].second==13&&packets[1].fields[1].second==27,"init then ratio protocol");
        Check(packets[1].fields[3].first=='l'&&packets[1].fields[3].second==std::bit_cast<std::int32_t>(1.0f),"ratio preserves original raw float32 writer");
        packets.clear();Check(f.services->UpdateBossHealth(0,f.error)&&packets.empty(),"health timer comparison is strict");
        Check(f.services->UpdateBossHealth(0.1f,f.error)&&packets.size()==1,"first poll sends changed cached health");
        packets.clear();f.monster.pev->health=750;
        Check(f.services->RecordBossHealth(f.call,f.error)&&packets.empty(),"nonlethal hit commits summary without immediate ratio");
        Check(f.services->UpdateBossHealth(0.4f,f.error)&&packets.empty(),"exact interval equality remains gated");
        Check(f.services->UpdateBossHealth(0.401f,f.error)&&packets.size()==1&&packets.back().fields[3].second==std::bit_cast<std::int32_t>(0.75f),"changed health emits at next original polling boundary");
        packets.clear();Check(f.services->UpdateBossHealth(1,f.error)&&packets.empty(),"unchanged health does not flood network");
        f.monster.pev->health=-10;
        Check(f.services->RecordBossHealth(f.call,f.error)&&f.monster.pev->health==0&&packets.size()==1,"lethal hit sends zero immediately and clamps cached host health");
        Check(f.services->UpdateBossHealth(1.4f,f.error)&&packets.size()==2,"later zero summary still follows original changed-health poll");
    }
    for(int boundary=1;boundary<=2;++boundary)
    {
        Fixture f;int messages{};packetHook=[&]{if(++messages==boundary)f.services->ResetBossDamageState();};
        Check(!f.services->InitializeBossHealth(f.call.spawn,f.error)&&f.error.message.empty(),"spawn message reentry cannot alter new Boss summary");
        Check(f.services->m_boss->entityIndex==0,"new Boss state stays unmodified after reset callback");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before frame cancellation");
        packetHook=[&]{++f.runtime->session->map.round;};
        Check(!f.services->UpdateBossHealth(1,f.error)&&f.services->m_boss->sentHealth==0,"frame cancellation cannot commit sentHealth into retired round");
    }
    for(int cause=0;cause!=5;++cause)
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before uncredited Boss disappearance");
        packets.clear();
        if(cause==0)f.monster.pev->health=0;
        if(cause==1)f.monster.pev->deadflag=1;
        if(cause==2)f.monster.borrowed=false;
        if(cause==3)++f.monster.lease;
        if(cause==4)++f.monster.ed.serial;
        Check(f.services->UpdateBossHealth(0.1f,f.error)&&packets.size()==1&&packets[0].fields[3].second==0,
            "uncredited death or retired lease clears even before first health poll");
        packets.clear();
        Check(f.services->UpdateBossHealth(0.5f,f.error)&&packets.empty(),"retired Boss does not repeat zero health forever");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before formal reset");
        packets.clear();++f.runtime->session->map.round;f.services->ResetBossDamageState();
        Check(f.services->UpdateBossHealth(0,f.error)&&packets.size()==1&&packets[0].fields[3].second==0,
            "round reset clears published health before disabled polling deadline");
        packets.clear();
        Check(f.services->UpdateBossHealth(1,f.error)&&packets.empty(),"retired reset notification is consumed once");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize former Boss");
        CCSOLScenarioMonster next;next.ed.index=65;next.pool=&f.runtime->session->pool;next.pev->health=75;
        CSOLMonsterSpawnCall spawn{&f.runtime->session->map,&next};
        Check(f.services->InitializeBossHealth(spawn,f.error),"replace Boss with same appearance");
        packets.clear();f.monster.pev->health=0;
        Check(f.services->RecordBossHealth(f.call,f.error)&&packets.empty()&&f.services->m_boss->entityIndex==65,
            "late lethal hit on former Boss cannot clear replacement of same appearance");
        Check(f.services->UpdateBossHealth(1,f.error)&&packets.size()==1&&packets[0].fields[3].second==std::bit_cast<std::int32_t>(0.75f),
            "replacement health remains authoritative after old lethal hit");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before recursive replacement");
        CCSOLScenarioMonster next;next.ed.index=65;next.pool=&f.runtime->session->pool;
        CSOLMonsterSpawnCall spawn{&f.runtime->session->map,&next};
        bool reentered{},nestedAccepted{true};
        packetHook=[&]{if(!reentered){reentered=true;nestedAccepted=f.services->InitializeBossHealth(spawn,f.error);}};
        f.monster.pev->health=0;packets.clear();
        Check(!f.services->RecordBossHealth(f.call,f.error)&&f.call.cancelled&&!nestedAccepted&&
            f.error.message=="Boss health initialization message failed"&&packets.size()==1,
            "nested initialization during MESSAGE_END is rejected without nesting packets");
        Check(f.services->m_boss->entityIndex==65&&f.services->m_boss->health==100,
            "old lethal continuation cannot overwrite replacement state after nested-send rejection");
        packetHook={};f.error={};packets.clear();
        Check(f.services->InitializeBossHealth(spawn,f.error)&&packets.size()==2,
            "replacement may initialize successfully after outer message guard closes");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before index callback replacement");
        CCSOLScenarioMonster next;next.ed.index=65;next.pool=&f.runtime->session->pool;
        CSOLMonsterSpawnCall spawn{&f.runtime->session->map,&next};
        bool reentered{};
        indexHook=[&]{if(!reentered){reentered=true;Check(f.services->InitializeBossHealth(spawn,f.error),
            "replacement initializes at reentrant entity-index boundary");}};
        f.monster.pev->health=0;packets.clear();
        Check(!f.services->RecordBossHealth(f.call,f.error)&&f.call.cancelled&&f.error.message.empty(),
            "same-state revision replacement cancels old lethal continuation");
        Check(packets.size()==2&&f.services->m_boss->entityIndex==65&&f.services->m_boss->health==100,
            "old lethal continuation cannot send clear or overwrite newly initialized Boss");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before late join");
        packets.clear();f.monster.pev->health=37;
        Check(f.services->ReplayBossHealth(EHANDLE(&f.players[3]),f.error)&&packets.size()==2,
            "late join receives init followed by fresh health ratio");
        Check(packets[0].dest==MSG_ONE&&packets[1].dest==MSG_ONE&&packets[0].recipient==4&&packets[1].recipient==4,
            "late join replay is private to the exact joining player");
        Check(packets[0].fields[1].second==13&&packets[1].fields[1].second==27&&
            packets[1].fields[3].second==std::bit_cast<std::int32_t>(0.37f),"replay reads live health before next broadcast poll");
        Check(f.services->m_boss->sentHealth==0&&f.services->m_boss->nextHealth==0,
            "late join does not consume everyone else's broadcast polling state");
    }
    for(int cause=0;cause!=7;++cause)
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize replay cancellation fixture");
        packets.clear();int count{};
        packetHook=[&]{if(++count==1){
            if(cause==0)f.monster.pev->health=0;
            if(cause==1)++f.monster.lease;
            if(cause==2)f.players[0].has_disconnected=true;
            if(cause==3)++f.players[0].ed.serial;
            if(cause==4)f.runtime->session->map.access->Revoke();
            if(cause==5)++f.runtime->session->map.round;
            if(cause==6)f.services->ResetBossDamageState();
        }};
        const bool result=f.services->ReplayBossHealth(EHANDLE(&f.players[0]),f.error);
        if(cause<2)
            Check(result&&packets.size()==2&&packets[1].fields[3].second==0,
                "death or lease return during replay clears just-initialized private panel");
        else
            Check(!result&&packets.size()==1&&f.error.message.empty(),
                "replay cancels across recipient serial map round or state retirement");
    }
    {
        Fixture f;Check(f.services->InitializeBossHealth(f.call.spawn,f.error),"initialize before revoked map reset");
        packets.clear();f.runtime->session->map.access->Revoke();
        f.runtime->session->map.access=std::make_shared<CSOLScenarioEntityAccess>();
        ++f.runtime->session->map.round;f.services->ResetBossDamageState();
        Check(f.services->UpdateBossHealth(1,f.error)&&packets.empty(),
            "new map token does not replay an old map's Boss removal message");
    }
    {
        Fixture f;bool nested{};beginHook=[&]{nested=!f.runtime->session->messages.SendBossHealthRatio(18,1,1,64);};
        Check(f.runtime->session->messages.SendBossParticipation(nullptr,0,1,3,100)&&nested&&packets.size()==1,"shared packet guard rejects nested Boss message");
        beginHook={};g_engfuncs.pfnWriteLong=false;
        Check(!f.runtime->session->messages.SendBossHealthRatio(18,1,1,64),"missing engine writer rejects packet before opening");
    }
    std::cout<<"checks="<<checks<<" failures=0\n";
}
