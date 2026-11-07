// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../overlay_layers.hpp"
#include "../../user_message_chain.hpp"
#include "wall_status.hpp"
#include "wall_upgrade_hud.hpp"
#include "../../../scenario/core/wall_status.hpp"
#include "../../../scenario/core/scenario_localization.hpp"
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
std::wstring upgradeFormat, upgradeMax, statusText;
bool localized{}, localizationReported{};
bool textDirty{true};
ULONGLONG localizationRetry{};
std::uint64_t resourceEpoch{};

void PrepareStatusText()
{
    if (!textDirty) return;
    statusText.clear();
    if (!state.Visible()) return;
    if (!state.Upgrade())
    {
        statusText = L"墙体耐久: " + std::to_wstring(state.Health());
        textDirty = false;
        return;
    }
    if (!localized && GetTickCount64() >= localizationRetry)
    {
        localizationRetry = GetTickCount64() + 5000;
        const auto generation = resourceEpoch;
        try
        {
            const auto bytes = ReadResource("resource/cso_chn.txt");
            if (generation != resourceEpoch) return;
            if (bytes.size() < 2 || bytes.size() % 2 || bytes[0] != std::byte{0xff} || bytes[1] != std::byte{0xfe})
                throw std::runtime_error("wall status localization must be UTF16LE");
            std::u16string text;
            text.reserve(bytes.size() / 2 - 1);
            for (std::size_t at = 2; at < bytes.size(); at += 2)
                text += char16_t(std::to_integer<unsigned>(bytes[at]) |
                    (std::to_integer<unsigned>(bytes[at + 1]) << 8));
            while (!text.empty() && text.back() == u'\0') text.pop_back();
            scenario_hud::LocalizedTokens tokens;
            const std::u16string level = u"CSO_ZBSVER2_StatusBar_WallLevel";
            const std::u16string maximum = u"CSO_ZBSVER2_StatusBar_WallLevelMax";
            std::string error;
            if (!scenario_hud::ReadLocalizedTokens(text, {level, maximum}, tokens, error))
                throw std::runtime_error(error);
            if (!tokens.contains(level) || !tokens.contains(maximum))
                throw std::runtime_error("missing wall status localization");
            const auto& format = tokens.at(level);
            const auto marker = format.find(u"%d");
            if (marker == format.npos || format.find(u"%d", marker + 2) != format.npos)
                throw std::runtime_error("invalid wall level format");
            upgradeFormat.assign(format.begin(), format.end());
            const auto& max = tokens.at(maximum);
            upgradeMax.assign(max.begin(), max.end());
            localized = true;
        }
        catch (const std::exception& error)
        {
            if (!localizationReported) engine.Con_DPrintf("CSOLSupport: wall status: %s\n", error.what());
            localizationReported = true;
        }
    }
    if (!localized || !state.Visible() || !state.Upgrade()) return;
    // Original02AC784C uses signed level<28 and the cached localized text.
    statusText = state.Health() >= 28 ? upgradeMax : upgradeFormat;
    if (state.Health() < 28)
        statusText.replace(statusText.find(L"%d"), 2, std::to_wstring(state.Health()));
    textDirty = false;
}

void PaintWall()
{
    if(!requested)return;
    requested=false;
    if(!surface || gameUI || console || (hidden&4) || engine.pfnGetCvarFloat("hud_draw")==0)return;
    PaintWallUpgradeHud(surface);
    if(!state.Visible() || statusText.empty() || view.health<=0 || view.spectator)return;
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
    int width{};surface->GetTextSize(font,statusText.c_str(),width,height);
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
    surface->DrawSetTextPos(x,y);surface->DrawPrintText(statusText.c_str(),int(statusText.size()));
    surface->DrawFlushText();surface->PopMakeCurrent(root);
}
class WallOverlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 20;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetWallStatus();ShutdownWallUpgradeHud(surface);surface=nullptr;font=0;fontHeight=0;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)QueueOverlayPaint(OverlayLayer::WallStatus,PaintWall);}
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
    if (parsed == wallstatus::ParseResult::Extended) { state.Apply(field); textDirty=true; }
    return 1;
}
int ReceiveReset(const char* name, int size, void* data)
{
    // Native ResetHUD is sent on every spawn. Original wall state is reset
    // by MPToCL9, not this life notification; preserve the server's27/10.
    state.Reset(); hidden=0; requested=false; statusText.clear(); textDirty=true;
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
    InstallChainedUserMessage(name, hook, previous);
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
void ResetWallStatus() { state.Reset(); ResetWallUpgradeHud(); hidden = 0; requested=false; statusText.clear(); textDirty=true; }
void InvalidateWallResources()
{
    ++resourceEpoch;
    InvalidateWallUpgradeHud(surface);
    requested=false; localized=false; localizationReported=false; localizationRetry=0; textDirty=true;
    upgradeFormat.clear(); upgradeMax.clear(); statusText.clear();
}
void ShutdownWallStatus()
{
    ResetWallStatus();
    InvalidateWallResources();
    ShutdownWallUpgradeHud(surface);
    if(extension && surface)extension->UnregisterBaseUICallbacks(&overlay);
    extension=nullptr;surface=nullptr;font=0;fontHeight=0;gameUI=console=false;
}

void DrawWallStatus(int intermission)
{
    PrepareStatusText();
    PrepareWallUpgradeHud(surface);
    requested=!intermission && (state.Visible() || WallUpgradeHudVisible());
}
}
