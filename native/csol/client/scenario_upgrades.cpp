// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_upgrades.hpp"
#include "../scenario_upgrades.hpp"
#include "../image_codec.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <ostream>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
upgrades::State state;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
std::array<int,unsigned(upgrades::Image::Count)> textures{};
std::array<int,10> digits{};
vgui::HFont font{};
HANDLE privateFont{};
int digitSprite{};
bool additiveSurface{};
pfnUserMsgHook previousZBS{},previousInit{},previousReset{},previousHide{};
bool prepared{},requested{},gameUI{},console{};
int hidden{};
float frameTime{};
std::uint64_t accepted{},malformed{},sounds{};

float RealTime()
{
    // The CSO Bink owner uses VGUI system time; GetTickCount64 likewise
    // advances while a demo is paused, independently of HUD/client time.
    static const ULONGLONG started=GetTickCount64();
    return float(double(GetTickCount64()-started)/1000.0);
}
void ReleaseImages()
{
    if(surface)
    {
        for(auto texture:textures)if(texture)surface->DeleteTextureByID(texture);
        for(auto texture:digits)if(texture)surface->DeleteTextureByID(texture);
    }
    textures={};digits={};prepared=false;
}
int Upload(const Image& image)
{
    const int texture=surface->CreateNewTextureID(true);
    if(texture)surface->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);
    return texture;
}
void Prepare()
{
    if(prepared||!surface)return;
    prepared=true;
    if(!additiveSurface)digitSprite=engine.pfnSPR_Load(upgrades::DigitSpritePath);
    for(unsigned i=0;i<textures.size();++i)
    {
        try
        {
            const auto& resource=upgrades::Resources[i];
            const auto image=DecodeTGA(ReadResource(resource.path));
            if(image.Width!=resource.width||image.Height!=resource.height)
                throw std::runtime_error("scenario upgrade image dimensions changed");
            if(i==unsigned(upgrades::Image::Digits))
            {
                Image cell{13,18,std::vector<std::uint8_t>(13*18*4)};
                for(unsigned digit=0;digit<10;++digit)
                {
                    for(unsigned y=0;y<18;++y)
                        std::copy_n(image.RGBA.data()+(y*130+digit*13)*4,13*4,cell.RGBA.data()+y*13*4);
                    digits[digit]=Upload(cell);
                }
            }
            else textures[i]=Upload(image);
        }
        catch(const std::exception& error)
        {engine.Con_DPrintf("CSOLSupport: %s: %s\n",upgrades::Resources[i].path,error.what());}
    }
    if(!privateFont)
    {
        try
        {
            const auto bytes=ReadResource(upgrades::FontPath);DWORD count{};
            privateFont=AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()),DWORD(bytes.size()),nullptr,&count);
        }
        catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: upgrade font: %s\n",error.what());}
    }
    if(privateFont&&!font)
    {
        font=surface->CreateFont();
        if(font&&!surface->AddGlyphSetToFont(font,"DFYuanW9-GB",20,0,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff))font=0;
    }
}
void Paint()
{
    if(!requested)return;requested=false;
    if(!surface||!prepared||gameUI||console||(hidden&4)||engine.pfnGetCvarFloat("hud_draw")==0)return;
    int width{},height{};surface->GetScreenSize(width,height);
    const auto root=surface->GetEmbeddedPanel();if(!root)return;
    const auto frame=state.Layout(width,height,frameTime,RealTime());
    if(!frame.count)return;
    surface->PushMakeCurrent(root,false);
    for(unsigned i=0;i<frame.count;++i)
    {
        const auto& q=frame.quads[i];
        const int texture=q.additive?digits[std::clamp(int(std::lround(q.u0*10)),0,9)]:textures[unsigned(q.image)];
        if(!texture||q.width<=0||q.height<=0)continue;
        surface->DrawSetColor(int(q.color[0]*255),int(q.color[1]*255),int(q.color[2]*255),int(q.color[3]*255));
        surface->DrawSetTexture(texture);
        if(q.additive)
        {
            if(additiveSurface)static_cast<vgui::ISurface_HL25*>(surface)->DrawTexturedRectAdd(
                int(q.x),int(q.y),int(q.x+q.width),int(q.y+q.height));
        }
        else
        {
            vgui::VGuiVertex vertices[]{
                {int(q.x),int(q.y),q.u0,q.v0},{int(q.x+q.width),int(q.y),q.u1,q.v0},
                {int(q.x+q.width),int(q.y+q.height),q.u1,q.v1},{int(q.x),int(q.y+q.height),q.u0,q.v1}};
            surface->DrawTexturedPolygon(vertices,4);
        }
    }
    if(frame.notice&&font)
    {
        wchar_t text[80];std::swprintf(text,std::size(text),L"等级提升！Level  %d",state.Total());
        int textWidth{},textHeight{};surface->GetTextSize(font,text,textWidth,textHeight);
        surface->DrawSetTextFont(font);surface->DrawSetTextColor(210,187,129,255);
        surface->DrawSetTextPos((width-textWidth)/2,frame.textY+(32-textHeight)/2);
        surface->DrawPrintText(text,int(std::wcslen(text)));surface->DrawFlushText();
    }
    surface->PopMakeCurrent(root);
    if(!additiveSurface&&digitSprite)
    {
        for(unsigned i=0;i<frame.count;++i)
        {
            const auto& q=frame.quads[i];if(!q.additive)continue;
            const int digit=std::clamp(int(std::lround(q.u0*10)),0,9);
            wrect_t rectangle{80+digit*13,93+digit*13,71,89};
            engine.pfnSPR_Set(digitSprite,int(q.color[0]*255),int(q.color[1]*255),int(q.color[2]*255));
            engine.pfnSPR_DrawAdditive(0,int(q.x),int(q.y),&rectangle);
        }
    }
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 22;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetScenarioUpgrades();surface=nullptr;textures={};digits={};font=0;prepared=false;}
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
    upgrades::Message message;
    const auto result=upgrades::Decode({static_cast<const std::byte*>(data),std::size_t(size)},message);
    if(result==upgrades::ParseResult::Malformed){++malformed;return 1;}
    if(result==upgrades::ParseResult::Accepted)
    {
        ++accepted;
        if(state.Apply(message,engine.GetClientTime(),RealTime()))
        {engine.pfnPlaySoundByNameAtPitch(upgrades::SoundPath,1,100);++sounds;}
    }
    return previousZBS?previousZBS(name,size,data):1;
}
int Init(const char* name,int size,void* data)
{ResetScenarioUpgrades();return previousInit?previousInit(name,size,data):1;}
int Reset(const char* name,int size,void* data)
{state.ClearPresentation();requested=false;hidden=0;return previousReset?previousReset(name,size,data):1;}
int Hide(const char* name,int size,void* data)
{if(data&&size==1)hidden=*static_cast<unsigned char*>(data);return previousHide?previousHide(name,size,data):1;}
void Install(const char* name,pfnUserMsgHook hook,pfnUserMsgHook& previous)
{
    const auto old=api->HookUserMsg(name,hook);if(old!=hook)previous=old;
    if(!old)engine.pfnHookUserMsg(name,hook);
}
}
void InitializeScenarioUpgrades()
{
    additiveSurface=api->GetEngineType()==ENGINE_GOLDSRC_HL25;
    Install("ZBSVer2",Receive,previousZBS);Install("InitHUD",Init,previousInit);
    Install("ResetHUD",Reset,previousReset);Install("HideWeapon",Hide,previousHide);
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
void ResetScenarioUpgrades(){state.Reset();hidden=0;requested=false;frameTime=0;}
void ScenarioUpgradesFrame(){if(state.Active())Prepare();}
void ScenarioUpgradesVideoInit(){ReleaseImages();digitSprite=0;}
void DrawScenarioUpgrades(float time,int intermission)
{state.Advance(time);frameTime=time;requested=!intermission&&state.Active();}
void ShutdownScenarioUpgrades()
{
    ResetScenarioUpgrades();ReleaseImages();
    if(extension&&surface)extension->UnregisterBaseUICallbacks(&overlay);
    if(privateFont)RemoveFontMemResourceEx(privateFont);
    privateFont=nullptr;font=0;extension=nullptr;surface=nullptr;gameUI=console=false;
}
void ScenarioUpgradesDiagnostics(std::ostream& out)
{out<<"scenarioUpgrades active="<<state.Active()<<" total="<<state.Total()<<" exp="<<state.Experience()
    <<" notice="<<state.NoticeVisible()<<" prepared="<<prepared<<" font="<<bool(font)
    <<" accepted="<<accepted<<" rejected="<<malformed<<" sounds="<<sounds<<'\n';}
}
