// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario_kill.hpp"
#include "../../native/csol/scenario_kill.hpp"
#include "../csol-wall-status/surface_stub.hpp"
#include <IVGUI2Extension.h>
#include <cstring>
#include <iostream>
#include <map>
#include <source_location>

namespace fixture
{
std::map<std::string,pfnUserMsgHook> receivers;
int checks{},forwards{},registrations{},unregistrations{},fallbacks{},uploads{},reads{},pushes{},pops{},nextTexture{};
bool existing=true;
float hud=1;
IVGUI2Extension_BaseUICallbacks* overlay{};
cl_entity_t local{};
struct Draw{int texture,alpha,left,top,right,bottom;float u0,u1;};
std::vector<Draw> draws;
int texture{},alpha{};
void Check(bool value,const std::source_location where=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("scenario kill client line "+std::to_string(where.line()));}
int Previous(const char*,int,void*){++forwards;return 73;}
void Console(const char*,...){}
int Send(const char* name,std::vector<std::byte> bytes)
{return receivers.at(name)(name,int(bytes.size()),bytes.data());}
void Frag(int player,unsigned count)
{Send("MPToCL",{std::byte{27},std::byte{2},std::byte(player),std::byte(count),std::byte(count>>8)});}
void Combo(int value,bool head=false)
{Send("ZBSVer2",{std::byte(head?2:1),std::byte(value),std::byte(value>>8),std::byte(value>>16),std::byte(value>>24)});}
struct Surface final:vgui::TestSurface
{
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    void GetScreenSize(int& w,int& h)override{w=1024;h=768;}
    void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
    void PopMakeCurrent(vgui::VPANEL)override{++pops;}
    int CreateNewTextureID(bool)override{return ++nextTexture;}
    bool DeleteTextureByID(int)override{return true;}
    void DrawSetColor(int r,int g,int b,int a)override{Check(r==255&&g==255&&b==255);alpha=a;}
    void DrawSetTexture(int id)override{texture=id;}
    void DrawSetTextureRGBA(int,const unsigned char* rgba,int w,int h,int,bool)override
    {
        ++uploads;Check(w>0&&h>0);
        // The production TGA decoder and uploader must retain transparent,
        // translucent, and opaque texels without replacing the alpha channel.
        Check(rgba[3]==0&&rgba[7]==127&&rgba[11]==255);
    }
    void DrawTexturedPolygon(vgui::VGuiVertex* vertices,int count)override
    {
        Check(count==4);draws.push_back({texture,alpha,vertices[0].GetX(),vertices[0].GetY(),
            vertices[2].GetX(),vertices[2].GetY(),vertices[0].GetU(),vertices[2].GetU()});
    }
} surface;
struct Extension final:IVGUI2Extension
{
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
void Draw(float time,int intermission=0)
{
    csol::client::DrawScenarioKill(time,intermission);
    csol::client::ScenarioKillFrame();
    if(overlay){int n{};VGUI2Extension_CallbackContext c;c.IsPost=true;overlay->Paint(n,n,n,n,&c);}
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return fixture::ExtensionFactory;}
namespace csol::client
{
metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};
void SynchronizeSession(){}
std::vector<std::byte> ReadResource(const char* path)
{
    ++fixture::reads;
    for(const auto& r:scenario_kill::Resources)if(!std::strcmp(path,r.path))
    {
        std::vector<std::byte> tga(18+std::size_t(r.width)*r.height*4);
        tga[2]=std::byte{2};tga[12]=std::byte(r.width);tga[13]=std::byte(r.width>>8);
        tga[14]=std::byte(r.height);tga[15]=std::byte(r.height>>8);tga[16]=std::byte{32};tga[17]=std::byte{0x28};
        tga[18+7]=std::byte{127};tga[18+11]=std::byte{255};return tga;
    }
    throw std::runtime_error("unexpected resource");
}
}
int main(int argc,char**)
{
    using namespace fixture;using namespace csol::client;
    try
    {
        existing=argc==1;local.index=1;
        if(existing)for(const char* name:{"MPToCL","ZBSVer2","ResetHUD","InitHUD","HideWeapon"})receivers[name]=Previous;
        static metahook_api_t host;api=&host;
        host.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook
        {const auto it=receivers.find(name);if(it==receivers.end())return nullptr;const auto old=it->second;it->second=hook;return old;};
        engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook hook)->int{++fallbacks;receivers[name]=hook;return 1;};
        host.GetEngineFactory=[]()->CreateInterfaceFn{return SurfaceFactory;};
        host.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return reinterpret_cast<HINTERFACEMODULE>(1);};
        engine.GetLocalPlayer=[]()->cl_entity_t*{return &local;};
        engine.pfnGetCvarFloat=[](const char*)->float{return hud;};
        engine.Con_DPrintf=Console;
        InitializeScenarioKill();InitializeScenarioKill();Check(registrations==1);Check(fallbacks==(existing?0:5));
        int before=forwards;Check(Send("MPToCL",{std::byte{206}})==(existing?73:1));Check(forwards==before+(existing?1:0));
        before=forwards;Send("MPToCL",{std::byte{27},std::byte{2},std::byte{1}});Check(forwards==before);
        Frag(2,1);Draw(1);Check(draws.empty());Frag(1,1);Draw(1);Check(draws.size()==1&&draws.back().texture==1);
        Check(draws.back().left==395&&draws.back().top==164&&draws.back().alpha==255);
        Check(uploads==7&&reads==7);Draw(1.25f);Check(draws.back().alpha==127&&draws.back().top==154);
        const auto count=draws.size();Draw(1.5f);Check(draws.size()==count);Check(uploads==7&&reads==7);
        Frag(1,2);Combo(30);Combo(1,true);Draw(2);Check(draws.size()==count+4);
        Check(draws[count].texture==5&&draws[count+1].texture==6&&draws[count+3].texture==7);
        Check(draws[count+1].right<=400&&draws[count+2].right==400);
        before=int(draws.size());hud=0;Draw(2.1f);hud=1;Draw(2.1f,1);Check(draws.size()==unsigned(before));
        Send("HideWeapon",{std::byte{4}});Draw(2.1f);Check(draws.size()==unsigned(before));
        Send("HideWeapon",{std::byte{0}});Draw(2.1f);Check(draws.size()==unsigned(before+4));
        VGUI2Extension_CallbackContext pre,post;post.IsPost=true;
        before=int(draws.size());overlay->ShowConsole(&pre);Draw(2.1f);Check(draws.size()==unsigned(before));
        overlay->HideConsole(&post);overlay->ActivateGameUI(&pre);Draw(2.1f);Check(draws.size()==unsigned(before));
        overlay->HideGameUI(&post);Draw(2.1f);Check(draws.size()==unsigned(before+4));
        Send("ResetHUD",{std::byte{0}});before=int(draws.size());Draw(2.2f);Check(draws.size()==unsigned(before));
        Frag(1,3);Draw(3);Check(draws.size()==unsigned(before+1));
        Send("InitHUD",{std::byte{0}});before=int(draws.size());Draw(0);Check(draws.size()==unsigned(before));
        Frag(1,1);Draw(0);Check(draws.size()==unsigned(before+1));
        ResetScenarioKill();before=int(draws.size());Draw(0);Check(draws.size()==unsigned(before));
        ScenarioKillVideoInit();Frag(1,1);Draw(4);Check(uploads==14&&nextTexture==7);
        Check(pushes==pops);ShutdownScenarioKill();Check(unregistrations==1);
        InitializeScenarioKill();Check(registrations==2);Frag(1,1);Draw(5);Check(draws.back().alpha==255);ShutdownScenarioKill();
        std::cout<<"scenario kill client "<<(existing?"existing":"missing")<<": "<<checks<<" checks passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
