// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace spawn_test
{
namespace
{
// Explicit-instantiation access probe, used only to model the independently
// recovered death-tail type restoration until its real owner exists. This
// uses the compiler's member pointer, never an assumed byte offset/layout.
struct TypeMember
{
    using type=const csol::scenario::MonsterType* CCSOLScenarioMonster::*;
    friend type Member(TypeMember);
};
template<class Tag,typename Tag::type member> struct Access
{ friend typename Tag::type Member(Tag) { return member; } };
template struct Access<TypeMember,&CCSOLScenarioMonster::m_type>;
void Initialization()
{
    for(int kind=0;kind<7;++kind)
    {
        OccupancyFixture f(false); Geometry g;
        if(kind==1) g.areas.push_back(nullptr);
        if(kind==2) g.Add(Vector(NAN,0,0),Vector(1,1,0));
        // A single reversed area also reverses the aggregate bounds. This
        // remains invalid even though a reversed area among other valid
        // areas is accepted, as in the original zs_deadend NAV.
        if(kind==3) g.Add(Vector(2,0,0),Vector(1,1,0));
        if(kind==4) g.Add(Vector(0,0,0),Vector(1300000,0,0));
        if(kind==5) g.Add(Vector(0,0,0),Vector(170000,170000,0));
        if(kind==6) { g.Add(Vector(0,0,0),Vector(1,1,0)); f.pool.Shutdown(); }
        Check(!f.space.Initialize(g.areas) && !f.space.Ready() && !f.space.Error().empty(),"invalid geometry/pool rejected explicitly");
        Check(f.space.Shutdown() && f.space.Error().empty(),"shutdown clears failed geometry state");
    }
    for(float extent:{0.0f,39.369998931884766f,1290000.0f})
    {
        OccupancyFixture f(false); Geometry g; g.Add(Vector(0,0,0),Vector(extent,0,0));
        Require(f.space.Initialize(g.areas),"valid one-row inclusive grid initializes");
        auto& m=f.Monster(0,Vector(extent,0,0));
        Check(f.space.MakeRoom(m.pev->origin) && f.deaths.requests==std::vector<std::size_t>{m.Slot()},"maximum inclusive cell remains queryable");
        Check(!f.space.Initialize(g.areas) && f.space.Ready(),"duplicate initialize leaves grid intact");
    }
    for(int axis=0;axis<2;++axis)
    {
        OccupancyFixture f(false); Geometry g;
        g.Add(Vector(0,0,0),Vector(0,0,0));
        Vector low(0,0,0),high(0,0,0); low[axis]=100; high[axis]=-100;
        g.Add(low,high);
        Require(f.space.Initialize(g.areas),"local reversed XY accepted when aggregate bounds are valid");
        Check(Equal(g.owned.back()->GetExtent()->lo,low) && Equal(g.owned.back()->GetExtent()->hi,high),"occupancy initialization preserves source corner order");
        auto& monster=f.Monster(0,low); monster.pev->maxs=Vector(500,500,500);
        // Original corner roles give a one-cell grid, so both positions
        // clamp into it. Normalizing each area would make a six-cell axis
        // and incorrectly exclude this large monster from the 5x5 scan.
        Check(f.space.MakeRoom(high) && f.deaths.requests==std::vector<std::size_t>{monster.Slot()},"aggregate follows original corner roles without local normalization");
    }
}
void ChainAndNeighborhood()
{
    {
    OccupancyFixture f;
    auto& a=f.Monster(0,Vector(0,0,0),false);
    auto& b=f.Monster(0,Vector(0,0,0),false);
    auto& c=f.Monster(0,Vector(0,0,0),false);
    Require(f.space.RefreshOccupancy(),"initial ordered refresh");
    auto query=[&](std::vector<std::size_t> expected,const char* label)
    { f.deaths.Clear(); Check(f.space.MakeRoom(Vector(0,0,0)) && f.deaths.requests==expected,label); };
    query({c.Slot(),b.Slot(),a.Slot()},"initial head insert reverses ascending slots");
    Require(f.space.RefreshOccupancy(),"stationary refresh"); query({c.Slot(),b.Slot(),a.Slot()},"same-cell refresh preserves list order");
    b.pev->solid=SOLID_NOT; Require(f.space.RefreshOccupancy(),"non-solid refresh"); query({c.Slot(),a.Slot()},"non-solid skips without unlink");
    b.pev->solid=SOLID_SLIDEBOX; query({c.Slot(),b.Slot(),a.Slot()},"restored solid retains original position");
    a.pev->origin.x=200; Require(f.space.RefreshOccupancy(),"move out");
    a.pev->origin.x=0; Require(f.space.RefreshOccupancy(),"move back"); query({a.Slot(),c.Slot(),b.Slot()},"cross-cell reentry becomes head");
    Require(f.space.ResetGrid() && f.space.RefreshOccupancy(),"reset and refresh"); query({c.Slot(),b.Slot(),a.Slot()},"reset rebuilds only once in ascending slot order");
    for(std::size_t i=3;i<f.pool.Capacity;++i) f.Monster(i<39?0:1,Vector(400,400,0));
    const auto oldLease=f.pool.Lease(b); Require(f.pool.Free(b),"free middle same-cell node");
    auto* reused=f.Acquire(0); Require(reused==&b,"full pool reuses the only idle node"); reused->pev->origin=Vector(0,0,0);
    Require(f.space.ApplyPriority(*reused) && f.space.Register(*reused),"new borrow attributes before same-cell registration");
    Check(f.pool.Lease(*reused)!=oldLease,"same-cell rebirth gets new lease"); query({c.Slot(),b.Slot(),a.Slot()},"same-cell reborrow preserves old middle chain position");
    }
    for(int edge=0;edge<2;++edge)
    {
        OccupancyFixture n(false); Geometry g; g.Add(Vector(0,0,0),Vector(400,400,0)); Require(n.space.Initialize(g.areas),"5x5 geometry");
        std::vector<std::size_t> expected;
        for(int y=0;y<6;++y) for(int x=0;x<6;++x)
        {
            auto& m=n.Monster(0,Vector(5+39.37f*x,5+39.37f*y,0)); m.pev->maxs=Vector(1000,1000,1000);
            if(edge ? x<=2 && y<=2 : x<=4 && y<=4) expected.push_back(m.Slot());
        }
        const Vector center=edge?Vector(5,5,0):Vector(5+2*39.37f,5+2*39.37f,0);
        Check(n.space.MakeRoom(center) && n.deaths.requests==expected,"5x5 row/column order and edge clipping");
    }
}
void BoundsAndProtection()
{
    for(int axis=0;axis<3;++axis) for(int side=-1;side<=1;++side) for(int sign:{-1,1})
    {
        OccupancyFixture f; auto& m=f.Monster(0); float radius=axis==2?69.0f:29.0f;
        const float distance=side<0?std::nextafter(radius,0.0f):side>0?std::nextafter(radius,INFINITY):radius;
        m.pev->origin[axis]=sign*distance; m.pev->mins=Vector(-9999,-9999,-9999); m.pev->absmin=Vector(-9999,-9999,-9999);
        Require(f.space.RefreshOccupancy(),"refresh strict boundary sample");
        Check(f.space.MakeRoom(Vector(0,0,0)) && f.deaths.requests.size()==static_cast<std::size_t>(side<0),"strict three-axis maxs boundary ignores mins/absmin");
    }
    for(int protectedPriority:{1,2})
    {
        OccupancyFixture f; auto& protectedMonster=f.Monster(protectedPriority); f.Monster(0); f.Monster(0);
        Check(!f.space.MakeRoom(Vector(0,0,0)) && f.space.Ready() && f.deaths.before.empty() && f.deaths.requests.empty(),"last protected candidate refuses before all callbacks");
        Check(f.pool.Borrowed(protectedMonster),"protected monster stays borrowed");
    }
    for(auto pair:std::array<std::array<int,2>,5>{{{999,149},{1000,149},{999,150},{-1,-1},{1001,151}}})
    {
        OccupancyFixture f; f.types[0].skill=static_cast<std::int16_t>(pair[0]); f.types[0].eventClass=static_cast<std::int16_t>(pair[1]); f.Monster(0);
        Check(f.space.MakeRoom(Vector(0,0,0)) && f.deaths.requests.size()==static_cast<std::size_t>(pair[0]<1000 && pair[1]<=149),"skill and event class signed boundaries");
    }
    {
        OccupancyFixture f; f.Monster(0); f.types[0].priority=1;
        Require(f.space.RefreshOccupancy(),"refresh retains copied priority");
        Check(f.space.MakeRoom(Vector(0,0,0)) && f.deaths.requests.size()==1,"priority is attribute-stage snapshot, not refreshed from type");
    }
    for(int operation=0;operation<3;++operation)
    {
        OccupancyFixture f; auto* m=f.Acquire(0); Require(m!=nullptr,"borrow uninitialized attributes");
        const bool result=operation==0?f.space.Register(*m):operation==1?f.space.RefreshOccupancy():
            (f.space.ApplyPriority(*m) && f.space.Register(*m) && f.pool.Free(*m) && f.space.MakeRoom(Vector(0,0,0)));
        Check(operation==2?result:!result,"missing attributes refuse; ordinary Free retains valid grid");
    }
}
void LeaseLifecycle()
{
    for(int callback=0;callback<3;++callback)
    {
        OccupancyFixture f; f.Fill();
        for(std::size_t i=0;i<f.pool.Capacity;++i)
        {
            auto* m=f.pool.At(i); m->pev->origin=Vector(400,400,0); m->pev->maxs=Vector(16,16,36);
            Require(f.space.ApplyPriority(*m),"full pool attribute installation"); Require(f.space.Register(*m),"full pool registration");
        }
        auto* target=f.pool.At(0); target->pev->origin=Vector(0,0,0);
        if(callback==2) f.pool.At(1)->pev->origin=Vector(0,0,0);
        Require(f.space.RefreshOccupancy(),"full pool target positions");
        const auto oldLease=f.pool.Lease(*target); const auto* type=target->Type(); const auto serial=target->edict()->serialnumber;
        auto reuse=[&](CCSOLScenarioMonster& current)
        {
            if(callback==2 && current.Slot()!=1) return;
            Require(f.pool.Free(*target),"callback frees target"); auto* replacement=f.Acquire(0);
            Check(replacement==target && replacement->Type()==type && replacement->edict()->serialnumber==serial && f.pool.Lease(*replacement)!=oldLease,"same pointer slot serial type; only lease distinguishes reborrow");
        };
        if(callback==1) f.deaths.onRequest=reuse; else f.deaths.onBefore=reuse;
        Check(!f.space.MakeRoom(Vector(0,0,0)) && !f.space.Ready() && !f.space.Error().empty(),"callback reborrow causes explicit failure");
        Check(f.pool.Borrowed(*target) && target->pev->solid!=SOLID_NOT,"replacement monster survives stale death candidate");
        Check(std::find(f.deaths.requestLeases.begin(),f.deaths.requestLeases.end(),f.pool.Lease(*target))==f.deaths.requestLeases.end(),"no death request sent for new lease");
    }
    for(int mode=0;mode<4;++mode)
    {
        OccupancyFixture f; auto& m=f.Monster(0); const auto lease=f.pool.Lease(m);
        csol::scenario::MonsterType restored=f.types[0]; restored.id=77;
        if(mode==0) f.deaths.onBefore=[&](auto& monster){Require(f.pool.Free(monster),"early Free");};
        if(mode==1) f.deaths.onRequest=[&](auto& monster){Require(f.pool.BeginDeath(monster,10,f.points),"real delayed BeginDeath");};
        if(mode==2) f.deaths.onRequest=[&](auto& monster){monster.*Member(TypeMember{})=&restored;};
        Check(f.space.MakeRoom(Vector(0,0,0)) && f.space.Ready() && f.deaths.requests.size()==1 && f.pool.Lease(m)==lease,"early Free/delayed death/type restore/declined death retain lease and return true");
    }
    {
        OccupancyFixture f; auto& m=f.Monster(0); const auto lease=f.pool.Lease(m);
        Require(f.pool.Shutdown() && f.pool.ForgetMap() && f.pool.Initialize(),"pool lifecycle rebuild");
        auto* next=f.Acquire(0); Require(next!=nullptr,"acquire after rebuild");
        Check(f.pool.Lease(*next)>lease,"lease monotonic across shutdown and initialize");
    }
}
void FailuresAndReady()
{
    for(int stage=0;stage<2;++stage) for(int fault=0;fault<3;++fault)
    {
        OccupancyFixture f; auto& monster=f.Monster(0);
        auto action=[&](CCSOLScenarioMonster& m)
        { if(fault==0) f.pool.Shutdown(); else if(fault==1) f.engine.DestroyPrivate(m.edict()); else ++m.edict()->serialnumber; };
        if(stage==0) f.deaths.onBefore=action; else f.deaths.onRequest=action;
        Check(!f.space.MakeRoom(Vector(0,0,0)) && !f.space.Ready(),"both callbacks reject pool or entity invalidation");
        (void)monster;
    }
    {
        OccupancyFixture f; auto& m=f.Monster(0); BehaviorStats stats;
        stats.onDestroy=[&]{Check(!f.space.Ready(),"Busy pool immediately unavailable");};
        Require(m.InstallBehavior(std::make_unique<BehaviorProbe>(stats)),"install first lifecycle observer");
        Require(m.InstallBehavior(std::make_unique<BehaviorProbe>(stats)),"replace behavior invokes Busy callback");
        Check(f.space.Ready(),"Ready recovers after non-destructive Busy scope");
        stats.onDestroy={};
        Require(f.pool.Shutdown(),"external pool shutdown"); Check(!f.space.Ready(),"Ready reflects external shutdown without refresh");
    }
    {
        OccupancyFixture f; f.Monster(0);
        f.deaths.onBefore=[&](auto& m)
        { Check(!f.space.ResetGrid() && !f.space.Shutdown() && !f.space.Register(m) && !f.space.RefreshOccupancy() && !f.space.MakeRoom(Vector()),"nested mutation rejected while querying"); };
        Check(f.space.MakeRoom(Vector()) && f.space.Ready() && f.deaths.requests.size()==1,"nested refusal does not damage outer query");
    }
}
void ExplicitServiceFailures()
{
    for(bool requestFailure:{false,true})
    {
        OccupancyFixture f; auto& monster=f.Monster(0);
        const auto lease=f.pool.Lease(monster);
        f.deaths.prepareResult=requestFailure;
        f.deaths.requestResult=!requestFailure;
        Check(!f.space.MakeRoom(Vector(0,0,0))&&!f.space.Ready()&&
            f.space.Error()==(requestFailure?"scenario spawn-clear death request failed":"scenario spawn-clear preparation failed"),
            "actual MakeRoom records explicit pre-clear or death-service failure instead of accepting the spawn");
        Check(f.deaths.before==std::vector<std::size_t>{monster.Slot()}&&
            f.deaths.requests.size()==static_cast<std::size_t>(requestFailure)&&
            f.pool.Lease(monster)==lease&&f.pool.Borrowed(monster)&&monster.pev->solid!=SOLID_NOT,
            "failed boundary stops subsequent calls without synthesizing a death or replacing the victim lease");
    }
}
}
void OccupancyInitializationCases() { Initialization(); }
void OccupancyCases() { Initialization(); ChainAndNeighborhood(); BoundsAndProtection(); LeaseLifecycle(); FailuresAndReady(); ExplicitServiceFailures(); }
}
