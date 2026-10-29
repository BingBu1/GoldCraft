// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace idle_test
{
namespace
{
struct Obstacle final : CBaseEntity
{
    int classification{16};
    std::vector<actions_test::Damage> damage;
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
    // Sample through the explicit provider, outside built-in skills 1/2/9.
    f.Spawn();f.SpawnState().skill=3;
    f.SpawnState().animationAt=1000;
    f.services->onSkill=[](CSOLOrdinaryMonsterState& state)
    {
        state.position.updateAfter=1000;
        state.findTarget.deadline=state.findPath.deadline=state.melee.attack.deadline=1000;
    };
    f.Tick(30);Require(f.services->observed,"real UseSkill observes private ordinary state through supported provider");
    f.SpawnState().skill=0;
    f.Monster().HostState().activeSkill=0;
    auto& state=*f.services->observed;state.position.updateAfter=1000;
    state.position.destination=Vector(1000,0,0);state.position.motion.jumpGoal=Vector(1000,0,0);
    state.lastObstacleOrigin=f.Monster().pev->origin;state.obstacleBlocked=false;
    state.melee.idleObstacleCheck.deadline=0;state.obstacleRecovery.deadline=0;
    f.traceCalls.clear();f.traces=0;return state;
}
void Tick(Fixture& f,float now=31)
{f.SpawnState().updateAt=0;f.SpawnState().animationAt=1000;f.Tick(now);}
void Hit(Fixture& f,Obstacle& target,float normal=0)
{
    f.traceResponse=[&target,normal](const nav_loader_test::TraceCall&,TraceResult& result)
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
        state.melee.idleObstacleCheck.deadline=31;Tick(f);
        Check(f.traceCalls.empty()&&state.melee.idleObstacleCheck.deadline==31,"obstacle check requires strict deadline greater");
        Tick(f,std::nextafter(31.f,100.f));
        Require(f.traceCalls.size()==1,"one unobstructed obstacle line through actual owner tree");
        const auto& t=f.traceCalls.front();const auto* appearance=csol::scenario::FindMonsterAppearance(f.Monster().Type()->appearance);
        Check(t.flags==1&&t.ignored==f.Monster().edict()&&t.from.z==f.Monster().pev->origin.z+appearance->feetZ+3,"obstacle uses line ignores monsters includes glass and feet plus3");
        const float length=f.Monster().pev->maxs.x+15;
        Check(Near(t.to.x-t.from.x,state.position.jumpDirection.x*length)&&t.to.z==t.from.z,"original maxs.x plus15 uses prepared horizontal direction");
        Check(state.obstacleBlocked&&Near(state.obstacleRecovery.interval,6)&&Near(state.melee.idleObstacleCheck.interval,.3f),"clear trace still records blocked history and first recovery6");
    }
    for(float displacement:{15.f,16.f})
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;
        state.lastObstacleOrigin=f.Monster().pev->origin-Vector(displacement,0,0);state.obstacleBlocked=true;
        state.obstacleRecovery.deadline=1000;Tick(f);
        Check((f.traceCalls.size()==1)==(displacement==15),"original squared movement threshold near248");
        if(displacement==16)Check(!state.obstacleBlocked&&state.lastObstacleOrigin==f.Monster().pev->origin,"sufficient movement clears blocked history");
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
        state.obstacleBlocked=true;state.obstacleRecovery.deadline=30.5f;state.position.jumpDirection=Vector2D(.25f,.5f);
        Tick(f);
        Check(f.traceCalls.empty()&&state.position.jumpDirection==Vector2D(.25f,.5f)&&state.position.motion.jumping,"expired six-second recovery jumps with saved direction without new trace");
        Check(state.obstacleRecovery.interval==999&&state.melee.idleObstacleCheck.interval==1,"timeout switches recovery999 and check1");
    }
}
void ObstacleCancellationCases()
{
    for(int stage=0;stage<4;++stage)
    {
        Fixture f;auto& state=Prepare(f);auto& target=Receiver(f);state.target=&target;Hit(f,target);
        bool reset{};auto cancel=[&]{if(reset)return;reset=true;Require(f.Reset(),"same-lease ordinary reset accepted during callback");};
        if(stage==0)f.onTrace=cancel;
        if(stage==1)target.onClassify=cancel;
        if(stage==2){f.HookAnimation();f.onAnimationHook=[&](Hook*,CBaseAnimating*){cancel();};}
        if(stage==3)f.onModel=cancel;
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
        f.traceResponse=[&](const nav_loader_test::TraceCall&,TraceResult& result)
        {result.pHit=reinterpret_cast<edict_t*>(1);f.ReleaseMap();};
        Tick(f);Check(f.pool.State()==CSOLMonsterPoolState::Empty,"map destruction during trace returns before poison edict or released owner access");
    }
}
}
