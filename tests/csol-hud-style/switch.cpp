// SPDX-License-Identifier: GPL-3.0-or-later
// Focused HUD_Frame -> weapon update -> redraw regression. This includes the
// complete production HUD consumer and uses real MetaHook/VGUI SDK interfaces.
#include "interface_fixture.hpp"
#define GetModuleHandleA interface_fixture::Module
#define CSOL_INTERFACE_TEST
#define main UnusedHudDrawingRegression
#include "client.cpp"
#undef main
#undef GetModuleHandleA

namespace interface_fixture {
IBaseInterface* EngineFactory(const char* name,int*)
{
    if(std::strcmp(name,VGUI_SURFACE_INTERFACE_VERSION)==0)return &fixture::capture;
    if(std::strcmp(name,VGUI_SCHEME_INTERFACE_VERSION)==0)++engineSchemeRequests;
    return nullptr;
}
IBaseInterface* VguiFactory(const char* name,int*)
{
    if(std::strcmp(name,VGUI_SCHEME_INTERFACE_VERSION)==0){++vguiSchemeRequests;return &fixture::schemeManager;}
    return nullptr;
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE module)
{
    if(module==reinterpret_cast<HINTERFACEMODULE>(interface_fixture::VguiModule()))return interface_fixture::VguiFactory;
    if(module==interface_fixture::ExtensionModule())return interface_fixture::ExtensionFactory;
    return nullptr;
}
namespace {
using namespace csol::client;
using namespace fixture;
using Kind=csol::hud_style::AmmoKind;
struct Sample { int id,slot;const char* name;Kind kind;int clip,reserve,total;bool blue; };
constexpr Sample samples[]{
    {197,0,"weapon_k3",Kind::Magazine,100,2000,0,false},
    {144,0,"weapon_cannon",Kind::Total,0,0,20,true},
    {22,0,"weapon_m4a1",Kind::Magazine,30,90,0,false},
    {29,2,"weapon_knife",Kind::None,0,0,0,false}
};
void Select(unsigned index)
{
    const auto& sample=samples[index];definition={};definition.id=sample.id;definition.slot=sample.slot;definition.name=sample.name;
    state={};state.id=sample.id;state.slot=sample.slot;state.kind=sample.kind;
    state.clip=sample.clip;state.reserve=sample.reserve;state.total=sample.total;state.specialAmmo=sample.blue;
}
void CheckCurrent()
{
    Check(HudStyleWeaponVisible(),"same redraw owns new weapon HUD without legacy ammo fallback");
    Check(weaponName==definition.name&&weapon.id==state.id,"selected resources and redraw state refer to the same weapon");
    const auto expected=state.kind==Kind::None?std::string{}:state.kind==Kind::Total?
        std::to_string(state.total):std::to_string(state.clip)+std::to_string(state.reserve);
    Check(WeaponDigits()==expected,"first redraw paints current authoritative ammunition");
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& text){return text.value==weaponTitle;}),
        "new weapon title reaches real Paint");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& quad){return quad.texture==weaponImage.id;}),
        "new weapon image reaches real Paint");
    if(state.kind!=Kind::None){
        const std::array<int,4> color=state.specialAmmo?std::array<int,4>{114,197,255,255}:std::array<int,4>{255,255,255,255};
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& quad){return quad.texture==ammoImage.id&&quad.color==color;}),
            "current ammunition icon uses special blue or ordinary white");
    }
    Check(HudStyleVitalsVisible()&&HudStyleMoneyVisible(),"weapon transition keeps independent portrait/vitals/money ownership");
}
void Setup()
{
    Configure();SetupResources();
    std::string catalogJson=json;
    const auto at=catalogJson.find("],\"portraits\"");
    catalogJson.insert(at,R"(,{"name":"weapon_m4a1","title":"M4A1","weapon":"resource/csol/hud/v1/m4.tga","ammo":"resource/csol/hud/v1/m4_ammo.tga","ammo2":""},{"name":"weapon_knife","title":"Knife","weapon":"resource/csol/hud/v1/knife.tga","ammo":"","ammo2":""})");
    Put(csol::hud_style::CatalogPath,catalogJson);
    files["resource/csol/hud/v1/m4.tga"]=Tga(170,45);
    files["resource/csol/hud/v1/m4_ammo.tga"]=Tga(24,24);
    files["resource/csol/hud/v1/knife.tga"]=Tga(170,45);
    surface=nullptr;schemes=nullptr;extension=nullptr;titleFont=0;
    apiData.GetEngineFactory=[](){return interface_fixture::EngineFactory;};
    apiData.GetPluginModuleHandleByBaseFileName=[](const char* name)->HINTERFACEMODULE{
        return std::strcmp(name,"VGUI2Extension")==0?interface_fixture::ExtensionModule():nullptr;};
    for(const char* name:{"Health","Battery","Money","ResetHUD","InitHUD"})messages[name].function=Previous;
    InitializeHudStyle();Select(0);HudStyleFrame();Seed();SendMoney(7500);Draw();
    Check(loaded&&titleFont==scheme.font,"real Init and Frame prepare the original font and base art");
    CheckCurrent();
}
void Transitions()
{
    for(unsigned from=0;from<4;++from)for(unsigned to=0;to<4;++to)if(from!=to){
        Select(from);HudStyleFrame();
        Check(weaponName==samples[from].name,"Frame prepared prior selection before inter-frame update");
        Select(to);Draw();CheckCurrent();
        const auto before=reads;Draw();CheckCurrent();
        Check(reads==before,"stable redraw does not re-read selected textures");
    }
    Select(0);HudStyleFrame();
    for(unsigned to:{1,2,3,0,1,0,2,0}){Select(to);Draw();CheckCurrent();}
    // Ammunition may also arrive after Frame while the selection stays fixed.
    Select(0);HudStyleFrame();state.clip=7;state.reserve=1993;Draw();CheckCurrent();
    Select(1);HudStyleFrame();state.total=19;Draw();CheckCurrent();
}
void HiddenAndMissing()
{
    for(int hidden:{1,4}){
        Select(0);HudStyleFrame();Select(1);BeginHudStyle(hidden,0);
        Check(!HudStyleWeaponVisible(),"hidden weapon never suppresses native fallback ownership");
        Draw();CheckCurrent();
    }
    Select(0);HudStyleFrame();Select(2);BeginHudStyle(0,1);
    Check(!HudStyleWeaponVisible(),"intermission keeps custom weapon ownership disabled");Draw();CheckCurrent();
    for(bool* gate:{&gameUI,&console}){
        Select(0);HudStyleFrame();Select(1);*gate=true;BeginHudStyle(0,0);
        Check(!HudStyleWeaponVisible(),"menu and console keep ownership disabled during selection update");*gate=false;Draw();CheckCurrent();
    }
    const auto icon=files.at(ammo);
    Select(0);HudStyleFrame();files.erase(ammo);Select(1);Draw();
    Check(!HudStyleWeaponVisible()&&!weaponImage.id&&!ammoImage.id&&WeaponDigits().empty(),
        "genuinely missing ammo icon falls back atomically instead of keeping a stale row");
    Check(HudStyleVitalsVisible()&&HudStyleMoneyVisible(),"missing weapon asset does not disable unrelated HUD panels");
    const auto failedReads=reads;Draw();Check(reads==failedReads,"failed asset read is throttled across repeated redraws");
    Select(0);Draw();CheckCurrent();
    files[ammo]=icon;Select(1);Draw();CheckCurrent();
    Select(0);HudStyleFrame();state.id=999;Draw();
    Check(!HudStyleWeaponVisible()&&!weaponImage.id,"unregistered weapon retains genuine legacy fallback");
    Select(3);Draw();CheckCurrent();
    loaded=false;Select(1);BeginHudStyle(0,0);
    Check(!HudStyleWeaponVisible(),"unavailable base HUD never permanently disables legacy weapon renderer");loaded=true;
    Draw();CheckCurrent();
}
}
int main()
{
    Setup();Transitions();HiddenAndMissing();
    privateFont=nullptr;ShutdownHudStyle();
    Check(!surface&&!schemes&&!extension&&capture.live.empty(),"all textures released after repeated switches");
    std::cout<<"hud_switch checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
