// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/wall_status.hpp"
#include "../../native/csol/wall_status.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include "surface_stub.hpp"
#include <IVGUI2Extension.h>
#include <source_location>

namespace fixture
{
std::map<std::string, pfnUserMsgHook> receivers;
int checks{}, forwarded{}, draws{}, lastX{}, lastY{};
std::wstring last;
IVGUI2Extension_BaseUICallbacks* overlay{};
int registrations{},unregistrations{},pushes{},pops{},flushes{};
float hud = 1, center = 1;
void Check(bool value, const std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("wall status check line " + std::to_string(where.line()));
}
int Previous(const char*, int, void*) { ++forwarded; return 73; }
int Send(const char* name, std::vector<std::byte> bytes)
{ return receivers.at(name)(name, int(bytes.size()), bytes.data()); }
std::vector<std::byte> Packet(int index, std::int32_t value)
{
    std::vector<std::byte> bytes{std::byte(index)};
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(std::byte(bits >> (8 * i)));
    return bytes;
}
void Wall(std::int32_t health = 25000)
{ Send("StatusValue", Packet(0, 3)); Send("StatusValue", Packet(1, health)); }
}
namespace fixture
{
struct Surface final:vgui::TestSurface {
 vgui::VPANEL GetEmbeddedPanel()override{return 1;}
 void GetScreenSize(int& w,int& h)override{w=1280;h=720;}
 vgui::HFont CreateFont()override{return 7;}
 bool AddGlyphSetToFont(vgui::HFont,const char* name,int height,int,int,int,int,int,int)override{Check(std::string(name)=="Microsoft YaHei" && height==16);return true;}
 void GetTextSize(vgui::HFont,const wchar_t*,int& w,int& h)override{w=160;h=16;}
 void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
 void PopMakeCurrent(vgui::VPANEL)override{++pops;}
 void DrawSetTextColor(int r,int g,int b,int a)override{Check(r==255 && g==178 && b==0 && a==255);}
 void DrawSetTextPos(int x,int y)override{lastX=x;lastY=y;}
 void DrawPrintText(const wchar_t* text,int len)override{++draws;last.assign(text,len);}
 void DrawFlushText()override{++flushes;}
} surface;
struct Extension final:IVGUI2Extension {
 void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override{overlay=p;++registrations;}
 void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks*)override{overlay=nullptr;++unregistrations;}
 void RegisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
 void RegisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
 void RegisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
 void RegisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
 void RegisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
 void RegisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
 void RegisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}
 void UnregisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
 void UnregisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
 void UnregisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
 void UnregisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
 void UnregisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
 void UnregisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
 void UnregisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}

 const char* GetBaseDirectory()const override{return "";}
 const char* GetCurrentLanguage()const override{return "schinese";}
} extension;
IBaseInterface* SurfaceFactory(const char*,int*){return &surface;}
IBaseInterface* ExtensionFactory(const char*,int*){return &extension;}
void Draw(int intermission){csol::client::DrawWallStatus(intermission);if(overlay){int n{};VGUI2Extension_CallbackContext c;c.IsPost=true;overlay->Paint(n,n,n,n,&c);}}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return fixture::ExtensionFactory;}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession() {}
}
int main()
{
    using namespace fixture;
    using namespace csol::client;
    try
    {
        metahook_api_t hooks{}; api = &hooks;
        hooks.GetEngineFactory=[]()->CreateInterfaceFn{return SurfaceFactory;};
        hooks.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return reinterpret_cast<HINTERFACEMODULE>(1);};
        hooks.HookUserMsg = [](const char* name, pfnUserMsgHook callback)->pfnUserMsgHook {
            const auto old = receivers.contains(name) ? receivers[name] : Previous;
            receivers[name] = callback; return old;
        };
        engine.pfnGetCvarFloat = [](const char* name)->float { return std::strcmp(name, "hud_draw") ? center : hud; };
        engine.pfnGetScreenInfo = [](SCREENINFO* s)->int { s->iWidth = 1280; s->iHeight = 720; return 1; };
        engine.pfnDrawConsoleStringLen = [](const char*, int* w, int* h) { *w = 160; *h = 16; };
        engine.pfnDrawConsoleString = [](int,int,char*)->int { Check(false);return 0; };
        InitializeWallStatus(); InitializeWallStatus();Check(registrations==1);
        view.health = 100;
        Wall(); Draw(0); Check(draws == 1 && last == L"墙体耐久: 25000" && lastX == 560 && lastY == 376);
        Wall(1000000); Draw(0); Check(last == L"墙体耐久: 1000000");
        center = 0; Draw(0); Check(lastX == 10 && lastY == 463);
        auto before = draws; center = std::numeric_limits<float>::infinity(); Draw(0);
        Check(draws == before + 1 && lastY == 463); center = 1;
        before = forwarded;
        const std::vector<std::byte> legacy{std::byte{1}, std::byte{2}, std::byte{0}};
        Check(Send("StatusValue", legacy) == 73 && forwarded == before + 1);
        before = draws; Draw(0); Check(draws == before);
        for (int index : {8, 255}) Check(Send("StatusValue", Packet(index, 3)) == 1);
        for (int type : {-1, 14, std::numeric_limits<int>::max()}) Check(Send("StatusValue", Packet(0, type)) == 1);
        for (int size : {0, 1, 2, 4, 6, 20}) Check(Send("StatusValue", std::vector<std::byte>(size)) == 1);
        Check(forwarded == 1);
        for (int type = 0; type <= 13; ++type)
        {
            Wall(); Send("StatusValue", Packet(0, type));
            if (type == 3) Send("StatusValue", Packet(1, 1));
            before = draws; Draw(0); Check(draws == before + (type == 3));
        }
        for (int health : {0, -1, std::numeric_limits<int>::min()})
        { Wall(health); before = draws; Draw(0); Check(draws == before); }
        Wall(); before = draws;
        hud = 0; Draw(0); hud = 1; Draw(1);
        view.health = 0; Draw(0); view.health = 100;
        view.spectator = 1; Draw(0); view.spectator = 0;
        Send("HideWeapon", {std::byte{4}}); Draw(0); Check(draws == before);
        Send("HideWeapon", {std::byte{0}}); Draw(0); Check(draws == before + 1);
        for (const char* name : {"ResetHUD", "InitHUD"})
        {
            Wall(); before = draws; Check(Send(name, {std::byte{0}}) == 73);
            Draw(0); Check(draws == before);
        }
        Wall(); ResetWallStatus(); before = draws; Draw(0); Check(draws == before);
        VGUI2Extension_CallbackContext beforeUI,afterUI;afterUI.IsPost=true;
        Wall();before=draws;overlay->ShowConsole(&beforeUI);Draw(0);Check(draws==before);
        overlay->HideConsole(&afterUI);Draw(0);Check(draws==before+1);
        before=draws;overlay->ActivateGameUI(&beforeUI);Draw(0);Check(draws==before);
        overlay->HideGameUI(&afterUI);Draw(0);Check(draws==before+1);
        Check(pushes==pops && pops==flushes && flushes==draws);
        before=draws;Wall(); ShutdownWallStatus(); Draw(0); Check(draws == before && unregistrations==1);
        std::cout << "wall status production consumer checks: " << checks << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
