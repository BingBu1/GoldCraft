// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/runtime.hpp"
#include "client/weapon_inventory.hpp"
#include "client/command_chain.hpp"
#include "client/user_message_chain.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <utility>

namespace csol::client
{
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
ref_params_t view{};
metahook_api_t* api{};
bool abilityOwnsKey{};
int abilityRequests{};
void SynchronizeSession(){}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void AddWeaponPickup(int){}
void ClearNativeWeaponSelection(){}
bool TryScenarioHealingSlot(int){return false;}
bool TryActivateScenarioAbility(){++abilityRequests;return abilityOwnsKey;}
}
namespace
{
using namespace csol::client;
constexpr std::array<const char*,13> names{
    "slot1","slot2","slot3","slot4","slot5","slot6","slot7","slot8","slot9","slot10",
    "invnext","invprev","cancelselect"};
std::map<std::string,cmd_function_t> commands;
std::map<std::string,usermsg_t> messages;
std::array<std::array<int,13>,2> nativeCalls;
std::array<int,13> layerCalls{},outerCalls{};
std::array<xcommand_t,13> layerPrevious{},outerPrevious{};
std::array<int,2> nativeMenus{};
int checks{},hookCalls{},messageHookCalls{},added{},customCalls{};
cvar_t protocol{};
float now=10;

void Check(bool value,const char* reason)
{
    ++checks;
    if(!value){std::cerr<<"FAIL "<<checks<<" "<<reason<<'\n';std::exit(1);}
}
template<int Generation,int Index>void Native(){++nativeCalls[Generation][Index];}
template<int Index>void Layer(){++layerCalls[Index];if(layerPrevious[Index])layerPrevious[Index]();}
template<int Index>void Outer(){++outerCalls[Index];if(outerPrevious[Index])outerPrevious[Index]();}
template<int Generation>int NativeMenu(const char*,int,void*){++nativeMenus[Generation];return 1;}
template<int Generation,std::size_t... Indices>
constexpr auto NativeTable(std::index_sequence<Indices...>){return std::array<xcommand_t,13>{Native<Generation,Indices>...};}
template<std::size_t... Indices>
constexpr auto LayerTable(std::index_sequence<Indices...>){return std::array<xcommand_t,13>{Layer<Indices>...};}
template<std::size_t... Indices>
constexpr auto OuterTable(std::index_sequence<Indices...>){return std::array<xcommand_t,13>{Outer<Indices>...};}
constexpr std::array nativeFunctions{NativeTable<0>(std::make_index_sequence<13>{}),NativeTable<1>(std::make_index_sequence<13>{})};
constexpr auto layerFunctions=LayerTable(std::make_index_sequence<13>{});
constexpr auto outerFunctions=OuterTable(std::make_index_sequence<13>{});

void RegisterNative(int generation)
{
    for(std::size_t i=0;i<names.size();++i)commands[names[i]].function=nativeFunctions[generation][i];
    messages["ShowMenu"].function=generation?NativeMenu<1>:NativeMenu<0>;
}
void InstallLayer()
{
    for(std::size_t i=0;i<names.size();++i)
        InstallChainedCommand(names[i],layerFunctions[i],layerPrevious[i]);
}
void Initialize(int replacement=-1,bool layer=false)
{
    const auto commandHeads=CaptureChainedCommandHeads();
    const auto messageHeads=CaptureChainedUserMessageHeads();
    if(replacement>=0)RegisterNative(replacement); // The actual downstream HUD_Init boundary.
    RefreshChainedCommandHeads(commandHeads);
    RefreshChainedUserMessageHeads(messageHeads);
    InitializeWeaponInventory();
    if(layer)InstallLayer();
}
void InvokeAll(int generation,bool layer=false,bool outer=false)
{
    for(std::size_t i=0;i<names.size();++i)
    {
        const int before=nativeCalls[generation][i],other=nativeCalls[1-generation][i];
        const int layers=layerCalls[i],outers=outerCalls[i];
        commands.at(names[i]).function();
        Check(nativeCalls[generation][i]==before+1,"each stock callback runs exactly once");
        Check(nativeCalls[1-generation][i]==other,"old native generation is not invoked");
        Check(layerCalls[i]==layers+int(layer),"registered wrapper remains in order");
        Check(outerCalls[i]==outers+int(outer),"external wrapper remains in order");
    }
}
void MenuFallback(int generation)
{
    unsigned char packet[]{0x40,0,255,0,'m',0};
    const int before=nativeMenus[generation],other=nativeMenus[1-generation];
    messages.at("ShowMenu").function("ShowMenu",sizeof(packet),packet);
    Check(nativeMenus[generation]==before+1&&nativeMenus[1-generation]==other,"ShowMenu keeps current native callback");
    const int skills=abilityRequests,native=nativeCalls[generation][6];
    commands.at("slot7").function();
    Check(abilityRequests==skills,"text menu owns key before skill activation");
    Check(nativeCalls[generation][6]==native+1,"text-menu slot reaches native exactly once");
    Check(!WeaponTextMenuVisible(),"valid menu key releases its ownership");
}
void SkillRouting(int generation)
{
    const int native=nativeCalls[generation][6],skills=abilityRequests;
    abilityOwnsKey=true;commands.at("slot7").function();
    Check(abilityRequests==skills+1&&nativeCalls[generation][6]==native,"owned skill consumes slot7 without native fallback");
    abilityOwnsKey=false;commands.at("slot7").function();
    Check(abilityRequests==skills+2&&nativeCalls[generation][6]==native+1,"unowned skill falls back once");
}
void Custom(){++customCalls;}
void CustomNative(){customCalls+=10;}
void CustomFallback();
xcommand_t customPrevious{};
void CustomFallback(){++customCalls;if(customPrevious)customPrevious();}
void MissingAndReplacement()
{
    xcommand_t latePrevious{};
    InstallChainedCommand("late_stock",Custom,latePrevious);
    Check(!commands.contains("late_stock")&&!latePrevious,"absent stock command is not invented");
    commands["late_stock"].function=CustomNative;
    InstallChainedCommand("late_stock",Custom,latePrevious);
    Check(commands.at("late_stock").function==Custom&&latePrevious==CustomNative,"late native command can be hooked");
    InstallChainedCommand("buffclass_skill",CustomFallback,customPrevious,true);
    Check(added==1&&!customPrevious,"missing extension command is added once");
    const int before=hookCalls;
    InstallChainedCommand("buffclass_skill",CustomFallback,customPrevious,true);
    Check(added==1&&hookCalls==before,"extension reinitialize never registers itself twice");
    customCalls=0;commands.at("buffclass_skill").function();
    Check(customCalls==1,"new extension command runs once");
    const auto heads=CaptureChainedCommandHeads();
    commands["buffclass_skill"].function=CustomNative;
    RefreshChainedCommandHeads(heads);
    InstallChainedCommand("buffclass_skill",CustomFallback,customPrevious,true);
    customCalls=0;commands.at("buffclass_skill").function();
    Check(customCalls==11&&customPrevious==CustomNative,"native extension replacement updates fallback");
    const auto removal=CaptureChainedCommandHeads();
    commands.erase("buffclass_skill");
    RefreshChainedCommandHeads(removal);
    InstallChainedCommand("buffclass_skill",CustomFallback,customPrevious,true);
    customCalls=0;commands.at("buffclass_skill").function();
    Check(customCalls==1&&!customPrevious&&added==2,"removed extension forgets stale native callback and is recreated");
}
}
int main()
{
    metahook_api_t hooks{};api=&hooks;
    hooks.FindCmd=[](const char* name)->cmd_function_t*{const auto it=commands.find(name);return it==commands.end()?nullptr:&it->second;};
    hooks.HookCmd=[](const char* name,xcommand_t callback)->xcommand_t{
        ++hookCalls;const auto it=commands.find(name);if(it==commands.end())return nullptr;
        return std::exchange(it->second.function,callback);};
    hooks.FindUserMsgHook=[](const char* name)->usermsg_t*{const auto it=messages.find(name);return it==messages.end()?nullptr:&it->second;};
    hooks.HookUserMsg=[](const char* name,pfnUserMsgHook callback)->pfnUserMsgHook{
        ++messageHookCalls;const auto it=messages.find(name);if(it==messages.end())return nullptr;
        return std::exchange(it->second.function,callback);};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook callback)->int{messages[name].function=callback;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t callback)->int{++added;commands[name].function=callback;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &protocol;};
    engine.Cvar_SetValue=[](const char*,float value){protocol.value=value;};
    engine.GetClientTime=[]()->float{return now;};
    view.health=100;RegisterNative(0);Initialize();
    Check(hookCalls==13&&messageHookCalls==1,"first initialization wraps thirteen commands and ShowMenu");
    InvokeAll(0);MenuFallback(0);SkillRouting(0);
    const int first=hookCalls;Initialize();Initialize();
    // Fail here on the old implementation, before invoking its recursive chain.
    Check(hookCalls==first,"repeated Initialize preserves installed command heads");
    Check(messageHookCalls==1,"repeated Initialize preserves ShowMenu chain");
    InvokeAll(0);MenuFallback(0);
    for(bool forget:{false,true})
    {
        ResetWeaponInventory(forget);Initialize();
        Check(hookCalls==first,"inventory Reset does not forget command installation");
        InvokeAll(0);MenuFallback(0);
    }
    InstallLayer();const int layered=hookCalls;Initialize(-1,true);Initialize(-1,true);
    Check(hookCalls==layered,"second registered wrapper does not create an indirect cycle");
    InvokeAll(0,true);MenuFallback(0);SkillRouting(0);
    for(std::size_t i=0;i<names.size();++i)outerPrevious[i]=std::exchange(commands.at(names[i]).function,outerFunctions[i]);
    Initialize(-1,true);
    Check(hookCalls==layered,"unknown external wrapper remains outermost on repeated Initialize");
    InvokeAll(0,true,true);MenuFallback(0);
    Initialize(1,true);
    Check(hookCalls==layered+26,"native reset reinstalls both known layers once");
    InvokeAll(1,true);MenuFallback(1);SkillRouting(1);
    Initialize(-1,true);InvokeAll(1,true);
    const int replaced=hookCalls;
    Initialize(1,true);
    Check(hookCalls==replaced+26,"native re-registering the same native callback starts a new chain");
    InvokeAll(1,true);MenuFallback(1);
    MissingAndReplacement();
    std::cout<<"command lifecycle "<<checks<<" checks, 0 failures\n";
}
