// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

// Forbidden business endpoints are explicit observation seams. This suite
// links the real SDK CNavArea destructor; no empty destructor substitute.
unsigned nav_loader_test::botDestroyNotifications{};
unsigned nav_loader_test::editNavResetCalls{};
void CCSBotManager::OnDestroyNavDataNotify(NavNotifyDestroyType,void*)
{ ++nav_loader_test::botDestroyNotifications; std::abort(); }
void EditNavAreasReset() { ++nav_loader_test::editNavResetCalls; }
// Actual global definitions reside in nav_area.cpp and bot_manager.cpp.
// The bounded SDK extraction supplies area counters/open-list globals once.
NavAreaList TheNavAreaList;
HidingSpotList TheHidingSpotList;
NavLadderList TheNavLadderList;
CNavAreaGrid TheNavAreaGrid;
CBotManager* TheBots{};

namespace nav_loader_test
{
Host::Host()
{
    Require(!current,"one NAV controlled engine");current=this;
    globals.time=10;globals.maxEntities=65536;
    g_engfuncs.pfnFindEntityByString=Find;g_engfuncs.pfnTraceLine=Trace;
}
Host::~Host(){onTrace={};onFind={};current=nullptr;}
CBaseEntity& Host::Ladder(const Vector& low,const Vector& high,bool climber)
{
    auto& e=Construct("func_ladder",static_cast<int>(edicts.size()+1));
    e.pev->absmin=low;e.pev->absmax=high;
    if(climber)e.pev->targetname=AllocString("climber");
    return e;
}
edict_t* Host::Find(edict_t* after,const char* key,const char* value)
{
    auto& h=*current;const auto n=h.finds++;const auto callback=h.onFind;
    if(callback)callback(n);
    Require(key&&value&&std::string_view(key)=="classname"&&std::string_view(value)=="func_ladder","real ladder classname enumeration");
    bool ready=!after;
    for(const auto& ed:h.edicts)
    {
        if(!ready){if(ed.get()==after)ready=true;continue;}
        if(!ed->free&&std::string_view(STRING(ed->v.classname))==value)return ed.get();
    }
    return nullptr;
}
void Host::Trace(const float* from,const float* to,int flags,edict_t* ignore,TraceResult* result)
{
    auto& h=*current;const auto n=h.traces.size();
    const Vector start(from[0],from[1],from[2]),end(to[0],to[1],to[2]);
    h.traces.push_back({start,end,flags,ignore});
    *result={};result->flFraction=1;result->vecEndPos=end;result->vecPlaneNormal=Vector(0,0,1);
    // Explicit environment response for vertical ground probes only. Facing,
    // clearance and top-side traces remain unobstructed unless a case changes it.
    if(end.z<start.z-1000){result->vecEndPos.z=0;result->flFraction=.5f;}
    const auto callback=h.onTrace;if(callback)callback(n,*result);
}
BotGlobals::BotGlobals()
{
    Require(TheNavAreaList.empty()&&TheHidingSpotList.empty()&&TheNavLadderList.empty(),"baseline Bot NAV globals empty");
    // Foreign sentinels use the tagged constructor so fixture cleanup itself
    // never invokes the absent ordinary Bot manager. They still populate the
    // real global lists, hash grid and SDK open list observed below.
    area=std::make_unique<CNavArea>(CSOLNavigationStorageTag{});areaId=area->GetID();
    spot=std::make_unique<HidingSpot>(CSOLNavigationStorageTag{});spotId=spot->GetID();
    TheNavAreaList.push_back(area.get());TheHidingSpotList.push_back(spot.get());TheNavLadderList.push_back(&ladder);
    TheNavAreaGrid.Initialize(-20,20,-20,20);TheNavAreaGrid.AddNavArea(area.get());
    area->Mark();spot->Mark();area->SetTotalCost(3);area->AddToOpenList();
    areaList.assign(TheNavAreaList.begin(),TheNavAreaList.end());
    spotList.assign(TheHidingSpotList.begin(),TheHidingSpotList.end());
    ladderList.assign(TheNavLadderList.begin(),TheNavLadderList.end());
    notifications=botDestroyNotifications;edits=editNavResetCalls;
}
BotGlobals::~BotGlobals()
{
    CNavArea::ClearSearchLists();TheNavLadderList.clear();
    // Explicitly unregister fixture sentinels before their tagged destruction.
    auto* pointer=area.get();TheNavAreaGrid.RemoveNavArea(pointer);TheNavAreaList.remove(pointer);area.reset();
    TheHidingSpotList.remove(spot.get());spot.reset();TheNavAreaGrid.Reset();
}
void BotGlobals::CheckUnchanged()
{
    Check(std::vector<CNavArea*>(TheNavAreaList.begin(),TheNavAreaList.end())==areaList,"CS Bot area global list unchanged");
    Check(std::vector<HidingSpot*>(TheHidingSpotList.begin(),TheHidingSpotList.end())==spotList,"CS Bot hiding global list unchanged");
    Check(std::vector<CNavLadder*>(TheNavLadderList.begin(),TheNavLadderList.end())==ladderList,"CS Bot ladder global list unchanged");
    const Vector origin(0,0,0);
    Check(TheNavAreaGrid.GetNavAreaCount()==1&&TheNavAreaGrid.GetNavArea(&origin)==area.get(),"CS Bot grid count and spatial query unchanged");
    Check(area->IsMarked()&&spot->IsMarked()&&area->IsOpen(),"independent CS Bot markers and open-list membership unchanged");
    Check(botDestroyNotifications==notifications&&editNavResetCalls==edits,"CSOL storage emits no ordinary Bot destruction or editor reset");
}
std::shared_ptr<CCSOLScenarioNavigation> Load(Files& files,const NavigationFile& file,std::optional<unsigned> bsp)
{
    files.contents["maps/nav_loader_test.nav"]=Encode(file);csol::scenario::ResourceError error;
    auto result=CCSOLScenarioNavigation::Load("nav_loader_test",files.Api(),error,bsp);
    Require(bool(result),error.detail.message.empty()?"real NAV owner load":error.detail.message.c_str());
    return result;
}
NavigationFile LadderFile()
{
    NavigationFile file;file.bspSize=321;
    file.areas={Area(1,0,0,0,120),Area(2,0,0,100,120)};return file;
}
CNavArea* FindArea(const CCSOLScenarioNavigation& owner,unsigned id)
{
    for(auto* a:owner.Areas())if(a->GetID()==id)return a;
    throw std::runtime_error("missing expected area id");
}
}
