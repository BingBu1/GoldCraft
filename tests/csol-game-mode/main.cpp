// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "interface.h"
#include "amxxmodule.h"
#include "goldcraft/csol_game_mode_api.hpp"
#include "goldcraft/csol_weapon_api.hpp"
#include "goldcraft/server_api.hpp"
#include "../../native/csol/server/game_mode.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <initializer_list>
#include <string>
#include <vector>

bool g_bAllowedCSBot{};
enginefuncs_t g_engfuncs{};
PFN_LOG g_fn_Log{};
PFN_REGISTER_FORWARD g_fn_RegisterForward{};
PFN_EXECUTE_FORWARD g_fn_ExecuteForward{};

namespace test
{
int checks{}, failures{}, registrations{};
cvar_t* request{};
cvar_t* difficultyRequest{};
cvar_t* shortDelayRequest{};
cvar_t* finishActionRequest{};
cvar_t* nextMapRequest{};
std::string requestedText;
std::string shortDelayText, difficultyText;
std::string finishActionText, nextMapText;
std::vector<std::string> calls;
bool modulePresent{}, exportPresent{}, modesPresent{}, oldServerPresent{}, oldGameplayPresent{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
void Register(cvar_t* value)
{
    ++registrations;
    if (std::strcmp(value->name, "csol_game_mode") == 0)
    { Check(!request, "mode cvar registered once"); request = value; }
    else if (std::strcmp(value->name, "csol_scenario_difficulty") == 0)
    { Check(!difficultyRequest, "difficulty cvar registered once"); difficultyRequest = value; }
    else if (std::strcmp(value->name, "csol_scenario_short_final_delay") == 0)
    { Check(!shortDelayRequest, "final-delay cvar registered once"); shortDelayRequest = value; }
    else if (std::strcmp(value->name, "csol_scenario_finish_action") == 0)
    { Check(!finishActionRequest, "finish action registered once"); finishActionRequest = value; }
    else if (std::strcmp(value->name, "csol_scenario_nextmap") == 0)
    { Check(!nextMapRequest, "next scenario map registered once"); nextMapRequest = value; }
    else Check(false, "unexpected cvar registration");
    const char* expected = value == nextMapRequest ? "zs_deadend" :
        value == shortDelayRequest || value == finishActionRequest ? "0" : "1";
    Check(std::strcmp(value->string, expected) == 0,
        "mode/difficulty default to one, short-delay/finish-action to zero and next-map to zs_deadend");
    Check((value->flags & FCVAR_SERVER) != 0, "server cvar flag");
}
void Request(const char* text)
{
    requestedText = text ? text : "";
    request->string = text ? requestedText.data() : nullptr;
}
struct Server final : goldcraft::IServerControl
{
    goldcraft::FormListener listener{};
    int GetForm(int) override { return 0; }
    int IsPaired(int) override { return 1; }
    int SetForm(int, int) override { return 1; }
    void SetFormListener(goldcraft::FormListener value) override
    { listener = value; calls.emplace_back(value ? "server.listener" : "server.clear"); }
} oldServer;
struct Gameplay final : goldcraft::IServerGameplay
{
    goldcraft::FormGuard guard{};
    goldcraft::PlayerAttackListener listener{};
    int GetPlayerFlags(int) override { return 1; }
    int GetPlayerAttack(int, int) override { return 0; }
    void SetFormGuard(goldcraft::FormGuard value) override
    { guard = value; calls.emplace_back(value ? "gameplay.guard" : "gameplay.guard.clear"); }
    void SetPlayerAttackListener(goldcraft::PlayerAttackListener value) override
    { listener = value; calls.emplace_back(value ? "gameplay.attack" : "gameplay.attack.clear"); }
} oldGameplay;
struct Weapons final : csol::IWeaponService
{
    bool zombie{true};
    int CanBuy(int, int) override { return 0; }
    int Buy(int, int) override { return 0; }
    void SetZombieMode(bool value) override { zombie=value; calls.emplace_back("weapons.clear"); }
    void SetPlayerRole(int, int) override {}
} oldWeapons;
IBaseInterface* Factory(const char* name, int* result)
{
    calls.emplace_back(std::string("factory:") + name);
    if (std::strcmp(name, csol::GameModeServiceVersion) == 0)
        return modesPresent ? CreateInterface(name, result) : nullptr;
    if (std::strcmp(name, goldcraft::server_control_api_version) == 0)
        return oldServerPresent ? &oldServer : nullptr;
    if (std::strcmp(name, goldcraft::server_gameplay_api_version) == 0)
        return oldGameplayPresent ? &oldGameplay : nullptr;
    return nullptr;
}
HMODULE WINAPI GetModule(const char* name)
{
    Check(std::strcmp(name, "mp.dll") == 0, "module resolver uses mp.dll");
    return modulePresent ? reinterpret_cast<HMODULE>(1) : nullptr;
}
FARPROC WINAPI GetExport(HMODULE handle, const char* name)
{
    Check(handle == reinterpret_cast<HMODULE>(1), "resolver uses returned handle");
    Check(std::strcmp(name, "CreateInterface") == 0, "resolver uses factory export");
    return exportPresent ? std::bit_cast<FARPROC>(&Factory) : nullptr;
}
void CaptureLog(const char*, ...) { calls.emplace_back("log.unavailable"); }
template<class... Args>
int RegisterForward(const char* name, ForwardExecType type, Args... arguments)
{
    const bool guard=std::strcmp(name,"goldcraft_form_request")==0;
    Check(type==(guard?ET_CONTINUE:ET_IGNORE),"legacy forward aggregation unchanged");
    calls.emplace_back(std::string("forward:")+name);
    const std::array<int,sizeof...(Args)> supplied{static_cast<int>(arguments)...};
    Check(!supplied.empty() && supplied.back()==FP_DONE,"forward argument list terminated");
    const std::vector<int> fields(supplied.begin(),supplied.end()-1);
    const std::vector<int> wanted=guard?std::vector<int>{FP_CELL,FP_CELL}:
        std::strcmp(name,"goldcraft_form_changed")==0?std::vector<int>{FP_CELL,FP_CELL,FP_CELL}:
        std::vector<int>{FP_CELL,FP_CELL,FP_CELL,FP_FLOAT,FP_FLOAT};
    Check(fields==wanted,"legacy forward argument types unchanged");
    return guard?12:std::strcmp(name,"goldcraft_form_changed")==0?11:13;
}
int ExecuteForward(int id, ...) { calls.emplace_back("execute:"+std::to_string(id)); return 1; }
}
namespace goldcraft::amxx_effects
{
void OnPluginsLoaded()
{
    test::calls.emplace_back("effects.load:"+std::to_string(CSOL_LockedGameMode()));
    test::Check(!CSOL_GameModeIsolationReady(),"BeginMap clears prior confirmation before effects load");
}
void OnPluginsUnloading() { test::calls.emplace_back("effects.unload"); }
}

// All copied function bodies and declarations below are exact source slices.
// OS symbol lookup, logging and forward registration are test captures.
// Forward call arguments are checked without exercising the AMXX varargs ABI.
#define GetModuleHandleA test::GetModule
#define GetProcAddress test::GetExport
#undef MF_RegisterForward
#define MF_RegisterForward test::RegisterForward
#define MF_Log test::CaptureLog
#include "module_excerpt.inc"
#undef MF_Log
#undef MF_RegisterForward
#define MF_RegisterForward g_fn_RegisterForward
#undef GetProcAddress
#undef GetModuleHandleA

namespace test
{
csol::IGameModeService* Service()
{
    int code=99;
    auto* service=static_cast<csol::IGameModeService*>(CreateInterface("CSOLGameMode001",&code));
    Check(service && code==IFACE_OK,"actual SDK CreateInterface registration");
    Check(CreateInterface("CSOLGameMode002",&code)==nullptr && code==IFACE_FAILED,"unknown version rejected");
    return service;
}
void FinishPolicyChecks(csol::IGameModeService& service)
{
    const auto set = [](cvar_t* variable, std::string& storage, const char* value)
    { storage = value ? value : ""; variable->string = value ? storage.data() : nullptr; };
    const auto seed = []
    {
        CSOLScenarioFinishPolicy result; result.action = CSOLScenarioFinishAction::ChangeMap;
        result.nextMap.fill('?'); return result;
    };
    const auto unchanged = [](const CSOLScenarioFinishPolicy& value, const CSOLScenarioFinishPolicy& previous)
    { return value.action == previous.action && value.nextMap == previous.nextMap; };
    Request("1"); Check(service.BeginMap() == 1 && service.ConfirmPluginIsolation(1), "seed unrelated locked mode for pure finish-policy reads");
    auto policy = seed();
    Check(CSOL_RequestedScenarioFinishPolicy(policy) && policy.action == CSOLScenarioFinishAction::FreePlay &&
        policy.nextMap == std::array<char,64>{}, "default local policy retains the map with an empty copied target");
    for (const char* action : std::initializer_list<const char*>{nullptr,"","-1","2","00","01","+1","-0"," 1","1 ","1.0","1e0","nan","inf","1\n","1;quit"})
    {
        set(finishActionRequest,finishActionText,action); policy = seed(); const auto before = policy;
        Check(!CSOL_RequestedScenarioFinishPolicy(policy) && unchanged(policy,before),
            "finish action requires exact zero/one without partially publishing output");
    }
    set(finishActionRequest,finishActionText,"1");
    set(nextMapRequest,nextMapText,"zs_deadend");
    for (const char* mode : std::initializer_list<const char*>{nullptr,"","0","1","02","2 ","+2","3"})
    {
        Request(mode); policy = seed(); const auto before = policy;
        Check(!CSOL_RequestedScenarioFinishPolicy(policy) && unchanged(policy,before),
            "map change requires the current requested mode to remain exactly scenario two");
    }
    Request("2");
    for (const auto& name : std::vector<std::string>{"a","Z","0","_","-","AZaz09_-",std::string(63,'x')})
    {
        set(nextMapRequest,nextMapText,name.c_str()); policy = seed();
        Check(CSOL_RequestedScenarioFinishPolicy(policy) && policy.action == CSOLScenarioFinishAction::ChangeMap &&
            std::strcmp(policy.nextMap.data(),name.c_str()) == 0 &&
            std::all_of(policy.nextMap.begin()+name.size(),policy.nextMap.end(),[](char value){return value == 0;}),
            "map policy copies one to sixty-three exact ASCII letters/digits/underscore/hyphen and clears the remainder");
    }
    std::vector<std::string> invalidMaps{"",std::string(64,'x'),"../zs_deadend","maps/zs_deadend","maps\\zs_deadend",
        "zs_deadend.bsp","C:map","a b","a\tb","a\nb","a\rb","\"map\"","map;quit",std::string(1,char(0x80))};
    for (const auto& name : invalidMaps)
    {
        set(nextMapRequest,nextMapText,name.c_str()); policy = seed(); const auto before = policy;
        Check(!CSOL_RequestedScenarioFinishPolicy(policy) && unchanged(policy,before),
            "invalid map bytes, path syntax and length reject transactionally");
    }
    set(nextMapRequest,nextMapText,nullptr); policy = seed(); const auto before = policy;
    Check(!CSOL_RequestedScenarioFinishPolicy(policy) && unchanged(policy,before), "change-map rejects a null target");
    set(finishActionRequest,finishActionText,"0");
    for (const char* mode : std::initializer_list<const char*>{nullptr,"0","2;quit"})
        for (const char* name : std::initializer_list<const char*>{nullptr,"","../invalid;quit"})
    {
        Request(mode); set(nextMapRequest,nextMapText,name); policy = seed();
        Check(CSOL_RequestedScenarioFinishPolicy(policy) && policy.action == CSOLScenarioFinishAction::FreePlay &&
            policy.nextMap == std::array<char,64>{}, "free play ignores next mode/map and clears an earlier change target");
    }
    Check(service.LockedMode() == 1 && service.PluginIsolationReady(), "policy getter never changes map lock or isolation state");
    Request("1"); set(finishActionRequest,finishActionText,"0"); set(nextMapRequest,nextMapText,"zs_deadend");
}
void ServiceChecks()
{
    auto* service=Service();
    Check(service->LockedMode()==-1 && !service->PluginIsolationReady(),"initial unprepared mode");
    Check(service->BeginMap()==-1,"BeginMap before registration fails closed");
    Check(!service->ConfirmPluginIsolation(-1),"unprepared confirmation rejected");
    bool shortDelay = true; std::uint16_t difficulty = 7;
    CSOLScenarioFinishPolicy finishBefore; finishBefore.action = CSOLScenarioFinishAction::ChangeMap; finishBefore.nextMap.fill('?');
    auto finish = finishBefore;
    Check(!CSOL_RequestedScenarioFinishPolicy(finish) && finish.action == finishBefore.action && finish.nextMap == finishBefore.nextMap,
        "unregistered finish policy preserves the complete caller value");
    Check(!CSOL_RequestedScenarioShortFinalDelay(shortDelay) && shortDelay &&
        !CSOL_RequestedScenarioDifficulty(difficulty) && difficulty == 7,
        "unregistered scenario options reject without changing caller output");
    CSOL_InitGameModes();CSOL_InitGameModes();
    Check(registrations==5 && request && difficultyRequest && shortDelayRequest && finishActionRequest && nextMapRequest,
        "five cvars register once per DLL");
    Check(CSOL_RequestedScenarioShortFinalDelay(shortDelay) && !shortDelay &&
        CSOL_RequestedScenarioDifficulty(difficulty) && difficulty == 1,
        "registered scenario option defaults are available independently of a map lock");
    Check(service->BeginMap()==1,"first map uses registered default");
    constexpr std::array<const char*,21> invalid{
        nullptr,"","-1","3","10","01"," 1","1 ","+1","1.0","1e0","nan","NaN","inf",
        "2147483647","4294967296","1\n","1\t","0x1","-0","2;quit"};
    for(const char* value:invalid)
    {
        Request("2");Check(service->BeginMap()==2,"seed valid map");
        Check(service->ConfirmPluginIsolation(2),"seed confirmation");
        Request(value);Check(service->BeginMap()==-1,"invalid full cvar text rejected");
        Check(CSOL_LockedGameMode()==-1 && !CSOL_GameModeIsolationReady(),"invalid map clears prior state");
        for(int mode=-1;mode<=3;++mode)Check(!service->ConfirmPluginIsolation(mode),"invalid mode cannot confirm");
    }
    for(int previous=0;previous<3;++previous)for(int next=0;next<3;++next)
    {
        Request(std::to_string(previous).c_str());Check(service->BeginMap()==previous,"latch requested map");
        Request(std::to_string(next).c_str());
        Check(service->LockedMode()==previous,"request does not mutate current map");
        Check(!service->ConfirmPluginIsolation((previous+1)%3),"wrong generation mode rejected");
        Check(!service->PluginIsolationReady(),"wrong confirmation keeps not ready");
        Check(service->ConfirmPluginIsolation(previous),"matching mode accepted");
        Check(CSOL_GameModeIsolationReady(),"ready after matching acknowledgement");
        Check(service->BeginMap()==next,"next map consumes new request including same-name reload");
        Check(!service->PluginIsolationReady(),"each BeginMap discards acknowledgement");
        service->EndMap();service->EndMap();
        Check(service->LockedMode()==-1 && !service->PluginIsolationReady(),"EndMap idempotently clears");
        Check(!service->ConfirmPluginIsolation(next),"late acknowledgement after unload rejected");
    }
    constexpr const char* invalidDelay[]{nullptr,"","-1","2","01","00"," 1","1 ","+1","1.0","1e0","nan","inf","1\n","1\t","0x1","-0","1;quit"};
    for (const char* value : invalidDelay) for (bool previous : {false,true})
    {
        shortDelayText = value ? value : "";
        shortDelayRequest->string = value ? shortDelayText.data() : nullptr;
        shortDelay = previous;
        Check(!CSOL_RequestedScenarioShortFinalDelay(shortDelay) && shortDelay == previous,
            "final-delay option accepts only exact zero/one and preserves output on malformed text");
    }
    for (bool selected : {false,true,false})
    {
        shortDelayText = selected ? "1" : "0"; shortDelayRequest->string = shortDelayText.data();
        shortDelay = !selected;
        Check(CSOL_RequestedScenarioShortFinalDelay(shortDelay) && shortDelay == selected,
            "option getter reads current request; map lifetime capture belongs to actual Rules Precache");
    }
    for (const char* value : {"0","-1","65536","+1","1.0"," 1","1x"})
    {
        difficultyText = value; difficultyRequest->string = difficultyText.data(); difficulty = 7;
        Check(!CSOL_RequestedScenarioDifficulty(difficulty) && difficulty == 7,
            "independent difficulty request rejects zero, overflow and partial numeric input");
    }
    difficultyText = "65535"; difficultyRequest->string = difficultyText.data();
    Check(CSOL_RequestedScenarioDifficulty(difficulty) && difficulty == 65535,
        "difficulty parser retains the full unsigned16 boundary");
    difficultyText = "1"; difficultyRequest->string = difficultyText.data();
    FinishPolicyChecks(*service);
}
void ModuleChecks()
{
    cell confirm[]{sizeof(cell),1},badCount[]{0,1};
    Check(GameModeVersion(nullptr,nullptr)==0,"missing module version 0");
    Check(GameModeCurrent(nullptr,nullptr)==-1,"missing module mode -1");
    Check(!GameModeConfirm(nullptr,confirm),"missing module cannot acknowledge");
    modulePresent=true;
    Check(GameModeVersion(nullptr,nullptr)==0,"missing export version 0");
    exportPresent=true;
    Check(GameModeVersion(nullptr,nullptr)==0,"missing interface version 0");
    modesPresent=true;
    Check(GameModeVersion(nullptr,nullptr)==1,"new interface resolved independently");
    Check(GameModeCurrent(nullptr,nullptr)==-1,"resolve alone does not BeginMap");
    Check(!GameModeConfirm(nullptr,badCount),"wrong parameter byte count rejected");
    CSOL_InitGameModes();
    calls.clear();OnPluginsLoaded();
    const std::vector<std::string> unavailable{
        "effects.load:1","factory:GoldCraftServerControl001","factory:GoldCraftServerGameplay001","log.unavailable"};
    Check(calls==unavailable,"mode preparation and effects retained when old server unavailable");
    Check(GameModeCurrent(nullptr,nullptr)==1,"module prepares before plugin precache");
    Check(GameModeConfirm(nullptr,confirm)==1,"module acknowledgement forwards matching mode");
    oldServerPresent=oldGameplayPresent=true;calls.clear();OnPluginsLoaded();
    const std::vector<std::string> loaded{
        "effects.load:1","factory:GoldCraftServerControl001","factory:GoldCraftServerGameplay001",
        "forward:goldcraft_form_changed","server.listener","forward:goldcraft_form_request",
        "forward:goldcraft_player_attack_post","gameplay.guard","gameplay.attack"};
    Check(calls==loaded,"legacy effect and control registration order unchanged");
    Check(!CSOL_GameModeIsolationReady(),"same mode reload clears old acknowledgement");
    Check(oldServer.listener && oldGameplay.guard && oldGameplay.listener,"legacy listeners preserved");
    calls.clear();oldServer.listener(1,0,1);Check(oldGameplay.guard(1,1)==1,"legacy guard return preserved");
    oldGameplay.listener(1,2,3,4.0f,5.0f);
    Check(calls==std::vector<std::string>{"execute:11","execute:12","execute:13"},"legacy callbacks use registered forward IDs");
    Request("2");Check(GameModeCurrent(nullptr,nullptr)==1,"native mode remains locked mid-map");
    weapons=&oldWeapons;calls.clear();OnPluginsUnloading();
    Check(calls==std::vector<std::string>{"weapons.clear","effects.unload","server.clear","gameplay.guard.clear","gameplay.attack.clear"},"legacy unload callback order preserved");
    Check(!oldWeapons.zombie && !oldServer.listener && !oldGameplay.guard && !oldGameplay.listener,"legacy listeners cleared");
    Check(changed_forward==-1 && guard_forward==-1 && attack_forward==-1,"legacy IDs cleared");
    Check(GameModeCurrent(nullptr,nullptr)==-1 && !CSOL_GameModeIsolationReady(),"unload clears map confirmation");
    Check(!GameModeConfirm(nullptr,confirm),"unload rejects late acknowledgement");
    calls.clear();OnPluginsLoaded();Check(GameModeCurrent(nullptr,nullptr)==2,"following map sees updated request");
    Check(!calls.empty() && calls.front()=="effects.load:2","mode locks before effects on next map");
    OnAmxxDetach();
    Check(!gameModes && !server && !gameplay && !weapons && !weaponAmmo,"detach clears all pointers");
    Check(CSOL_LockedGameMode()==-1,"detach ended map");
    modulePresent=false;Check(GameModeVersion(nullptr,nullptr)==0,"detached resolver cannot reuse old pointer");
}
}
int main(int argc,char** argv)
{
    static_assert(sizeof(void*)==4 && sizeof(cell)==4 && sizeof(csol::GameMode)==4);
    static_assert(static_cast<int>(csol::GameMode::Native)==0 &&
        static_cast<int>(csol::GameMode::ZombiePlague)==1 && static_cast<int>(csol::GameMode::Scenario)==2);
    g_engfuncs.pfnCVarRegister=test::Register;
    g_fn_Log=test::CaptureLog;g_fn_ExecuteForward=test::ExecuteForward;
    if(argc==2 && std::strcmp(argv[1],"--service")==0)test::ServiceChecks();
    else if(argc==2 && std::strcmp(argv[1],"--module")==0)test::ModuleChecks();
    else return 2;
    std::cout<<"game_mode "<<argv[1]<<" checks="<<test::checks<<" failures="<<test::failures<<'\n';
    return test::failures?1:0;
}
