// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "support.hpp"
#include "../csol-scenario-point-manager/engine_fixture.h"
#include "../csol-scenario-monster-ordinary/allocation_probe.hpp"
#include "../../native/csol/server/scenario_navigation.h"

namespace nav_loader_test
{
struct TraceCall { Vector from,to; int flags{}; edict_t* ignored{}; };
struct Host : Engine
{
    inline static Host* current{};
    std::function<void(std::size_t,TraceResult&)> onTrace;
    std::function<void(std::size_t)> onFind;
    std::vector<TraceCall> traces;
    std::size_t finds{};
    Host();
    ~Host();
    CBaseEntity& Ladder(const Vector& low,const Vector& high,bool climber=false);
    static edict_t* Find(edict_t*,const char*,const char*);
    static void Trace(const float*,const float*,int,edict_t*,TraceResult*);
};
extern unsigned botDestroyNotifications, editNavResetCalls;
struct BotGlobals
{
    std::unique_ptr<CNavArea> area;
    std::unique_ptr<HidingSpot> spot;
    CNavLadder ladder;
    std::vector<CNavArea*> areaList;
    std::vector<HidingSpot*> spotList;
    std::vector<CNavLadder*> ladderList;
    unsigned notifications{},edits{},areaId{},spotId{};
    BotGlobals();
    ~BotGlobals();
    void CheckUnchanged();
};
std::shared_ptr<CCSOLScenarioNavigation> Load(Files&,const NavigationFile&,std::optional<unsigned> bsp={});
NavigationFile LadderFile();
CNavArea* FindArea(const CCSOLScenarioNavigation&,unsigned);
}
