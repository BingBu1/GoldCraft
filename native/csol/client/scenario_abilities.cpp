// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "overlay_layers.hpp"
#include "scenario_abilities.hpp"
#include "character_skill_input.hpp"
#include "user_message_chain.hpp"
#include "command_chain.hpp"
#include "characters.hpp"
#include "buy_menu.hpp"
#include "weapon_inventory.hpp"
#include "../scenario_abilities.hpp"
#include "../buy_zone_hint.hpp"
#include "../image_codec.hpp"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <ostream>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
abilities::State state;
buy_hint::State buyHint;
std::array<int,3> buyImages{};
vgui::HFont buyFont{};
HANDLE buyFontMemory{};
double buyRetryAt{};
bool buyPrepared{};
CharacterSkillInput activation;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
struct Textures { std::array<int,3> images{};bool attempted{};double retryAt{}; };
std::array<Textures,55> textures;
std::array<int,11> overlays{};
bool overlaysPrepared{},requested{},gameUI{},console{},clockStarted{};
bool dead{true},intermission{};
double overlaysRetryAt{};
int hidden{};
float lastTime{};
pfnUserMsgHook previousZBS{},previousInit{},previousReset{},previousHide{},previousSnapshot{},previousGate{};
void (*previousActivate)(){};
std::uint64_t accepted{},malformed{},draws{};

void ReleaseImages()
{
    if(surface)
    {
        for(const auto& type:textures)for(auto texture:type.images)if(texture)surface->DeleteTextureByID(texture);
        for(auto texture:overlays)if(texture)surface->DeleteTextureByID(texture);
        for(auto texture:buyImages)if(texture)surface->DeleteTextureByID(texture);
    }
    textures={};overlays={};overlaysPrepared=false;overlaysRetryAt=0;
    buyImages={};buyPrepared=false;buyRetryAt=0;
}
void PrepareBuyHint()
{
    if(!surface||!buyHint.Visible()||!engine.GetClientTime)return;
    const double now=engine.GetClientTime();
    if(!std::isfinite(now)||(buyPrepared&&now<buyRetryAt&&now>=buyRetryAt-5))return;
    for(unsigned i=0;i<buyImages.size();++i)if(!buyImages[i])
    {
        try
        {
            const auto image=DecodeTGA(ReadResource(buy_hint::Images[i]));
            if(image.Width!=46||image.Height!=46)throw std::runtime_error("buy zone key image dimensions changed");
            const int texture=surface->CreateNewTextureID(true);
            if(texture)surface->DrawSetTextureRGBA(texture,image.RGBA.data(),46,46,1,true);
            buyImages[i]=texture;
        }
        catch(const std::exception& error)
        {if(!buyPrepared)engine.Con_DPrintf("CSOLSupport: %s: %s\n",buy_hint::Images[i],error.what());}
    }
    if(!buyFontMemory)
    {
        try
        {
            const auto bytes=ReadResource(buy_hint::Font);DWORD count{};
            buyFontMemory=AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()),DWORD(bytes.size()),nullptr,&count);
        }
        catch(const std::exception& error)
        {if(!buyPrepared)engine.Con_DPrintf("CSOLSupport: buy zone font: %s\n",error.what());}
    }
    if(buyFontMemory&&!buyFont)
    {
        const auto font=surface->CreateFont();
        if(font&&surface->AddGlyphSetToFont(font,"DFYuanW9-GB",14,300,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff))buyFont=font;
    }
    buyPrepared=true;buyRetryAt=now+5;
}
bool BuyHintVisible()
{
    if(!buyHint.Visible()||!buyFont||BuyMenuVisible()||CharacterMenuVisible()||WeaponTextMenuVisible())return false;
    for(unsigned i=buyHint.Mode();i<buyImages.size();++i)if(!buyImages[i])return false;
    return true;
}
void PaintBuyHint(int width,int height)
{
    const int left=(width-330)/2,top=height-188;
    const unsigned mode=buyHint.Mode();
    for(unsigned i=mode;i<buyImages.size();++i)
    {
        const int cell=left+int(i-mode)*110+int(mode)*55;
        surface->DrawSetTexture(buyImages[i]);surface->DrawSetColor(255,255,255,255);
        vgui::VGuiVertex vertices[]{{cell+32,top,0,0},{cell+78,top,1,0},
            {cell+78,top+46,1,1},{cell+32,top+46,0,1}};
        surface->DrawTexturedPolygon(vertices,4);
        int textWidth{},textHeight{};surface->GetTextSize(buyFont,buy_hint::Labels[i],textWidth,textHeight);
        surface->DrawSetTextFont(buyFont);surface->DrawSetTextColor(213,162,71,255);
        // Original Label::a_north: the text begins at y=46 in its cell.
        surface->DrawSetTextPos(cell+(110-textWidth)/2,top+46);
        surface->DrawPrintText(buy_hint::Labels[i],int(std::wcslen(buy_hint::Labels[i])));
    }
    surface->DrawFlushText();
}
int Load(const char* path,bool report=true)
{
    if(!path)return 0;
    try
    {
        const auto image=DecodeTGA(ReadResource(path));
        if(image.Width!=77||image.Height!=75)throw std::runtime_error("scenario skill image dimensions changed");
        const int texture=surface->CreateNewTextureID(true);
        if(texture)surface->DrawSetTextureRGBA(texture,image.RGBA.data(),77,75,1,true);
        return texture;
    }
    catch(const std::exception& error){if(report)engine.Con_DPrintf("CSOLSupport: %s: %s\n",path,error.what());return 0;}
}
void Prepare()
{
    if(!surface||!state.Visible()||!engine.GetClientTime)return;
    const double now=engine.GetClientTime();if(!std::isfinite(now))return;
    const auto* resource=abilities::ResourceFor(state.Type());
    if(!resource)return;
    auto& type=textures[state.Type()-1];
    if(!type.attempted||now>=type.retryAt||now<type.retryAt-5)
    {
        const char* paths[]{resource->background,resource->icon,resource->key};
        for(unsigned i=0;i<3;++i)if(!type.images[i]&&paths[i])type.images[i]=Load(paths[i],!type.attempted);
        type.attempted=true;type.retryAt=now+5;
    }
    if(!overlaysPrepared||now>=overlaysRetryAt||now<overlaysRetryAt-5)
    {
        if(!overlays[0])overlays[0]=Load(abilities::ReadyHighlight,!overlaysPrepared);
        if(!overlays[1])overlays[1]=Load(abilities::NumberHighlight,!overlaysPrepared);
        for(unsigned i=0;i<abilities::NumberImages.size();++i)
            if(!overlays[i+2])overlays[i+2]=Load(abilities::NumberImages[i],!overlaysPrepared);
        overlaysPrepared=true;overlaysRetryAt=now+5;
    }
}
void AdvanceTo(float time)
{
    if(!std::isfinite(time))return;
    // Packet time may be slightly ahead of HUD_Redraw. Session ownership
    // handles actual demo seeks/reconnects; a render correction is no reset.
    if(clockStarted&&time<lastTime)return;
    if(clockStarted)state.Advance(double(time)-double(lastTime));
    lastTime=time;clockStarted=true;
    buyHint.Advance(time);
}
void Paint()
{
    if(!requested)return;requested=false;
    if(!surface||gameUI||console||(hidden&4)||view.health<=0||view.spectator||engine.pfnGetCvarFloat("hud_draw")==0)return;
    int width{},height{};surface->GetScreenSize(width,height);
    // CSO raises this panel only while BuyZoneKeyPanel or RogueLite AbilityKey
    // is actually visible (02850AA3..02850B04). Native StatusIcon/buyzone does
    // not create either panel, including in scenarios with unlimited buying.
    const bool showBuyHint=BuyHintVisible();
    const auto type=state.Type();
    const bool showSkill=abilities::ResourceFor(type)&&textures[type-1].attempted&&
        (!activation.HasGate()||activation.Type()==type);
    const auto frame=showSkill?state.Layout(width,height,showBuyHint):abilities::Frame{};
    if(!frame.count&&!showBuyHint)return;
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    surface->PushMakeCurrent(root,false);
    if(showBuyHint)PaintBuyHint(width,height);
    for(unsigned i=0;i<frame.count;++i)
    {
        const auto& q=frame.quads[i];int texture{};
        if(activation.HasGate())
        {
            if(q.image==abilities::Image::Key&&!activation.Manual())continue;
            if(q.image==abilities::Image::Ready&&!(activation.Flags()&skillgate::Ready))continue;
        }
        switch(q.image)
        {
        case abilities::Image::Background:texture=textures[type-1].images[0];break;
        case abilities::Image::Icon:texture=textures[type-1].images[1];break;
        case abilities::Image::Key:texture=textures[type-1].images[2];break;
        case abilities::Image::Ready:texture=overlays[0];break;
        case abilities::Image::NumberFlash:texture=overlays[1];break;
        case abilities::Image::Number:texture=overlays[unsigned(state.Number())+1];break;
        }
        if(!texture)continue;
        surface->DrawSetTexture(texture);surface->DrawSetColor(255,255,255,q.alpha);
        vgui::VGuiVertex vertices[]{
            {q.x,q.y,0,q.v0},{q.x+q.width,q.y,1,q.v0},
            {q.x+q.width,q.y+q.height,1,q.v1},{q.x,q.y+q.height,0,q.v1}};
        surface->DrawTexturedPolygon(vertices,4);++draws;
    }
    surface->PopMakeCurrent(root);
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 23;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetScenarioAbilities();surface=nullptr;textures={};overlays={};overlaysPrepared=false;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)QueueOverlayPaint(OverlayLayer::ScenarioAbilities,csol::client::Paint);}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
} overlay;
int Receive(const char* name,int size,void* data)
{
    SynchronizeSession();
    if(!data||size<0)return previousZBS?previousZBS(name,size,data):1;
    const auto hint=buyHint.Receive({static_cast<const std::uint8_t*>(data),std::size_t(size)},engine.GetClientTime());
    if(hint==buy_hint::Result::Malformed){++malformed;return 1;}
    if(hint==buy_hint::Result::Accepted)++accepted;
    abilities::Message message;
    const auto result=abilities::Decode({static_cast<const std::byte*>(data),std::size_t(size)},message);
    if(result==abilities::ParseResult::Malformed){++malformed;return 1;}
    if(result==abilities::ParseResult::Accepted)
    {
        const auto now=engine.GetClientTime();AdvanceTo(now);state.Apply(message);
        activation.Observe(message,now);++accepted;
    }
    return previousZBS?previousZBS(name,size,data):1;
}
int Init(const char* name,int size,void* data)
{ResetScenarioAbilities();return previousInit?previousInit(name,size,data):1;}
int Snapshot(const char*,int size,void* data)
{
    SynchronizeSession();
    abilities::Snapshot snapshot;
    if(!data||size<0||abilities::DecodeSnapshot({static_cast<const std::byte*>(data),std::size_t(size)},snapshot)!=abilities::ParseResult::Accepted)
    {++malformed;return 1;}
    const auto time=engine.GetClientTime();
    if(!std::isfinite(time)){++malformed;return 1;}
    state.Restore(snapshot);activation.Restore(snapshot,time);lastTime=time;clockStarted=true;++accepted;
    return 1;
}
int Gate(const char*,int size,void* data)
{
    SynchronizeSession();
    skillgate::Snapshot snapshot;
    if(!data||size<0||!skillgate::Decode({static_cast<const std::uint8_t*>(data),std::size_t(size)},snapshot)||!engine.GetClientTime)
    {++malformed;return 1;}
    const double now=engine.GetClientTime();if(!std::isfinite(now)){++malformed;return 1;}
    const bool becameReady=activation.HasGate()&&activation.Type()==snapshot.subtype&&state.Type()==snapshot.subtype&&
        !(activation.Flags()&skillgate::Ready)&&(snapshot.flags&skillgate::Ready);
    activation.Restore(snapshot,now);
    if(becameReady)state.NotifyReady();
    if(!snapshot.subtype)state.Reset();
    ++accepted;return 1;
}
int Reset(const char* name,int size,void* data)
{
    requested=false;hidden=0;activation.ResetLife();buyHint.Reset();dead=true;
    return previousReset?previousReset(name,size,data):1;
}
int Hide(const char* name,int size,void* data)
{if(data&&size==1)hidden=*static_cast<unsigned char*>(data);return previousHide?previousHide(name,size,data):1;}
void Install(const char* name,pfnUserMsgHook hook,pfnUserMsgHook& previous)
{
    InstallChainedUserMessage(name,hook,previous);
}
void Activate()
{
    if(!TryActivateScenarioAbility()&&previousActivate)previousActivate();
}
}
void InitializeScenarioAbilities()
{
    Install("ZBSVer2",Receive,previousZBS);Install("InitHUD",Init,previousInit);
    Install("ResetHUD",Reset,previousReset);Install("HideWeapon",Hide,previousHide);
    Install("CsoSkState",Snapshot,previousSnapshot);
    Install(skillgate::MessageName,Gate,previousGate);
    if(api->HookCmd)
        InstallChainedCommand("buffclass_skill",Activate,previousActivate,true);
    if(!extension)
    {
        const auto factory=api->GetEngineFactory();
        surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
        const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory=module?Sys_GetFactory(module):nullptr;
        extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
        if(extension&&surface)extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetScenarioAbilities()
{
    state.Reset();activation.Reset();buyHint.Reset();hidden=0;requested=clockStarted=intermission=false;
    // HUD snapshots can arrive before CalcRefdef publishes the new life's
    // health. Do not erase that new gate using the previous life's dead view.
    dead=true;lastTime=0;
}
void ScenarioAbilitiesFrame(){PrepareBuyHint();Prepare();}
bool TryActivateScenarioAbility()
{
    SynchronizeSession();
    if(!activation.OwnsKey())return false;
    const bool gameplay=!gameUI&&!console&&!BuyMenuVisible()&&!CharacterMenuVisible()&&!WeaponTextMenuVisible()&&
        !intermission&&!(hidden&4)&&view.health>0&&!view.spectator&&(!surface||surface->HasFocus());
    if(engine.GetClientTime&&engine.pfnServerCmd&&activation.Request(engine.GetClientTime(),gameplay))
        engine.pfnServerCmd("buffclass_skill\n");
    return true;
}
void ScenarioAbilitiesVideoInit(){ReleaseImages();}
void DrawScenarioAbilities(float time,int inIntermission)
{
    AdvanceTo(time);intermission=inIntermission!=0;
    const bool nowDead=view.health<=0||view.spectator;
    if(nowDead&&!dead){activation.ResetLife();buyHint.Reset();}
    dead=nowDead;requested=!intermission&&(state.Visible()||buyHint.Visible());
}
void ShutdownScenarioAbilities()
{
    ResetScenarioAbilities();ReleaseImages();
    if(buyFontMemory)RemoveFontMemResourceEx(buyFontMemory);
    buyFontMemory=nullptr;buyFont=0;
    if(extension&&surface)extension->UnregisterBaseUICallbacks(&overlay);
    extension=nullptr;surface=nullptr;gameUI=console=false;
}
void ScenarioAbilitiesDiagnostics(std::ostream& out)
{out<<"scenarioAbilities visible="<<state.Visible()<<" type="<<state.Type()<<" progress="<<state.Fraction()
    <<" flashes="<<state.FlashCount()<<" number="<<state.Number()<<" accepted="<<accepted
    <<" rejected="<<malformed<<" draws="<<draws<<" gate="<<activation.HasGate()
    <<" flags="<<activation.Flags()<<" prepare="<<activation.PrepareSeconds(lastTime)
    <<" active="<<activation.ActiveSeconds(lastTime)<<" buyHint="<<buyHint.Visible()
    <<" buyHintMode="<<buyHint.Mode()<<'\n';}
}
