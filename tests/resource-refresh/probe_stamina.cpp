// SPDX-License-Identifier: GPL-3.0-or-later
#define FIXTURE_RESOURCE_READER
#include "native/csol/client/scenario/core/stamina_hud.cpp"
#include "tests/csol-challenge-hud/ui_fixture.hpp"
#include "native/csol/client/resource_font_memory.hpp"
#include "tests/csol-hud-style/allocation_probe.hpp"
#include <fstream>
#include <map>
namespace test_stamina
{
bool buy{},characters{},vote{},sprint{};
const char* sprintBinding="SHIFT";
unsigned reads{},bindingReads{};
std::vector<std::byte> fontBytes;
struct Surface final:fixture::Surface
{
    unsigned fills{},texts{},createdFonts{},uploads{};
    std::map<vgui::HFont,int> heights;
    std::array<int,4> lastFill{};
    int textX{},textY{};
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    void DrawSetTextureRGBA(int,const unsigned char*,int,int,int,bool)override{++uploads;}
    vgui::HFont CreateFont()override{return 100+ ++createdFonts;}
    bool AddGlyphSetToFont(vgui::HFont id,const char*,int tall,int,int,int,int,int,int)override{heights[id]=tall;return true;}
    int GetFontTall(vgui::HFont id)override{return heights[id];}
    void GetTextSize(vgui::HFont id,const wchar_t* text,int& w,int& h)override
    {h=heights.at(id);w=0;for(;*text;++text)w+=*text>127?h:h*3/5;}
    void DrawFilledRect(int x,int y,int right,int bottom)override{++fills;lastFill={x,y,right,bottom};}
    void DrawSetTextPos(int x,int y)override{textX=x;textY=y;}
    void DrawPrintText(const wchar_t* text,int length)override
    {
        using namespace csol::client;
        ++texts;
        fixture::Check(length==std::wcslen(text),"prepared string length matches drawing");
        int w{},h{};GetTextSize(font,text,w,h);
        fixture::Check(textX>=layout.x&&textX+w<=layout.x+layout.width&&textY>=0&&
            textY+h<=layout.y+layout.height-layout.padding-layout.barHeight,"labels fit above stamina track");
    }
};
const char* SprintBinding(const char* command)
{
    fixture::Check(std::string_view(command)=="speed","engine lookup uses the native sprint binding name");
    ++bindingReads;
    return sprintBinding;
}
}
namespace csol::client
{
bool BuyMenuVisible(){return test_stamina::buy;}
bool CharacterMenuVisible(){return test_stamina::characters;}
bool ScenarioMapVoteVisible(){return test_stamina::vote;}
bool StandaloneSprintAvailable() noexcept{return test_stamina::sprint;}
float HudStyleScale(){return 1;}
std::vector<std::byte> ReadResource(const char* path)
{++test_stamina::reads;fixture::Check(std::string_view(path)==upgrades::FontPath,"stamina reads only verified font");return test_stamina::fontBytes;}
}
int main(int argc,char** argv)
{
    using namespace csol::client;using fixture::Check;namespace custom=csol::scenario::custom;
    Check(argc>=2,"real font supplied");std::ifstream in(argv[1],std::ios::binary|std::ios::ate);Check(bool(in),"font opens");
    const auto size=in.tellg();test_stamina::fontBytes.resize(size);in.seekg(0);in.read(reinterpret_cast<char*>(test_stamina::fontBytes.data()),size);Check(bool(in),"font fully read");
    test_stamina::Surface fake;surface=&fake;ownerThread=GetCurrentThreadId();view.health=100;
    engine.Key_LookupBinding=test_stamina::SprintBinding;
    auto send=[&](custom::StaminaSnapshot value)
    {const auto bytes=custom::EncodeStamina(value);Check(bool(bytes),"valid test message");Receive(custom::StaminaMessage,int(bytes->size()),const_cast<std::byte*>(bytes->data()));StaminaHudFrame();};
    send({1,1,37.5f,100,true,false,false});Check(prepared&&accepted==1&&display.amount==37.5f&&display.maximum==100,"actual owner retains exact authority");
    DrawStaminaHud(0);Paint();Check(fake.fills==3&&fake.texts==1,"actual HUD draws background, track, exact fill and label");
    Check(fill==int(float(layout.width-2*layout.padding)*.375f),"bar ratio uses server amount rather than a local timer");
    allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<100;++i){StaminaHudFrame();DrawStaminaHud(0);Paint();}
    allocation_probe::active=false;Check(!allocation_probe::calls&&test_stamina::reads==1&&fake.uploads==0,"warm100 stamina frames allocate/read/upload nothing");
    Check(display.amount==37.5f,"100 local frames never invent restored stamina");
    send({1,2,.1f,100,true,false,true});Check(std::wstring_view(label.data()).find(L"恢复中")!=std::wstring_view::npos,"exhaustion label follows real flag");
    test_stamina::sprint=true;
    send({2,1,150,150,true,true,false});Check(std::wstring_view(label.data()).find(L"无限")!=std::wstring_view::npos&&fill==layout.width-2*layout.padding,"noon renders explicit unlimited bar");
    Check(!test_stamina::bindingReads&&!sprintHintLength,"unlimited noon never draws a sprint prompt");
    const auto rejectedBefore=rejected;send({1,9,50,100,true,false,false});Check(rejected==rejectedBefore+1&&display.infinite,"late old life cannot replace current HUD");
    send({2,2,145,150,true,false,false});
    Check(sprintHintLength>0&&std::wstring_view(sprintHint.data())==L"按住 [Shift] 奔跑","grant displays the actual default key in dusk");
    auto texts=fake.texts;DrawStaminaHud(0);Paint();Check(fake.texts==texts+2,"authorized frame draws energy and sprint labels");
    const auto lookups=test_stamina::bindingReads;
    allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<100;++i){StaminaHudFrame();DrawStaminaHud(0);Paint();}
    allocation_probe::active=false;
    Check(!allocation_probe::calls&&test_stamina::bindingReads==lookups,"100 warm authorized frames reuse key and label without allocation");
    test_stamina::sprintBinding="MWHEELDOWN";nextSprintBindingCheck=0;StaminaHudFrame();
    Check(std::wstring_view(sprintHint.data()).find(L"[MWHEELDOWN]")!=std::wstring_view::npos,"remapping refreshes the key without a stamina update");
    for(const auto [w,h]:{std::pair{320,240},{640,480},{864,480},{1024,768},{1280,720},{1920,1080},{1920,1200},{2560,1080},{3840,2160}})
    {
        fake.width=w;fake.height=h;StaminaHudFrame();DrawStaminaHud(0);Paint();
        Check(layout.y>=0&&layout.x+layout.width<=w&&layout.y+layout.height<=h-layout.padding&&layout.kickY==h-layout.padding-26,"actual resolution change keeps bottom controls within safe margins");
    }
    test_stamina::sprintBinding=nullptr;nextSprintBindingCheck=0;StaminaHudFrame();
    Check(std::wstring_view(sprintHint.data())==L"奔跑键未绑定","unbound sprint gets a truthful prompt");
    test_stamina::sprint=false;StaminaHudFrame();Check(!sprintHintLength,"server revoke hides the hint immediately");
    test_stamina::sprint=true;test_stamina::sprintBinding="ALT";
    send({2,3,90,100,true,false,false});
    Check(sprintHintLength>0&&std::wstring_view(sprintHint.data()).find(L"[ALT]")!=std::wstring_view::npos,"night keeps the new binding visible after a fresh grant");
    const auto prior=fake.fills;
    for(auto* menu:{&test_stamina::buy,&test_stamina::characters,&test_stamina::vote}){*menu=true;DrawStaminaHud(0);Paint();*menu=false;}
    Check(fake.fills==prior,"menus suppress stamina HUD");view.health=0;DrawStaminaHud(0);Paint();Check(fake.fills==prior,"death suppresses immediately even before retire packet");view.health=100;
    InvalidateStaminaHudResources();StaminaHudFrame();Check(prepared&&display.life==2&&sprintHintLength>0&&test_stamina::reads==1,"resource refresh keeps authority and font owner");
    send({2,4,0,0,false,false,false});DrawStaminaHud(0);Paint();Check(fake.fills==prior&&!display.visible&&!sprintHintLength,"retire snapshot hides bar and sprint hint");
    Init("InitHUD",0,nullptr);Check(!state.Sample(),"new connection clears old stamina");ShutdownStaminaHud();Check(!privateFont&&SnapshotResourceFonts().empty(),"shutdown releases actual GDI font registry");
    return fixture::Passed();
}
