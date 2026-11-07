// SPDX-License-Identifier: GPL-3.0-or-later
// Actual Precache/Start bodies, actual MapResources/catalog/presentation/vote
// objects, controlled engine callbacks and read-only server filesystem.
#include "../../native/csol/scenario/core/map_resources.hpp"
#include "../../native/csol/scenario/core/map_catalog.hpp"
#include "../../native/csol/scenario/core/map_support.hpp"
#include "../../native/csol/scenario/core/map_vote.hpp"
#include "../../native/csol/scenario/core/map_vote_presentation.hpp"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
using namespace csol::scenario;
using namespace csol::scenario::map_vote;

unsigned checks{}, failures{}, allocations{}, releases{}, registrations{};
void Check(bool ok, const char* label)
{ ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
std::filesystem::path root;
std::string mapName, missingFile, blockedMap, onlyMap;
// The public lifecycle currently admits two maps. A larger verified local
// deployment can explicitly run the original three-map acceptance matrix.
bool threeMaps{};
std::vector<std::string> advertised, requested;
int registration{66};
std::function<void()> loadHook, precacheHook, validHook;
void CSOL_ResetScenarioMapVote();
unsigned char* Load(const char* path, int* length)
{
    requested.emplace_back(path);
    if (loadHook) { auto hook=std::move(loadHook); hook(); }
    *length=0;
    if (missingFile==path) return nullptr;
    std::ifstream stream(root/path,std::ios::binary);
    if (!stream) return nullptr;
    std::string data((std::istreambuf_iterator<char>(stream)),{});
    auto* memory=static_cast<unsigned char*>(std::malloc(data.size()+1));
    if (!memory) return nullptr;
    std::memcpy(memory,data.data(),data.size());memory[data.size()]=0;
    *length=int(data.size());++allocations;return memory;
}
void Release(void* memory) { ++releases;std::free(memory); }
int Precache(char* path)
{
    advertised.emplace_back(path);
    if (precacheHook) { auto hook=std::move(precacheHook);hook(); }
    return int(advertised.size());
}
int Register(const char* name,int size)
{
    Check(!std::strcmp(name,MessageName)&&size==-1,"actual message registration contract");
    ++registrations;return registration;
}
int MapValid(char* name)
{
    if (validHook) { auto hook=std::move(validHook);hook(); }
    return blockedMap!=name&&(onlyMap.empty()||onlyMap==name)&&
        std::filesystem::is_regular_file(root/"maps"/(std::string(name)+".bsp"));
}
struct Engine {
    unsigned char* (*pfnLoadFileForMe)(const char*,int*){Load};
    void (*pfnFreeFile)(void*){Release};
    int (*pfnPrecacheGeneric)(char*){Precache};
    int (*pfnRegUserMsg)(const char*,int){Register};
    int (*pfnIsMapValid)(char*){MapValid};
} g_engfuncs;
struct Globals { float time{10};const char* mapname{}; } globals,*gpGlobals=&globals;
#define PRECACHE_GENERIC(path) g_engfuncs.pfnPrecacheGeneric(path)
#define REG_USER_MSG(name,size) g_engfuncs.pfnRegUserMsg(name,size)
#define IS_MAP_VALID(name) g_engfuncs.pfnIsMapValid(name)
#define STRING(value) (value)
constexpr int FALSE=0;
class CCSOLScenarioGameplay {
public:
#include "actual_support.inc"
};
Vote vote;
std::uint32_t generation{};
std::uint64_t epoch{1};
int message{};
bool busy{},sending{},resourcesReady{};
struct Recipient {std::uint32_t token{},revision{};std::uint16_t seconds{};double sentAt{-1};};
std::array<Recipient,MaxPlayers> recipients{};
#include "actual_startup.inc"

void Reset(const char* name)
{
    CSOL_ResetScenarioMapVote();mapName=name;globals={10,mapName.c_str()};
    g_engfuncs={};missingFile.clear();blockedMap.clear();onlyMap.clear();advertised.clear();requested.clear();
    registration=66;registrations=0;loadHook={};precacheHook={};validHook={};
    Check(allocations==releases,"every prior production loader allocation released");
}
bool HasChoice(const Snapshot& value,unsigned id)
{
    return std::any_of(value.choices.begin(),value.choices.begin()+value.count,
        [=](const Count& c){return c.id==id;});
}
void RealFiles()
{
    for (const auto* current : {"zs_deadend","zs_2gate","zs_lostcity"}) {
        if(!threeMaps&&std::string_view(current)=="zs_2gate")continue;
        Reset(current);Check(CSOL_PrecacheScenarioMapVote(),"real presentation closure precaches");
        Check(resourcesReady&&message==66&&registrations==1&&!advertised.empty(),"ready only after complete closure and registration");
        const bool started=Start();const auto snapshot=vote.View(0,0,globals.time);
        std::cout<<current<<" start="<<started<<" candidates="<<unsigned(snapshot.count)<<'\n';
        Check(started,"completed implemented map can start a ballot");
        Check(snapshot.count==(threeMaps?3:2),"real installation retains exactly the supported validated destinations");
        for (const auto& pair : std::array{std::pair{74,"zs_lostcity"},std::pair{75,"zs_2gate"},std::pair{98,"zs_deadend"}})
            Check(HasChoice(snapshot,pair.first)==(pair.first!=75||threeMaps),"current and supported maps retained, unadmitted map excluded");
        Check(!HasChoice(snapshot,85)&&!HasChoice(snapshot,101),"unimplemented chaos/nightmare remain excluded");
        Check(!HasChoice(snapshot,76)&&!HasChoice(snapshot,77),"absent BSP/resource destinations remain excluded");
        if(started) {
            const unsigned currentId=std::string_view(current)=="zs_deadend"?98:
                std::string_view(current)=="zs_2gate"?75:74;
            Check(vote.Connect(1,101)&&vote.Cast(1,101,generation,std::uint16_t(currentId),globals.time)==Ballot::Accepted,
                "real startup allows a ballot for the current map");
            Check(!Start(),"active ballot cannot be replaced");
            Check(!vote.Finish(globals.time+19,0),"server deadline cannot finish early");
            const auto winner=vote.Finish(globals.time+20,0);
            Check(winner&&winner->id==currentId&&winner->mapName==current,
                "real-start current-map plurality wins at deadline");
        }
    }
}
void Failures()
{
    Reset("zs_deadend");missingFile=PresentationResource;
    Check(!CSOL_PrecacheScenarioMapVote()&&!Start()&&advertised.empty()&&registrations==0,"missing presentation blocks start without advertising a partial closure");
    Reset("zs_deadend");int n{};auto* bytes=Load(PresentationResource,&n);
    const auto presentation=ParsePresentation({reinterpret_cast<char*>(bytes),std::size_t(n)});Release(bytes);
    missingFile=presentation.fontResource;
    Check(!CSOL_PrecacheScenarioMapVote()&&!Start()&&advertised.empty()&&registrations==0,"missing referenced font blocks start before any advertising");
    Reset("zs_deadend");registration=63;
    Check(!CSOL_PrecacheScenarioMapVote()&&!resourcesReady&&!Start(),"invalid user-message registration cannot enable ballot");
    Reset("zs_deadend");loadHook=[](){CSOL_ResetScenarioMapVote();};
    Check(!CSOL_PrecacheScenarioMapVote()&&!resourcesReady&&registrations==0,"map reset during manifest load cancels preparation");
    Reset("zs_deadend");precacheHook=[](){CSOL_ResetScenarioMapVote();};
    Check(!CSOL_PrecacheScenarioMapVote()&&!resourcesReady&&registrations==0,"map reset during advertising cannot publish ready");
    Reset("zs_2gate");Check(CSOL_PrecacheScenarioMapVote(),"prepare reset-during-start case");
    validHook=[](){CSOL_ResetScenarioMapVote();};
    Check(!Start()&&vote.State()==Phase::Idle,"map reset in existence callback cancels slate generation");
    Reset("zs_2gate");Check(CSOL_PrecacheScenarioMapVote(),"prepare missing-catalog case");missingFile="resource/maplist.csv";
    Check(!Start()&&vote.State()==Phase::Idle,"missing real catalog blocks start");
    Reset("zs_deadend");Check(CSOL_PrecacheScenarioMapVote(),"prepare missing-bsp case");blockedMap="zs_lostcity";
    const bool started=Start();const auto snapshot=vote.View(0,0,globals.time);
    Check(started&&snapshot.count==(threeMaps?2:1)&&HasChoice(snapshot,75)==threeMaps&&HasChoice(snapshot,98)&&!HasChoice(snapshot,74),
        "engine BSP rejection removes only the missing destination and retains the current map");
    Reset("zs_2gate");Check(CSOL_PrecacheScenarioMapVote(),"prepare data-load failure case");
    missingFile="maps/zbs_wave_zs_deadend.cso";
    const bool loaded=Start();const auto data=vote.View(0,0,globals.time);
    Check(loaded&&data.count==(threeMaps?2:1)&&HasChoice(data,74)&&HasChoice(data,75)==threeMaps&&!HasChoice(data,98),
        "actual MapResources rejection removes missing-table destination and retains current map");
    Reset("zs_deadend");g_engfuncs.pfnPrecacheGeneric=nullptr;
    Check(!CSOL_PrecacheScenarioMapVote()&&!Start(),"missing engine precache service cannot enable vote");
    Reset("zs_deadend");Check(allocations==releases,"all actual file loads released after negative cases");
}
void SingleCurrentMap()
{
    Reset("zs_deadend");onlyMap="zs_deadend";
    Check(CSOL_PrecacheScenarioMapVote(),"prepare single current-map installation");
    const bool started=Start();Check(started,"a single validated current map is sufficient to start");
    if(!started)return;
    const auto prior=generation;const auto snapshot=vote.View(0,0,globals.time);
    Check(snapshot.count==1&&HasChoice(snapshot,98),"single-current slate contains exactly the real installed map");
    Check(vote.Connect(1,101)&&vote.Cast(1,101,prior,98,globals.time)==Ballot::Accepted,
        "single-current startup accepts its current-generation vote");
    const auto winner=vote.Finish(globals.time+20,UINT32_MAX);
    Check(winner&&winner->mapName=="zs_deadend","single current map settles normally");
    Reset("zs_deadend");onlyMap="zs_deadend";
    Check(CSOL_PrecacheScenarioMapVote()&&Start()&&generation>prior,
        "actual map reset and startup preserve monotonically increasing generation");
    Check(vote.Connect(1,101)&&vote.Cast(1,101,prior,98,globals.time)==Ballot::Stale,
        "previous-map generation cannot vote after reloading the same map");
    Reset("zs_deadend");onlyMap="zs_deadend";blockedMap="zs_deadend";
    Check(CSOL_PrecacheScenarioMapVote()&&!Start(),"current map still fails the ordinary BSP existence gate");
    Reset("zs_deadend");onlyMap="zs_deadend";
    Check(CSOL_PrecacheScenarioMapVote(),"prepare current-map missing-table case");
    missingFile="maps/zbs_wave_zs_deadend.cso";
    Check(!Start(),"current map still requires its actual MapResources closure");
}
int main(int argc,char** argv)
{
    if(argc<2||argc>3)return 2;root=argv[1];
    if(argc==3){if(std::string_view(argv[2])!="three-maps")return 2;threeMaps=true;}
    RealFiles();Failures();SingleCurrentMap();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
