// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace actions_test
{
namespace
{
using R=CSOLMonsterFollowResult;
void Link(CNavArea* a,CNavArea* b)
{a->ConnectTo(b,EAST);b->ConnectTo(a,WEST);}
void LongRoute(Fixture& f,CCSOLMonsterPathFollower& follower)
{
    auto* a=f.Flat(0,0);auto* b=f.Flat(1000,0);Link(a,b);f.Grid();f.ClearTraces();
    Require(follower.Build(*f.call,f.world,Vector(0,0,0),Vector(1000,0,0),false,{}),"real long NAV route built");
}
}
void FollowCases()
{
    {
        Fixture f;CCSOLMonsterPathFollower follower;f.ClearTraces();CSOLMonsterFollowInput input;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Empty,"unbuilt follower is empty");
        auto* a=f.Flat(0,0);f.Grid();
        Require(follower.Build(*f.call,f.world,Vector(0,0,0),Vector(20,0,0),false,{}),"contained trivial route built");
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving,"exact20 endpoint distance does not arrive");
        f.monster->pev->origin.x=.01f;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Arrived&&!follower.HasRoute(),"strict less20 endpoint arrives and invalidates");
        Check(a!=nullptr,"real SDK containing area retained through arrival");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;
        const auto step=follower.Update(*f.call,f.world,f.appearance,input);
        Check(step.result==R::Moving&&std::isfinite(step.goal.x)&&step.goal.x>0&&step.clearClimb,"ordinary follower returns finite forward goal and clears climb");
        Check(!f.traces.empty()&&f.traces.front().flags==1&&Equal(f.traces.front().from,Vector(0,0,64)),"visibility uses actual appearance eyes and glass-enabled trace");
        f.areas.front()->SetAttributes(NAV_CROUCH);input.lastArea=f.areas.front();input.crouching=true;input.jumping=true;
        const auto crouch=follower.Update(*f.call,f.world,f.appearance,input);
        Check(crouch.result==R::Moving&&!crouch.crouch,"ordinary CSO Crouch clears flag instead of hostage short hull");
    }
    for(int caseIndex=0;caseIndex<5;++caseIndex)
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);
        CSOLMonsterFollowInput input;input.crouching=true;input.standUntil=caseIndex==1?10.f:9.f;
        input.jumping=caseIndex==2;int probes{};std::vector<TraceCall> upward;
        f.traceResponse=[&](const TraceCall& call,TraceResult& result)
        {
            result={};result.flFraction=1;result.vecEndPos=call.to;
            if(call.flags==257)
            {
                ++probes;upward.push_back(call);
                if(caseIndex==3&&probes==7)result.flFraction=.5f;
                if(caseIndex==4&&probes==1)result.flFraction=std::numeric_limits<float>::quiet_NaN();
            }
        };
        const auto step=follower.Update(*f.call,f.world,f.appearance,input);
        if(caseIndex==0)
        {
            Check(step.result==R::Moving&&!step.crouch&&step.clearAnimationQueue&&probes==25,"all25 stand probes permit stand and request animation-queue clear");
            for(int i=0;i<25;++i)
                Check(Equal(upward[i].from,Vector(0,0,3))&&Equal(upward[i].to,Vector(-24+(i%5)*12,-24+(i/5)*12,72))&&upward[i].ignore==f.monster->edict(),"stand grid uses feet3 to square72 with self ignored");
        }
        if(caseIndex==1||caseIndex==2)
            Check(step.result==R::Moving&&step.crouch&&!step.clearAnimationQueue&&probes==0,"deadline equality/jumping keeps crouch without upward probes");
        if(caseIndex==3)
            Check(step.result==R::Moving&&step.crouch&&!step.clearAnimationQueue&&probes==7,"first overhead obstruction preserves crouch and stops probe grid");
        if(caseIndex==4)
            Check(step.result==R::Failed&&step.crouch&&!step.clearAnimationQueue&&probes==1,"nonfinite stand trace fails without queue mutation");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterRouteMotion motion;
        motion.crouching=true;motion.moveType=2;motion.moveState=1;
        Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Moving&&!motion.crouching&&motion.clearAnimationQueue,"Advance consumes stand pose and publishes queue-clear request");
        Check(motion.moveState==1&&f.monster->pev->sequence==1,"route does not substitute sequence reset for queue clear");
    }
    for(int blocked=0;blocked<4;++blocked)
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;
        input.running=true;
        auto baselineFollower=follower;
        const auto baseline=baselineFollower.Update(*f.call,f.world,f.appearance,input);
        Require(baseline.result==R::Moving,"same random-portal route supplies unavoided goal");
        f.traces.clear();input.avoidObstacles=true;int feelers{};std::array<TraceCall,2> rays;
        f.traceResponse=[&](const TraceCall& call,TraceResult& result)
        {
            result={};result.flFraction=1;result.vecEndPos=call.to;result.vecPlaneNormal=Vector(0,0,1);
            if(call.from.z==18.1f&&call.to.z==18.1f)
            {
                ++feelers;
                Require(feelers<=2,"only two feeler rays recorded");rays[feelers-1]=call;
                Check(call.flags==1&&call.ignore==nullptr,"feelers retain monster-ignore/glass-enabled policy");
                const Vector delta=call.to-call.from;
                Check(std::abs(delta.Length()-50)<.001f,"running feeler length is50");
                if((blocked&1)&&feelers==1)result.flFraction=.5f;
                if((blocked&2)&&feelers==2)result.flFraction=.5f;
            }
        };
        const auto step=follower.Update(*f.call,f.world,f.appearance,input);
        Check(step.result==R::Moving&&feelers==2,"both lateral feelers queried");
        const Vector actual=step.goal-baseline.goal;
        // The route's portal is intentionally random, not centered at y=0.
        // Opposite ray origins are50 apart; the required300-unit response is
        // six times that observed spacing, directed away from the blocked ray.
        const Vector expected=blocked==1?6.0f*(rays[1].from-rays[0].from):
            blocked==2?6.0f*(rays[0].from-rays[1].from):Vector(0,0,0);
        const bool shifted=std::abs(actual.x-expected.x)<.001f&&
            std::abs(actual.y-expected.y)<.001f&&actual.z==0;
        if(!shifted)std::cerr<<"FEELERS blocked="<<blocked<<" baseline="<<baseline.goal.x<<','<<baseline.goal.y
            <<" result="<<step.goal.x<<','<<step.goal.y<<" delta="<<actual.x<<','<<actual.y
            <<" expected="<<expected.x<<','<<expected.y<<'\n';
        if(blocked==1||blocked==2)Check(shifted&&std::abs(actual.Length()-300)<.001f,"blocked feeler shifts same-route baseline300 away from that ray");
        else Check(Equal(step.goal,baseline.goal),"equal feelers preserve full same-route goal");
    }
    {
        Fixture f;auto* area=f.Flat(0,0);f.Grid();f.ClearTraces();CCSOLMonsterPathFollower follower;
        Require(follower.Build(*f.call,f.world,Vector(0,0,0),Vector(10,0,0),false,{}),"arrival motion route");
        CSOLMonsterRouteMotion motion;motion.moveState=1;motion.climbSequence=true;
        Check(follower.Advance(*f.call,f.world,f.appearance,area,motion,.1f,0,false)==R::Arrived&&motion.moveState==2&&!motion.climbSequence,"arrival applies MoveDone after own terminal route reset");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;
        for(int i=0;i<6;++i)
        { f.engine.globals.time=10+static_cast<float>(i);Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving,"first six still samples do not fail before timer"); }
        f.engine.globals.time=17;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving,"stuck exact two seconds retained");
        f.engine.globals.time=std::nextafter(17.f,100.f);
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Stuck&&!follower.HasRoute(),"stuck fails strictly after two seconds from sixth sample");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;
        for(int i=0;i<6;++i){f.engine.globals.time=10+static_cast<float>(i);follower.Update(*f.call,f.world,f.appearance,input);}
        f.monster->pev->origin.x=std::nextafter(75.f,100.f);f.engine.globals.time=16;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving,"movement above75 clears stuck state");
        f.engine.globals.time=18;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving&&follower.HasRoute(),"cleared stuck timer does not later invalidate route");
    }
    for(bool down:{false,true})
    {
        Fixture f;auto* bottom=f.Flat(0,0);auto* top=f.Flat(0,0,100);CNavLadder ladder;
        ladder.m_bottomArea=bottom;ladder.m_topForwardArea=top;ladder.m_bottom=Vector(0,0,0);ladder.m_top=Vector(0,0,100);ladder.m_dir=EAST;ladder.m_isDangling=false;
        bottom->AddLadderUp(&ladder);top->AddLadderDown(&ladder);f.Grid();f.ClearTraces();CCSOLMonsterPathFollower follower;
        f.monster->pev->origin=down?Vector(0,0,100):Vector(0,0,0);
        Require(follower.Build(*f.call,f.world,f.monster->pev->origin,down?Vector(0,0,0):Vector(0,0,100),true,{}),"actual ladder route built");
        CSOLMonsterRouteMotion motion;motion.moveType=2;motion.maxMoveType=2;
        Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Ladder&&motion.traversingLadder,"first ladder advance starts traversal");
        if(down)
        {
            f.monster->pev->origin.z=60;
            Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Ladder&&f.monster->pev->velocity.z==-100,"descending below top36 sets -100 velocity");
            f.monster->pev->origin.z=35;
            Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Ladder,"descending below bottom36 completes ladder segment");
        }
        else
        {
            Check(motion.moveType==1&&motion.climbSequence&&f.monster->pev->velocity.z==150,"ascending near ladder caps walk and sets150 vertical speed");
            f.monster->pev->origin.z=101;
            Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Ladder&&!motion.traversingLadder,"above top clears traverse before departure");
            Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Ladder,"next ladder tick selects actual top exit and completes");
        }
    }
}
void FollowCancellationCases()
{
    for(int stage=0;stage<5;++stage)
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;bool fired{};
        f.traceResponse=[&](const TraceCall&,TraceResult& result)
        {
            Require(!fired,"no further trace after first cancellation");fired=true;
            if(stage==0)f.ClearGraph();
            if(stage==1)follower.Reset();
            if(stage==2)Require(f.requests.Reset(),"trace dispatcher reset");
            if(stage==3)f.ReborrowSameSlot();
            if(stage==4){f.ClearGraph();f.DestroyMapStorage();}
            result={};result.flFraction=.5f;result.pHit=reinterpret_cast<edict_t*>(1);
        };
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Cancelled&&fired&&f.traces.size()==1,"Trace invalidation returns before old pHit/area/entity access");
    }
    for(int stage=0;stage<3;++stage)
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterRouteMotion motion;
        motion.crouching=true;motion.climbSequence=true;motion.velocity=Vector2D(7,8);motion.moveState=1;
        int probes{};
        f.traceResponse=[&](const TraceCall& call,TraceResult& result)
        {
            result={};result.flFraction=1;result.vecEndPos=call.to;
            if(call.flags!=257)return;
            Require(++probes==1,"cancelled stand emits no further trace");
            if(stage==0)follower.Reset();
            if(stage==1)f.ReborrowSameSlot();
            if(stage==2)f.ClearGraph();
            result.flFraction=.5f;result.pHit=reinterpret_cast<edict_t*>(1);
        };
        Check(follower.Advance(*f.call,f.world,f.appearance,nullptr,motion,.1f,0,false)==R::Cancelled&&probes==1,"stand callback invalidates owner before movement commit");
        Check(motion.crouching&&motion.climbSequence&&!motion.clearAnimationQueue&&motion.velocity.x==7&&motion.velocity.y==8&&motion.moveState==1,"cancelled Advance does not write stale pose or queue to replacement owner");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);CSOLMonsterFollowInput input;
        input.crouching=true;int probes{};
        f.traceResponse=[&](const TraceCall& call,TraceResult& result)
        {
            result={};result.flFraction=1;result.vecEndPos=call.to;
            if(call.flags!=257)return;
            ++probes;
            if(probes==1)f.monster->pev->origin=Vector(15,20,5);
            else Check(Equal(call.from,Vector(15,20,8)),"stand refreshes entity feet after each trace callback");
        };
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Moving&&probes==25,"moving callback retains current route and all standing probes");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);f.ReborrowSameSlot();
        CSOLMonsterSpawnCall fresh(f.map,f.pool,f.requests,*f.monster);CSOLMonsterFollowInput input;
        Check(follower.Update(fresh,f.world,f.appearance,input).result==R::Cancelled&&!follower.HasRoute(),"new call cannot drive old route on same raw slot");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;LongRoute(f,follower);bool nested{};std::uint64_t newRevision{};
        f.traceResponse=[&](const TraceCall& call,TraceResult& result)
        {
            if(!nested)
            {nested=true;Require(follower.Build(*f.call,f.world,Vector(0,0,0),Vector(10,0,0),false,{}),"nested callback builds replacement route");newRevision=follower.Revision();}
            result={};result.flFraction=1;result.vecEndPos=call.to;result.vecPlaneNormal=Vector(0,0,1);
        };
        CSOLMonsterFollowInput input;
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Cancelled&&follower.HasRoute()&&follower.Revision()==newRevision,"old update cancels without erasing callback replacement route");
        Check(follower.Update(*f.call,f.world,f.appearance,input).result==R::Arrived,"replacement route remains usable");
    }
    {
        Fixture f;auto* bottom=f.Flat(0,0);auto* top=f.Flat(0,0,100);CNavLadder ladder;
        ladder.m_bottomArea=bottom;ladder.m_topForwardArea=top;ladder.m_bottom=Vector(0,0,0);ladder.m_top=Vector(0,0,100);ladder.m_dir=EAST;ladder.m_isDangling=false;
        bottom->AddLadderUp(&ladder);f.Grid();f.ClearTraces();CCSOLMonsterPathFollower follower;
        Require(follower.Build(*f.call,f.world,Vector(0,0,0),Vector(0,0,100),true,{}),"ladder owner route");CSOLMonsterFollowInput input;
        const auto step=follower.Update(*f.call,f.world,f.appearance,input);Require(step.result==R::Ladder,"capture ladder token");
        f.ReborrowSameSlot();CSOLMonsterSpawnCall fresh(f.map,f.pool,f.requests,*f.monster);
        Check(!follower.CompleteLadder(fresh,step,f.world),"same slot fresh lease cannot acknowledge old ladder token");
    }
}
}
