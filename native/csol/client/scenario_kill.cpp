// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_kill.hpp"
#include "../scenario_kill.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
scenario_kill::State state;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
pfnUserMsgHook previousMP{},previousZBS{},previousInit{},previousReset{},previousHide{};
std::array<int,unsigned(scenario_kill::Image::Count)> textures{};
bool prepared{},requested{},gameUI{},console{};
int hidden{};

void PrepareImages()
{
    if(prepared||!surface)return;
    prepared=true;
    for(unsigned i=0;i<textures.size();++i)
    {
        try
        {
            const auto& resource=scenario_kill::Resources[i];
            const auto image=DecodeTGA(ReadResource(resource.path));
            if(image.Width!=unsigned(resource.width)||image.Height!=unsigned(resource.height))
                throw std::runtime_error("scenario kill image dimensions changed");
            if(!textures[i])textures[i]=surface->CreateNewTextureID(true);
            if(textures[i])surface->DrawSetTextureRGBA(textures[i],image.RGBA.data(),
                image.Width,image.Height,1,true);
        }
        catch(const std::exception& error)
        {
            engine.Con_DPrintf("CSOLSupport: %s: %s\n",scenario_kill::Resources[i].path,error.what());
        }
    }
}
void Paint()
{
    if(!requested)return;
    requested=false;
    if(!surface||!prepared||gameUI||console||(hidden&4)||engine.pfnGetCvarFloat("hud_draw")==0)return;
    int width{},height{};surface->GetScreenSize(width,height);
    const auto root=surface->GetEmbeddedPanel();
    if(!root)return;
    const auto frame=state.Layout(width,height);
    if(!frame.count)return;
    surface->PushMakeCurrent(root,false);
    for(unsigned i=0;i<frame.count;++i)
    {
        const auto& q=frame.quads[i];
        const int texture=textures[unsigned(q.image)];
        if(!texture)continue;
        surface->DrawSetColor(255,255,255,q.alpha);
        surface->DrawSetTexture(texture);
        vgui::VGuiVertex vertices[]{
            {q.left,q.top,q.u0,q.v0},{q.right,q.top,q.u1,q.v0},
            {q.right,q.bottom,q.u1,q.v1},{q.left,q.bottom,q.u0,q.v1}};
        surface->DrawTexturedPolygon(vertices,4);
    }
    surface->PopMakeCurrent(root);
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 21;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetScenarioKill();surface=nullptr;textures={};prepared=false;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)csol::client::Paint();}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
} overlay;

int Receive(const char* name,int size,void* data,bool combo)
{
    SynchronizeSession();
    const auto previous=combo?previousZBS:previousMP;
    if(!data||size<0)return previous?previous(name,size,data):1;
    scenario_kill::Message message;
    const std::span<const std::byte> bytes{static_cast<const std::byte*>(data),std::size_t(size)};
    const auto result=combo?scenario_kill::DecodeCombo(bytes,message):scenario_kill::DecodeFrags(bytes,message);
    if(result==scenario_kill::ParseResult::Malformed)return 1;
    if(result==scenario_kill::ParseResult::Accepted)state.Apply(message);
    // MPToCL is also consumed by damage/status/weapon owners. Never swallow
    // their opcodes, and preserve independent scoreboard consumers of frags.
    return previous?previous(name,size,data):1;
}
int ReceiveMP(const char* name,int size,void* data){return Receive(name,size,data,false);}
int ReceiveZBS(const char* name,int size,void* data){return Receive(name,size,data,true);}
int ReceiveInit(const char* name,int size,void* data)
{
    ResetScenarioKill();
    return previousInit?previousInit(name,size,data):1;
}
int ReceiveReset(const char* name,int size,void* data)
{
    state.ClearPresentation();requested=false;hidden=0;
    return previousReset?previousReset(name,size,data):1;
}
int ReceiveHide(const char* name,int size,void* data)
{
    if(data&&size==1)hidden=*static_cast<unsigned char*>(data);
    return previousHide?previousHide(name,size,data):1;
}
void Install(const char* name,pfnUserMsgHook hook,pfnUserMsgHook& previous)
{
    const auto old=api->HookUserMsg(name,hook);
    if(old!=hook)previous=old;
    if(!old)engine.pfnHookUserMsg(name,hook);
}
}
void InitializeScenarioKill()
{
    Install("MPToCL",ReceiveMP,previousMP);
    Install("ZBSVer2",ReceiveZBS,previousZBS);
    Install("InitHUD",ReceiveInit,previousInit);
    Install("ResetHUD",ReceiveReset,previousReset);
    Install("HideWeapon",ReceiveHide,previousHide);
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
void ResetScenarioKill(){state.Reset();hidden=0;requested=false;}
void ScenarioKillFrame()
{
    // Prepare once during HUD_Frame, outside the overlay draw path. A frag
    // packet can precede our first valid local entity, so loading is harmless.
    if(state.NeedsResources())PrepareImages();
}
void ScenarioKillVideoInit(){prepared=false;}
void DrawScenarioKill(float time,int intermission)
{
    const auto* player=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
    state.Advance(player?player->index:0,time);
    requested=!intermission&&state.NeedsResources();
}
void ShutdownScenarioKill()
{
    ResetScenarioKill();
    if(extension&&surface)extension->UnregisterBaseUICallbacks(&overlay);
    if(surface)for(const int texture:textures)if(texture)surface->DeleteTextureByID(texture);
    textures={};prepared=false;surface=nullptr;extension=nullptr;gameUI=console=false;
}
}
