// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <cstring>

// The fixture owns actual SDK NAV C++ storage; global Bot notifications are
// outside this component. All route/search methods come from matching SDK.
CNavArea::~CNavArea() {}
void CBaseAnimating::ResetSequenceInfo()
{ g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(&CBaseAnimating::ResetSequenceInfo_OrigFunc,this); }
int SENTENCEG_Lookup(const char*,char (&)[32]) {throw std::runtime_error("unexpected sentence lookup");}
void Sys_Error(const char*,...) {throw std::runtime_error("unexpected SDK fatal error");}

namespace motion_test
{
Fixture::Fixture()
{
    Require(!active,"one motion fixture");active=this;engine.globals.time=10;
    monster=&Monster();monster->pev->origin=Vector(5,5,0);monster->pev->maxspeed=200;
    monster->pev->velocity=Vector(0,0,7);monster->SpeedState().baseSpeed=200;
    call.emplace(map,pool,requests,*monster);
    appearance.feetZ=0;appearance.centroidZ=0;appearance.eyesZ=64;
    g_engfuncs.pfnTraceLine=Trace;g_engfuncs.pfnModelIndex=ModelIndex;
}
Fixture::~Fixture()
{
    onTrace={};onModelIndex={};world.Reset();CNavArea::ClearSearchLists();
    occupancy.Shutdown();areas.clear();storage.clear();active=nullptr;
}
CNavArea* Fixture::Flat(float x,float y,float half)
{
    const Vector lo(x-half,y-half,0),hi(x+half,y+half,0),ne(x+half,y-half,0),sw(x-half,y+half,0);
    void* bytes=::operator new(sizeof(CNavArea));std::memset(bytes,0,sizeof(CNavArea));
    auto* area=new(bytes) CNavArea(&lo,&ne,&hi,&sw);storage.emplace_back(area);areas.push_back(area);return area;
}
void Fixture::Space()
{Flat(200,200,200);Require(occupancy.Initialize(std::list<CNavArea*>{areas.begin(),areas.end()}),"motion occupancy initialized");Register(*monster);}
void Fixture::Register(CCSOLScenarioMonster& value)
{Require(occupancy.ApplyPriority(value)&&occupancy.Register(value),"actual attribute/register contract");}
CCSOLScenarioMonster& Fixture::Add(const Vector& origin,int priority)
{auto& next=Monster(priority,100+static_cast<int>(types.size()));next.pev->origin=origin;Register(next);return next;}
void Fixture::Route(CCSOLMonsterPathFollower& follower)
{
    auto* a=Flat(5,5);auto* b=Flat(1005,5);a->ConnectTo(b,EAST);b->ConnectTo(a,WEST);
    Require(world.Initialize(areas),"real motion NAV world");
    Require(follower.Build(*call,world,monster->pev->origin,Vector(1005,5,0),false,{}),"real motion route");
}
void Fixture::Trace(const float*,const float* to,int,edict_t*,TraceResult* result)
{++active->traces;const auto callback=active->onTrace;if(callback)callback();*result={};result->flFraction=1;result->vecEndPos=Vector(to);result->vecPlaneNormal=Vector(0,0,1);}
int Fixture::ModelIndex(const char* name)
{++active->modelIndexCalls;active->modelNames.emplace_back(name);const int answer=active->modelIndex;const auto callback=active->onModelIndex;if(callback)callback();return answer;}
}
