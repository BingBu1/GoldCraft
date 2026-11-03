// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK Map/Pool/SpawnCall/animation. Projectile creation is the sole
// business boundary: this suite does not validate the bomb entity or service.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_monster_bomb.h"
#include "../../native/csol/server/scenario_monster_ordinary.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include <bit>
#include <cmath>
#include <limits>

#pragma clang fp contract(off)

namespace {
using namespace sdk_test;
using Seq = csol::scenario::MonsterSequence;
struct Waves : csol::scenario::TriggerWaveEvents {
    bool Check(csol::scenario::ConfigError&) const override { return true; }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override {}
};
struct Deaths : CCSOLMonsterDeathExecutor {
    bool ExecuteDeath(CCSOLScenarioMonster&, const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("unexpected death in startup-only case"); }
};
int assertions{}, taskFailures{};
void Expect(bool condition, const char* label) {
    ++assertions; if (!condition) { ++taskFailures; std::cerr << "ASSERT " << label << '\n'; }
}
bool Bits(float a, float b) { return std::bit_cast<unsigned>(a) == std::bit_cast<unsigned>(b); }
bool Bits(const Vector& a, const Vector& b) { return Bits(a.x,b.x)&&Bits(a.y,b.y)&&Bits(a.z,b.z); }
Vector OriginalVector(std::array<unsigned,3> words) {
    return {std::bit_cast<float>(words[0]),std::bit_cast<float>(words[1]),std::bit_cast<float>(words[2])};
}
struct Case {
    inline static Case* active{};
    Engine& engine;
    CCSOLScenarioMap map;
    CCSOLMonsterPool pool;
    Deaths executor;
    CCSOLMonsterDeathRequests deaths{executor};
    Waves waves;
    csol::scenario::MonsterType type;
    CSOLMonsterSpawnState spawn;
    CSOLOrdinaryMonsterState state;
    CCSOLScenarioMonster* monster{};
    csol::scenario::ConfigError error;
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t,4> sequences{}; } model;
    Vector origin{}, velocity{};
    float damage{};
    int creates{}, modelQueries{};
    bool current{true}, createResult{true};
    std::function<void()> onCreate, onModel;
    explicit Case(Engine& e) : engine(e) {
        active=this; engine.globals.time=100;
        for (const char* name : {"playerspawn","zombiespawn","zbstrigger"}) {
            for (int id : {1,2,30}) {
                if (std::strcmp(name,"zbstrigger") && id!=1) continue;
                auto* ed=Engine::Create(Engine::String(name));
                auto* base=CBaseEntity::Instance(ed);
                if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(base)) { p->m_iId=1; p->m_iTeam=2; }
                if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(base)) { p->m_iId=7; p->m_iTeam=1; p->m_iMaxSpawn=1000; }
                if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(base)) { p->m_iId=id; ed->v.model=Engine::String("*1"); }
                Require(engine.api.pfnSpawn(ed)==0,"actual marker DispatchSpawn");
                ed->v.mins=Vector(-8,-8,-8); ed->v.maxs=Vector(8,8,8);
            }
        }
        Require(map.BeginMap("map_session_test"),"actual Map resource load");
        Require(map.CollectEntities(),"actual marker collector");
        Require(map.BeginRound(0,100,0,waves),"actual round identity");
        Require(pool.Initialize(),"actual full SDK pool allocation");
        type.id=24; type.appearance="zombi_origin";
        monster=pool.Acquire(type,7,map.SpawnPoints().Points());
        Require(monster!=nullptr,"actual pool borrow");
        monster->pev->origin=Vector(11,13,17); monster->pev->velocity=Vector(31,32,33);
        monster->pev->frame=88; monster->Animation().LockUntil(77);
        spawn.attributes.attackRange=2000; spawn.attributes.attackDamage=37.25f;
        spawn.attributes.attackInterval=2.75f;
        spawn.sequences[Seq::ShootGrenadeIdle]=1; spawn.sequences[Seq::ShootGrenadeWalk]=2;
        spawn.sequences[Seq::ShootGrenadeRun]=3;
        state.skill={6,71,65}; state.skillActive={7,72,65}; state.findPath={8,73,65};
        state.melee.attack={9,74,65}; state.melee.avoidance={10,75,65}; state.melee.idleObstacleCheck={11,76,65};
        state.position.faceGoal=Vector(15,16,17); state.position.destination=Vector(100,200,300);
        state.position.motion.velocity=Vector2D(21,22); state.position.motion.moveType=3;
        state.position.motion.maxMoveType=4; state.deltaX=23; state.deltaY=24; state.motionRemainder=25;
        model.header.numseq=4; model.header.length=sizeof(model);
        model.header.seqindex=offsetof(Model,sequences);
        for(auto& seq:model.sequences) { seq.fps=10; seq.numframes=11; seq.flags=STUDIO_LOOPING; }
        g_engfuncs.pfnGetModelPtr=[](edict_t*)->void* {
            auto& self=*active; ++self.modelQueries; const auto cb=self.onModel;
            if(cb)cb(); return &self.model;
        };
    }
    ~Case() {
        onCreate={}; onModel={}; engine.onOffset={}; engine.onIndexLookup={};
        deaths.Reset(); pool.Shutdown(); map.EndMap(); engine.Clear(); pool.ForgetMap(); active=nullptr;
    }
    CBasePlayer& Player() {
        auto* ed=engine.Reserve(1,"player");
        auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.health=100; ed->v.deadflag=DEAD_NO; ed->v.flags=FL_CLIENT;
        ed->v.origin=monster->pev->origin+Vector(300,400,9999); state.target=p; return *p;
    }
    bool Run(bool suppressed=false) {
        CSOLMonsterSpawnCall call(map,pool,deaths,*monster);
        return CSOL_UseMonsterZombieBomb(call,state,spawn,{},suppressed,
            {[](const void* p){return static_cast<const Case*>(p)->current;},this},error);
    }
    void Tail(float now=100) {
        Expect(state.skillActive.interval==1&&state.skillActive.deadline==now+1&&state.skillActive.stamp==now,"active uses one second");
        Expect(state.skill.interval==spawn.attributes.attackInterval&&state.skill.deadline==now+spawn.attributes.attackInterval&&state.skill.stamp==now,"cooldown uses attackInterval without jitter");
    }
    void NoTail() { Expect(state.skill.deadline==71&&state.skillActive.deadline==72,"cancelled call omits both tail clocks"); }
    void NoLaunch() {
        Expect(creates==0&&modelQueries==0,"early rejection skips animation and creation");
        Expect(state.melee.attack.deadline==74&&state.melee.avoidance.deadline==75&&state.melee.idleObstacleCheck.deadline==76&&monster->Animation().LockedUntil()==77,"early rejection leaves attack clocks unchanged");
        Expect(!state.position.faceRequested&&state.position.faceGoal==Vector(15,16,17),"early rejection leaves facing unchanged");
    }
    void NoStop(const Vector& oldVelocity) {
        Expect(monster->pev->velocity==oldVelocity&&state.findPath.deadline==73,"skill6 retains movement and findPath");
        Expect(state.position.destination==Vector(100,200,300)&&state.position.motion.velocity.x==21&&state.position.motion.velocity.y==22&&state.deltaX==23&&state.deltaY==24&&state.motionRemainder==25,"skill6 has no skill8 Stop/planar reset tail");
    }
};
void Early(Engine& e) {
    for(int which=0;which<6;++which) {
        Case s(e);
        if(which==1)s.Player();
        if(which==2)s.state.target=s.monster;
        if(which==3){auto& p=s.Player();p.pev->origin.x+=2000;}
        if(which==4){s.Player();++s.state.target.Get()->serialnumber;}
        if(which==5){s.Player();s.current=false;}
        Expect(s.Run(which==1)==(which!=5),"root309/target/range/owner early result");
        if(which==5)s.NoTail();else s.Tail();s.NoLaunch();
    }
    for(int which=0;which<2;++which) {
        Case s(e);auto& p=s.Player();
        if(which==0)p.pev->origin.x=std::numeric_limits<float>::quiet_NaN();
        else s.spawn.attributes.attackRange=std::numeric_limits<float>::infinity();
        Expect(!s.Run()&&!s.error.message.empty(),"nonfinite geometry fails with explicit error");s.NoTail();s.NoLaunch();
    }
}
void Launch(Engine& e) {
    const std::array<float,7> speeds{0,15,std::nextafter(15.0f,100.0f),159,160,std::nextafter(160.0f,200.0f),200};
    for(float speed:speeds) {
        Case s(e);auto& p=s.Player();s.monster->pev->velocity=Vector(speed,0,999);
        const Vector old=s.monster->pev->velocity; s.state.jitter=.9f;
        s.spawn.attributes.attackRange=500; // Exact XY boundary; high Z is irrelevant.
        Expect(s.Run()&&s.error.message.empty(),"exact XY range accepted");
        Expect(s.creates==1&&s.modelQueries==2,"one create follows real two-model-query reset");
        const int sequence=speed>160?3:speed>15?2:1;
        Expect(s.monster->pev->sequence==sequence&&s.monster->pev->frame==0,"strict stationary/walk/run thresholds");
        Expect(s.state.melee.attack.deadline==101&&s.state.melee.avoidance.deadline==101&&s.state.melee.idleObstacleCheck.deadline==101&&s.monster->Animation().LockedUntil()==101,"one-second attack avoidance obstacle animation clocks");
        Expect(s.state.position.faceRequested&&s.state.position.faceGoal==p.pev->origin,"facing preserves full target origin");
        Expect(Bits(s.origin,OriginalVector({1101319373,1104045670,1113325568}))&&Bits(s.velocity,OriginalVector({1129447425,1133248512,1127153664}))&&Bits(s.damage,37.25f),"original x86 3-4-5 launch bits (including noninteger 210.000015)");
        s.Tail();s.NoStop(old);
    }
    // Values observed by executing original MP 113F1628..113F1778, with only
    // eyes=(11,13,47) and the creation call controlled. No duplicate formula.
    struct Expected { Vector delta; std::array<unsigned,3> origin,velocity; };
    const Expected cases[]{
        {{0,0,300},{1093664768,1095761920,1111270359},{0,0,1004888129}},
        {{3,4,-500},{1101319373,1104045670,1111438131},{1074161255,1077097267,1043542834}},
        {{30,40,600},{1101319372,1104045670,1113325568},{1101529087,1105199103,1099694080}},
        {{1200,1600,1},{1101319373,1104045670,1113325568},{1138819073,1142292480,1136361472}}
    };
    for(const auto& sample:cases) {
        Case s(e);auto& p=s.Player();p.pev->origin=s.monster->pev->origin+sample.delta;
        Expect(s.Run(),"distance endpoint launches");
        Expect(Bits(s.origin,OriginalVector(sample.origin))&&Bits(s.velocity,OriginalVector(sample.velocity)),"zero/near/Z clamp/speed cap match original x86 bits");s.Tail();
    }
}
void Cancel(Engine& e) {
    { Case s(e);auto& player=s.Player();
      s.onModel=[&]{player.pev->origin=Vector(901,902,903);};
      Expect(s.Run(),"animation callback may move target without cancellation");
      Expect(s.state.position.faceGoal==Vector(901,902,903),"original 113F1618 rereads target position after SetSequence");
      Expect(Bits(s.velocity,OriginalVector({1129447425,1133248512,1127153664})),"original keeps launch XY sampled before SetSequence"); }
    for(int which=0;which<4;++which) {
        Case s(e);s.Player();s.onModel=[&]{if(s.modelQueries==(which<2?1:2)) {if(which%2)s.deaths.Reset();else s.current=false;}};
        Expect(!s.Run(),"model callback cancellation");Expect(s.creates==0,"cancelled reset cannot create projectile");s.NoTail();
        Expect(s.state.melee.attack.deadline==74&&s.monster->Animation().LockedUntil()==77,"cancelled reset cannot publish attack clocks");
    }
    { Case s(e);s.Player();s.onModel=[&]{s.pool.Free(*s.monster);s.monster->m_flLastEventCheck=777;};
      Expect(!s.Run(),"model callback may park borrowed monster");
      Expect(s.modelQueries==1&&s.monster->m_flLastEventCheck==777,"sequence reset stops immediately after pool return");
      Expect(s.creates==0,"parked monster cannot create projectile");s.NoTail(); }
    for(int which=0;which<4;++which) {
        Case s(e);s.Player();
        s.onCreate=[&]{if(which==0)s.current=false;if(which==1)s.deaths.Reset();if(which==2)s.pool.Free(*s.monster);if(which==3)s.engine.globals.time=104;};
        Expect(s.Run()==(which==3),"creation callback outcome");Expect(s.creates==1,"creation boundary called once");
        if(which==3)s.Tail(104);else s.NoTail();
    }
    { Case s(e);s.Player();s.createResult=false;Expect(!s.Run(),"creation service failure propagates");s.NoTail();
      Expect(s.state.melee.attack.deadline==101&&s.state.position.faceRequested,"creation failure keeps already-committed launch animation"); }
}
}
bool CSOL_CreateMonsterZombieBomb(CSOLMonsterSpawnCall& call,const std::shared_ptr<CCSOLScenarioRuntime>&,
    const Vector& origin,const Vector& velocity,float damage,csol::scenario::ConfigError&) {
    auto& s=*Case::active;Expect(call.Current()!=nullptr,"create receives current lease");
    Expect(s.state.melee.attack.deadline==101&&s.monster->Animation().LockedUntil()==101&&s.state.position.faceRequested,"launch state precedes Create");
    Expect(s.state.skill.deadline==71&&s.state.skillActive.deadline==72,"cooldown tail follows Create");
    ++s.creates;s.origin=origin;s.velocity=velocity;s.damage=damage;const auto cb=s.onCreate;if(cb)cb();return s.createResult;
}
int main() {
    try { auto engine=std::make_unique<sdk_test::Engine>();Early(*engine);Launch(*engine);Cancel(*engine); }
    catch(const std::exception& error){++taskFailures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"startup assertions="<<assertions<<" failures="<<taskFailures<<" SDK boundary checks="<<sdk_test::checks<<" SDK failures="<<sdk_test::failures<<'\n';
    return taskFailures||sdk_test::failures?1:0;
}
