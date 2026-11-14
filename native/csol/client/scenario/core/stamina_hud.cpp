// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../resource_refresh.hpp"
#include "../../user_message_chain.hpp"
#include "../../overlay_layers.hpp"
#include "../../menu_font.hpp"
#include "../../buy_menu.hpp"
#include "../../characters.hpp"
#include "../../hud_style.hpp"
#include "scenario_map_vote.hpp"
#include "stamina_hud.hpp"
#include "../../../scenario/custom/stamina_snapshot.hpp"
#include "../../../scenario/custom/action_hud_layout.hpp"
#include "../../../scenario/core/scenario_upgrades.hpp"
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>
#include <cwchar>
#include <ostream>

namespace csol::client
{
namespace
{
namespace custom=scenario::custom;
custom::StaminaPresentation state;
custom::StaminaSnapshot display;
custom::StaminaHudLayout layout;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
MenuFontCache fonts;
vgui::HFont font{};
HANDLE privateFont{};
pfnUserMsgHook previous{},previousInit{},previousHide{};
cvar_t* hudDraw{};
DWORD ownerThread{};
ULONGLONG retryAfter{};
bool registered{},requested{},gameUI{},console{},prepared{},dirty{true};
int hidden{},width{},height{},textWidth{},textHeight{},fill{};
float styleScale{};
std::array<wchar_t,80> label{};
std::uint64_t accepted{},rejected{},draws{};
bool Owner(){return ownerThread&&ownerThread==GetCurrentThreadId();}
void Prepare(int w,int h)
{
    const float currentScale=HudStyleScale();
    if(!surface||!ResourceReadsAllowed()||(prepared&&w==width&&h==height&&currentScale==styleScale)||GetTickCount64()<retryAfter)return;
    if(w!=width||h!=height||currentScale!=styleScale)prepared=false;
    const auto next=custom::StaminaLayout(w,h,currentScale);if(!next.width)return;
    retryAfter=GetTickCount64()+5000;
    try
    {
        if(!privateFont)
        {
            const auto bytes=ReadResource(upgrades::FontPath);DWORD count{};
            privateFont=RegisterResourceFont(privateFont,upgrades::FontPath,"DFYuanW9-GB",bytes,&count);
            if(!privateFont)throw std::runtime_error("cannot register stamina HUD font");
        }
        fonts.InvalidateFailures();
        font=fonts.Get(*surface,"DFYuanW9-GB",next.fontHeight,0,vgui::FONTFLAG_ANTIALIAS);
        if(!font)throw std::runtime_error("cannot prepare stamina HUD font");
        layout=next;width=w;height=h;styleScale=currentScale;prepared=true;dirty=true;retryAfter=0;
    }
    catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: stamina HUD: %s\n",error.what());}
}
void Paint()
{
    if(!requested)return;requested=false;
    if(!prepared||!display.visible||gameUI||console||view.health<=0||view.spectator||
        BuyMenuVisible()||CharacterMenuVisible()||ScenarioMapVoteVisible()||(hidden&4)||(hudDraw&&hudDraw->value==0))return;
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    const auto& p=layout;
    surface->PushMakeCurrent(root,false);
    surface->DrawSetColor(13,19,28,210);surface->DrawFilledRect(p.x,p.y,p.x+p.width,p.y+p.height);
    surface->DrawSetTextFont(font);
    surface->DrawSetTextColor(display.exhausted?245:225,display.exhausted?110:237,display.exhausted?107:248,255);
    surface->DrawSetTextPos(p.x+p.padding,p.y+p.padding/2);surface->DrawPrintText(label.data(),int(std::wcslen(label.data())));
    const int x=p.x+p.padding,y=p.y+p.height-p.padding-p.barHeight,right=p.x+p.width-p.padding;
    surface->DrawSetColor(38,48,61,255);surface->DrawFilledRect(x,y,right,y+p.barHeight);
    if(fill)
    {
        if(display.exhausted)surface->DrawSetColor(214,66,64,255);
        else if(display.infinite)surface->DrawSetColor(128,210,250,255);
        else surface->DrawSetColor(96,193,221,255);
        surface->DrawFilledRect(x,y,x+fill,y+p.barHeight);
    }
    surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 23;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override
    {const auto factory=api?api->GetEngineFactory():nullptr;surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;}
    void Shutdown()override{ResetStaminaHud();InvalidateStaminaHudResources();fonts.Invalidate();surface=nullptr;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override
    {if(context->IsPost)QueueOverlayPaint(OverlayLayer::Stamina,csol::client::Paint);}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
}overlay;
int Receive(const char* name,int size,void* data)
{
    if(Owner())
    {
        SynchronizeSession();
        if(!data||size<0||!state.Receive({static_cast<const std::byte*>(data),std::size_t(size)}))++rejected;
        else{++accepted;dirty=true;}
    }
    return previous?previous(name,size,data):1;
}
int Init(const char* name,int size,void* data)
{ResetStaminaHud();return previousInit?previousInit(name,size,data):1;}
int Hide(const char* name,int size,void* data)
{if(Owner()&&data&&size==1)hidden=*static_cast<unsigned char*>(data);return previousHide?previousHide(name,size,data):1;}
}
void InitializeStaminaHud()
{
    if(ownerThread&&!Owner())return;ownerThread=GetCurrentThreadId();
    InstallChainedUserMessage(custom::StaminaMessage,Receive,previous);
    InstallChainedUserMessage("InitHUD",Init,previousInit);InstallChainedUserMessage("HideWeapon",Hide,previousHide);
    hudDraw=engine.pfnGetCvarPointer?engine.pfnGetCvarPointer("hud_draw"):nullptr;
    if(!extension)
    {
        const auto factory=api->GetEngineFactory();surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
        const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto create=module?Sys_GetFactory(module):nullptr;
        extension=create?static_cast<IVGUI2Extension*>(create(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    }
    if(extension&&surface&&!registered){extension->RegisterBaseUICallbacks(&overlay);registered=true;}
}
void StaminaHudFrame()
{
    if(!Owner())return;
    const auto value=state.Sample();if(!value||!value->visible){display={};requested=false;return;}
    display=*value;int w{},h{};if(surface)surface->GetScreenSize(w,h);Prepare(w,h);
    if(!prepared||!dirty)return;dirty=false;
    if(display.infinite)std::swprintf(label.data(),label.size(),L"体力  无限");
    else std::swprintf(label.data(),label.size(),L"体力  %.0f / %.0f%ls",double(display.amount),double(display.maximum),display.exhausted?L"  恢复中":L"");
    surface->GetTextSize(font,label.data(),textWidth,textHeight);
    fill=int(float(layout.width-2*layout.padding)*(display.infinite?1.f:std::clamp(display.amount/display.maximum,0.f,1.f)));
}
void DrawStaminaHud(int intermission){requested=Owner()&&!intermission&&prepared&&display.visible;}
void ResetStaminaHud(){if(!Owner())return;state.Reset();display={};requested=false;hidden=0;dirty=true;}
void InvalidateStaminaHudResources(){if(!Owner())return;font=0;prepared=false;dirty=true;width=height=0;retryAfter=0;requested=false;}
void ShutdownStaminaHud()
{
    if(!Owner())return;ResetStaminaHud();InvalidateStaminaHudResources();
    if(extension&&registered)extension->UnregisterBaseUICallbacks(&overlay);
    if(privateFont)RemoveFontMemResourceEx(privateFont);fonts.Invalidate();
    privateFont=nullptr;surface=nullptr;extension=nullptr;registered=false;hudDraw=nullptr;gameUI=console=false;
}
void StaminaHudDiagnostics(std::ostream& out)
{out<<"staminaHud life="<<display.life<<" revision="<<display.revision<<" visible="<<display.visible<<" amount="<<display.amount<<" maximum="<<display.maximum<<" infinite="<<display.infinite<<" exhausted="<<display.exhausted<<" accepted="<<accepted<<" rejected="<<rejected<<" draws="<<draws<<'\n';}
}
