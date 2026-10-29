// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace navigation_test
{
namespace
{
unsigned FirstResidueZero()
{ for(unsigned seed=0;seed<10000;++seed) { std::srand(seed); if(std::rand()%10==0) return seed; } throw std::runtime_error("missing CRT residue"); }
void AssertInvalid(const CSOLScenarioRoute& route)
{ Check(route.count==0 && route.generation==0 && route.search==CSOLNavSearchResult::Failed,"failed route fully invalidated"); }
}
void RouteCases()
{
    using R=CSOLNavSearchResult;
    {
        Graph graph; auto* a=graph.Area(Vector(0,0,0),Vector(100,100,100),20,80);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"trivial sloped world");
        Engine engine; CSOLScenarioRoute route;
        Check(CSOL_BuildScenarioRoute(route,world,Vector(25,25,30),Vector(75,75,80),false,{}),"same area builds trivial route");
        Check(route.count==2 && route.search==R::Reached && route.generation==world.Generation(),"trivial route count and generation");
        Check(route.segments[0].area==a && route.segments[1].area==a &&
            Equal(route.segments[0].pos,Vector(25,25,25)) && Equal(route.segments[1].pos,Vector(75,75,75)),"trivial endpoints use input XY and actual surface Z");
        Check(engine.calls.empty(),"trivial contained endpoints do not invoke engine");
        world.Reset(); Check(route.generation!=world.Generation(),"retained route generation detects later reset");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(100,0); auto* goal=graph.Flat(200,0,20);
        Connect(a,b,EAST); Connect(b,goal,EAST); CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"connected route world");
        CSOLScenarioRoute route; std::srand(FirstResidueZero());
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(200,0,21),false,{}),"connected route built");
        Check(route.count==4 && route.search==R::Reached && route.segments[0].area==a && route.segments[1].area==b && route.segments[2].area==goal,"connected corridor preserves actual parents");
        Check(Equal(route.segments[0].pos,Vector(0,0,0)) && route.segments[1].pos.x==85 && route.segments[1].pos.z==0,"first point center then entering edge plus5");
        Check(Equal(route.segments[3].pos,Vector(200,0,20)) && route.segments[3].area==goal,"corrected endpoint appended in reserved slot");
        world.Reset();
    }
    {
        Graph graph; auto* a=graph.Flat(0,0,100); auto* b=graph.Flat(100,0,0); Connect(a,b,EAST,false);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"one-way fall world"); CSOLScenarioRoute route;
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,100),Vector(100,0,0),false,{}),"one-way corridor builds fall segment");
        Check(route.count==4 && route.segments[1].pos.x==110 && route.segments[1].pos.z==100 &&
            route.segments[2].pos.x==135 && route.segments[2].pos.z==0,"one-way adds25 at source Z then25 at destination Z");
        world.Reset();
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(100,0); auto* unreachable=graph.Flat(300,0,30); Connect(a,b,EAST);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"partial route world"); CSOLScenarioRoute route;
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(300,0,31),false,{}),"disconnected target builds original partial corridor");
        Check(route.count==3 && route.search==R::Partial && route.segments[1].area==b && route.segments[2].area==b && route.segments[2].area!=unreachable,"partial uses closest area and no fake graph connection");
        Check(Equal(route.segments[2].pos,Vector(300,0,30)),"partial retains original corrected goal endpoint policy");
        Engine engine; engine.Solid();
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(500,0,77),false,{}),"failed target ground still permits reachable partial corridor");
        Check(route.count==3 && route.search==R::Partial && Equal(route.segments[2].pos,Vector(500,0,77)) && engine.calls.size()==10,"ground failure preserves caller goal Z");
        engine.Layers({15});
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(500,0,77),false,{}),"successful target ground permits partial corridor");
        Check(Equal(route.segments[route.count-1].pos,Vector(500,0,15)),"ground success corrects absent-area endpoint height");
        world.Reset();
    }
    {
        Graph graph; for(int i=0;i<300;++i) graph.Flat(static_cast<float>(i*60),0);
        for(std::size_t i=1;i<graph.areas.size();++i) Connect(graph.areas[i-1],graph.areas[i],EAST);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"300-area corridor world"); CSOLScenarioRoute route;
        Check(CSOL_BuildScenarioRoute(route,world,*graph.areas.front()->GetCenter(),*graph.areas.back()->GetCenter(),false,{}),"long route accepted");
        Check(route.count==256 && route.search==R::Reached,"255 corridor plus endpoint fits exact capacity");
        Check(route.segments[0].area==graph.areas[45] && route.segments[254].area==graph.areas[299] && route.segments[255].area==graph.areas[299],"truncation keeps goal end rather than start end");
        for(int i=0;i<255;++i) Check(route.segments[i].area==graph.areas[static_cast<std::size_t>(45+i)],"long corridor retains consecutive final areas");
        Check(Equal(route.segments[255].pos,*graph.areas[299]->GetCenter()),"long route endpoint is original goal");
        world.Reset();
    }
    {
        Graph graph; auto* bottom=graph.Flat(0,0); auto* top=graph.Flat(0,0,100); auto ladder=Ladder(bottom,top);
        bottom->AddLadderUp(&ladder); top->AddLadderDown(&ladder);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"ladder route world"); CSOLScenarioRoute route;
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(0,0,100),true,{}),"ascending route built");
        Check(route.count==3 && route.segments[1].how==GO_LADDER_UP && route.segments[1].ladder==&ladder && Equal(route.segments[1].pos,Vector(32,0,0)),"ascending route uses bottom plus32 toward ladder");
        Check(CSOL_BuildScenarioRoute(route,world,Vector(0,0,100),Vector(0,0,0),true,{}),"descending route built");
        Check(route.count==3 && route.segments[1].how==GO_LADDER_DOWN && route.segments[1].ladder==&ladder && Equal(route.segments[1].pos,Vector(-32,0,100)),"descending route uses top plus32 opposite ladder");
        world.Reset();
    }
    for(int stage=0;stage<3;++stage)
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(100,0); Connect(a,b,EAST);
        CCSOLScenarioNavWorld world; Require(world.Initialize(graph.areas),"route reset world"); Engine engine; engine.Layers({0});
        CSOLScenarioRoute route; route.count=7; route.generation=91; route.search=R::Reached;
        engine.onTrace=[&](std::size_t,TraceResult& result) { world.Reset(); graph.Clear(); result.pHit=reinterpret_cast<edict_t*>(1); };
        const Vector start=stage==0?Vector(500,0,0):Vector(0,0,0);
        const Vector goal=stage==1?Vector(500,0,0):Vector(100,0,0);
        if(stage==2)
        {
            // A disconnected search whose closest is its start enters lazy
            // trivial fallback. Its goal Nearest calls ground only after
            // original endpoint ground has finished ten responses.
            world.Reset(); graph.Clear(); a=graph.Flat(0,0); b=graph.Flat(300,0); Require(world.Initialize(graph.areas),"lazy fallback world reinitialized");
            engine.Layers({0}); engine.steps.resize(11); engine.steps[10].z=0;
            engine.onTrace=[&](std::size_t index,TraceResult& result) { if(index==10) { world.Reset(); graph.Clear(); result.pHit=reinterpret_cast<edict_t*>(1); } };
            Check(!CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(500,0,0),false,{}),"lazy trivial target Nearest reset cancels");
            Check(engine.calls.size()==11,"lazy fallback starts goal query after endpoint ground");
        }
        else
        {
            Check(!CSOL_BuildScenarioRoute(route,world,start,goal,false,{}),"start or target query reset cancels route");
            Check(engine.calls.size()==1,"route does not trace after generation change");
        }
        AssertInvalid(route);
    }
    {
        CCSOLScenarioNavWorld world; CSOLScenarioRoute route;
        Check(!CSOL_BuildScenarioRoute(route,world,Vector(0,0,0),Vector(1,1,1),false,{}),"empty world rejects route"); AssertInvalid(route);
        Check(!CSOL_BuildScenarioRoute(route,world,Vector(std::numeric_limits<float>::quiet_NaN(),0,0),Vector(1,1,1),false,{}),"nonfinite route rejects before queries"); AssertInvalid(route);
    }
}
}
