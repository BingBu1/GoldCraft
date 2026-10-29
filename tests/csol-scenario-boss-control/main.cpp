// SPDX-License-Identifier: GPL-3.0-or-later
// Preparation only. Actual SDK/map/pool/spawn lease and animation chain;
// explicit control-edge observers do not implement a playable Oberon.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_monster_boss_control.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../csol-scenario-difficulty/support.hpp"
#include <optional>

namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using csol::scenario::BossSkillTimer;
using Begin = CSOLBossBeginResult;
using Hook = IReGameHook_CBaseAnimating_ResetSequenceInfo;
bool Equal(const Vector& a,const Vector& b) { return a.x==b.x&&a.y==b.y&&a.z==b.z; }
bool Timer(const BossSkillTimer& timer,float duration,float deadline,float start)
{ return timer.duration==duration&&timer.deadline==deadline&&timer.startedAt==start; }

struct Events final : csol::scenario::TriggerWaveEvents
{
    bool Check(ConfigError& error) const override { error={};return true; }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override { throw std::runtime_error("control fixture cannot complete a map"); }
};
struct NoDeath final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&,const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("Boss damage/death is outside the control fixture"); }
};
struct IdentityBehavior final : CCSOLMonsterBehavior
{
    void Think(CCSOLScenarioMonster&) override { throw std::runtime_error("Boss Think not implemented by this fixture"); }
    BOOL TakeDamage(CCSOLScenarioMonster&,entvars_t*,entvars_t*,float,int) override
    { throw std::runtime_error("Boss damage not implemented by this fixture"); }
    void Killed(CCSOLScenarioMonster&,entvars_t*,int) override
    { throw std::runtime_error("Boss death not implemented by this fixture"); }
};
struct PlayerEdge
{
    int calls{};bool alive{true};std::function<void()> action;
};
struct Player final : CBasePlayer
{
    PlayerEdge* edge{}; // Fixture-owned, no nontrivial private-data destructor.
    BOOL IsAlive() override
    {
        auto* state=edge;++state->calls;const bool alive=state->alive;const auto action=state->action;
        if(action)action();return alive;
    }
};
struct RuleEdge final : CCSOLBossControlServices
{
    bool current{true}, accept{true};int calls{};
    CCSOLScenarioMessages messages;
    std::function<void(CSOLMonsterSpawnCall&)> action;
    bool OwnerCurrent() const override { return current; }
    bool RulesSkillStarted(CSOLMonsterSpawnCall& call,ConfigError& error) override
    {
        ++calls;Require(call.Current()!=nullptr,"required v354 observation receives a real live spawn lease");
        const auto callback=action;if(callback)callback(call);
        if(!accept)error.message="explicit rule observer rejection";
        return accept;
    }
    const CCSOLScenarioMessages& BossMessages() const override { return messages; }
};

csol::scenario::BossSkillTable Skills(int message=-1)
{
    csol::scenario::BossSkillTable table;
    for(int index=0;index<2;++index)
    {
        csol::scenario::BossSkillRow row;
        row.id=1002;row.type=index?9:3;row.ratio=255;row.cooltime=3;row.animationTime=2.5f;
        row.damageDelay=.5f;row.sequence="attack";row.forceAttackTime=4;
        row.startDistanceMaxSquared=1000000;row.startHealthMax=100;row.approachDistanceSquared=10000;
        row.startMessage=static_cast<std::int16_t>(message);
        table.groups[1002].push_back(table.rows.size());table.rows.push_back(std::move(row));
    }
    return table;
}

struct Fixture
{
    inline static Fixture* active{};
    Engine& engine;
    enginefuncs_t saved;
    CCSOLScenarioMap map;
    CCSOLMonsterPool pool;
    Events events;
    NoDeath noDeath;
    CCSOLMonsterDeathRequests deaths{noDeath};
    csol::scenario::MonsterType type;
    CCSOLScenarioMonster* monster{};
    std::shared_ptr<CSOLMonsterSpawnState> spawn=std::make_shared<CSOLMonsterSpawnState>();
    std::shared_ptr<RuleEdge> rules=std::make_shared<RuleEdge>();
    std::shared_ptr<csol::scenario::BossSkills> skills;
    std::shared_ptr<CCSOLMonsterBossControl> control;
    std::optional<CSOLMonsterSpawnCall> call;
    std::array<PlayerEdge,33> playerEdges;
    struct Model { studiohdr_t header{};std::array<mstudioseqdesc_t,3> sequences{}; } model;
    int modelCalls{},animationCalls{};
    std::function<void()> onModel,onAnimation;
    ConfigError error;
    explicit Fixture(Engine& value,csol::scenario::BossSkillTable table=Skills()) : engine(value),saved(g_engfuncs)
    {
        Require(!active,"single Boss control fixture");active=this;
        engine.Clear();engine.files=session_test::Resources();engine.globals.time=10;engine.globals.maxClients=32;
        engine.nextEntity=33;engine.Prepare("0");g_pGameRules=InstallGameRules();
        Require(g_pGameRules&&!CSOL_RulesRejected(),"actual native rules host only the independent control fixture");
        // Complete round metadata, without Session/ordinary combat providers.
        engine.files["maps/zbs_map_session_test.cso"]=difficulty_test::Encrypt(
            "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nRewardRound,0\r\n"
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,1\r\n");
        engine.files["maps/zbs_wave_map_session_test.cso"]=difficulty_test::Encrypt(
            "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\r\n"
            "1,1,0,10,1,1,1,\r\n");
        engine.files["maps/ZBS_TRG_map_session_test.cso"]=difficulty_test::Encrypt(
            "id,type,map trigger id,wave id,cool time,dest ratio,end wave\r\n"
            "1,g,1,,0,100,\r\n");
        for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int id:{1,2,30})
        {
            if(std::strcmp(name,"zbstrigger")&&id!=1)continue;
            auto* ed=CREATE_NAMED_ENTITY(Engine::String(name));
            if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=2;}
            if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))){p->m_iId=id;p->m_iTeam=1;p->m_iMaxSpawn=100;}
            if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))){p->m_iId=id;ed->v.model=Engine::String("*1");}
            Require(engine.api.pfnSpawn(ed)==0,"real SDK marker DispatchSpawn");
            ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
        }
        Require(map.BeginMap("map_session_test")&&map.CollectEntities()&&map.BeginRound(0,10,0,events),
            "real Map/TRG owns a live round generation");
        Require(pool.BindEntityAccess(map.EntityAccess())&&pool.Initialize(),"actual45-slot pool shares map access");
        type.id=30200;type.skill=1002;type.appearance="zbs_bossL_big02";type.priority=2;
        type.speed=175;type.color={71,72,73};type.column21={2,3,155};
        monster=pool.Acquire(type,1,map.SpawnPoints().Points());
        Require(monster&&monster->InstallBehavior(std::make_unique<IdentityBehavior>()),"real pool borrow installs only a nonplayable behavior identity");
        monster->pev->origin=Vector(0,0,55);monster->pev->health=monster->pev->max_health=100;
        monster->pev->velocity=Vector(8,9,10);monster->pev->angles=Vector(0,37,0);
        monster->Animation().SynchronizeSequence(0);monster->pev->sequence=0;
        spawn->lease=pool.Lease(*monster);spawn->attributes.speed=175;spawn->skill=1002;
        call.emplace(map,pool,deaths,*monster,monster->Behavior(),rules);
        Require(call->Current()==monster&&rules->messages.Register(),"actual lease and engine-registered channels ready");
        skills=std::make_shared<csol::scenario::BossSkills>(std::move(table));
        control=std::make_shared<CCSOLMonsterBossControl>(skills,rules);
        model.header.numseq=3;model.header.seqindex=static_cast<int>(reinterpret_cast<char*>(model.sequences.data())-reinterpret_cast<char*>(&model));
        for(auto& seq:model.sequences){seq.numframes=11;seq.fps=10;seq.flags=STUDIO_LOOPING;}
        std::strcpy(model.sequences[0].label,"idle");std::strcpy(model.sequences[1].label,"attack");std::strcpy(model.sequences[2].label,"other");
        g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*
        {auto& f=*active;++f.modelCalls;const auto action=f.onModel;if(action)action();return &f.model;};
        g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.registerHook(AnimationHook,150);
        Require(control->Reset(*call,*spawn,error),"actual controller reset accepts shared spawn and live type30200/skill1002 lease");
    }
    static void AnimationHook(Hook* chain,CBaseAnimating* entity)
    {
        auto& f=*active;++f.animationCalls;const auto action=f.onAnimation;if(action)action();
        chain->callNext(entity);
    }
    Player& Target(int slot=1,Vector origin=Vector(50,30,55))
    {
        auto* ed=engine.Reserve(slot,"player");auto* player=GetClassPtr<CCSPlayer>(reinterpret_cast<Player*>(&ed->v));
        player->edge=&playerEdges[slot];player->m_iTeam=CT;ed->v.health=100;ed->v.deadflag=DEAD_NO;
        ed->v.origin=origin;ed->v.flags=FL_CLIENT;return *player;
    }
    std::shared_ptr<CSOLBossControlState> Select()
    {
        Require(control->Select(*call,error)&&control->State()->row==0,"actual live selection chooses first priority row");
        return control->State();
    }
    ~Fixture()
    {
        engine.onIndexLookup={};engine.onOffset={};engine.onFind={};onModel={};onAnimation={};rules->action={};
        g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.unregisterHook(AnimationHook);
        control.reset();call.reset();rules->current=false;deaths.Reset();
        if(pool.EntityAccessAllowed())
            Check(pool.Shutdown()&&pool.RegisteredCount()==0,"live controller fixture detaches pool registrations before map end");
        else
            Check(!pool.Shutdown()&&pool.RegisteredCount()==45,
                "revoked map retains all pool registrations for engine-owned OnDestroy callbacks");
        // All controller/map operations have returned; EndMap can reset its
        // handle snapshots without accessing revoked private entity storage.
        Check(map.EndMap(),"idle controller fixture ends real map");
        // The engine owns delayed private-data freeing; no synchronous
        // UTIL_Remove claim. Clear performs the real SDK free callbacks here.
        engine.Clear();Check(pool.RegisteredCount()==0&&pool.ForgetMap(),
            "pool forgets handles only after actual SDK OnFree callbacks drain its registrations");
        g_engfuncs=saved;engine.globals.maxClients=4;active=nullptr;
    }
};

void ResetAndSelection(Engine& engine)
{
    Fixture f(engine);auto& target=f.Target(32);auto state=f.control->State();
    Check(Equal(f.monster->pev->velocity,g_vecZero)&&state->position.destination.x==-99999&&
        f.monster->Animation().LockedUntil()==0&&state->effect.duration==99999&&state->effect.deadline==100009,
        "initial reset stops actual motion and writes documented clocks without initial sequence lookup");
    const auto selected=f.Select();
    Check(selected->target.Get<CBaseEntity>()==&target&&f.skills->State(0)->cooldownDeadline==13&&
        Timer(selected->clocks.end,6.5f,16.5f,10)&&Timer(selected->clocks.selection,7,17,10)&&
        Timer(selected->clocks.startCheck,-1,9,10)&&Timer(selected->clocks.force,4,14,10)&&
        f.monster->HostState().activeSkill==3&&f.monster->HostState().hasSkillRow,
        "live INDEXENT includes32 and selection writes shared cooldown and five original clocks");
    selected->movement.duration=71;selected->avoidance.duration=72;selected->path.duration=73;
    selected->clocks.target.duration=74;selected->clocks.selection.duration=75;
    selected->effect={1,2,3};selected->repeatCount=7;selected->launchOriginZ=80;selected->solidAfter=90;
    selected->position.motion.maxMoveType=-2;selected->position.motion.velocity=Vector2D(5,6);
    f.monster->pev->gravity=3.5f;f.monster->HostState().knockbackBlocked=true;
    f.monster->HostState().enhancedUntil=88;f.monster->pev->sequence=2;
    f.monster->Animation().LockUntil(99);f.engine.globals.time=20;
    Require(f.control->Reset(*f.call,*f.spawn,f.error),"explicit second reset succeeds");
    state=f.control->State();
    Check(state!=selected&&state->row==0&&!state->target.Get<CBaseEntity>()&&Timer(state->clocks.end,6.5f,16.5f,10)&&
        Timer(state->movement,71,-1,-1)&&Timer(state->avoidance,72,-1,-1)&&Timer(state->path,73,-1,-1)&&
        Timer(state->clocks.target,74,-1,-1)&&Timer(state->clocks.selection,75,-1,-1)&&
        Timer(state->effect,99999,100019,20)&&state->repeatCount==0&&state->launchOriginZ==-999999&&state->solidAfter==0,
        "explicit reset clones retained row/end, preserves expired-clock durations and clears only original writes");
    Check(f.monster->HostState().activeSkill==3&&f.monster->HostState().hasSkillRow&&
        f.monster->HostState().knockbackBlocked&&f.monster->HostState().enhancedUntil==-1&&
        f.monster->pev->gravity==3.5f&&f.monster->pev->sequence==2&&f.skills->State(0)->cooldownDeadline==13&&
        state->position.motion.moveType==-2&&f.modelCalls==0,
        "reset does not erase host current skill, gravity, shared deadline or install a new model sequence");
    f.engine.globals.time=0;
    f.control=std::make_shared<CCSOLMonsterBossControl>(f.skills,f.rules);
    Require(f.control->Reset(*f.call,*f.spawn,f.error),"fresh controller reset at engine time zero");
    state=f.control->State();
    Check(state->knownGoodValid&&state->previousKnownGoodValid&&Equal(state->knownGood,g_vecZero)&&
        Equal(state->previousKnownGood,g_vecZero)&&Timer(state->knownGoodClock,1,1,0),
        "base known-good clock starts expired so time-zero reset records previous and current feet");
}

void LiveSelectionEdges(Engine& engine)
{
    auto table=Skills();table.rows[0].ratio=0;
    {
        Fixture f(engine,table);auto& player=f.Target();engine.globals.maxClients=1;
        csol::scenario::BossSkillClocks unused;Require(f.skills->CommitSelection(1,17,unused),"second shared row cooling until20");
        player.edge->action=[&]{engine.globals.time=21;};
        Require(f.control->Select(*f.call,f.error),"live per-row scan survives advancing IsAlive callback");
        Check(player.edge->calls==2&&f.control->State()->row==1&&f.skills->State(1)->cooldownDeadline==24,
            "second row samples new engine time after first row IsAlive rather than one frozen candidate snapshot");
    }
    for(bool atAlive:{false,true})
    {
        Fixture f(engine);auto& player=f.Target();int cancelled{};
        const auto cancel=[&]{if(!cancelled++){f.map.RevokeEntityAccess();}};
        if(atAlive)player.edge->action=cancel;
        else engine.onIndexLookup=[&](int slot,edict_t*){if(slot==1)cancel();};
        Check(!f.control->Select(*f.call,f.error)&&cancelled==1&&!f.control->State()->row&&
            f.skills->State(0)->cooldownDeadline==0&&!f.monster->HostState().hasSkillRow,
            "live INDEXENT/IsAlive revocation cancels before shared state or host selection commits");
    }
    {
        Fixture f(engine);auto& player=f.Target();int calls{};
        player.edge->action=[&]{if(!calls++){Require(f.pool.Free(*f.monster),"callback returns actual borrowed pool slot");}};
        Check(!f.control->Select(*f.call,f.error)&&!f.pool.Borrowed(*f.monster)&&!f.control->State()->row,
            "same entity and lease parked by IsAlive cannot receive old selection state");
    }
    {
        Fixture f(engine);auto& player=f.Target();
        player.edge->action=[&]{++player.edict()->serialnumber;};
        Check(f.control->Select(*f.call,f.error)&&!f.control->State()->row,
            "client generation changes during IsAlive invalidate captured candidate handles");
    }
}

void BeginStages(Engine& engine)
{
    Fixture f(engine);auto& target=f.Target();auto state=f.Select();engine.globals.time=11;
    f.monster->pev->frame=77;f.monster->pev->framerate=.25f;f.monster->pev->animtime=-5;
    f.monster->pev->velocity=Vector(1,2,3);state->deltaX=4;state->deltaY=5;state->motionRemainder=6;
    bool ordered{};
    f.rules->action=[&](CSOLMonsterSpawnCall&)
    {
        ordered=state->position.faceRequested&&Equal(state->position.faceGoal,target.pev->origin)&&
            f.monster->pev->angles.y==37&&f.animationCalls==1&&f.modelCalls==3&&
            Timer(state->effect,.5f,11.5f,11)&&Timer(state->clocks.end,2.5f,13.5f,11)&&
            Timer(state->clocks.selection,3,14,11)&&f.monster->Animation().LockedUntil()==13.5f;
    };
    Check(f.control->TryBegin(*f.call,f.error)==Begin::Started&&ordered&&f.rules->calls==1&&
        f.skills->State(0)->sequence==1&&f.monster->pev->sequence==1&&f.monster->pev->frame==0&&
        f.monster->pev->framerate==1&&f.monster->pev->animtime==11&&Equal(f.monster->pev->velocity,g_vecZero)&&
        state->deltaX==0&&state->deltaY==0&&state->motionRemainder==0,
        "real lookup/guarded ReAPI reset forces frame0, locks animation, resets motion, queues facing then invokes required rule edge");
    engine.globals.time=20;f.rules->action={};f.Select();engine.globals.time=21;
    f.monster->pev->frame=93;f.monster->pev->framerate=.5f;
    Check(f.control->TryBegin(*f.call,f.error)==Begin::Started&&f.modelCalls==3&&f.animationCalls==1&&
        f.monster->pev->frame==0&&f.monster->pev->framerate==1&&f.monster->pev->animtime==11,
        "same shared cached sequence forces frame/rate but does not retrigger sequence timing reset");
    // Observe mandatory rule failure after its preceding effects; no Boss
    // extension is implemented or reported by this fixture.
    engine.globals.time=30;f.Select();engine.globals.time=31;f.rules->accept=false;
    Check(f.control->TryBegin(*f.call,f.error)==Begin::Failed&&f.error.message=="explicit rule observer rejection",
        "required v354 failure is observable and cannot return Started");
}

void WaitingAndCancellation(Engine& engine)
{
    {
        auto table=Skills();table.rows[0].approachDistanceSquared=0;
        Fixture f(engine,table);f.Target();auto state=f.Select();
        engine.globals.time=14;
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Waiting&&Timer(state->clocks.startCheck,.2f,14.2f,14)&&
            f.modelCalls==0&&f.rules->calls==0,"distance beyond approach at force deadline equality still waits");
        engine.globals.time=14.5f;
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Started,"strictly later force deadline permits start with valid target");
    }
    {
        Fixture f(engine);auto state=f.control->State();state->clocks.startCheck.deadline=10;
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Waiting&&state->clocks.startCheck.deadline==10,
            "start-check deadline equality does not even reschedule its clock");
        engine.globals.time=11;
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Waiting&&Timer(state->clocks.startCheck,99999,100010,11),
            "elapsed check with no current row disables the polling clock");
        f.Target();engine.globals.time=12;state=f.Select();state->target=nullptr;engine.globals.time=40;
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Waiting&&Timer(state->clocks.startCheck,.2f,40.2f,40)&&f.rules->calls==0,
            "missing target retains short recheck even after force time, without animation or rule effect");
    }
    for(int edge=0;edge<3;++edge)
    {
        Fixture f(engine);f.Target();auto state=f.Select();engine.globals.time=11;int cancelled{};
        const auto cancel=[&]{if(!cancelled++){f.rules->current=false;}};
        if(edge==0)f.onModel=cancel;
        if(edge==1)f.onAnimation=cancel;
        if(edge==2)f.rules->action=[&](CSOLMonsterSpawnCall&){cancel();};
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Cancelled&&cancelled==1&&
            f.rules->calls==(edge==2?1:0),"sequence lookup, actual reset hook and required rule callback revalidate owner before later work");
    }
    {
        Fixture f(engine);f.Target();auto state=f.Select();engine.globals.time=11;
        f.onModel=[&]{state->row.reset();};
        Check(f.control->TryBegin(*f.call,f.error)==Begin::Cancelled&&f.control->State()==state&&
            !state->row&&f.modelCalls==1&&f.animationCalls==0&&f.rules->calls==0,
            "model callback clearing row on the same shared state cancels before sequence lookup and later begin effects");
    }
    {
        Fixture f(engine);f.Target();f.Select();engine.globals.time=11;
        const std::weak_ptr weak=f.control;auto* control=f.control.get();
        f.onModel=[&]{f.control.reset();};
        Check(control->TryBegin(*f.call,f.error)==Begin::Started&&weak.expired()&&f.rules->calls==1,
            "entry retains the last shared controller across engine callbacks then releases it after return");
    }
}

void InterruptAndFinish(Engine& engine)
{
    for(const std::int16_t interrupt:{std::int16_t{-1},std::int16_t{7}})
    {
        Fixture f(engine);f.Target();auto state=f.Select();engine.globals.time=11;std::int16_t reason=interrupt;
        state->repeatCount=3;state->launchOriginZ=444;state->movement={70,71,72};state->avoidance={73,74,75};
        f.monster->Animation().LockUntil(80);f.monster->pev->gravity=3.5f;f.monster->pev->velocity=Vector(1,2,3);
        Require(f.control->Interrupt(*f.call,reason,f.error),"current-row interrupt processed");
        Check(reason==0&&Equal(f.monster->pev->velocity,g_vecZero)&&Timer(state->movement,70,-1,-1)&&
            Timer(state->avoidance,73,-1,-1)&&Timer(state->effect,99999,100010,11)&&
            state->repeatCount==0&&state->launchOriginZ==444&&f.monster->pev->gravity==3.5f,
            "interrupt resets documented motion/clocks but retains launchZ and gravity");
        if(interrupt==-1)
            Check(!state->row&&!f.monster->HostState().hasSkillRow&&f.monster->HostState().activeSkill==0&&
                f.skills->State(0)->cooldownDeadline==11&&f.monster->Animation().LockedUntil()==11&&
                Timer(state->clocks.end,6.5f,-1,-1)&&Timer(state->clocks.selection,7,-1,-1),
                "minusone interrupt releases shared row cooldown and current selection with retained durations");
        else
            Check(state->row==0&&f.monster->HostState().hasSkillRow&&f.monster->HostState().activeSkill==3&&
                f.skills->State(0)->cooldownDeadline==13&&f.monster->Animation().LockedUntil()==80&&
                Timer(state->clocks.end,6.5f,16.5f,10)&&Timer(state->clocks.selection,7,17,10),
                "other interrupt keeps current row, shared deadline, animation lock and end/selection timers");
    }
    {
        Fixture f(engine);std::int16_t reason=-1;f.monster->pev->velocity=Vector(1,2,3);
        Check(f.control->Interrupt(*f.call,reason,f.error)&&reason==-1&&Equal(f.monster->pev->velocity,Vector(1,2,3)),
            "interrupt with no row preserves incoming state and motion");
    }
    {
        Fixture f(engine);f.Target();auto state=f.Select();state->movement={7,8,9};state->path={10,11,12};
        state->effect={13,14,15};state->launchOriginZ=456;state->repeatCount=3;
        f.monster->Animation().LockUntil(88);f.monster->pev->velocity=Vector(4,5,6);f.monster->pev->gravity=3.5f;
        f.monster->pev->maxspeed=1;f.monster->pev->rendercolor=g_vecZero;engine.globals.time=16.5f;
        Check(f.control->Finish(*f.call,f.error)&&state->row==0&&state->repeatCount==3,
            "finish deadline equality does not clear skill");
        engine.globals.time=17;
        Check(f.control->Finish(*f.call,f.error)&&!state->row&&state->repeatCount==0&&state->launchOriginZ==-999999&&
            Timer(state->clocks.end,99999,100016,17)&&Timer(state->movement,7,-1,-1)&&
            Timer(state->path,10,11,12)&&Timer(state->effect,13,14,15)&&
            f.monster->pev->maxspeed==175&&Equal(f.monster->pev->rendercolor,Vector(71,72,73))&&
            f.monster->pev->rendermode==2&&f.monster->pev->renderfx==3&&f.monster->pev->renderamt==155&&
            Equal(f.monster->pev->velocity,Vector(4,5,6))&&f.monster->pev->gravity==3.5f&&
            f.monster->Animation().LockedUntil()==88&&f.skills->State(0)->cooldownDeadline==13,
            "finite Finish restores spawn speed/type rendering and clears listed fields without ResetMotion or broad clock/cache reset");
    }
}

struct PacketCapture
{
    inline static PacketCapture* active{};enginefuncs_t saved{g_engfuncs};
    int destination{},message{};const float* origin{};edict_t* recipient{};
    std::vector<std::uint8_t> bytes;bool ended{};int begins{};
    std::function<void()> edge;
    PacketCapture()
    {
        Require(!active,"single packet capture");active=this;
        g_engfuncs.pfnMessageBegin=[](int d,int m,const float* o,edict_t* r)
        {auto& c=*active;++c.begins;c.destination=d;c.message=m;c.origin=o;c.recipient=r;c.bytes.clear();c.ended=false;c.Step();};
        g_engfuncs.pfnWriteByte=[](int v){active->bytes.push_back(static_cast<std::uint8_t>(v));active->Step();};
        g_engfuncs.pfnWriteShort=[](int v)
        {active->bytes.push_back(static_cast<std::uint8_t>(v));active->bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned>(v)>>8));active->Step();};
        g_engfuncs.pfnWriteString=[](const char*){throw std::runtime_error("Boss start must have no trailing string");};
        g_engfuncs.pfnMessageEnd=[]{active->ended=true;active->Step();};
    }
    void Step(){const auto callback=edge;if(callback)callback();}
    ~PacketCapture(){g_engfuncs=saved;active=nullptr;}
};
void MessagesAndReentry(Engine& engine)
{
    for(int boundary=0;boundary<5;++boundary)
    {
        Fixture f(engine,Skills(205));f.Target();PacketCapture wire;int step{},attempts{};
        CCSOLScenarioMessages other;Require(other.Register(),"second real message owner registered");
        wire.edge=[&]
        {
            if(step++!=boundary)return;++attempts;std::int16_t interrupt=-1;ConfigError nested;
            Check(!f.control->Select(*f.call,nested)&&f.control->TryBegin(*f.call,nested)==Begin::Cancelled&&
                !f.control->Reset(*f.call,*f.spawn,nested)&&!f.control->Interrupt(*f.call,interrupt,nested)&&
                !f.control->Finish(*f.call,nested)&&!other.SendBossSkillStart(1),
                "each actual start-packet edge blocks controller reentry and cross-owner message nesting");
        };
        Require(f.control->Select(*f.call,f.error),"selection with original start message succeeds");wire.edge={};
        Check(attempts==1&&wire.begins==1&&wire.ended&&wire.destination==MSG_ALL&&
            wire.message==engine.messages.at("MPToCL")&&!wire.origin&&!wire.recipient&&
            wire.bytes==std::vector<std::uint8_t>{27,7,205,0},"Boss27/7 packet is exactly four payload bytes with no STRING/NUL");
        Check(other.SendBossSkillStart(-32768)&&wire.bytes==std::vector<std::uint8_t>{27,7,0,128},
            "message guard recovers and preserves signed16 low wire bits");
    }
}
}

int main()
{
    std::unique_ptr<Engine> engine;
    try
    {
        engine=std::make_unique<Engine>();
        ResetAndSelection(*engine);LiveSelectionEdges(*engine);BeginStages(*engine);
        WaitingAndCancellation(*engine);InterruptAndFinish(*engine);MessagesAndReentry(*engine);
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    engine.reset();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
