// SPDX-License-Identifier: GPL-3.0-or-later
// Complete upgrade consumer and state. VGUI/engine boundaries are controlled;
// this checks original layer ownership and coordinates, not GPU/font pixels.
namespace csol::client { bool HudStyleLevelVisible(); }
#include "native/csol/client/scenario_upgrades.cpp"
#include "native/csol/scenario_upgrades.cpp"
#include "tests/csol-hud-style/surface_stub.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <source_location>

namespace fixture {
unsigned checks{},failures{},forwards{},styleQueries{};
float hudTime=10,hudDraw=1;
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
int Previous(const char*,int,void*){++forwards;return 73;}
void Log(const char*,...){}
std::vector<std::byte> Tga(unsigned w,unsigned h)
{
    std::vector<std::byte> b(18+w*h*4,std::byte{0xff});std::fill_n(b.begin(),18,std::byte{});
    b[2]=std::byte{2};b[12]=std::byte(w);b[13]=std::byte(w>>8);b[14]=std::byte(h);b[15]=std::byte(h>>8);
    b[16]=std::byte{32};b[17]=std::byte{0x28};return b;
}
struct Quad {int texture,x,y,right,bottom;float u0,v0,u1,v1;std::array<int,4> color;bool additive;};
struct Sprite {int texture,x,y;wrect_t rect;std::array<int,3> color;};
struct Surface final:vgui::HudTestSurface
{
    int width=1280,height=720,root=1,next=50,current{},pushes{},pops{};
    std::array<int,4> color{};
    std::set<int> live;
    std::vector<Quad> quads;
    std::vector<std::wstring> texts;
    vgui::VPANEL GetEmbeddedPanel()override{return root;}
    void GetScreenSize(int& w,int& h)override{w=width;h=height;}
    void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
    void PopMakeCurrent(vgui::VPANEL)override{++pops;}
    int CreateNewTextureID(bool)override{live.insert(++next);return next;}
    bool DeleteTextureByID(int id)override{Check(live.erase(id)==1,"level texture released once");return true;}
    void DrawSetTextureRGBA(int id,const unsigned char* data,int w,int h,int,bool)override
    {Check(live.contains(id)&&data&&w>0&&h>0,"real decoder uploads a valid owned level image");}
    void DrawSetTexture(int id)override{current=id;}
    void DrawSetColor(int r,int g,int b,int a)override{color={r,g,b,a};}
    void DrawTexturedPolygon(vgui::VGuiVertex* v,int n)override
    {Check(n==4,"level uses quad vertices");quads.push_back({current,v[0].GetX(),v[0].GetY(),v[2].GetX(),v[2].GetY(),v[0].GetU(),v[0].GetV(),v[2].GetU(),v[2].GetV(),color,false});}
    void DrawTexturedRectAdd(int x,int y,int r,int b)override{quads.push_back({current,x,y,r,b,0,0,1,1,color,true});}
    void DrawPrintText(const wchar_t* text,int n)override{texts.emplace_back(text,n);}
} capture;
int sprite{};
std::array<int,3> spriteColor{};
std::vector<Sprite> sprites;
void SendLevel(unsigned value)
{
    std::array<std::byte,5> packet{std::byte{4}};
    for(unsigned i=0;i<4;++i)packet[i+1]=std::byte(value>>(8*i));
    const auto calls=forwards;
    Check(csol::client::Receive("ZBSVer2",int(packet.size()),packet.data())==73&&forwards==calls+1,
        "real subtype4 level observer forwards native handler once");
}
void Draw()
{
    capture.quads.clear();capture.texts.clear();sprites.clear();
    csol::client::DrawScenarioUpgrades(hudTime,0);csol::client::Paint();
    Check(capture.pushes==capture.pops,"level surface context balanced");
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};
void SynchronizeSession(){}
bool HudStyleLevelVisible(){++fixture::styleQueries;return true;}
std::vector<std::byte> ReadResource(const char* path)
{
    for(const auto& resource:upgrades::Resources)
        if(std::strcmp(path,resource.path)==0)return fixture::Tga(resource.width,resource.height);
    throw std::runtime_error("unexpected level resource");
}
}
int main()
{
    using namespace csol::client;using namespace fixture;using csol::upgrades::Image;
    engine.Con_DPrintf=Log;engine.GetClientTime=[](){return hudTime;};
    engine.pfnGetCvarFloat=[](const char*){return hudDraw;};
    engine.pfnPlaySoundByNameAtPitch=[](const char*,float,int){};
    engine.pfnSPR_Load=[](const char* path){Check(std::strcmp(path,csol::upgrades::DigitSpritePath)==0,"fallback uses independent original level digit sprite");return 401;};
    engine.pfnSPR_Set=[](int id,int r,int g,int b){sprite=id;spriteColor={r,g,b};};
    engine.pfnSPR_DrawAdditive=[](int,int x,int y,const wrect_t* rect){sprites.push_back({sprite,x,y,*rect,spriteColor});};
    surface=&capture;privateFont=reinterpret_cast<HANDLE>(std::uintptr_t{1});font=7;previousZBS=Previous;
    additiveSurface=true;Prepare();Check(prepared,"complete original level resource set prepared");
    SendLevel(121);Check(ScenarioHudLevel()==121,"real level message commits original panel state");
    for(const auto dimensions:{std::array<int,2>{640,480},{1024,768},{1280,720},{1920,1080}})
    {
        capture.width=dimensions[0];capture.height=dimensions[1];Draw();
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==textures[unsigned(Image::LevelBackground)]&&q.x==0&&q.y==capture.height-70&&q.right==100&&q.bottom==capture.height-46&&!q.additive;}),
            "original level background survives modern HUD at fixed H-70 anchor");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==textures[unsigned(Image::LevelLabel)]&&q.x==12&&q.y==capture.height-66&&q.right==37&&q.bottom==capture.height-52&&
                q.u0==0&&q.v0==1.f/16&&q.u1==25.f/256&&q.v1==15.f/16&&!q.additive;}),
            "original Lv label is the exact 25 by14 TGA crop, not a text font");
        int count{};for(const auto& q:capture.quads)if(q.additive)
        {
            if(count>=3){Check(false,"level must not add extra digit sprites");++count;continue;}
            const int value=std::array{1,2,1}[count];
            Check(q.texture==digits[value]&&q.x==43+13*count&&q.y==capture.height-67&&q.right-q.x==13&&q.bottom-q.y==18,
                "original level digits keep 13 by18 cells with no padding or style scale");++count;
        }
        Check(count==3&&capture.texts.empty()&&!styleQueries,"all original level layers draw without consulting modern HUD ownership or synthesizing Lv text");
    }
    capture.width=1280;capture.height=720;SendLevel(0);Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==digits[0]&&q.x==43&&q.y==653&&q.color==std::array<int,4>{75,75,75,255};}),"zero level keeps original single gray glyph");
    ScenarioUpgradesVideoInit();Check(capture.live.empty(),"level video reload releases all original images");
    additiveSurface=false;Prepare();SendLevel(12);Draw();
    Check(sprites.size()==2&&sprites[0].texture==401&&sprites[0].x==43&&sprites[0].y==653&&
        sprites[0].rect.left==93&&sprites[0].rect.right==106&&sprites[0].rect.top==71&&sprites[0].rect.bottom==89&&sprites[1].x==56,
        "non-HL25 engine fallback preserves original level digit sprite coordinates");
    Check(std::count_if(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==textures[unsigned(Image::LevelBackground)]||q.texture==textures[unsigned(Image::LevelLabel)];})==2&&!styleQueries,
        "fallback also preserves both alpha level layers despite modern style visibility");
    hidden=4;Draw();Check(capture.quads.empty()&&sprites.empty(),"global hide bit still hides original panel");hidden=0;
    for(bool* gate:{&gameUI,&console}){*gate=true;Draw();Check(capture.quads.empty()&&sprites.empty(),"menus and console preserve original level visibility gate");*gate=false;}
    hudDraw=0;Draw();Check(capture.quads.empty()&&sprites.empty(),"hud_draw0 still hides original level panel");hudDraw=1;
    ResetScenarioUpgrades();Draw();Check(capture.quads.empty()&&sprites.empty()&&ScenarioHudLevel()==-1,"new session does not retain previous level");
    ScenarioUpgradesVideoInit();Check(capture.live.empty(),"fallback textures also release completely");
    privateFont=nullptr;surface=nullptr;
    std::cout<<"hud_style_level checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
