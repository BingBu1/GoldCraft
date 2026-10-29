// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace obstacle_test
{
namespace
{
struct Obstacle final : CBaseEntity
{
    int classification{16};
    struct Damage { entvars_t* inflictor; entvars_t* attacker; float amount; int flags; };
    std::vector<Damage> damage;
    std::function<void()> onClassify,onDamage;
    int Classify() override {const int result=classification;const auto action=onClassify;if(action)action();return result;}
    BOOL TakeDamage(entvars_t* inflictor,entvars_t* attacker,float amount,int flags) override
    {damage.push_back({inflictor,attacker,amount,flags});const auto action=onDamage;if(action)action();return TRUE;}
};
Obstacle& Receiver(Fixture& f,int slot=600)
{
    auto* ed=f.engine.Raw("zbsbreak_test_receiver",slot);auto* target=new(&ed->v) Obstacle;
    target->pev=&ed->v;target->m_pEntity=nullptr;target->pev->health=100;
    target->pev->origin=Vector(1000,0,0);
    f.engine.privateOwners[ed]=[](void* p){static_cast<Obstacle*>(p)->~Obstacle();std::free(p);};
    return *target;
}
CSOLOrdinaryMonsterState& Prepare(Fixture& f)
{
    f.Spawn(); auto& state=f.CaptureState();
    // Capture runs at100, then these tests advance monotonically from101.
    state.position.updateAfter=10000;
    state.position.destination=Vector(1000,0,0);state.position.motion.jumpGoal=Vector(1000,0,0);
    state.lastObstacleOrigin=f.Monster().pev->origin;state.obstacleBlocked=false;
    state.melee.idleObstacleCheck.deadline=0;state.obstacleRecovery.deadline=0;
    f.traceCalls.clear();f.traces=0;return state;
}
void Tick(Fixture& f,float now=101)
{f.SpawnState().updateAt=0;f.SpawnState().animationAt=10000;f.Tick(now);}
void Hit(Fixture& f,Obstacle& target,float normal=0)
{
    f.traceResponse=[&target,normal](const Fixture::TraceCall&,TraceResult& result)
    {result.flFraction=.5f;result.vecPlaneNormal.z=normal;result.pHit=target.edict();};
}
}
void ObstacleCases()
{
    {
        Fixture f;auto& state=Prepare(f);Tick(f);
        Check(f.traceCalls.empty()&&state.melee.idleObstacleCheck.deadline==0,"no target skips obstacle clock and trace");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.melee.idleObstacleCheck.deadline=101;Tick(f);
        Check(f.traceCalls.empty()&&state.melee.idleObstacleCheck.deadline==101,"obstacle check requires strict deadline greater");
        Tick(f,std::nextafter(101.f,200.f));
        Require(f.traceCalls.size()==1,"one unobstructed obstacle line through actual owner tree");
        const auto& t=f.traceCalls.front();const auto* appearance=csol::scenario::FindMonsterAppearance(f.Monster().Type()->appearance);
        Check(t.flags==1&&t.ignored==f.Monster().edict()&&t.from.z==f.Monster().pev->origin.z+appearance->feetZ+3,"obstacle uses line ignores monsters includes glass and feet plus3");
        const float length=f.Monster().pev->maxs.x+15;
        Check(Near(t.to.x-t.from.x,state.position.jumpDirection.x*length)&&t.to.z==t.from.z,"original maxs.x plus15 uses prepared horizontal direction");
        Check(state.obstacleBlocked&&Near(state.obstacleRecovery.interval,6)&&Near(state.melee.idleObstacleCheck.interval,.3f),"clear trace still records blocked history and first recovery6");
    }
    for(float displacement:{std::bit_cast<float>(std::uint32_t{0x417bf7ce}),
                            std::bit_cast<float>(std::uint32_t{0x417bf7cf})})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.lastObstacleOrigin=f.Monster().pev->origin-Vector(displacement,0,0);state.obstacleBlocked=true;
        state.obstacleRecovery.deadline=1000;Tick(f);
        const bool below=std::bit_cast<std::uint32_t>(displacement)==0x417bf7ce;
        Check((f.traceCalls.size()==1)==below,"adjacent float positions straddle original double247.99949218750004 movement threshold");
        if(!below)Check(!state.obstacleBlocked&&state.lastObstacleOrigin==f.Monster().pev->origin,"sufficient movement clears blocked history");
    }
    for(int classification:{0,16,23})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;target.classification=classification;Hit(f,target);
        f.services->suppressed=true;f.Monster().HostState().enhancedUntil=1000;
        f.SpawnState().attributes.attackDamage=77;
        Tick(f);
        if(classification==16)
        {
            Require(target.damage.size()==1,"only Classify16 receives direct obstacle damage");
            Check(target.damage[0].amount==77&&target.damage[0].flags==DMG_BULLET&&target.damage[0].attacker==f.Monster().pev,"obstacle uses current spawn damage without suppression or enhanced+10");
            Check(f.Monster().pev->velocity==Vector(0,0,0)&&!state.position.motion.jumping,"breakable branch stops and attacks rather than jumping");
        }
        else Check(target.damage.empty()&&state.position.motion.jumping&&state.melee.idleObstacleCheck.interval==1,"stock Classify0 or23 obstacle requests jump with one-second retry");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        const_cast<csol::scenario::MonsterType*>(f.Monster().Type())->attackDamage=0;f.SpawnState().attributes.attackDamage=88;
        Tick(f);Require(target.damage.size()==1,"zero-type-damage breakable hit");Check(target.damage[0].amount==99999,"type zero uses original force-break damage");
    }
    for(bool climbing:{false,true})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;target.classification=0;Hit(f,target);
        state.position.motion.climbSequence=climbing;state.position.jumpUntil=1000;Tick(f);
        Check(target.damage.empty()&&!state.position.motion.jumping,"climb or active cooldown does not start Jump");
        Check(Near(state.melee.idleObstacleCheck.interval,climbing?.3f:1.f),"ignored Jump cooldown still delays1 but climb retains.3");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.obstacleBlocked=true;state.obstacleRecovery.deadline=100.5f;state.position.jumpDirection=Vector2D(.25f,.5f);
        Tick(f);
        Check(f.traceCalls.empty()&&state.position.jumpDirection==Vector2D(.25f,.5f)&&state.position.motion.jumping,"expired six-second recovery jumps with saved direction without new trace");
        Check(state.obstacleRecovery.interval==999&&state.melee.idleObstacleCheck.interval==1,"timeout switches recovery999 and check1");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.lastObstacleOrigin.z-=1000;
        f.SpawnState().attributes.attackRange=9999;
        Tick(f);
        Require(f.traceCalls.size()==1,"pure vertical travel does not skip original planar obstacle test");
        const auto line=f.traceCalls.front();
        const float oldDeadline=state.obstacleRecovery.deadline;
        f.traceCalls.clear();f.SpawnState().attributes.attackRange=1;
        state.melee.idleObstacleCheck.deadline=0;Tick(f,102);
        Require(f.traceCalls.size()==1,"second ordinary obstacle test uses same line contract");
        Check(f.traceCalls[0].from==line.from&&f.traceCalls[0].to==line.to,
            "ordinary obstacle range does not add attackRange unlike separate propensity3 branch");
        Check(state.obstacleRecovery.deadline==oldDeadline,"already-blocked path does not restart six-second recovery every tick");
        Check(state.lastObstacleOrigin==f.Monster().pev->origin,"trace stores full XYZ history after planar comparison");
    }
    for(float normal:{.5f,std::nextafter(.5f,0.f)})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        target.classification=0;Hit(f,target,normal);Tick(f);
        Check(state.position.motion.jumping==(normal<.5f),"slope normal threshold is strict below.5");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.obstacleBlocked=true;state.obstacleRecovery={6,101,95};
        Tick(f);Check(f.traceCalls.size()==1&&!state.position.motion.jumping,"recovery equality still prepares direction and traces");
        state.melee.idleObstacleCheck.deadline=0;f.traceCalls.clear();
        Tick(f,std::nextafter(101.f,200.f));
        Check(f.traceCalls.empty()&&state.obstacleRecovery.interval==999,"strictly later recovery jumps without a trace");
        const auto recovery=state.obstacleRecovery.deadline;
        state.melee.idleObstacleCheck.deadline=0;state.findPath.deadline=state.findTarget.deadline=10000;
        state.position.motion.jumping=false;f.traceCalls.clear();Tick(f,recovery);
        Check(f.traceCalls.size()==1&&state.obstacleRecovery.deadline==recovery,
            "999-second recovery equality does not repeatedly re-enter timeout jump");
    }
    for(bool originalZero:{false,true})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        auto* type=const_cast<csol::scenario::MonsterType*>(f.Monster().Type());
        type->attackDamage=originalZero?0:12;
        f.Monster().pev->velocity=Vector(3,4,5);
        f.HookAnimation();f.onAnimationHook=[&](Hook* chain,CBaseAnimating* monster)
        {
            f.engine.globals.time=105;
            f.SpawnState().attributes.attackInterval=1.25f;
            f.SpawnState().attributes.attackDamage=93;
            f.Monster().HostState().enhancedUntil=100;
            type->attackDamage=originalZero?12:0;
            chain->callNext(monster);
        };
        Tick(f);Require(target.damage.size()==1,"hooked real obstacle attack delivers one damage call");
        Check(target.damage[0].amount==(originalZero?99999.f:93.f),
            "force-break decision retains pre-trace original type while amount rereads post-hook attributes");
        Check(state.melee.attack.deadline==106.25f&&state.melee.avoidance.deadline==106.25f&&
              state.melee.idleObstacleCheck.deadline==106.25f,
            "three attack clocks reread current time and interval after animation hook");
        Check(f.Monster().pev->velocity==Vector(0,0,0),"attack stopped actual host velocity before animation hook");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        auto* raw=f.engine.Raw("world_without_private",601);
        auto* worldEd=&f.engine.world;worldEd->v.pContainingEntity=worldEd;worldEd->serialnumber=1;
        auto* world=new(&worldEd->v) Obstacle;world->pev=&worldEd->v;world->m_pEntity=nullptr;
        f.engine.privateOwners[worldEd]=[](void* p){static_cast<Obstacle*>(p)->~Obstacle();std::free(p);};
        f.traceResponse=[&](const Fixture::TraceCall&,TraceResult& result)
        {result.flFraction=.5f;result.vecPlaneNormal.z=0;result.pHit=raw;};
        Tick(f);
        Check(world->damage.size()==1,"hit without private data resolves actual INDEXENT0 fallback before Classify16 damage");
    }
    for(bool fraction : {false,true})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        f.traceResponse=[&](const Fixture::TraceCall&,TraceResult& result)
        {
            if(fraction)result.flFraction=std::numeric_limits<float>::quiet_NaN();
            else result.vecPlaneNormal.z=std::numeric_limits<float>::infinity();
        };
        Tick(f);
        Check(!f.Behavior().Error().message.empty()&&target.damage.empty()&&!state.position.motion.jumping,
            "nonfinite actual Trace result stops owner without attack or jump");
    }
}
void ObstacleCancellationCases()
{
    for(int stage=0;stage<5;++stage)
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        bool reset{};auto cancel=[&]{if(reset)return;reset=true;Require(f.Reset(),"same-lease ordinary reset accepted during callback");};
        if(stage==0)f.onTrace=cancel;
        if(stage==1)target.onClassify=cancel;
        if(stage==2){f.HookAnimation();f.onAnimationHook=[&](Hook*,CBaseAnimating*){cancel();};}
        if(stage==3)f.onModel=cancel;
        if(stage==4){int* queries=&f.modelQueries;const int first=*queries;f.onModel=[&,first,queries]{if(*queries==first+2)cancel();};}
        Tick(f);
        Check(reset&&target.damage.empty()&&f.Behavior().Error().message.empty(),"trace/classify/animation/model reset cancels old attack without corrupting new state");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        auto* ed=target.edict();target.onClassify=[&]{f.engine.Dispose(ed);};Tick(f);
        Check(!ed->pvPrivateData&&f.Behavior().Error().message.empty(),"Classify self-destruction rechecks EHANDLE before damage");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        target.onDamage=[&]{Require(f.Reset(),"reset from actual virtual damage");};Tick(f);
        Check(target.damage.size()==1&&f.Behavior().Error().message.empty(),"damage callback reset cancels later old-state writes");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        f.traceResponse=[&](const Fixture::TraceCall&,TraceResult& result)
        {result.pHit=reinterpret_cast<edict_t*>(1);f.ReleaseMap();};
        Tick(f);Check(f.pool.State()==CSOLMonsterPoolState::Empty,"map destruction during trace returns before poison edict or released owner access");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        auto* ed=target.edict();f.HookAnimation();
        f.onAnimationHook=[&](Hook* chain,CBaseAnimating* monster){f.engine.Dispose(ed);chain->callNext(monster);};
        Tick(f);Check(!ed->pvPrivateData&&f.Behavior().Error().message.empty(),
            "animation callback deleting obstacle prevents stale target damage access");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        auto* raw=f.engine.Raw("world_without_private",601);
        f.traceResponse=[&](const Fixture::TraceCall&,TraceResult& result)
        {result.flFraction=.5f;result.vecPlaneNormal.z=0;result.pHit=raw;};
        f.engine.onQuery=[&](char kind,int index){if(kind=='l'&&index==0){f.engine.onQuery={};f.ReleaseMap();}};
        Tick(f);Check(f.pool.State()==CSOLMonsterPoolState::Empty,
            "INDEXENT0 callback map release cancels before consuming old world edict");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        target.onDamage=[&]
        {
            CSOLMonsterDeathArguments death;death.suppressSound=true;
            Require(f.deathOwner->Requests().Request(f.Monster(),death)==CSOLDeathRequestResult::Executed,
                "real damage callback requests synchronous owner death");
        };
        Tick(f);Check(f.Monster().pev->deadflag==DEAD_DEAD&&target.damage.size()==1,
            "obstacle damage callback enters real death owner and cancels living tail");
    }
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        bool replaced{};
        f.onTrace=[&]
        {
            if(replaced)return;replaced=true;
            auto replacement=std::make_unique<CCSOLOrdinaryMonsterBehavior>(f.services);
            auto* owner=replacement.get();
            Require(f.Monster().InstallBehavior(std::move(replacement)),"trace callback replaces same-lease owner");
            auto call=f.Call();csol::scenario::ConfigError error;
            Require(owner->ResetForSpawn(call,f.SpawnState(),error),"replacement receives actual existing factory spawn state");
            f.Monster().pev->velocity=Vector(777,778,779);
        };
        Tick(f);
        Check(replaced&&target.damage.empty()&&f.Monster().pev->velocity==Vector(777,778,779),
            "same-lease behavior replacement stops old obstacle branch before target damage or velocity writes");
    }
}
}
