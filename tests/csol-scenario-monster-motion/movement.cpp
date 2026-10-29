// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace motion_test
{
void NeighborCases()
{
    {
        Fixture f;f.Space();
        f.monster->pev->origin=Vector(83.74f,83.74f,0);Require(f.occupancy.Register(*f.monster),"move center to grid2,2");
        std::vector<std::size_t> expected;
        for(int y=0;y<5;++y)for(int x=0;x<5;++x)
        {
            auto& entity=x==2&&y==2?*f.monster:f.Add(Vector(5+39.37f*x,5+39.37f*y,0));
            if(x>=1&&x<=3&&y>=1&&y<=3)expected.push_back(entity.Slot());
        }
        std::array<CCSOLScenarioOccupancy::Neighbor,CCSOLMonsterPool::Capacity> result{};std::size_t count{};
        Require(f.occupancy.MovementNeighbors(f.monster->pev->origin,result,count),"movement snapshot");
        std::vector<std::size_t> actual;for(std::size_t i=0;i<count;++i)actual.push_back(result[i].slot);
        Check(actual==expected&&count==9,"movement radius1 row-major 3x3 excludes radius2 outer ring");
        Require(f.occupancy.MovementNeighbors(Vector(5,5,0),result,count),"edge movement snapshot");
        Check(count==4,"radius1 clips to inclusive grid edge");
    }
    {
        Fixture f;f.Space();auto& a=f.Add(Vector(5,5,0));auto& b=f.Add(Vector(5,5,0));
        std::array<CCSOLScenarioOccupancy::Neighbor,CCSOLMonsterPool::Capacity> result{};std::size_t count{};
        Require(f.occupancy.MovementNeighbors(Vector(5,5,0),result,count),"same-cell snapshot");
        Check(count==3&&result[0].slot==b.Slot()&&result[1].slot==a.Slot()&&result[2].slot==f.monster->Slot(),"same-cell chain retains head insertion order");
        b.pev->solid=SOLID_NOT;b.pev->deadflag=DEAD_DEAD;
        Require(f.occupancy.RefreshOccupancy()&&f.occupancy.MovementNeighbors(Vector(5,5,0),result,count),"refresh dead/nonsolid retained node");
        Check(count==3&&result[0].slot==b.Slot()&&f.occupancy.ResolveNeighbor(result[0])==&b,"query leaves solid/dead filtering to movement consumer");
        const auto old=result[2];f.Reborrow(*f.monster);
        Check(f.occupancy.ResolveNeighbor(old)==nullptr,"old neighbor lease cannot resolve reused same entity");
        f.Register(*f.monster);
        Require(f.occupancy.MovementNeighbors(Vector(5,5,0),result,count),"new lease same cell snapshot");
        Check(result[2].slot==f.monster->Slot()&&f.occupancy.ResolveNeighbor(result[2])==f.monster,"new lease resolves without reordering stationary chain");
    }
}
void VelocityCases()
{
    for(int kind=0;kind<5;++kind)
    {
        Fixture f;CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(100,0);int flags=17;
        auto& speed=f.monster->SpeedState();
        if(kind==1)f.monster->pev->maxspeed=40;
        if(kind>=2){speed.impulse=Vector2D(300,400);speed.impulseUntil=11;speed.limits={{1,20,1,.2f}};}
        if(kind==3)f.types.front().skill=1000;
        if(kind==4)f.types.front().eventClass=150;
        Require(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,.1f,10,flags)==R::Applied,"normal/impulse motion applied");
        if(kind==0)Check(Equal(f.monster->pev->velocity,Vector(70,0,7))&&motion.velocity.x==70,"friction reduces accumulator and preserves vertical velocity");
        if(kind==1)Check(Equal(f.monster->pev->velocity,Vector(40,0,7)),"speed clamp follows friction");
        if(kind==2)Check(Equal(f.monster->pev->velocity,Vector(300,400,7))&&f.monster->pev->maxspeed==200,"ordinary impulse skips friction and ring speed update");
        if(kind>=3)Check(Equal(f.monster->pev->velocity,Vector(1,0,7)),"boss/special event skip forced ordinary XY branch");
        Check(flags==0,"successful velocity commit clears motion flags");
    }
    {
        Fixture f;CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(100,0);int flags=1;
        auto& speed=f.monster->SpeedState();speed.impulse=Vector2D(300,400);speed.impulseUntil=10;speed.impulseProtectionUntil=10;
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,.1f,10,flags)==R::Applied&&motion.velocity.x==70&&speed.impulseProtectionUntil==0,"equal impulse deadlines expire without suppressing normal movement");
        motion.velocity=Vector2D(100,0);speed.recovery=.995f;f.monster->pev->velocity.z=10;
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,0,10,flags)==R::Applied&&speed.recovery>1&&Near(f.monster->pev->velocity.x,100.5f)&&Near(f.monster->pev->velocity.z,10.05f),"recovery adds per-update double0.01, crosses1 and scales XYZ");
        Check(motion.velocity.x==100,"recovery output does not feed back into planar accumulator");
        motion.velocity=Vector2D(100,0);speed.recovery=.49f;speed.AddEffect(4,30,-2,10,0,Vector(0,0,0),10);f.monster->pev->velocity.z=10;
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,0,10,flags)==R::Applied&&Near(f.monster->pev->velocity.x,30)&&Near(f.monster->pev->velocity.z,3),"recovery scales before independent velocity list limits and vertical add");
    }
    for(int kind=0;kind<5;++kind)
    {
        Fixture f;f.Space();CSOLMonsterRouteMotion motion;int flags=2;
        auto& other=f.Add(Vector(45,5,0));
        if(kind==1)other.pev->solid=SOLID_NOT;
        if(kind==2)other.pev->deadflag=DEAD_DEAD;
        if(kind==3)other.pev->origin.z=1000;
        const float until=kind==4?10:9;
        Require(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,0,until,flags)==R::Applied,"neighbor avoidance motion");
        if(kind<=1)Check(std::bit_cast<unsigned>(f.monster->pev->velocity.x)==0xc21a78b1&&motion.velocity.y==0,"independent inverse-sqrt separation reference, including nonsolid neighbor");
        else Check(motion.velocity.x==0&&motion.velocity.y==0,"dead, outside3D range, or equal avoidance deadline adds no force");
    }
    {
        Fixture f;f.Space();CSOLMonsterRouteMotion motion;int flags=2;
        for(int i=0;i<4;++i)f.Add(Vector(6,5,0));
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,0,9,flags)==R::Applied&&f.monster->pev->velocity.x < -300,"crowd force remains above maxspeed because it follows clamp");
    }
    {
        Fixture f;CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(7,8);int flags=4;const Vector before=f.monster->pev->velocity;
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,-1,10,flags)==R::Failed&&Equal(before,f.monster->pev->velocity)&&motion.velocity.x==7&&flags==4,"invalid timestep fails without velocity commit");
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,0,9,flags)==R::Failed&&Equal(before,f.monster->pev->velocity)&&flags==4,"required unavailable occupancy refuses instead of silently dropping avoidance");
    }
    {
        Fixture f;CCSOLMonsterPathFollower follower;CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(100,0);int flags=5;
        const auto step=CSOL_AdvanceMonsterMotion(*f.call,follower,f.world,f.occupancy,f.appearance,nullptr,motion,.1f,0,false,10,flags);
        Check(step.follow==CSOLMonsterFollowResult::Empty&&step.motion==R::Applied&&f.monster->pev->velocity.x==70,"empty route still performs friction and final entity velocity update");
    }
}
void CancellationCases()
{
    for(int kind=0;kind<4;++kind)
    {
        Fixture f;CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(7,8);int flags=9;
        if(kind==0)f.requests.Reset();
        if(kind==1)f.monster->pev->deadflag=DEAD_DEAD;
        if(kind==2)f.pool.Free(*f.monster);
        if(kind==3)f.Reborrow(*f.monster);
        f.monster->pev->velocity=Vector(31,32,33);
        Check(CSOL_CommitMonsterMotion(*f.call,f.occupancy,f.appearance,motion,.1f,10,flags)==R::Cancelled&&Equal(f.monster->pev->velocity,Vector(31,32,33))&&motion.velocity.x==7&&flags==9,"stale session/dead/park/reused lease cannot commit velocity");
    }
    for(int kind=0;kind<5;++kind)
    {
        Fixture f;CCSOLMonsterPathFollower follower;f.Route(follower);CSOLMonsterRouteMotion motion;motion.velocity=Vector2D(7,8);int flags=9;bool fired{};
        f.onTrace=[&]
        {
            Require(!fired,"no further motion trace after cancellation");fired=true;
            if(kind==0)f.world.Reset();
            if(kind==1)follower.Reset();
            if(kind==2)f.requests.Reset();
            if(kind==3)f.Reborrow(*f.monster);
            if(kind==4){f.world.Reset();f.DestroyMapStorage();return;}
            f.monster->pev->velocity=Vector(31,32,33);
        };
        const auto result=CSOL_AdvanceMonsterMotion(*f.call,follower,f.world,f.occupancy,f.appearance,nullptr,motion,.1f,0,false,10,flags);
        Check(fired&&result.motion==R::Cancelled&&motion.velocity.x==7&&motion.velocity.y==8&&flags==9,"trace callback cancellation stops new final-velocity bridge");
        if(kind!=4)Check(Equal(f.monster->pev->velocity,Vector(31,32,33)),"callback replacement velocity survives cancelled old route");
    }
}
}
