// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace navigation_test
{
namespace
{
void Bounds(Graph& graph)
{ graph.Area(Vector(0,0,0),Vector(1,1,0),0,0); graph.Area(Vector(3300,3300,0),Vector(3301,3301,0),0,0); }
void ActualGeometry(const char* path)
{
    std::ifstream file(path,std::ios::binary); Require(bool(file),"frozen zs_deadend geometry opened");
    const auto word=[&]() { std::uint32_t value{}; file.read(reinterpret_cast<char*>(&value),4); if(!file) throw std::runtime_error("truncated geometry word"); return value; };
    Require(word()==0x4756414e && word()==1247,"actual 1247-area NAV geometry signature/count");
    struct Row { std::uint32_t id; std::array<float,8> v; };
    Graph graph; std::vector<Row> rows; CNavArea* reversed{};
    for(unsigned i=0;i<1247;++i)
    {
        Row row; row.id=word(); for(auto& value:row.v) value=std::bit_cast<float>(word());
        const auto& v=row.v; auto* area=graph.Area(Vector(v[0],v[1],v[2]),Vector(v[3],v[4],v[5]),v[6],v[7]);
        if(row.id==3148) reversed=area; rows.push_back(row);
    }
    Require(file.peek()==std::char_traits<char>::eof(),"geometry has no trailing or missing data");
    Require(reversed!=nullptr,"original area 3148 retained");
    Check(reversed->GetExtent()->lo.y==-204.2353057861328f && reversed->GetExtent()->hi.y==-225.0f,"3148 original reversed Y preserved");
    CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"new world accepts complete actual map including reversed area");
    Check(world.AreaAt(*reversed->GetCenter())!=reversed,"reversed area not normalized into containing space");
    // Independent exhaustive scan of raw records: no production grid, SDK
    // closest query, or SDK height method participates in the expected result.
    for(std::size_t sample=0;sample<rows.size();sample+=31)
    {
        const auto& q=rows[sample].v; const Vector point((q[0]+q[3])/2,(q[1]+q[4])/2,(q[2]+q[5])/2+1);
        CNavArea* expected{}; float best=-1e8f;
        for(std::size_t i=0;i<rows.size();++i)
        {
            const auto& v=rows[i].v;
            if(point.x<v[0] || point.x>v[3] || point.y<v[1] || point.y>v[4]) continue;
            float z=v[6];
            if(v[3]!=v[0] && v[4]!=v[1])
            {
                const float u=(point.x-v[0])/(v[3]-v[0]),w=(point.y-v[1])/(v[4]-v[1]);
                const float north=v[2]+u*(v[6]-v[2]), south=v[7]+u*(v[5]-v[7]); z=north+w*(south-north);
            }
            if(z<=point.z+5 && z>=point.z-120 && z>best) { best=z; expected=graph.areas[i]; }
        }
        Check(world.AreaAt(point)==expected,"actual grid query equals independent raw-area exhaustive scan");
    }
    world.Reset();
}
}
void WorldCases(const char* navFixture)
{
    {
        Graph graph; auto* low=graph.Flat(0,0,0); auto* high=graph.Flat(0,0,30); auto* tie=graph.Flat(0,0,30);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"overlap world initializes");
        Check(world.AreaAt(Vector(20,20,25))==high,"inclusive XY and five-unit upward allowance");
        Check(world.AreaAt(Vector(0,0,24.99f))==low,"height above five units rejected");
        Check(world.AreaAt(Vector(0,0,150))==high,"beneath 120 inclusive");
        Check(!world.AreaAt(Vector(0,0,150.01f)),"beneath over 120 rejected");
        Check(world.AreaAt(Vector(0,0,30))==high && high!=tie,"equal highest preserves load order");
        const auto generation=world.Generation(); Check(!world.Initialize({}),"empty init fails");
        Check(world.Generation()==generation+1 && !world.AreaAt(Vector(0,0,0)),"failed init invalidates old cells");
        std::array<CNavArea*,2> bad{low,nullptr}; Check(!world.Initialize(bad),"null area rejected");
    }
    {
        Graph graph; graph.Area(Vector(0,0,0),Vector(100,100,100),20,80);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"sloped area initialized");
        Check(world.AreaAt(Vector(50,50,50))==graph.areas[0],"bilinear sloped surface through real SDK GetZ");
        world.Reset();
        graph.Area(Vector(1e30f,1e30f,0),Vector(1e30f,1e30f,0),0,0);
        Check(!world.Initialize(graph.areas) && !world.AreaAt(Vector(50,50,50)),"huge allocation bound rejected without stale grid");
    }
    {
        Engine engine; CCSOLScenarioNavWorld world; float height=-999; Vector normal(9,8,7);
        engine.Layers({0,35,71,90});
        Check(world.GroundHeight(Vector(2,3,100),height,&normal) && height==35 && Equal(normal,Vector(0,0,1)),"35 climbs but exact36 separates ground layers");
        Check(engine.calls.size()==10,"ground makes offsets1 through91");
        for(std::size_t i=0;i<engine.calls.size();++i)
        {
            Check(Equal(engine.calls[i].start,Vector(2,3,101+10*static_cast<float>(i))) &&
                Equal(engine.calls[i].end,Vector(2,3,100-9999.900390625f)),"ground endpoints match original offsets/depth");
            Check(engine.calls[i].flags==1,"ground ignores monsters but includes glass");
        }
        engine.Layers({0,35,70,105}); Check(world.GroundHeight(Vector(0,0,0),height) && height==105,"successive sub36 gaps continue through all layers");
        engine.Layers({0,36}); Check(world.GroundHeight(Vector(0,0,0),height) && height==0,"exact36 stops at lower layer");
        engine.Layers({10,5,10,20}); Check(world.GroundHeight(Vector(0,0,0),height) && height==20,"only strictly increasing layers recorded");
        engine.Layers({42}); engine.steps[0].fraction=1; Check(world.GroundHeight(Vector(0,0,0),height) && height==42,"fraction1 endpoint is accepted by original ground policy");
        engine.Layers({900,800,700,12});
        engine.steps[0].hit=&engine.entities[0]; engine.steps[1].hit=&engine.entities[1]; engine.steps[2].hit=&engine.entities[2];
        Check(world.GroundHeight(Vector(0,0,0),height) && height==12,"doors and damageable glass skipped");
        Check(engine.calls[0].ignored==nullptr && engine.calls[1].ignored==&engine.entities[0] && engine.calls[2].ignored==&engine.entities[1],"skip entity progresses across traces");
        for(std::size_t i=3;i<10;++i) Check(engine.calls[i].ignored==&engine.entities[2],"last ignored entity persists through later layers");
        engine.Layers({24}); engine.entities[2].v.takedamage=DAMAGE_NO; engine.steps[0].hit=&engine.entities[2];
        Check(world.GroundHeight(Vector(0,0,0),height) && height==24 && engine.calls[1].ignored==nullptr,"nondamageable breakable remains ground");
        engine.Solid(); height=321; normal=Vector(9,8,7);
        Check(!world.GroundHeight(Vector(0,0,0),height,&normal) && height==321 && Equal(normal,Vector(9,8,7)),"all-startsolid failure preserves outputs");
        engine.Layers({13}); engine.steps.resize(1);
        Check(world.SimpleGroundHeight(Vector(1,2,3),height,&normal) && height==13 && engine.calls.size()==1 && Equal(engine.calls[0].start,Vector(1,2,3)),"simple ground uses one unraised trace");
        engine.Solid(1); height=321;
        Check(!world.SimpleGroundHeight(Vector(0,0,0),height) && height==321,"simple startsolid rejects without output change");
        engine.Layers({13}); engine.steps.resize(1); engine.steps[0].normal.x=std::numeric_limits<float>::quiet_NaN();
        Check(!world.SimpleGroundHeight(Vector(0,0,0),height) && height==321,"nonfinite plane rejected");
    }
    {
        Graph graph; Bounds(graph); auto* inner=graph.Flat(1520,1520,0,10);
        auto* firstRing=graph.Flat(1220,1220,900,10); auto* betterLater=graph.Flat(920,1520,-200,10);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"multi-ring world initializes");
        Check(world.Nearest(Vector(1520,1520,-200),true,true)==firstRing,"first completed ring wins even if later ring is closer");
        Check(firstRing!=inner && firstRing!=betterLater,"center-cell and global nearest are not substituted");
        world.Reset();
    }
    {
        Graph graph; Bounds(graph); auto* north=graph.Flat(1520,1220,-200,10); auto* west=graph.Flat(1220,1520,-200,10);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"ring order world initializes");
        Check(world.Nearest(Vector(1520,1520,-300),true,true)==west,"x outer y inner beats input order on equal distance");
        Engine engine; engine.Layers({0}); engine.steps.resize(2); engine.steps[0].fraction=0.5f;
        Check(world.Nearest(Vector(1520,1520,-100),false,true)==north,"occluded first ring candidate rejected, next accepted");
        Check(engine.calls.size()==2 && engine.calls[0].flags==257 && engine.calls[1].flags==257,"nearest visibility ignores glass with real trace wrapper");
        Check(engine.calls[0].end.z==-164 && engine.calls[1].end.z==-164,"nearest visibility destination is area plus36");
        engine.ResetTrace(); Check(!world.Nearest(Vector(1520,1520,-300),false,true) && engine.calls.empty(),"candidates above source rejected before trace");
        world.Reset();
    }
    {
        Graph graph; auto* area=graph.Flat(0,0); CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"nearest shortcut world");
        Engine engine;
        Check(world.Nearest(Vector(0,0,0),false,false)==area && engine.calls.empty(),"containing area shortcut avoids ground trace");
        world.Reset();
    }
    for(int operation=0;operation<3;++operation)
    {
        Graph graph; Bounds(graph); graph.Flat(1220,1520,-200,10); CCSOLScenarioNavWorld world;
        Require(world.Initialize(graph.areas),"cancellation world initializes"); Engine engine; engine.Layers({17});
        const auto oldGeneration=world.Generation();
        engine.onTrace=[&](std::size_t,TraceResult& result) { world.Reset(); graph.Clear(); result.pHit=reinterpret_cast<edict_t*>(1); };
        float height=321; Vector normal(9,8,7); bool cancelled{};
        if(operation==0) cancelled=!world.GroundHeight(Vector(0,0,0),height,&normal);
        else if(operation==1) cancelled=!world.SimpleGroundHeight(Vector(0,0,0),height,&normal);
        else cancelled=world.Nearest(Vector(1520,1520,-100),false,true)==nullptr;
        Check(cancelled && engine.calls.size()==1 && graph.areas.empty() && world.Generation()==oldGeneration+1,"Trace Reset cancels before stale hit/area read");
        Check(height==321 && Equal(normal,Vector(9,8,7)),"cancel leaves height and normal untouched");
    }
    ActualGeometry(navFixture);
}
}
