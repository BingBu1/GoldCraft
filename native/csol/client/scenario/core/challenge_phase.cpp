// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../resource_refresh.hpp"
#include "../../user_message_chain.hpp"
#include "../../overlay_layers.hpp"
#include "../../menu_font.hpp"
#include "../../buy_menu.hpp"
#include "../../characters.hpp"
#include "scenario_map_vote.hpp"
#include "challenge_phase.hpp"
#include "../../../scenario/custom/phase_snapshot.hpp"
#include "../../../scenario/custom/phase_art.hpp"
#include "../../../image_codec.hpp"
#include "../../../scenario/core/scenario_upgrades.hpp"
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>
#include <algorithm>
#include <cwchar>
#include <ostream>

namespace csol::client
{
namespace
{
namespace custom=scenario::custom;
custom::PhasePresentation state;
custom::PhaseSnapshot display;
custom::PhaseHudLayout layout;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
MenuFontCache fonts;
vgui::HFont font{};
HANDLE privateFont{};
pfnUserMsgHook previous{},previousInit{},previousHide{};
cvar_t* hudDraw{};
std::array<int,2> textures{};
int nightTexture{};
unsigned nightFrame{};
ULONGLONG animationStart{};
std::array<int,3> labelWidth{},labelHeight{};
std::array<wchar_t,32> timeText{};
int timeWidth{},timeHeight{},seconds=-1,screenWidth{},screenHeight{};
custom::Phase lastPhase{custom::Phase::Ended};
bool lastPreparing{},prepared{},requested{},known{},gameUI{},console{},registered{};
int hidden{};
DWORD ownerThread{};
ULONGLONG retryAfter{};
std::uint64_t accepted{},rejected{},draws{};
constexpr std::array<const wchar_t*,3> Names{L"正午",L"黄昏",L"永夜"};
bool Owner(){return ownerThread&&ownerThread==GetCurrentThreadId();}
double Now(){return double(GetTickCount64())*.001;}
void Release()
{
    if(surface)for(const int texture:textures)if(texture)surface->DeleteTextureByID(texture);
    if(surface&&nightTexture)surface->DeleteTextureByID(nightTexture);
    nightTexture=0;nightFrame=0;animationStart=0;
    textures={};prepared=false;font=0;screenWidth=screenHeight=0;seconds=-1;lastPhase=custom::Phase::Ended;
    retryAfter=0;requested=false;
}
void Prepare(int width,int height)
{
    if(!surface||!ResourceReadsAllowed())return;
    const auto next=custom::PhaseLayout(width,height);
    if(!next.width)return;
    if(prepared&&width==screenWidth&&height==screenHeight)return;
    if(width!=screenWidth||height!=screenHeight)prepared=false;
    if(GetTickCount64()<retryAfter)return;
    retryAfter=GetTickCount64()+5000;
    try
    {
        for(unsigned i=0;i<textures.size();++i)if(!textures[i])
        {
            const auto pixels=custom::MakePhaseIcon(custom::Phase(i));
            const int texture=surface->CreateNewTextureID(true);
            if(!texture)throw std::runtime_error("cannot create challenge phase icon");
            surface->DrawSetTextureRGBA(texture,pixels.data(),custom::PhaseIconSize,custom::PhaseIconSize,1,true);
            textures[i]=texture;
        }
        if(!nightTexture)
        {
            const auto image=DecodeTGA(ReadResource(custom::NightAnimationAtlas));
            if(image.Width!=custom::NightAnimationWidth||image.Height!=custom::NightAnimationHeight)
                throw std::runtime_error("challenge night animation atlas dimensions changed");
            const int texture=surface->CreateNewTextureID(true);
            if(!texture)throw std::runtime_error("cannot create challenge night animation");
            surface->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);
            nightTexture=texture;
        }
        if(!privateFont)
        {
            const auto bytes=ReadResource(upgrades::FontPath);DWORD count{};
            privateFont=RegisterResourceFont(privateFont,upgrades::FontPath,"DFYuanW9-GB",bytes,&count);
            if(!privateFont)throw std::runtime_error("cannot register challenge phase font");
        }
        fonts.InvalidateFailures();
        const auto preparedFont=fonts.Get(*surface,"DFYuanW9-GB",next.fontHeight,0,vgui::FONTFLAG_ANTIALIAS);
        if(!preparedFont)throw std::runtime_error("cannot configure challenge phase font");
        for(unsigned i=0;i<Names.size();++i)surface->GetTextSize(preparedFont,Names[i],labelWidth[i],labelHeight[i]);
        font=preparedFont;layout=next;screenWidth=width;screenHeight=height;prepared=true;seconds=-1;retryAfter=0;
    }
    catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: challenge phase: %s\n",error.what());}
}
void Text(const wchar_t* text,int count,int x,int y)
{
    surface->DrawSetTextPos(x,y);surface->DrawPrintText(text,count);
}
void Paint()
{
    if(!requested)return;requested=false;
    if(!prepared||!known||display.phase>=custom::Phase::Ended||gameUI||console||
        BuyMenuVisible()||CharacterMenuVisible()||ScenarioMapVoteVisible()||(hidden&4)||(hudDraw&&hudDraw->value==0))return;
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    const auto& p=layout;const unsigned phase=unsigned(display.phase);
    surface->PushMakeCurrent(root,false);
    surface->DrawSetColor(13,19,28,205);surface->DrawFilledRect(p.x,p.y,p.x+p.width,p.y+p.height);
    surface->DrawSetColor(255,255,255,255);surface->DrawSetTexture(phase==unsigned(custom::Phase::Night)?nightTexture:textures[phase]);
    const int iconX=p.x+p.padding/2,iconY=p.y+(p.height-p.icon)/2;
    if(phase==unsigned(custom::Phase::Night))
    {
        const unsigned col=nightFrame%custom::NightAnimationColumns,row=nightFrame/custom::NightAnimationColumns;
        const float u0=(col*custom::PhaseIconSize+.5f)/custom::NightAnimationWidth;
        const float v0=(row*custom::PhaseIconSize+.5f)/custom::NightAnimationHeight;
        const float u1=((col+1)*custom::PhaseIconSize-.5f)/custom::NightAnimationWidth;
        const float v1=((row+1)*custom::PhaseIconSize-.5f)/custom::NightAnimationHeight;
        vgui::VGuiVertex vertices[]{{iconX,iconY,u0,v0},{iconX+p.icon,iconY,u1,v0},
            {iconX+p.icon,iconY+p.icon,u1,v1},{iconX,iconY+p.icon,u0,v1}};
        surface->DrawTexturedPolygon(vertices,4);
    }
    else surface->DrawTexturedRect(iconX,iconY,iconX+p.icon,iconY+p.icon);
    surface->DrawSetTextFont(font);surface->DrawSetTextColor(239,244,251,255);
    const int textX=iconX+p.icon+p.padding/2;
    Text(Names[phase],2,textX,p.y+p.height/2-labelHeight[phase]);
    surface->DrawSetTextColor(169,188,207,255);
    Text(timeText.data(),int(std::wcslen(timeText.data())),textX,p.y+p.height/2);
    surface->DrawFlushText();surface->PopMakeCurrent(root);++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 22;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override
    {
        const auto factory=api?api->GetEngineFactory():nullptr;
        surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
    }
    void Shutdown()override{ResetChallengePhase();Release();fonts.Invalidate();surface=nullptr;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override
    {if(context->IsPost)QueueOverlayPaint(OverlayLayer::ChallengePhase,csol::client::Paint);}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
}overlay;
int Receive(const char* name,int size,void* bytes)
{
    if(!Owner())return previous?previous(name,size,bytes):1;
    SynchronizeSession();
    if(!bytes||size<0||!state.Receive({static_cast<const std::byte*>(bytes),std::size_t(size)},Now()))++rejected;
    else ++accepted;
    return previous?previous(name,size,bytes):1;
}
int Init(const char* name,int size,void* bytes)
{ResetChallengePhase();return previousInit?previousInit(name,size,bytes):1;}
int Hide(const char* name,int size,void* bytes)
{if(Owner()&&bytes&&size==1)hidden=*static_cast<unsigned char*>(bytes);return previousHide?previousHide(name,size,bytes):1;}
}
void InitializeChallengePhase()
{
    if(ownerThread&&!Owner())return;ownerThread=GetCurrentThreadId();
    InstallChainedUserMessage(custom::PhaseMessage,Receive,previous);
    InstallChainedUserMessage("InitHUD",Init,previousInit);InstallChainedUserMessage("HideWeapon",Hide,previousHide);
    hudDraw=engine.pfnGetCvarPointer?engine.pfnGetCvarPointer("hud_draw"):nullptr;
    if(!extension)
    {
        const auto factory=api->GetEngineFactory();
        surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
        const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");const auto create=module?Sys_GetFactory(module):nullptr;
        extension=create?static_cast<IVGUI2Extension*>(create(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    }
    if(extension&&surface&&!registered){extension->RegisterBaseUICallbacks(&overlay);registered=true;}
}
void ChallengePhaseFrame()
{
    if(!Owner())return;
    const auto value=state.Sample(Now());known=value.has_value();
    if(!value){requested=false;return;}
    display=*value;
    if(display.phase>=custom::Phase::Ended){requested=false;return;}
    int width{},height{};if(surface)surface->GetScreenSize(width,height);Prepare(width,height);
    if(!prepared)return;
    const auto now=GetTickCount64();
    if(display.phase==custom::Phase::Night)
    {
        if(!animationStart)animationStart=now;
        nightFrame=custom::NightFrame(now-animationStart);
    }
    else animationStart=0;
    const int nextSeconds=int(std::ceil(display.remaining));
    if(nextSeconds==seconds&&lastPhase==display.phase&&lastPreparing==display.preparing)return;
    seconds=nextSeconds;lastPhase=display.phase;lastPreparing=display.preparing;
    if(display.preparing)std::swprintf(timeText.data(),timeText.size(),L"准备中");
    else if(display.phase==custom::Phase::Night)std::swprintf(timeText.data(),timeText.size(),L"持续至本局结束");
    else std::swprintf(timeText.data(),timeText.size(),L"%02d:%02d",seconds/60,seconds%60);
    surface->GetTextSize(font,timeText.data(),timeWidth,timeHeight);
}
void ResetChallengePhase(){if(!Owner())return;state.Reset();known=requested=false;hidden=0;seconds=-1;lastPhase=custom::Phase::Ended;animationStart=0;nightFrame=0;}
void ChallengePhaseVideoInit(){if(Owner())Release();}
void InvalidateChallengePhaseResources(){ChallengePhaseVideoInit();}
void DrawChallengePhase(int intermission){requested=Owner()&&!intermission&&prepared&&known&&display.phase<custom::Phase::Ended;}
void ShutdownChallengePhase()
{
    if(!Owner())return;ResetChallengePhase();Release();
    if(extension&&registered)extension->UnregisterBaseUICallbacks(&overlay);
    if(privateFont)RemoveFontMemResourceEx(privateFont);
    fonts.Invalidate();privateFont=nullptr;surface=nullptr;extension=nullptr;registered=false;font=0;hudDraw=nullptr;gameUI=console=false;
}
void ChallengePhaseDiagnostics(std::ostream& out)
{out<<"challengePhase known="<<known<<" phase="<<unsigned(display.phase)<<" preparing="<<display.preparing
    <<" challenge="<<display.challenge<<" revision="<<display.revision<<" remaining="<<display.remaining
    <<" accepted="<<accepted<<" rejected="<<rejected<<" draws="<<draws<<'\n';}
}
