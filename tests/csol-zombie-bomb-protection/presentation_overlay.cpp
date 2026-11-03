// SPDX-License-Identifier: GPL-3.0-or-later
// Full character_skill_display.cpp + image decoder with actual VGUI ABI.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/character_skill_display.hpp"
#include "../csol-wall-status/surface_stub.hpp"
#include <IVGUI2Extension.h>
#include <bit>
#include <iostream>
#include <map>
#include <source_location>
namespace screen_test
{
int checks{},forwards{},reads{},uploads{},deletes{},pushes{},pops{},texture{},width=1280,height=720;
float now=100,hud=1;
std::array<int,4> color;
std::map<std::string,pfnUserMsgHook> hooks;
std::vector<IVGUI2Extension_BaseUICallbacks*> overlays;
struct Draw {std::array<int,4> color;std::array<int,4> rect;};
std::vector<Draw> draws;
void Check(bool value,std::source_location at=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("bomb overlay line "+std::to_string(at.line()));}
int Previous(const char*,int,void*){++forwards;return 73;}
void Log(const char*,...){}
struct Surface final:vgui::TestSurface
{
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    void GetScreenSize(int& w,int& h)override{w=width;h=height;}
    int CreateNewTextureID(bool)override{return ++texture;}
    bool DeleteTextureByID(int)override{++deletes;return true;}
    void DrawSetTextureRGBA(int,const unsigned char* pixels,int w,int h,int,bool)override
    {++uploads;Check(w==2&&h==2&&pixels[3]==0&&pixels[7]==127&&pixels[11]==255);}
    void DrawSetColor(int r,int g,int b,int a)override{color={r,g,b,a};}
    void DrawSetTexture(int)override{}
    void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
    void PopMakeCurrent(vgui::VPANEL)override{++pops;}
    void DrawTexturedPolygon(vgui::VGuiVertex* v,int n)override
    {Check(n==4);draws.push_back({color,{v[0].GetX(),v[0].GetY(),v[2].GetX(),v[2].GetY()}});}
}surface;
struct Extension final:IVGUI2Extension
{
    void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override{overlays.push_back(p);}
    void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override{std::erase(overlays,p);}
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
}extension;
IBaseInterface* SurfaceFactory(const char*,int*){return &surface;}
IBaseInterface* ExtensionFactory(const char*,int*){return &extension;}
int Send(const char* name,std::vector<unsigned char> bytes)
{return hooks.at(name)(name,int(bytes.size()),bytes.empty()?nullptr:bytes.data());}
std::vector<unsigned char> Packet(float duration,unsigned alpha=255)
{
    std::vector<unsigned char> bytes{54,0};const auto raw=std::bit_cast<unsigned>(duration);
    for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(raw>>(8*i)));
    bytes.insert(bytes.end(),{190,190,190,static_cast<unsigned char>(alpha)});return bytes;
}
void Paint()
{
    using namespace csol::client;draws.clear();DrawCharacterSkillDisplay(now,0);CharacterSkillDisplayFrame();
    for(auto* p:overlays){int n{};VGUI2Extension_CallbackContext c;c.IsPost=true;p->Paint(n,n,n,n,&c);}
    Check(pushes==pops);
}
void Run()
{
    using namespace csol::client;metahook_api_t host{};api=&host;
    host.GetEngineFactory=[]()->CreateInterfaceFn{return SurfaceFactory;};
    host.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return reinterpret_cast<HINTERFACEMODULE>(1);};
    host.HookUserMsg=[](const char* name,pfnUserMsgHook callback)->pfnUserMsgHook
    {const auto old=hooks.contains(name)?hooks[name]:Previous;hooks[name]=callback;return old;};
    engine.GetClientTime=[](){return now;};engine.pfnGetCvarFloat=[](const char*){return hud;};engine.Con_DPrintf=Log;
    view.health=100;InitializeCharacterSkillDisplay();Check(overlays.size()==1);
    Check(Send("MPToCL",Packet(4))==1);
    // These six expected pulses are literals from the original modulo200 triangle.
    const std::array<std::pair<float,int>,6> pulses{{{100,150},{100.125f,175},{100.25f,200},{100.5f,250},{100.75f,200},{101,150}}};
    for(const auto [time,alpha]:pulses)
    {now=time;Paint();Check(draws.size()==4);for(const auto& draw:draws)Check(draw.color==std::array<int,4>{190,190,190,alpha});}
    for(const auto size:std::array<std::array<int,2>,4>{{{640,480},{800,600},{1280,720},{1920,1080}}})
    {width=size[0];height=size[1];Paint();Check(draws.size()==4);
     Check(draws[0].rect==std::array<int,4>{0,0,width/2,height/2}&&draws[1].rect==std::array<int,4>{width,0,width/2,height/2}&&
           draws[2].rect==std::array<int,4>{0,height,width/2,height/2}&&draws[3].rect==std::array<int,4>{width,height,width/2,height/2});}
    now=102;Send("MPToCL",Packet(1,0));Paint();Check(draws.size()==4&&draws[0].color[3]==150); // wire alpha is not opacity
    now=103;Paint();Check(draws.empty());Send("MPToCL",Packet(5));Paint();Check(draws.size()==4);
    Send("MPToCL",Packet(0));Paint();Check(draws.empty());
    for(const char* reset:{"ResetHUD","InitHUD"}){Send("MPToCL",Packet(5));Send(reset,{});Paint();Check(draws.empty());}
    view.health=0;Send("ResetHUD",{});Send("MPToCL",Packet(5));Paint();Check(draws.empty());view.health=100;Paint();Check(draws.size()==4);
    view.health=0;Paint();view.health=100;Paint();Check(draws.empty());
    Send("MPToCL",Packet(5));hud=0;Paint();Check(draws.empty());hud=1;Paint();Check(draws.size()==4);
    view.spectator=1;Paint();view.spectator=0;Paint();Check(draws.empty());
    Send("MPToCL",Packet(5));for(int size:{1,9,11}){auto p=Packet(0);p.resize(size);Check(Send("MPToCL",p)==1);}Paint();Check(draws.size()==4);
    Send("MPToCL",{199,1,0,53});Check(forwards>0);ShutdownCharacterSkillDisplay();Check(overlays.empty()&&uploads==deletes);
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return screen_test::ExtensionFactory;}
namespace csol::client
{
metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};
void SynchronizeSession(){}
std::vector<std::byte> ReadResource(const char* path)
{
    ++screen_test::reads;screen_test::Check(std::string_view(path)=="resource/zombi/directitem.tga");
    std::vector<std::byte> bytes(18+16);bytes[2]=std::byte{2};bytes[12]=bytes[14]=std::byte{2};bytes[16]=std::byte{32};bytes[17]=std::byte{0x28};
    bytes[18+7]=std::byte{127};bytes[18+11]=std::byte{255};return bytes;
}
}
int main(){try{screen_test::Run();std::cout<<"bomb overlay checks="<<screen_test::checks<<" failures=0\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
