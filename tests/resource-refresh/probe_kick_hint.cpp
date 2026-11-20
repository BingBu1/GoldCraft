// SPDX-License-Identifier: GPL-3.0-or-later
#define FIXTURE_RESOURCE_READER
#include "native/csol/client/scenario/core/human_kick.cpp"
#include "native/csol/client/scenario/core/night_player_lights.cpp"
#include "tests/csol-challenge-hud/ui_fixture.hpp"
#include "native/csol/client/resource_font_memory.hpp"
#include "tests/csol-hud-style/allocation_probe.hpp"
#include <fstream>
namespace test_hint
{
std::vector<std::byte> fontBytes;
unsigned reads{};
cl_entity_t localPlayer{}, viewModel{};
pfnUserMsgHook nightMessage{};
unsigned nativeImpulses{}, kickCommands{};
struct Surface final:fixture::Surface
{
    unsigned fills{},texts{};std::array<int,4> key{};
    vgui::VPANEL GetEmbeddedPanel()override{return 1;}
    vgui::HFont CreateFont()override{return 123;}
    bool AddGlyphSetToFont(vgui::HFont,const char*,int,int,int,int,int,int,int)override{return true;}
    void GetTextSize(vgui::HFont,const wchar_t* text,int& w,int& h)override
    {h=18;w=0;for(auto p=text;*p;++p)w+=*p>127?18:9;}
    void DrawFilledRect(int x,int y,int right,int bottom)override{++fills;key={x,y,right,bottom};}
    void DrawPrintText(const wchar_t*,int)override{++texts;}
};
}
namespace csol::client
{
bool BuyMenuVisible(){return false;}
bool CharacterMenuVisible(){return false;}
bool ScenarioMapVoteVisible(){return false;}
float HudStyleScale(){return 1;}
void InitializeHumanKickAnimation(){}
void ResetHumanKickAnimation(){}
void HumanKickAnimationSnapshot(const scenario::human_kick::Snapshot&,const scenario::human_kick::Snapshot&){}
std::vector<std::byte> ReadResource(const char* path)
{++test_hint::reads;fixture::Check(std::string_view(path)==upgrades::FontPath,"kick hint uses verified font");return test_hint::fontBytes;}
}
int main(int argc,char** argv)
{
    using namespace csol::client;using fixture::Check;
    Check(argc>=2,"real font provided");std::ifstream in(argv[1],std::ios::binary|std::ios::ate);Check(bool(in),"font opens");
    const auto size=in.tellg();test_hint::fontBytes.resize(size);in.seekg(0);in.read(reinterpret_cast<char*>(test_hint::fontBytes.data()),size);Check(bool(in),"font read");
    test_hint::Surface fake;surface=&fake;ownerThread=GetCurrentThreadId();view.health=100;
    namespace kick=csol::scenario::human_kick;
    const auto bytes=kick::Encode({1,kick::Enabled|kick::Ready,0});Check(state.Receive(*bytes),"actual availability received");
    namespace custom = csol::scenario::custom;
    metahook_api_t hooks{};
    hooks.HookUserMsg = [](const char*, pfnUserMsgHook callback) -> pfnUserMsgHook {
        test_hint::nightMessage = callback;
        return nullptr;
    };
    api = &hooks;
    engine.pfnHookUserMsg = [](const char*, pfnUserMsgHook) { return 1; };
    engine.GetLocalPlayer = [] { return &test_hint::localPlayer; };
    engine.GetViewModel = [] { return &test_hint::viewModel; };
    event_api_t events{};
    events.EV_FindModelIndex = [](const char*) { return 1; };
    engine.pEventAPI = &events;
    InitializeNightPlayerLights();
    auto nightPacket = custom::EncodeNightLighting({{}, custom::Phase::Night, 1, 1});
    Check(nightPacket && test_hint::nightMessage, "the real hand light receives a night policy");
    test_hint::nightMessage(custom::NightLightingMessage, int(nightPacket->size()), nightPacket->data());

    model_t rifle{}, flashlight{};
    std::strcpy(rifle.name, "models/v_ak47.mdl");
    std::strcpy(flashlight.name, custom::NightFlashlightView);
    test_hint::viewModel.model = &rifle;
    HumanKickFrame();DrawHumanKick(0,0);Paint();Check(fake.fills==1&&fake.texts==2,"actual Paint renders F key and hint");
    engine.Cmd_Argc = [] { return 2; };
    engine.Cmd_Argv = [](int) -> char* { static char argument[] = "100"; return argument; };
    engine.pfnServerCmd = [](const char* command) {
        Check(std::string_view(command) == "csol_kick\n", "send the current server kick command");
        ++test_hint::kickCommands;
        return 1;
    };
    previousImpulse = [] {
        ++test_hint::nativeImpulses;
        test_hint::localPlayer.curstate.effects ^= EF_DIMLIGHT;
    };
    Impulse();
    test_hint::viewModel.model = &flashlight;
    Impulse();
    DrawHumanKick(0, 0); Paint();
    Check(test_hint::nativeImpulses == 1 && test_hint::kickCommands == 1 && fake.fills == 1 &&
        (test_hint::localPlayer.curstate.effects & EF_DIMLIGHT),
        "holding the light forwards impulse100 and hides the kick hint");

    test_hint::viewModel.model = &rifle;
    Impulse();
    DrawHumanKick(0, 0); Paint();
    Check(test_hint::nativeImpulses == 1 && test_hint::kickCommands == 2 && fake.fills == 2 &&
        (test_hint::localPlayer.curstate.effects & EF_DIMLIGHT),
        "switching from the lit flashlight to a rifle restores the kick command and hint");
    for(auto [w,h]:{std::pair{640,480},{800,600},{1280,720},{1920,1080},{1920,1200},{2560,1080},{3840,2160}})
    {
        fake.width=w;fake.height=h;DrawHumanKick(0,0);Paint();const auto layout=csol::scenario::custom::StaminaLayout(w,h);
        Check(fake.key[0]==(w-26-10-hintWidth)/2&&fake.key[1]==h-layout.padding-26,"actual F hint follows bottom-center arrow");
        Check(fake.key[0]+26+10+hintWidth<=w-layout.padding&&fake.key[3]<=h-layout.padding,"complete key/text stay inside viewport");
    }
    allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<100;++i){HumanKickFrame();DrawHumanKick(0,0);Paint();}
    allocation_probe::active=false;Check(!allocation_probe::calls&&test_hint::reads==1,"warm hint100 frames allocate/read nothing");
    const auto count=fake.fills;view.health=0;DrawHumanKick(0,0);Paint();Check(fake.fills==count,"death hides hint");
    ShutdownNightPlayerLights();
    ShutdownHumanKick();Check(!privateFont&&SnapshotResourceFonts().empty(),"shutdown releases real font owner");return fixture::Passed();
}
