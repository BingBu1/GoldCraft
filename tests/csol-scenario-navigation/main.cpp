// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
bool g_bAllowedCSBot{};
// Fixture boundary: members are really destroyed, but the ordinary CS Bot
// manager/global NAV grid notification chain is deliberately not installed.
// No routing, height, connection, list, or trace algorithm is substituted.
CNavArea::~CNavArea() {}

namespace navigation_test
{
int checks{}, failures{};
Engine* Engine::current{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
void Require(bool value, const char* label)
{ Check(value, label); if (!value) throw std::runtime_error(label); }
bool Equal(const Vector& a, const Vector& b)
{ return a.x == b.x && a.y == b.y && a.z == b.z; }
CNavArea* Graph::Area(const Vector& lo, const Vector& hi, float ne, float sw)
{
    // The upstream runtime ctor leaves some search fields untouched. Seed
    // raw storage before starting object lifetime; never modify private SDK
    // fields or replace the real constructor used below.
    void* storage = ::operator new(sizeof(CNavArea));
    std::memset(storage, 0, sizeof(CNavArea));
    const Vector northeast(hi.x, lo.y, ne), southwest(lo.x, hi.y, sw);
    CNavArea* area{};
    try { area = new (storage) CNavArea(&lo, &northeast, &hi, &southwest); }
    catch (...) { ::operator delete(storage); throw; }
    owned.emplace_back(area); areas.push_back(area); return area;
}
CNavArea* Graph::Flat(float x, float y, float z, float half)
{ return Area(Vector(x-half,y-half,z), Vector(x+half,y+half,z), z, z); }
void Graph::Clear()
{ CNavArea::ClearSearchLists(); areas.clear(); owned.clear(); }
void Connect(CNavArea* a, CNavArea* b, NavDirType direction, bool reverse)
{ a->ConnectTo(b, direction); if (reverse) b->ConnectTo(a, OppositeDirection(direction)); }
CNavLadder Ladder(CNavArea* bottom, CNavArea* top, bool dangling)
{
    CNavLadder ladder;
    ladder.m_bottomArea=bottom; ladder.m_topForwardArea=top;
    ladder.m_bottom=*bottom->GetCenter(); ladder.m_top=*top->GetCenter();
    ladder.m_dir=EAST; ladder.m_dirVector=Vector2D(1,0);
    ladder.m_length=100; ladder.m_isDangling=dangling; return ladder;
}
Engine::Engine()
{
    Require(current == nullptr, "single controlled engine");
    previousEngine=g_engfuncs; previousGlobals=gpGlobals;
    const char data[]="\0func_door\0func_door_rotating\0func_breakable\0worldspawn\0";
    std::memcpy(strings.data(),data,sizeof(data));
    globals.pStringBase=strings.data(); gpGlobals=&globals;
    entities[0].v.classname=1; entities[1].v.classname=11;
    entities[2].v.classname=30; entities[2].v.takedamage=DAMAGE_YES;
    entities[3].v.classname=45;
    current=this; g_engfuncs.pfnTraceLine=&Trace;
}
Engine::~Engine()
{ current=nullptr; gpGlobals=previousGlobals; g_engfuncs=previousEngine; }
void Engine::Trace(const float* start, const float* end, int flags, edict_t* ignored, TraceResult* result)
{
    auto& engine=*current; const auto index=engine.calls.size();
    engine.calls.push_back({Vector(start[0],start[1],start[2]),Vector(end[0],end[1],end[2]),flags,ignored});
    Require(index < engine.steps.size(), "trace has an explicit response");
    const auto step=engine.steps[index];
    *result={}; result->fStartSolid=step.solid; result->flFraction=step.fraction;
    result->vecEndPos=Vector(end[0],end[1],step.z); result->vecPlaneNormal=step.normal; result->pHit=step.hit;
    if (engine.onTrace) engine.onTrace(index,*result);
}
void Engine::ResetTrace() { steps.clear(); calls.clear(); onTrace={}; }
void Engine::Layers(std::initializer_list<float> heights)
{ ResetTrace(); for(float z:heights) steps.push_back({z}); while(steps.size()<10) steps.push_back(steps.back()); }
void Engine::Solid(unsigned count)
{ ResetTrace(); steps.resize(count); for(auto& step:steps) step.solid=true; }
}
int main(int argc, char** argv)
{
    using namespace navigation_test;
    try
    {
        if(argc==2 && std::string(argv[1])=="--search") SearchCases();
        else if(argc==3 && std::string(argv[1])=="--world") WorldCases(argv[2]);
        else if(argc==2 && std::string(argv[1])=="--route") RouteCases();
        else throw std::runtime_error("expected --search, --world frozen-geometry, or --route");
    }
    catch(const std::exception& error) { ++failures; std::cerr<<"EXCEPTION: "<<error.what()<<'\n'; }
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n'; return failures?1:0;
}
