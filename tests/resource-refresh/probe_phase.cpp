// SPDX-License-Identifier: GPL-3.0-or-later
#define FIXTURE_RESOURCE_READER
#include "native/csol/client/scenario/core/challenge_phase.cpp"
#include "tests/csol-challenge-hud/ui_fixture.hpp"
#include "native/csol/client/resource_font_memory.hpp"
#include "tests/csol-hud-style/allocation_probe.hpp"
#include <fstream>
#include <map>
#include <thread>
namespace test_phase
{
bool buy{},characters{},vote{};
unsigned reads{};
std::vector<std::byte> fontBytes;
struct Surface final:fixture::Surface
{
    int nextTexture{10};unsigned uploads{},createdFonts{},configuredFonts{},filled{},texts{};
    std::map<vgui::HFont,int> heights;
    std::vector<std::array<int,4>> rectangles;
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    int CreateNewTextureID(bool)override{return ++nextTexture;}
    void DrawSetTextureRGBA(int,const unsigned char* rgba,int w,int h,int,bool)override
    {
        ++uploads;fixture::Check(w==64&&h==64,"actual UI uploads square64px phase icons");
        fixture::Check(std::any_of(rgba,rgba+w*h*4,[](auto byte){return byte!=0;}),"icon upload contains authored pixels");
    }
    vgui::HFont CreateFont()override{return 100+ ++createdFonts;}
    bool AddGlyphSetToFont(vgui::HFont h,const char*,int tall,int,int,int,int,int,int)override
    {++configuredFonts;heights[h]=tall;return true;}
    int GetFontTall(vgui::HFont h)override{return heights[h];}
    void GetTextSize(vgui::HFont h,const wchar_t* text,int& w,int& t)override{t=heights[h];w=int(std::wcslen(text))*t/2;}
    void DrawFilledRect(int,int,int,int)override{++filled;}
    void DrawTexturedRect(int x,int y,int r,int b)override{rectangles.push_back({x,y,r,b});}
    void DrawPrintText(const wchar_t*,int)override{++texts;}
};
}
namespace csol::client
{
bool BuyMenuVisible(){return test_phase::buy;}
bool CharacterMenuVisible(){return test_phase::characters;}
bool ScenarioMapVoteVisible(){return test_phase::vote;}
std::vector<std::byte> ReadResource(const char* path)
{++test_phase::reads;fixture::Check(std::string_view(path)==upgrades::FontPath,"only verified phase font is read");return test_phase::fontBytes;}
}
int main(int argc,char** argv)
{
    using namespace csol::client;using fixture::Check;namespace custom=csol::scenario::custom;
    Check(argc==3,"two real font fixtures supplied");
    std::ifstream in(argv[1],std::ios::binary|std::ios::ate);Check(bool(in),"real private font opens");
    const auto size=in.tellg();test_phase::fontBytes.resize(size);in.seekg(0);in.read(reinterpret_cast<char*>(test_phase::fontBytes.data()),size);Check(bool(in),"real private font fully read");
    test_phase::Surface fake;surface=&fake;ownerThread=GetCurrentThreadId();
    const auto packet=custom::EncodePhase({custom::Phase::Noon,false,1,1,180});
    Receive(custom::PhaseMessage,int(packet->size()),const_cast<std::byte*>(packet->data()));
    ChallengePhaseFrame();Check(prepared&&font&&privateFont&&accepted==1,"actual message prepares real GDI font and procedural icons");
    Check(fake.uploads==3&&test_phase::reads==1&&fake.createdFonts==1,"cold path reads once and uploads three icons");
    DrawChallengePhase(0);Paint();Check(fake.filled==1&&fake.texts==2&&fake.rectangles.size()==1,"actual Paint draws one icon and both phase/countdown labels");
    const auto bounds=fake.rectangles.back();Check(bounds[2]-bounds[0]==bounds[3]-bounds[1],"actual VGUI icon keeps equal axes");
    fake.rectangles.reserve(256);allocation_probe::calls=0;allocation_probe::active=true;
    for(unsigned i=0;i<100;++i){ChallengePhaseFrame();DrawChallengePhase(0);Paint();}
    allocation_probe::active=false;
    Check(allocation_probe::calls==0,"actual warm Frame/Paint performs no C++ heap allocations");
    Check(fake.uploads==3&&test_phase::reads==1&&fake.createdFonts==1,"warm frames do not read files, upload textures or allocate font handles");
    for(const auto [w,h]:{std::pair{640,480},{1024,768},{1920,1080},{1920,1200},{2560,1080},{3840,2160},{1280,960}})
    {
        fake.width=w;fake.height=h;ChallengePhaseFrame();DrawChallengePhase(0);Paint();const auto r=fake.rectangles.back();
        Check(r[0]>=0&&r[1]>=0&&r[2]<=w&&r[3]<=h&&r[2]-r[0]==r[3]-r[1],"real consumer remains on-screen and square across viewport changes");
    }
    Check(fake.createdFonts<=11&&fake.uploads==3&&test_phase::reads==1,"resolution tiers keep a bounded font cache and immutable art");
    const unsigned before=fake.filled;
    for(auto* menu:{&test_phase::buy,&test_phase::characters,&test_phase::vote}){*menu=true;DrawChallengePhase(0);Paint();*menu=false;}
    Check(fake.filled==before,"all custom menus suppress phase drawing");
    view.health=0;ChallengePhaseFrame();DrawChallengePhase(0);Paint();Check(fake.filled==before+1,"individual death retains map phase display");
    const auto saved=state.Sample(Now());InvalidateChallengePhaseResources();Check(!prepared&&state.Sample(Now())->challenge==saved->challenge,"resource retirement preserves authoritative phase");
    const unsigned fontsBefore=fake.createdFonts;ChallengePhaseFrame();Check(prepared&&fake.uploads==6&&test_phase::reads==1&&fake.createdFonts==fontsBefore,"resource rebuild reuses immutable GDI/font handles");
    const auto night=custom::EncodePhase({custom::Phase::Night,false,2,1,0});
    Receive(custom::PhaseMessage,int(night->size()),const_cast<std::byte*>(night->data()));ChallengePhaseFrame();
    Check(display.phase==custom::Phase::Night&&std::wstring_view(timeText.data())==L"持续至本局结束","actual night renders a lifetime label instead of a countdown");
    const auto ended=custom::EncodePhase({custom::Phase::Ended,false,3,1,0});
    Receive(custom::PhaseMessage,int(ended->size()),const_cast<std::byte*>(ended->data()));ChallengePhaseFrame();const auto last=fake.filled;DrawChallengePhase(0);Paint();Check(fake.filled==last,"ended server phase hides display");
    Init("InitHUD",0,nullptr);Check(!state.Sample(Now())&&!known,"new connection InitHUD clears old map ownership");
    Check(SnapshotResourceFonts().size()==1,"actual font registry tracks phase owner for update transactions");
    ShutdownChallengePhase();Check(!privateFont&&SnapshotResourceFonts().empty(),"actual shutdown releases GDI owner and registry record");
    return fixture::Passed();
}
