// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/weapons/guest_cof_admission.hpp"
#include "../../native/csol/client/weapons/guest_cof_input.hpp"
#include "../../native/csol/client/weapons/guest_cof_resources.hpp"
#include "../../native/csol/client/weapons/weapon_inventory.hpp"
#include "../../native/csol/hud_style.hpp"
#include "../../native/csol/buy_menu.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <usercmd.h>
#include <in_buttons.h>

namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
}
namespace
{
using namespace csol::client;
unsigned checks{}, failures{}, publications{}, menuLoads{};
std::uint64_t generation{1}, resourceSerial{7};
bool& allowReads=resource_refresh_detail::readsAllowed;
bool audio{true}, particles{true}, effects{true}, muzzle{true}, points{true}, mouse{true}, crosshair{true},
     control{true}, ironsight{true}, hud{true}, menu{true}, retireInMenu{};
cvar_t protocol{};
GuestCofResourceState resources;
csol::WeaponCurrent current;
std::map<std::string, cmd_function_t> commands;
const char* commandKey="1";
void Check(bool okay,int line){++checks;if(!okay){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
std::uint64_t Generation(){return generation;}
cvar_t* Lookup(const char* name){CHECK(!std::strcmp(name,"cof_protocol"));return &protocol;}
cvar_t* Register(const char*,const char*,int){CHECK(false);return nullptr;}
void Publish(const char* name,float value){CHECK(!std::strcmp(name,"cof_protocol"));protocol.value=value;++publications;}
cmd_function_t* Find(const char* name){auto it=commands.find(name);return it==commands.end()?nullptr:&it->second;}
int Add(const char* name,xcommand_t callback){commands[name].function=callback;return 1;}
char* Arg(int index){CHECK(index==1);return const_cast<char*>(commandKey);}
void Press(const char* key="1"){commandKey=key;commands.at("+cof_aim").function();}
void Release(const char* key="1"){commandKey=key;commands.at("-cof_aim").function();}
void NativeCommand(){}
usercmd_t Input(int active=1,bool admitted=true)
{
    usercmd_t value{};value.buttons=IN_USE|IN_ATTACK2|IN_FORWARD;
    GuestCofCreateMove(&value,active,admitted);
    CHECK((value.buttons&(IN_USE|IN_ATTACK2|IN_FORWARD))==(IN_USE|IN_ATTACK2|IN_FORWARD));
    return value;
}
std::string Read(const std::string& path)
{
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),{}};
}
}
namespace csol::client
{
namespace {bool visible{},buyVisible{},voteVisible{};float menuAngles[3]{};unsigned nativeMoves{};}
void WeaponInventoryCreateMove(float,usercmd_t* cmd,int)
{
    ++nativeMoves;
    if(cmd){*cmd={};cmd->buttons=IN_USE|IN_ATTACK2|IN_FORWARD;}
}
bool CharacterMenuVisible(){return visible;}
void BossImpactCreateMove(int){}
void BuyMenuCreateMove(usercmd_t* cmd,int){if(cmd&&buyVisible)cmd->buttons=0;}
void ScenarioMapVoteCreateMove(usercmd_t* cmd,int){if(cmd&&voteVisible)cmd->buttons=0;}
void ChainAngles(float*){}
#include "native-create-move-chain.inc"
std::uint64_t GuestCofEffectsResourceSerial() noexcept{return resourceSerial;}
GuestCofResourceState GuestCofResourcesState() noexcept{return resources;}
bool GuestCofParticlesReady() noexcept{return particles;}
bool GuestCofEffectsReady() noexcept{return effects&&audio;}
bool GuestCofMuzzleReady() noexcept{return muzzle;}
bool GuestCofPointLightsReady() noexcept{return points;}
bool GuestCofControlReady() noexcept{return control;}
bool GuestCofIronsightReady() noexcept{return ironsight;}
bool GuestCofMouseReady() noexcept{return mouse;}
void GuestCofMouseFrame() noexcept{}
const char* GuestCofMouseInitialization() noexcept{return "controlled";}
bool GuestCofCrosshairReady() noexcept{return crosshair;}
void GuestCofCrosshairFrame() noexcept{}
bool HudStyleWeaponResourcesReady(int id) noexcept{CHECK(id==896);return hud;}
bool PrepareBuyMenuItem(int id){CHECK(id==896);++menuLoads;if(retireInMenu)++generation;return menu;}
const WeaponCurrent& CurrentExtendedWeapon(){return current;}
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    const std::string root=argv[1];
    const auto catalog=csol::hud_style::ParseCatalog(Read(root+"/models/csol/hud_v7.json"));
    const auto buy=csol::buy::ParsePresentation(Read(root+"/"+csol::buy::PresentationResource));
    CHECK(catalog.weapons.contains("weapon_cof_glock"));
    const auto& art=catalog.weapons.at("weapon_cof_glock");
    CHECK(art.title=="客串武器·Glock 19"&&art.selectedKnown&&art.selected==art.weapon&&!art.ammo.empty());
    CHECK(buy.images.contains(896)&&buy.weaponInfo.items.contains(896));
    CHECK(buy.images.at(896).width==170&&buy.images.at(896).height==80);
    CHECK(buy.weaponInfo.items.at(896).table==false&&buy.weaponInfo.items.at(896).grade==std::array<int,3>{-1,-1,-1});
    const auto paths=csol::buy::Resources(buy);CHECK(std::find(paths.begin(),paths.end(),"gfx/csol/buy/cof_glock.tga")!=paths.end());
    metahook_api_t table{};api=&table;table.FindCmd=Find;
    engine.pfnGetCvarPointer=Lookup;engine.pfnRegisterVariable=Register;engine.Cvar_SetValue=Publish;
    engine.Cmd_Argv=Arg;engine.pfnAddCommand=Add;
    protocol.flags=FCVAR_USERINFO;view.health=100;current.id=896;current.state=1;
    CHECK(InitializeGuestCofInput()&&GuestCofInputReady());
    CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(Input().buttons&IN_CANCEL);CHECK(Input().buttons&IN_CANCEL);
    Press();Release();CHECK(!(Input().buttons&IN_CANCEL));
    Press();Release();CHECK(Input().buttons&IN_CANCEL);CHECK(!(Input().buttons&IN_CANCEL));
    Press("1");Press("2");Release("1");CHECK(Input().buttons&IN_CANCEL);Release("2");CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(!(Input(0).buttons&IN_CANCEL));CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(!(Input(1,false).buttons&IN_CANCEL));CHECK(!(Input().buttons&IN_CANCEL));
    Press();view.health=0;CHECK(!(Input().buttons&IN_CANCEL));view.health=100;CHECK(!(Input().buttons&IN_CANCEL));
    Press();view.spectator=1;CHECK(!(Input().buttons&IN_CANCEL));view.spectator=0;
    Press();current.id=16;CHECK(!(Input().buttons&IN_CANCEL));current.id=896;CHECK(!(Input().buttons&IN_CANCEL));
    Press("bad");CHECK(!(Input().buttons&IN_CANCEL));Press("");CHECK(Input().buttons&IN_CANCEL);Release("");CHECK(!(Input().buttons&IN_CANCEL));
    ShutdownGuestCofInput();commands["+cof_aim"].function=NativeCommand;
    CHECK(!InitializeGuestCofInput()&&!GuestCofInputReady());
    CHECK(commands.at("+cof_aim").function==NativeCommand);
    commands.clear();CHECK(InitializeGuestCofInput());
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
    resources.status=GuestCofResourceStatus::Prepared;resources.generation=1;
    resources.studioAndRecoilPrepared=resources.decalsPrepared=true;
    InitializeGuestCofAdmission(&provider);CHECK(!GuestCofAdmitted()&&protocol.value==0);
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted()&&protocol.value==1&&menuLoads==1);
    engine.SetViewAngles=ChainAngles;
    usercmd_t chain{};
    Press();CreateMove(.01f,&chain,1);CHECK((chain.buttons&IN_CANCEL)&&nativeMoves==1);
    CHECK((chain.buttons&(IN_USE|IN_ATTACK2|IN_FORWARD))==(IN_USE|IN_ATTACK2|IN_FORWARD));
    Release();CreateMove(.01f,&chain,1);CHECK(!(chain.buttons&IN_CANCEL)&&nativeMoves==2);
    for(bool* panel:{&visible,&buyVisible,&voteVisible})
    {
        Press();*panel=true;CreateMove(.01f,&chain,1);CHECK(chain.buttons==0);
        Release();*panel=false;CreateMove(.01f,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));
    }
    Press();CreateMove(.01f,&chain,0);CHECK(!(chain.buttons&IN_CANCEL));
    Release();
    // The outer HUD_Frame deliberately returns during resource replacement;
    // input must stop immediately without waiting for that skipped callback.
    allowReads=false;CHECK(!GuestCofAdmitted());
    allowReads=true;CHECK(GuestCofAdmitted());
    const auto published=publications;for(int i=0;i<10000;++i)GuestCofAdmissionFrame();
    CHECK(publications==published&&menuLoads==1);
    for(bool* gate:{&allowReads,&audio,&particles,&effects,&muzzle,&points,&control,&ironsight,&mouse,&crosshair,&hud})
    {
        *gate=false;GuestCofAdmissionFrame();
        if(GuestCofAdmitted()||protocol.value!=0)std::printf("gate=%td remained admitted=%d protocol=%g\n",gate==&allowReads?0:gate==&audio?1:gate==&particles?2:gate==&effects?3:gate==&muzzle?4:gate==&points?5:gate==&control?6:gate==&ironsight?7:8,GuestCofAdmitted(),protocol.value);
        CHECK(!GuestCofAdmitted()&&protocol.value==0);
        *gate=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted()&&protocol.value==1);
    }
    ++generation;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted());
    resources.generation=generation;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted()&&menuLoads==2);
    ++resourceSerial;menu=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted()&&menuLoads==3);
    for(int i=0;i<100;++i)GuestCofAdmissionFrame();CHECK(menuLoads==3);
    menu=true;InvalidateGuestCofAdmission();GuestCofAdmissionFrame();CHECK(GuestCofAdmitted()&&menuLoads==4);
    InvalidateGuestCofAdmission();retireInMenu=true;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted());
    retireInMenu=false;resources.generation=generation;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted());
    bool foreign=true;const auto writes=publications;
    std::thread other([&]{foreign=GuestCofAdmitted();GuestCofAdmissionFrame();InvalidateGuestCofAdmission();});other.join();
    CHECK(!foreign&&GuestCofAdmitted()&&publications==writes);
    generation=0;CHECK(!GuestCofAdmitted());GuestCofAdmissionFrame();CHECK(protocol.value==0);
    ShutdownGuestCofAdmission();ShutdownGuestCofInput();
    std::printf("host presentation/input/admission checks=%u failures=%u; controlled services; live=no performance=no\n",checks,failures);
    return failures?1:0;
}
