// SPDX-License-Identifier: GPL-3.0-or-later
// Keep both complete production consumers; this test drives installed numeric
// commands through real Cycle/ShowMenu and the real scenario healing handler.
#define CSOL_RESPAWN_ROUTE_TEST
#define wmain UnusedConsumerEntryPoint
#include "client_main.cpp"
#undef wmain
#include "native/csol/client/weapons/weapon_inventory.cpp"
#include "native/csol/client/scenario/core/night_player_lights.cpp"

namespace route_fixture
{
std::map<std::string,xcommand_t> keys;
int fallback{},clears{},skillCalls{};
bool skillReady{};
bool fastSwitch{true};
model_t heldModel{};
cl_entity_t viewModel{};
void Native(){++fallback;}
}
namespace csol::client
{
cl_exportfuncs_t downstream{};
void ClearNativeWeaponSelection(){++route_fixture::clears;}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void AddWeaponPickup(int){}
bool TryActivateScenarioAbility(){++route_fixture::skillCalls;return route_fixture::skillReady;}
}
int main()
{
    using fixture::Check;using fixture::Send;using fixture::Item;using fixture::Bytes;
    using namespace csol;using namespace csol::client;using namespace route_fixture;
    metahook_api_t storage{};api=&storage;
    storage.GetEngineFactory=[]()->CreateInterfaceFn{return nullptr;};
    storage.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return nullptr;};
    storage.HookUserMsg=[](const char*name,pfnUserMsgHook hook)->pfnUserMsgHook
    {auto old=fixture::hooks.contains(name)?fixture::hooks.at(name):fixture::Previous;fixture::hooks[name]=hook;return old;};
    storage.HookCmd=[](const char*name,xcommand_t callback)->xcommand_t{keys[name]=callback;return Native;};
    engine.GetClientTime=[]{return fixture::now;};engine.Con_DPrintf=fixture::Log;
    engine.pfnGetCvarFloat=[](const char*){return fastSwitch ? 1.f : 0.f;};
    engine.pfnServerCmd=[](const char*command){fixture::sent.emplace_back(command);return 1;};
    engine.pfnHookUserMsg=[](const char*name,pfnUserMsgHook hook){fixture::hooks[name]=hook;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{static cvar_t option{};return &option;};
    engine.Cvar_SetValue=[](const char*,float){};
    engine.pfnAddCommand=[](const char*name,xcommand_t callback){keys[name]=callback;return 1;};
    engine.pfnGetScreenInfo=[](SCREENINFO*screen){screen->iWidth=1280;screen->iHeight=720;return 1;};
    engine.pfnSPR_GetList=[](char*,int*count)->client_sprite_t*{*count=0;return nullptr;};
    view.health=100;InitializeScenarioRespawnUi();InitializeWeaponInventory();InitializeNightPlayerLights();
    Check(keys.size()==13,"native ten slots/wheel/cancel installed without changing key bindings");
    for(int key=1;key<=6;++key)keys.at("slot"+std::to_string(key))();
    Check(fallback==6&&fixture::sent.empty(),"ordinary stock server retains slots1 through6");
    Send("MPToCL",Item(0,0,3));Send("MPToCL",Item(1,0,4));
    keys.at("slot5")();keys.at("slot6")();
    Check(fixture::sent==std::vector<std::string>{"useitem \"zbs3\"\n","useitem \"zbs10\"\n"},
        "real numeric callbacks reach real 5/6 commands before weapon routing");
    for(int key:{5,6})
    {
        const auto count=fixture::sent.size();const auto native=fallback;
        Send("ShowMenu",Bytes({255,255,255,0,'A',0}));keys.at("slot"+std::to_string(key))();
        Check(fallback==native+1&&fixture::sent.size()==count&&!WeaponTextMenuVisible(),"AMXX valid5/6 consumes first key and closes locally");
        keys.at("slot"+std::to_string(key))();Check(fixture::sent.size()==count+1,"next key after AMXX returns to scenario item");
    }
    Send("ShowMenu",Bytes({1,0,255,0,'A',0}));auto count=fixture::sent.size();auto native=fallback;
    keys.at("slot5")();Check(fallback==native+1&&fixture::sent.size()==count&&WeaponTextMenuVisible(),"invalid AMXX item stays menu and never uses consumable");
    Send("ShowMenu",Bytes({0,0,0,0,0}));
    skillReady=true;keys.at("slot7")();Check(skillCalls==1,"character skill7 preserved");
    Send("ShowMenu",Bytes({64,0,255,0,'A',0}));keys.at("slot7")();
    Check(skillCalls==1,"AMXX7 retains priority over character skill");
    for(const WeaponDefinition& definition:std::vector<WeaponDefinition>{
        {197,0,1,11,-1,200,-1,150,0,"weapon_k3"},{220,1,6,14,-1,30,-1,1,24,"weapon_tknifeex2"},
        {29,2,0,-1,-1,-1,-1,-1,0,"weapon_knife"},{4,3,0,12,-1,1,-1,-1,0,"weapon_hegrenade"}})
        definitions[definition.id]=definition;
    available=true;WeaponOwnership owned{};for(int id:{197,220,29,4})owned[id]=true;
    for(const auto& packet:EncodeWeaponInventory(owned,1))Send(WeaponInventoryMessage,packet);
    const std::array expected{"weapon_k3","weapon_tknifeex2","weapon_knife","weapon_hegrenade"};
    for(int key=1;key<=4;++key)
    {
        count=fixture::sent.size();keys.at("slot"+std::to_string(key))();
        Check(fixture::sent.size()==count+1&&fixture::sent.back()==expected[key-1],"real slots1 through4 still select their weapon");
    }
    event_api_t events{};
    events.EV_FindModelIndex=[](const char*){return 1;};
    engine.pEventAPI=&events;
    engine.GetViewModel=[] { return &viewModel; };
    namespace custom = csol::scenario::custom;
    custom::NightLightingSnapshot night{{},custom::Phase::Night,1,1};
    const auto sendNight=[&] {
        const auto bytes=*custom::EncodeNightLighting(night);
        Send(custom::NightLightingMessage,{bytes.begin(),bytes.end()});
    };
    sendNight();
    fastSwitch=false;keys.at("slot1")();
    Check(selected==197,"slot 1 has a pending rifle selection before the equipment key");
    native=fallback;count=fixture::sent.size();
    Send("ShowMenu",Bytes({16,0,255,0,'A',0}));keys.at("slot5")();
    Check(fallback==native+1&&fixture::sent.size()==count,"AMXX slot 5 takes precedence during the night");
    keys.at("slot1")();keys.at("slot5")();
    Check(!selected&&fixture::sent.size()==count+1&&fixture::sent.back()==custom::NightFlashlightSelect,
        "slot 5 selects the actual night equipment and closes the pending rifle selection");
    std::strcpy(heldModel.name,custom::NightFlashlightView);viewModel.model=&heldModel;
    keys.at("slot5")();
    Check(fixture::sent.size()==count+1,"holding the flashlight never falls through to the slot 5 healing item");
    keys.at("slot6")();
    Check(fixture::sent.size()==count+2&&fixture::sent.back()=="useitem \"zbs10\"\n","slot 6 retains healing during the night");
    night.phase=custom::Phase::Ended;++night.revision;sendNight();fastSwitch=true;
    const auto skills=skillCalls;keys.at("invnext")();keys.at("invprev")();
    Check(skillCalls==skills,"wheel does not become scenario item or skill activation");
    Send("ShowMenu",Bytes({16,0,1,0,'A',0}));fixture::now+=2;count=fixture::sent.size();keys.at("slot5")();
    Check(fixture::sent.size()==count+1&&fixture::sent.back()=="useitem \"zbs3\"\n","expired AMXX menu no longer blocks original item input");
    ResetScenarioRespawnUi();ResetWeaponInventory(true);native=fallback;
    keys.at("slot5")();keys.at("slot6")();Check(fallback==native+2,"ordinary session after reset regains native slots");
    Check(!keys.contains("+reload")&&!keys.contains("useitem"),"R and server useitem remain native bindings and parsing");
    std::cout<<"scenario_respawn_slots checks="<<fixture::checks<<" failures="<<fixture::failures<<'\n';return fixture::failures?1:0;
}
