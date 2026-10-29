// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace navigation_test
{
void SearchCases()
{
    using R=CSOLNavSearchResult;
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(60,0); auto* goal=graph.Flat(120,0);
        Connect(a,goal,EAST); Connect(a,b,NORTH); Connect(b,goal,EAST);
        CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,goal,*goal->GetCenter(),false,{},closest)==R::Reached,"connected goal reached");
        Check(closest==goal && goal->GetParent()==b && b->GetParent()==a,"squared edge cost prefers two short edges");
        Check(goal->GetCostSoFar()==7200 && a->GetParent()==nullptr,"exact squared path cost and root parent");
        Check(goal->GetParentHow()==GO_EAST,"real SDK stores traversal direction");
        Check(CSOL_FindScenarioRoute(a,a,*a->GetCenter(),false,{},closest)==R::Reached && closest==a,"same area search");
        Check(CSOL_FindScenarioRoute(nullptr,goal,*goal->GetCenter(),false,{},closest)==R::Failed && !closest,"null start fails");
        const float nan=std::numeric_limits<float>::quiet_NaN();
        Check(CSOL_FindScenarioRoute(a,goal,Vector(nan,0,0),false,{},closest)==R::Failed && !closest,"invalid destination fails");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* north=graph.Flat(60,-10); auto* east=graph.Flat(60,10); auto* goal=graph.Flat(120,0);
        Connect(a,east,EAST); Connect(a,north,NORTH); Connect(north,goal,EAST); Connect(east,goal,EAST);
        CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,goal,*goal->GetCenter(),false,{},closest)==R::Reached,"equal routes reached");
        Check(goal->GetParent()==north && goal->GetCostSoFar()==7400,"N before E and equal cost preserves first parent");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* expensive=graph.Flat(100,0); auto* via=graph.Flat(40,0); auto* goal=graph.Flat(140,0);
        Connect(a,expensive,NORTH); Connect(a,via,EAST); Connect(via,expensive,EAST); Connect(expensive,goal,EAST);
        CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,goal,*goal->GetCenter(),false,{},closest)==R::Reached,"open-list reduction reaches goal");
        Check(expensive->GetParent()==via && goal->GetCostSoFar()==6800,"real SDK updates open entry to cheaper predecessor");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(50,0); auto* close=graph.Flat(100,0); auto* goal=graph.Flat(300,0);
        Connect(a,b,EAST); Connect(b,close,EAST); CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,goal,*goal->GetCenter(),false,{},closest)==R::Partial && closest==close,"disconnected goal returns reachable closest");
        Check(close->GetParent()==b && b->GetParent()==a && !goal->GetParent(),"partial route contains no fabricated edge");
        Check(CSOL_FindScenarioRoute(a,nullptr,Vector(300,0,0),false,{},closest)==R::Partial && closest==close,"absent goal retains closest search");
    }
    {
        Graph graph; auto* bottom=graph.Flat(0,0); auto* forward=graph.Flat(0,0,100); auto* left=graph.Flat(-40,0,100);
        auto* right=graph.Flat(40,0,100); auto* behind=graph.Flat(0,-40,100); auto* second=graph.Flat(0,0,200);
        auto dangling=Ladder(bottom,second,true), first=Ladder(bottom,forward), later=Ladder(bottom,second);
        first.m_topLeftArea=left; first.m_topRightArea=right; first.m_topBehindArea=behind;
        bottom->AddLadderUp(&dangling); bottom->AddLadderUp(&first); bottom->AddLadderUp(&later);
        std::array<const CNavLadder*,1> restricted{&first}; CNavArea* closest{};
        for(auto* exit:{forward,left,right})
        {
            Check(CSOL_FindScenarioRoute(bottom,exit,*exit->GetCenter(),true,restricted,closest)==R::Reached && closest==exit,"first non-dangling ladder F/L/R reachable");
            Check(exit->GetParent()==bottom && exit->GetParentHow()==GO_LADDER_UP,"up traversal stored by actual SDK");
        }
        Check(CSOL_FindScenarioRoute(bottom,forward,*forward->GetCenter(),false,restricted,closest)==R::Partial && closest==bottom,"climber-only first consumes cursor for nonclimber");
        Check(CSOL_FindScenarioRoute(bottom,second,*second->GetCenter(),true,{},closest)==R::Partial && closest==forward,"later up ladder not consumed");
        Check(CSOL_FindScenarioRoute(bottom,behind,*behind->GetCenter(),true,{},closest)==R::Partial && closest!=behind,"behind is not an ascending exit");
        forward->AddLadderDown(&dangling);
        Check(CSOL_FindScenarioRoute(forward,bottom,*bottom->GetCenter(),false,{},closest)==R::Reached,"down ignores dangling marker");
        std::array<const CNavLadder*,1> downRestricted{&dangling};
        Check(CSOL_FindScenarioRoute(forward,bottom,*bottom->GetCenter(),false,downRestricted,closest)==R::Partial,"down honors climber-only restriction");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* b=graph.Flat(100,0);
        a->AddLadderUp(nullptr); CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,b,*b->GetCenter(),false,{},closest)==R::Failed && !closest,"null ladder fails without stale closest");
        Check(CNavArea::IsOpenListEmpty(),"invalid ladder clears search list");
    }
    {
        Graph graph; auto* a=graph.Flat(0,0); auto* invalid=graph.Flat(std::numeric_limits<float>::infinity(),0); auto* goal=graph.Flat(100,0);
        Connect(a,invalid,EAST,false); CNavArea* closest{};
        Check(CSOL_FindScenarioRoute(a,goal,*goal->GetCenter(),false,{},closest)==R::Failed && !closest && CNavArea::IsOpenListEmpty(),"invalid adjacent center cancels search");
    }
}
}
