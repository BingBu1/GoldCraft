// SPDX-License-Identifier: GPL-3.0-or-later
// Unlike the drawing fixture, acquire every interface through the real Init.
#include "interface_fixture.hpp"
#define GetModuleHandleA interface_fixture::Module
#define CSOL_INTERFACE_TEST
#define main HudStyleDrawingRegression
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
int main()
{
    using namespace csol::client;using namespace fixture;
    Configure();SetupResources();surface=nullptr;schemes=nullptr;extension=nullptr;titleFont=0;
    apiData.GetEngineFactory=[](){return interface_fixture::EngineFactory;};
    apiData.GetPluginModuleHandleByBaseFileName=[](const char* name)->HINTERFACEMODULE{
        return std::strcmp(name,"VGUI2Extension")==0?interface_fixture::ExtensionModule():nullptr;};
    for(const char* name:{"Health","Battery","Money","ResetHUD","InitHUD"})messages[name].function=Previous;
    InitializeHudStyle();
    Check(surface==&capture&&schemes==&schemeManager&&extension==&interface_fixture::extension,
        "full Init acquires surface from engine and scheme from loaded vgui2");
    Check(interface_fixture::engineSchemeRequests==0&&interface_fixture::vguiSchemeRequests==1,
        "scheme never requested from engine factory");
    Check(interface_fixture::extension.registrations==1,"actual overlay registers once");
    HudStyleFrame();Seed();SendMoney(7500);Draw();
    Check(loaded&&titleFont==scheme.font,"real preparation restores original EngineFont");
    Check(HudStyleVitalsVisible()&&HudStyleWeaponVisible(),"character and right weapon HUD become visible");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==portrait.id;}),
        "character portrait reaches real Paint");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==weaponImage.id;}),
        "weapon silhouette reaches real Paint");
    state.secondary=3;Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==secondaryImage.id;}),
        "secondary ammunition reaches real Paint with initialized scheme");
    const auto acquisitions=interface_fixture::vguiSchemeRequests;
    InitializeHudStyle();HudStyleFrame();Seed();Draw();
    Check(interface_fixture::vguiSchemeRequests==acquisitions&&interface_fixture::extension.registrations==1,
        "repeated Init neither reacquires active interfaces nor duplicates overlay");
    privateFont=nullptr;ShutdownHudStyle();
    Check(!surface&&!schemes&&!extension&&capture.live.empty(),"shutdown releases full initialized HUD");
    Check(interface_fixture::extension.unregistrations==1,"shutdown removes registered overlay");
    std::cout<<"hud_interface checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
