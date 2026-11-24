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
#include "../../native/guest/cof/admission.hpp"
#include <VGUI/IGameUIFuncs.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <demo_api.h>
#include <fstream>
#include <keydefs.h>
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
     control{true}, ironsight{true}, viewmodel{true}, hud{true}, menu{true}, retireInMenu{};
cvar_t protocol{};
cvar_t weaponMask{}, extensionMask{};
bool famasHud{};
bool p345Hud{true},p345Muzzle{true},revolverHud{true},revolverMuzzle{true};
bool vp70Hud{true},vp70Muzzle{true},tmpHud{true},tmpMuzzle{true};
bool mp5Hud{},m16Hud{},m16Muzzle{true},g43Hud{},g43Muzzle{true};
bool shotgunHud{},shotgunMuzzle{true};
GuestCofResourceState resources;
csol::WeaponCurrent current;
std::map<std::string, cmd_function_t> commands;
const char* commandKey="1";
int scenarioLevel{-1};
bool demoPlayback{};
const char* mouse3{};
unsigned bindingReads{},defaultBinds{};
void Check(bool okay,int line){++checks;if(!okay){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
struct BindingKeys final : IGameUIFuncs
{
    const char* Key_BindingForKey(int key) override { CHECK(key==K_MOUSE3);++bindingReads;return mouse3; }
    bool IsKeyDown(const char*,bool&) override { return false; }
    const char* Key_NameForKey(int) override { return nullptr; }
    vgui::KeyCode GetVGUI2KeyCodeForBind(const char*) override { return {}; }
    void GetVideoModes(vmode_s**,int*) override {}
    void GetCurrentVideoMode(int*,int*,int*) override {}
    void GetCurrentRenderer(char*,int,int*) override {}
    bool IsConnectedToVACSecureServer() override { return false; }
    int Key_KeyStringToKeyNum(const char*) override { return -1; }
} bindingKeys;
IBaseInterface* BindingFactory(const char* name,int*)
{
    CHECK(!std::strcmp(name,VENGINE_GAMEUIFUNCS_VERSION));
    return &bindingKeys;
}
int Bind(const char* command)
{
    CHECK(!std::strcmp(command,"bind MOUSE3 +cof_aim\n"));
    ++defaultBinds;
    mouse3="+cof_aim";
    return 1;
}
std::uint64_t Generation(){return generation;}
cvar_t* Lookup(const char* name){if(!std::strcmp(name,"cof_w2"))return &extensionMask;if(!std::strcmp(name,"cof_weapons"))return &weaponMask;CHECK(!std::strcmp(name,"cof_protocol"));return &protocol;}
cvar_t* Register(const char*,const char*,int){CHECK(false);return nullptr;}
void Publish(const char* name,float value){if(!std::strcmp(name,"cof_weapons"))weaponMask.value=value;
    else if(!std::strcmp(name,"cof_w2"))extensionMask.value=value;
    else{CHECK(!std::strcmp(name,"cof_protocol"));protocol.value=value;}++publications;}
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
namespace {bool visible{},buyVisible{},voteVisible{};float menuAngles[3]{};unsigned nativeMoves{},mountMoves{};}
void WeaponInventoryCreateMove(float,usercmd_t* cmd,int)
{
    ++nativeMoves;
    if(cmd){*cmd={};cmd->buttons=IN_USE|IN_ATTACK2|IN_FORWARD;}
}
bool CharacterMenuVisible(){return visible;}
bool BuyMenuVisible(){return buyVisible;}
bool ScenarioMapVoteVisible(){return voteVisible;}
int ScenarioHudLevel(){return scenarioLevel;}
// This fixture supplies no standalone sprint or recovery input.
void ReleaseStandaloneMovementInput(){}
void BossImpactCreateMove(int){}
// This suite has no mounted seat. The unrelated transform is an observed
// orchestration boundary, with the exact current mount_weapon.hpp signature.
void MountWeaponCreateMove(usercmd_s*,int){++mountMoves;}
void BuyMenuCreateMove(usercmd_t* cmd,int){if(cmd&&buyVisible)cmd->buttons=0;}
void ScenarioMapVoteCreateMove(usercmd_t* cmd,int){if(cmd&&voteVisible)cmd->buttons=0;}
void ChainAngles(float*){}
#include "native-create-move-chain.inc"
std::uint64_t GuestCofEffectsResourceSerial() noexcept{return resourceSerial;}
GuestCofResourceState GuestCofResourcesState() noexcept{return resources;}
bool GuestCofParticlesReady() noexcept{return particles;}
bool GuestCofEffectsReady() noexcept{return effects&&audio;}
bool GuestCofMuzzleReady() noexcept{return muzzle;}
bool GuestCofMuzzleReady(unsigned originalWeapon) noexcept{return muzzle&&(originalWeapon==3||(originalWeapon==7&&p345Muzzle)||(originalWeapon==21&&revolverMuzzle)||(originalWeapon==19&&vp70Muzzle)||(originalWeapon==14&&tmpMuzzle)||originalWeapon==27||(originalWeapon==9&&m16Muzzle)||(originalWeapon==26&&g43Muzzle)||(originalWeapon==4&&shotgunMuzzle));}
bool GuestCofPointLightsReady() noexcept{return points;}
bool GuestCofControlReady() noexcept{return control;}
bool GuestCofIronsightReady() noexcept{return ironsight;}
bool GuestCofViewmodelReady() noexcept{return viewmodel;}
bool GuestCofMouseReady() noexcept{return mouse;}
void GuestCofMouseFrame() noexcept{}
const char* GuestCofMouseInitialization() noexcept{return "controlled";}
bool GuestCofCrosshairReady() noexcept{return crosshair;}
void GuestCofCrosshairFrame() noexcept{}
bool HudStyleWeaponResourcesReady(int id,unsigned frames) noexcept{CHECK(((id==896||id==897||id==899||id==900||id==901||id==903||id==904||id==905)&&frames==1)||(id==898&&frames==6)||(id==902&&frames==2));return hud&&(id!=905||famasHud)&&(id!=904||shotgunHud)&&(id!=903||g43Hud)&&(id!=902||m16Hud)&&(id!=901||mp5Hud)&&(id!=897||p345Hud)&&(id!=898||revolverHud)&&(id!=899||vp70Hud)&&(id!=900||tmpHud);}
bool PrepareBuyMenuItem(int id){CHECK(id>=896&&id<=905);++menuLoads;if(retireInMenu)++generation;return menu;}
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
    CHECK(catalog.weapons.contains("weapon_cof_p345")&&catalog.weapons.at("weapon_cof_p345").title=="客串武器·P345");
    CHECK(buy.images.contains(897)&&buy.weaponInfo.items.contains(897)&&buy.images.at(897).width==170&&buy.images.at(897).height==80);
    if(catalog.weapons.contains("weapon_cof_revolver"))
    {
        const auto& revolver=catalog.weapons.at("weapon_cof_revolver");
        CHECK(revolver.title=="客串武器·Taurus .357"&&revolver.ammoFrames.size()==6&&revolver.ammoFrames[0]==revolver.ammo);
        CHECK(buy.images.contains(898)&&buy.weaponInfo.items.contains(898)&&buy.images.at(898).width==170&&buy.images.at(898).height==80);
        const auto download=csol::hud_style::ResourcePaths(catalog);
        for(const auto& frame:revolver.ammoFrames)CHECK(!frame.empty()&&std::find(download.begin(),download.end(),frame)!=download.end());
    }
    if(catalog.weapons.contains("weapon_cof_tmp"))
    {
        const auto& tmp=catalog.weapons.at("weapon_cof_tmp");
        CHECK(tmp.title=="客串武器·MP9"&&tmp.ammoFrames[0].empty()&&!tmp.ammo.empty());
        CHECK(buy.images.contains(900)&&buy.weaponInfo.items.contains(900)&&
            buy.images.at(900).width==170&&buy.images.at(900).height==80);
        CHECK(buy.weaponInfo.items.at(900).description.find("冲锋枪")!=std::string::npos);
        const auto* item=csol::buy::Find(900);CHECK(item&&item->category==3&&item->extendedPrice==2000);
    }
    CHECK(buy.weaponInfo.items.at(896).table==false&&buy.weaponInfo.items.at(896).grade==std::array<int,3>{-1,-1,-1});
    const auto paths=csol::buy::Resources(buy);CHECK(std::find(paths.begin(),paths.end(),"gfx/csol/buy/cof_glock.tga")!=paths.end());
    metahook_api_t table{};api=&table;table.FindCmd=Find;
    table.GetEngineFactory=[]()->CreateInterfaceFn{return BindingFactory;};
    demo_api_t demo{};demo.IsPlayingback=[]{return int(demoPlayback);};
    engine.pDemoAPI=&demo;engine.pfnClientCmd=Bind;
    engine.pfnGetCvarPointer=Lookup;engine.pfnRegisterVariable=Register;engine.Cvar_SetValue=Publish;
    engine.Cmd_Argv=Arg;engine.pfnAddCommand=Add;
    protocol.flags=weaponMask.flags=extensionMask.flags=FCVAR_USERINFO;view.health=100;current.id=896;current.state=1;
    CHECK(InitializeGuestCofInput()&&GuestCofInputReady());
    ApplyGuestCofAimDefault(1);CHECK(!bindingReads&&!defaultBinds);
    scenarioLevel=0;demoPlayback=true;
    ApplyGuestCofAimDefault(1);CHECK(!bindingReads&&!defaultBinds);
    demoPlayback=false;ApplyGuestCofAimDefault(0);CHECK(!bindingReads&&!defaultBinds);
    ApplyGuestCofAimDefault(1);CHECK(bindingReads==1&&defaultBinds==1&&std::string_view(mouse3)=="+cof_aim");
    mouse3=nullptr;ResetGuestCofInput();ApplyGuestCofAimDefault(1);CHECK(bindingReads==1&&!mouse3);
    mouse3="+voicerecord";ApplyGuestCofAimDefault(2);CHECK(bindingReads==2&&defaultBinds==1&&std::string_view(mouse3)=="+voicerecord");
    mouse3="";ApplyGuestCofAimDefault(3);CHECK(bindingReads==3&&defaultBinds==2);
    CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(Input().buttons&IN_CANCEL);CHECK(Input().buttons&IN_CANCEL);
    Press();Release();CHECK(!(Input().buttons&IN_CANCEL));
    Press();Release();CHECK(Input().buttons&IN_CANCEL);CHECK(!(Input().buttons&IN_CANCEL));
    Press("1");Press("2");Release("1");CHECK(Input().buttons&IN_CANCEL);Release("2");CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(!(Input(0).buttons&IN_CANCEL));CHECK(!(Input().buttons&IN_CANCEL));
    Press();CHECK(!(Input(1,false).buttons&IN_CANCEL));CHECK(!(Input().buttons&IN_CANCEL));
    Press();view.health=0;CHECK(!(Input().buttons&IN_CANCEL));view.health=100;CHECK(!(Input().buttons&IN_CANCEL));
    Press();view.spectator=1;CHECK(!(Input().buttons&IN_CANCEL));view.spectator=0;
    current.id=900;Press();CHECK(Input().buttons&IN_CANCEL);Release();
    current.id=896;
    Press();current.id=16;CHECK(!(Input().buttons&IN_CANCEL));current.id=896;CHECK(!(Input().buttons&IN_CANCEL));
    Press("bad");CHECK(!(Input().buttons&IN_CANCEL));Press("");CHECK(Input().buttons&IN_CANCEL);Release("");CHECK(!(Input().buttons&IN_CANCEL));
    ShutdownGuestCofInput();commands["+cof_aim"].function=NativeCommand;
    CHECK(!InitializeGuestCofInput()&&!GuestCofInputReady());
    CHECK(commands.at("+cof_aim").function==NativeCommand);
    commands.clear();CHECK(InitializeGuestCofInput());
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
    resources.status=GuestCofResourceStatus::Prepared;resources.generation=1;
    resources.studioAndRecoilPrepared=resources.decalsPrepared=true;
    resources.weapons=guest::cof::PistolCapability(guest::cof::Pistol::Glock);
    InitializeGuestCofAdmission(&provider);CHECK(!GuestCofAdmitted()&&protocol.value==0);
    mouse3=nullptr;
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted()&&protocol.value==1&&menuLoads==1);
    CHECK(defaultBinds==3&&std::string_view(mouse3)=="+cof_aim");
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
    for(bool* gate:{&allowReads,&audio,&particles,&effects,&muzzle,&points,&control,&ironsight,&viewmodel,&mouse,&crosshair,&hud})
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
    CHECK(GuestCofAdmitted(896)&&!GuestCofAdmitted(897)&&weaponMask.value==1);
    resources.weapons|=guest::cof::PistolCapability(guest::cof::Pistol::P345);
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(896)&&GuestCofAdmitted(897)&&weaponMask.value==3);
    current.id=897;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
    p345Hud=false;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(896)&&!GuestCofAdmitted(897)&&weaponMask.value==1);
    Press();CreateMove(.01F,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));Release();p345Hud=true;
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(897));
    p345Muzzle=false;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(896)&&!GuestCofAdmitted(897));
    p345Muzzle=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(897));
    resources.weapons|=guest::cof::PistolCapability(guest::cof::Pistol::Revolver);
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(896)&&GuestCofAdmitted(897)&&GuestCofAdmitted(898)&&weaponMask.value==7);
    current.id=898;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
    revolverHud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(898)&&GuestCofAdmitted(897)&&weaponMask.value==3);
    Press();CreateMove(.01F,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));Release();
    revolverHud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(898));
    revolverMuzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(898)&&weaponMask.value==3);
    revolverMuzzle=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(898)&&weaponMask.value==7);
    resources.weapons|=guest::cof::PistolCapability(guest::cof::Pistol::VP70);
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(899)&&GuestCofAdmitted(898)&&weaponMask.value==15);
    current.id=899;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
    vp70Hud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(899)&&GuestCofAdmitted(898)&&weaponMask.value==7);
    Press();CreateMove(.01F,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));Release();
    vp70Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(899));
    vp70Muzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(899)&&weaponMask.value==7);
    vp70Muzzle=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(899)&&weaponMask.value==15);
    using guest::cof::ParsePistolAdmission;
    CHECK(ParsePistolAdmission("1","").weapons==1&&ParsePistolAdmission("1","3").weapons==3);
    CHECK(ParsePistolAdmission("0","3").weapons==0&&ParsePistolAdmission("1","0").weapons==0);
    for(const char* invalid:{"-1","+3"," 3","3 ","3x","16","4294967296"})CHECK(ParsePistolAdmission("1",invalid).weapons==0);
    CHECK(!GuestCofAdmitted(900)&&weaponMask.value==15); // Resources are still absent.
    resources.weapons|=guest::cof::FirearmCapability(guest::cof::Firearm::TMP);
    GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(900)&&weaponMask.value==31);
    current.id=900;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
    tmpHud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(900)&&GuestCofAdmitted(899)&&weaponMask.value==15);
    tmpHud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(900)&&weaponMask.value==31);
    tmpMuzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(900)&&weaponMask.value==15);
    tmpMuzzle=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(900)&&weaponMask.value==31);
    resources.weapons|=guest::cof::FirearmCapability(guest::cof::Firearm::MP5);
    const auto beforeMP5Menu=menuLoads;GuestCofAdmissionFrame();
    CHECK(!GuestCofAdmitted(901)&&GuestCofAdmitted(900)&&weaponMask.value==31&&menuLoads==beforeMP5Menu);
    current.id=901;Press();CreateMove(.01F,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));Release();
    if(catalog.weapons.contains("weapon_cof_mp5"))
    {
        const auto& mp5=catalog.weapons.at("weapon_cof_mp5");
        CHECK(mp5.title=="客串武器·MP5A3"&&mp5.ammoFrames[0].empty()&&!mp5.ammo.empty());
        CHECK(buy.images.at(901).width==170&&buy.images.at(901).height==80&&
            buy.weaponInfo.items.at(901).description.find("冲锋枪")!=std::string::npos);
        const auto* item=csol::buy::Find(901);CHECK(item&&item->category==3&&item->extendedPrice==2400);
        mp5Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(901)&&weaponMask.value==63);
        current.id=901;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
        mp5Hud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(901)&&GuestCofAdmitted(900)&&weaponMask.value==31);
    }
    resources.weapons|=guest::cof::FirearmCapability(guest::cof::Firearm::M16);
    const auto beforeM16Menu=menuLoads;GuestCofAdmissionFrame();
    CHECK(!GuestCofAdmitted(902)&&GuestCofAdmitted(900)&&weaponMask.value==31&&menuLoads==beforeM16Menu);
    current.id=902;Press();CreateMove(.01F,&chain,1);CHECK(!(chain.buttons&IN_CANCEL));Release();
    if(catalog.weapons.contains("weapon_cof_m16"))
    {
        const auto& m16=catalog.weapons.at("weapon_cof_m16");
        CHECK(m16.title=="客串武器·M16"&&m16.ammoFrames[0]==m16.ammo&&!m16.ammoFrames[1].empty()&&m16.ammoFrames[2].empty());
        const auto dependencies=csol::hud_style::ResourcePaths(catalog);
        CHECK(std::find(dependencies.begin(),dependencies.end(),m16.ammoFrames[1])!=dependencies.end());
        CHECK(buy.images.at(902).width==170&&buy.images.at(902).height==80&&
            buy.weaponInfo.items.at(902).description.find("突击步枪")!=std::string::npos);
        const auto* item=csol::buy::Find(902);CHECK(item&&item->category==4&&item->extendedPrice==3100);
        mp5Hud=m16Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(902)&&weaponMask.value==127);
        Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
        m16Muzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(902)&&GuestCofAdmitted(901)&&weaponMask.value==63);
        m16Muzzle=true;m16Hud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(902)&&GuestCofAdmitted(901)&&weaponMask.value==63);
        m16Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(902)&&weaponMask.value==127);
        resources.weapons&=~guest::cof::FirearmCapability(guest::cof::Firearm::M16);GuestCofAdmissionFrame();
        CHECK(!GuestCofAdmitted(902)&&GuestCofAdmitted(901)&&weaponMask.value==63);
    }
    resources.weapons|=guest::cof::FirearmCapability(guest::cof::Firearm::G43);
    GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(903)&&GuestCofAdmitted(900));
    if(catalog.weapons.contains("weapon_cof_g43"))
    {
        const auto& g43=catalog.weapons.at("weapon_cof_g43");
        CHECK(g43.title=="客串武器·G43"&&g43.ammoFrames[0].empty()&&!g43.ammo.empty());
        CHECK(buy.images.at(903).width==170&&buy.images.at(903).height==80&&
            buy.weaponInfo.items.at(903).description.find("半自动步枪")!=std::string::npos);
        const auto* item=csol::buy::Find(903);CHECK(item&&item->category==4&&item->extendedPrice==3500);
        g43Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(903)&&GuestCofAdmitted(901));
        current.id=903;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
        g43Muzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(903)&&GuestCofAdmitted(901));
        g43Muzzle=true;g43Hud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(903));
        g43Hud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(903));
        resources.weapons&=~guest::cof::FirearmCapability(guest::cof::Firearm::G43);GuestCofAdmissionFrame();
        CHECK(!GuestCofAdmitted(903)&&GuestCofAdmitted(901));
    }
    resources.weapons|=guest::cof::FirearmCapability(guest::cof::Firearm::Shotgun);
    const auto beforeShotgunMenu=menuLoads;GuestCofAdmissionFrame();
    CHECK(!GuestCofAdmitted(904)&&menuLoads==beforeShotgunMenu);
    if(catalog.weapons.contains("weapon_cof_shotgun"))
    {
        const auto& shotgun=catalog.weapons.at("weapon_cof_shotgun");
        CHECK(shotgun.title=="客串武器·Remington 870"&&shotgun.ammoFrames[0].empty()&&!shotgun.ammo.empty());
        CHECK(buy.images.at(904).width==170&&buy.images.at(904).height==80&&
            buy.weaponInfo.items.at(904).description.find("逐发装填")!=std::string::npos);
        const auto* item=csol::buy::Find(904);CHECK(item&&item->category==2&&item->extendedPrice==3000);
        shotgunHud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(904)&&GuestCofAdmitted(901));
        current.id=904;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
        shotgunMuzzle=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(904)&&GuestCofAdmitted(901));
        shotgunMuzzle=true;shotgunHud=false;GuestCofAdmissionFrame();CHECK(!GuestCofAdmitted(904));
        shotgunHud=true;GuestCofAdmissionFrame();CHECK(GuestCofAdmitted(904));
        resources.weapons&=~guest::cof::FirearmCapability(guest::cof::Firearm::Shotgun);GuestCofAdmissionFrame();
        CHECK(!GuestCofAdmitted(904)&&GuestCofAdmitted(901));
    }
    if(catalog.weapons.contains("weapon_cof_famas"))
    {
        const auto& famas=catalog.weapons.at("weapon_cof_famas");
        CHECK(famas.title=="客串武器·FAMAS G2" && !famas.ammo.empty() && famas.ammoFrames[0].empty());
        const auto* item=csol::buy::Find(905);
        CHECK(item && item->category==4 && item->extendedPrice==3200 && buy.images.at(905).width==170);
        mp5Hud=m16Hud=g43Hud=shotgunHud=famasHud=true;
        resources.weapons=guest::cof::FirearmCapabilityMask;
        GuestCofAdmissionFrame();
        CHECK(weaponMask.value==511 && extensionMask.value==1 && GuestCofAdmitted(905));
        for(int id=896;id<=905;++id)CHECK(GuestCofAdmitted(id));
        CHECK(guest::cof::ParseFirearmAdmission("1","511","1").weapons==1023);
        CHECK(guest::cof::ParseFirearmAdmission("1","511").weapons==511);
        CHECK(guest::cof::ParseFirearmAdmission("1","511","bad").weapons==511);
        CHECK(guest::cof::ParseFirearmAdmission("1","1023","1").weapons==0);
        CHECK(guest::cof::ParseFirearmAdmission("0","511","1").weapons==0);
        current.id=905;Press();CreateMove(.01F,&chain,1);CHECK(chain.buttons&IN_CANCEL);Release();
        famasHud=false;GuestCofAdmissionFrame();
        CHECK(!GuestCofAdmitted(905) && weaponMask.value==511 && extensionMask.value==0);
        for(int id=896;id<=904;++id)CHECK(GuestCofAdmitted(id));
    }
    generation=0;CHECK(!GuestCofAdmitted());GuestCofAdmissionFrame();CHECK(protocol.value==0&&weaponMask.value==0&&extensionMask.value==0);
    ShutdownGuestCofAdmission();ShutdownGuestCofInput();
    CHECK(mountMoves>0);
    std::printf("host presentation/input/admission checks=%u failures=%u; controlled services; live=no performance=no\n",checks,failures);
    return failures?1:0;
}
