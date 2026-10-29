// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "wall_status.hpp"
#include "../wall_status.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
wallstatus::State state;
pfnUserMsgHook previousStatus{}, previousReset{}, previousInit{}, previousHide{};
int hidden{};
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
vgui::HFont font{};
int fontHeight{};
bool requested{}, gameUI{}, console{};

void PaintWall()
{
    if(!requested)return;
    requested=false;
    if(!surface || gameUI || console || !state.Visible() || (hidden&4) ||
        view.health<=0 || view.spectator || engine.pfnGetCvarFloat("hud_draw")==0)return;
    int screenWidth{},screenHeight{};surface->GetScreenSize(screenWidth,screenHeight);
    const auto root=surface->GetEmbeddedPanel();
    if(screenWidth<=0 || screenHeight<=0 || !root)return;
    // Match the host console line height, but render the original Chinese
    // localization through the same Unicode surface as the character menu.
    int asciiWidth{},height{};engine.pfnDrawConsoleStringLen("M",&asciiWidth,&height);
    height=std::clamp(height,9,64);
    if(!font)font=surface->CreateFont();
    if(!font)return;
    if(fontHeight!=height)
    {
        if(!surface->AddGlyphSetToFont(font,"Microsoft YaHei",height,400,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff))return;
        fontHeight=height;
    }
    wchar_t text[64];std::swprintf(text,std::size(text),L"墙体耐久: %d",state.Health());
    int width{};surface->GetTextSize(font,text,width,height);
    int x=10,y=screenHeight-257;
    const float center=engine.pfnGetCvarFloat("hud_centerid");
    if(std::isfinite(center) && center!=0)
    {
        x=std::max(1,(screenWidth-width)/2);
        const double line=screenHeight/2+double(height)*center;
        if(!std::isfinite(line) || line<0 || line>screenHeight)return;
        y=int(line);
    }
    surface->PushMakeCurrent(root,false);
    surface->DrawSetTextFont(font);surface->DrawSetTextColor(255,178,0,255);
    surface->DrawSetTextPos(x,y);surface->DrawPrintText(text,int(std::wcslen(text)));
    surface->DrawFlushText();surface->PopMakeCurrent(root);
}
class WallOverlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 20;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetWallStatus();surface=nullptr;font=0;fontHeight=0;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)PaintWall();}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
} overlay;

int ReceiveStatus(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size < 0) return 1;
    wallstatus::Field field;
    const auto parsed = wallstatus::Decode({static_cast<const std::byte*>(data), std::size_t(size)}, field);
    if (parsed == wallstatus::ParseResult::Legacy)
    {
        state.Reset();
        return previousStatus ? previousStatus(name, size, data) : 1;
    }
    if (parsed == wallstatus::ParseResult::Extended) state.Apply(field);
    return 1;
}
int ReceiveReset(const char* name, int size, void* data)
{
    ResetWallStatus();
    return previousReset ? previousReset(name, size, data) : 1;
}
int ReceiveInit(const char* name, int size, void* data)
{
    ResetWallStatus();
    return previousInit ? previousInit(name, size, data) : 1;
}
int ReceiveHide(const char* name, int size, void* data)
{
    if (data && size == 1) hidden = *static_cast<unsigned char*>(data);
    return previousHide ? previousHide(name, size, data) : 1;
}
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    const auto old = api->HookUserMsg(name, hook);
    if (old != hook) previous = old;
    if (!old) engine.pfnHookUserMsg(name, hook);
}
}

void InitializeWallStatus()
{
    Install(wallstatus::MessageName, ReceiveStatus, previousStatus);
    Install("ResetHUD", ReceiveReset, previousReset);
    Install("InitHUD", ReceiveInit, previousInit);
    Install("HideWeapon", ReceiveHide, previousHide);
    if(!extension)
    {
        const auto factory=api->GetEngineFactory();
        surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
        const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory=module?Sys_GetFactory(module):nullptr;
        extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
        if(extension && surface)extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetWallStatus() { state.Reset(); hidden = 0; requested=false; }
void ShutdownWallStatus()
{
    ResetWallStatus();
    if(extension && surface)extension->UnregisterBaseUICallbacks(&overlay);
    extension=nullptr;surface=nullptr;font=0;fontHeight=0;gameUI=console=false;
}

void DrawWallStatus(int intermission)
{
    requested=!intermission && state.Visible();
}
}
