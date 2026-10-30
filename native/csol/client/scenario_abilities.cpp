// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_abilities.hpp"
#include "../scenario_abilities.hpp"
#include "../image_codec.hpp"
#include <cmath>
#include <cstring>
#include <ostream>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
abilities::State state;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
struct Textures { std::array<int,3> images{};bool attempted{}; };
std::array<Textures,55> textures;
std::array<int,11> overlays{};
bool overlaysPrepared{},requested{},gameUI{},console{},buyZone{},clockStarted{};
int hidden{};
float lastTime{};
pfnUserMsgHook previousZBS{},previousInit{},previousReset{},previousHide{},previousStatus{},previousSnapshot{};
std::uint64_t accepted{},malformed{},draws{};

void ReleaseImages()
{
    if(surface)
    {
        for(const auto& type:textures)for(auto texture:type.images)if(texture)surface->DeleteTextureByID(texture);
        for(auto texture:overlays)if(texture)surface->DeleteTextureByID(texture);
    }
    textures={};overlays={};overlaysPrepared=false;
}
int Load(const char* path)
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
    catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: %s: %s\n",path,error.what());return 0;}
}
void Prepare()
{
    if(!surface||!state.Visible())return;
    const auto* resource=abilities::ResourceFor(state.Type());
    if(!resource)return;
    auto& type=textures[state.Type()-1];
    if(!type.attempted)
    {type.attempted=true;type.images={Load(resource->background),Load(resource->icon),Load(resource->key)};}
    if(!overlaysPrepared)
    {
        overlaysPrepared=true;
        overlays[0]=Load(abilities::ReadyHighlight);overlays[1]=Load(abilities::NumberHighlight);
        for(unsigned i=0;i<abilities::NumberImages.size();++i)overlays[i+2]=Load(abilities::NumberImages[i]);
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
}
void Paint()
{
    if(!requested)return;requested=false;
    if(!surface||gameUI||console||(hidden&4)||view.health<=0||view.spectator||engine.pfnGetCvarFloat("hud_draw")==0)return;
    const auto type=state.Type();if(!abilities::ResourceFor(type)||!textures[type-1].attempted)return;
    int width{},height{};surface->GetScreenSize(width,height);
    const auto frame=state.Layout(width,height,buyZone);if(!frame.count)return;
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    surface->PushMakeCurrent(root,false);
    for(unsigned i=0;i<frame.count;++i)
    {
        const auto& q=frame.quads[i];int texture{};
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
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)csol::client::Paint();}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
} overlay;
int Receive(const char* name,int size,void* data)
{
    SynchronizeSession();
    if(!data||size<0)return previousZBS?previousZBS(name,size,data):1;
    abilities::Message message;
    const auto result=abilities::Decode({static_cast<const std::byte*>(data),std::size_t(size)},message);
    if(result==abilities::ParseResult::Malformed){++malformed;return 1;}
    if(result==abilities::ParseResult::Accepted){AdvanceTo(engine.GetClientTime());state.Apply(message);++accepted;}
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
    state.Restore(snapshot);lastTime=time;clockStarted=true;++accepted;
    return 1;
}
int Reset(const char* name,int size,void* data)
{requested=false;hidden=0;buyZone=false;return previousReset?previousReset(name,size,data):1;}
int Hide(const char* name,int size,void* data)
{if(data&&size==1)hidden=*static_cast<unsigned char*>(data);return previousHide?previousHide(name,size,data):1;}
int Status(const char* name,int size,void* data)
{
    if(data&&size>=2)
    {
        const auto* bytes=static_cast<const unsigned char*>(data);
        const auto* end=static_cast<const unsigned char*>(std::memchr(bytes+1,0,std::size_t(size-1)));
        if(end&&end-bytes==8&&std::memcmp(bytes+1,"buyzone",7)==0)buyZone=bytes[0]!=0;
    }
    return previousStatus?previousStatus(name,size,data):1;
}
void Install(const char* name,pfnUserMsgHook hook,pfnUserMsgHook& previous)
{
    const auto old=api->HookUserMsg(name,hook);if(old!=hook)previous=old;
    if(!old)engine.pfnHookUserMsg(name,hook);
}
}
void InitializeScenarioAbilities()
{
    Install("ZBSVer2",Receive,previousZBS);Install("InitHUD",Init,previousInit);
    Install("ResetHUD",Reset,previousReset);Install("HideWeapon",Hide,previousHide);Install("StatusIcon",Status,previousStatus);
    Install("CsoSkState",Snapshot,previousSnapshot);
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
{state.Reset();hidden=0;requested=buyZone=clockStarted=false;lastTime=0;}
void ScenarioAbilitiesFrame(){Prepare();}
void ScenarioAbilitiesVideoInit(){ReleaseImages();}
void DrawScenarioAbilities(float time,int intermission)
{AdvanceTo(time);requested=!intermission&&state.Visible();}
void ShutdownScenarioAbilities()
{
    ResetScenarioAbilities();ReleaseImages();
    if(extension&&surface)extension->UnregisterBaseUICallbacks(&overlay);
    extension=nullptr;surface=nullptr;gameUI=console=false;
}
void ScenarioAbilitiesDiagnostics(std::ostream& out)
{out<<"scenarioAbilities visible="<<state.Visible()<<" type="<<state.Type()<<" progress="<<state.Fraction()
    <<" flashes="<<state.FlashCount()<<" number="<<state.Number()<<" accepted="<<accepted
    <<" rejected="<<malformed<<" draws="<<draws<<'\n';}
}
