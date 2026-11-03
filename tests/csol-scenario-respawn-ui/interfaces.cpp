// SPDX-License-Identifier: GPL-3.0-or-later
#include "tests/csol-hud-style/interface_fixture.hpp"
#define GetModuleHandleA interface_fixture::Module
#define CSOL_INTERFACE_TEST
#define wmain RespawnMessageRegression
#include "client_main.cpp"
#undef wmain
#undef GetModuleHandleA
#include "tests/csol-hud-style/surface_stub.hpp"

namespace interface_fixture {
struct Surface final:vgui::HudTestSurface
{
    int next{},x{},y{},pushes{},pops{};unsigned textDraws{};
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    void GetScreenSize(int& w,int& h)override{w=1280;h=720;}
    void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
    void PopMakeCurrent(vgui::VPANEL)override{++pops;}
    int CreateNewTextureID(bool)override{return ++next;}
    bool DeleteTextureByID(int)override{return true;}
    void DrawPrintText(const wchar_t*,int)override{++textDraws;}
    void GetTextSize(vgui::HFont,const wchar_t* text,int& w,int& h)override{w=int(std::wcslen(text))*12;h=16;}
} capture;
struct Scheme final:vgui::IScheme
{
    const char* GetResourceString(const char*)override{return "1";}
    vgui::IBorder* GetBorder(const char*)override{return nullptr;}
    vgui::HFont GetFont(const char* name,bool proportional)override
    {fixture::Check(proportional,"original fonts use proportional request");return std::strcmp(name,"EngineFont")==0?7:8;}
    Color GetColor(const char*,Color fallback)override{return fallback;}
} scheme;
struct SchemeManager final:vgui::ISchemeManager
{
    vgui::HScheme LoadSchemeFromFile(const char*,const char*)override{return 1;}
    void ReloadSchemes()override{}
    vgui::HScheme GetDefaultScheme()override{return 1;}
    vgui::HScheme GetScheme(const char*)override{return 1;}
    vgui::IImage* GetImage(const char*,bool)override{return nullptr;}
    vgui::HTexture GetImageID(const char*,bool)override{return 0;}
    vgui::IScheme* GetIScheme(vgui::HScheme handle)override{return handle==1?&scheme:nullptr;}
    void Shutdown(bool)override{}
    int GetProportionalScaledValue(int v)override{return v;}
    int GetProportionalNormalizedValue(int v)override{return v;}
} schemeManager;
IBaseInterface* EngineFactory(const char* name,int*)
{
    if(std::strcmp(name,VGUI_SURFACE_INTERFACE_VERSION)==0)return &capture;
    if(std::strcmp(name,VGUI_SCHEME_INTERFACE_VERSION)==0)++engineSchemeRequests;
    return nullptr;
}
IBaseInterface* VguiFactory(const char* name,int*)
{
    if(std::strcmp(name,VGUI_SCHEME_INTERFACE_VERSION)==0){++vguiSchemeRequests;return &schemeManager;}
    // The center/countdown HUD does not require a native scoreboard tree.
    return nullptr;
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE module)
{
    if(module==reinterpret_cast<HINTERFACEMODULE>(interface_fixture::VguiModule()))return interface_fixture::VguiFactory;
    if(module==interface_fixture::ExtensionModule())return interface_fixture::ExtensionFactory;
    return nullptr;
}
int wmain(int argc,wchar_t** argv)
{
    using namespace fixture;using namespace csol::client;
    if(argc!=2)return 2;common=argv[1];
    metahook_api_t storage{};api=&storage;
    storage.GetEngineFactory=[](){return interface_fixture::EngineFactory;};
    storage.GetPluginModuleHandleByBaseFileName=[](const char*){return interface_fixture::ExtensionModule();};
    storage.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook{
        const auto prior=hooks.contains(name)?hooks.at(name):Previous;hooks[name]=hook;return prior;};
    engine.GetClientTime=[]{return now;};engine.Con_DPrintf=Log;
    engine.pfnGetCvarFloat=[](const char*){return 1.f;};view.health=100;
    InitializeScenarioRespawnUi();
    Check(surface==&interface_fixture::capture&&schemes==&interface_fixture::schemeManager,
        "full respawn Init obtains engine surface and native vgui2 scheme");
    Check(interface_fixture::vguiSchemeRequests==1&&interface_fixture::engineSchemeRequests==0,
        "respawn scheme never requested from engine factory");
    Send("MPToCL",Item(0,0,3));
    ScenarioRespawnUiFrame();
    Check(localized&&centerFont==7&&hintFont==8,"actual Prepare loads localization and original named fonts");
    Check(textures[2]!=0,"actual TGA decoder prepares center background");
    Send("TextMsg",Center("#CSO_ZBS_AutoRespawnWait","30"));
    DrawScenarioRespawnUi(now,0);Paint();
    Check(interface_fixture::capture.textDraws>0,"countdown reaches actual Paint with original font");
    Check(interface_fixture::capture.pushes==interface_fixture::capture.pops,"surface context balanced");
    InitializeScenarioRespawnUi();
    Check(interface_fixture::extension.registrations==1&&interface_fixture::vguiSchemeRequests==1,
        "repeat initialization preserves one overlay and interface owner");
    ShutdownScenarioRespawnUi();
    Check(!surface&&!schemes&&!extension&&interface_fixture::extension.unregistrations==1,
        "shutdown removes initialized respawn overlay");
    std::cout<<"respawn_interface checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
