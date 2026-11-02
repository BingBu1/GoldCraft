// SPDX-License-Identifier: GPL-3.0-or-later
// Unchanged production controller/draw functions with controlled VGUI/engine
// boundaries. This verifies dispatch and ownership, not actual GPU pixels.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../native/csol/map_vote_presentation.hpp"
#include "../../native/csol/scenario/map_vote.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
using namespace csol;
namespace mv=csol::scenario::map_vote;
namespace vgui {enum{MOUSE_LEFT,MOUSE_RIGHT,FONTFLAG_ANTIALIAS};}
unsigned checks{},failures{};
void Check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
mv::Presentation presentation,original;
mv::Snapshot state;
mv::Countdown countdown;
std::unordered_map<std::string,int> textures;
bool loaded{},visible{},mouseHeld{},restoreMouse{},swallowMouse{},registered{},listPainting{},dragging{},gameUiVisible{};
int left{},top{},fontHeight{},scroll{},hover{-1},dragY{},dragStart{},font{};
float scale{1},angles[3]{};
std::uint16_t selection{};
unsigned rejected{},draws{},submitted{},restores{},readCalls{},activations{},deactivations{},clears{},buyCloses{},characterCloses{};
constexpr int Confirm=100,CloseButton=101,ScrollTrack=102,IN_ATTACK=1,IN_ATTACK2=2048;
bool focused{true},buyVisible{},characterVisible{},consoleVisible{};
double clockTime{};
std::vector<std::string> commands;
struct usercmd_t {unsigned buttons{};float forwardmove{},sidemove{},upmove{},viewangles[3]{};};
struct Panel {
    bool visible{};std::array<int,4> bounds{};
    void SetVisible(bool value){visible=value;}
    void SetBounds(int x,int y,int w,int h){bounds={x,y,w,h};}
    unsigned GetVPanel(){return 2;}
}panel,*listPanel=&panel;
struct Input {
    bool down[2]{};int x{},y{};
    void SetMouseCapture(int){}void SetCursorOveride(int){}
    bool IsMouseDown(int button){return down[button];}
    void GetCursorPos(int& a,int& b){a=x;b=y;}
}inputValue,*input=&inputValue;
struct Event {std::string kind;int texture{};unsigned panel{};std::array<int,4> rect{};std::wstring text;};
struct Surface {
    int width{1920},height{1080},texture{};std::vector<unsigned> stack;std::vector<Event> events;
    unsigned GetEmbeddedPanel(){return 1;}
    void PushMakeCurrent(unsigned p,bool){stack.push_back(p);}
    void PopMakeCurrent(unsigned p){Check(!stack.empty()&&stack.back()==p,"VGUI panel stack balanced");stack.pop_back();}
    void GetScreenSize(int& w,int& h){w=width;h=height;}
    int CreateFont(){return 1;}
    void AddGlyphSetToFont(int,const char*,int,int,int,int,int,int,int){}
    void DrawSetColor(int,int,int,int){}
    void DrawSetTexture(int value){texture=value;}
    void DrawTexturedRect(int x,int y,int r,int b){events.push_back({"image",texture,stack.back(),{x,y,r,b}});}
    void DrawSetTextFont(int){}void DrawSetTextColor(int,int,int,int){}void DrawSetTextPos(int,int){}
    void DrawPrintText(const wchar_t* value,int size){events.push_back({"text",0,stack.back(),{},std::wstring(value,size)});}
    void DrawFlushText(){}
    void GetTextSize(int,const wchar_t* s,int& w,int& h){w=int(wcslen(s))*fontHeight/2;h=fontHeight;}
    void SetCursor(int){}void LockCursor(){}void UnlockCursor(){}
}surfaceValue,*surface=&surfaceValue;
struct Client {void HideAllVGUIMenu(){}}client,*clientUI=&client;
int extensionValue{};int* extension=&extensionValue;
struct Engine {
    double GetClientTime(){return clockTime;}
    int(*Con_IsVisible)()=[](){return int(consoleVisible);};
    void GetViewAngles(float* data){std::fill_n(data,3,0);}
    void SetViewAngles(float*){}
    void pfnServerCmd(char* text){commands.emplace_back(text);}
}engine;
struct Downstream {
    void(*IN_ClearStates)()=[](){++clears;};
    void(*IN_ActivateMouse)()=[](){++activations;};
    void(*IN_DeactivateMouse)()=[](){++deactivations;};
}downstream;
bool Focused(){return focused;}
bool BuyMenuVisible(){return buyVisible;}
bool CharacterMenuVisible(){return characterVisible;}
void CloseBuyMenuForOverlay(){buyVisible=false;++buyCloses;}
void CloseCharacterMenuForOverlay(){characterVisible=false;++characterCloses;}
void ClearNativeWeaponSelection(){}
void SynchronizeSession(){}
bool Load(){return loaded;}
std::vector<std::byte> ReadResource(const char*){++readCalls;throw std::runtime_error("draw attempted resource IO");}
#include "actual_client.inc"

void Reset(unsigned count=7)
{
    presentation=original;state={};state.generation=1;state.revision=1;state.phase=mv::Phase::Open;state.seconds=20;state.count=std::uint8_t(count);
    for(unsigned i=0;i<count;++i){
        if(i>=presentation.maps.size()){auto map=presentation.maps[0];map.id=std::uint16_t(200+i);map.mapName="test_"+std::to_string(i);presentation.maps.push_back(map);}
        state.choices[i].id=presentation.maps[i].id;
    }
    textures.clear();int id{};for(const auto& path:mv::Resources(presentation))if(path.ends_with(".tga"))textures.emplace(path,++id);
    countdown.Reset();clockTime=1;countdown.Observe(state,clockTime);loaded=true;visible=false;mouseHeld=restoreMouse=swallowMouse=dragging=gameUiVisible=listPainting=false;
    focused=true;buyVisible=characterVisible=consoleVisible=false;selection=0;hover=-1;scroll=0;font=0;fontHeight=0;
    surfaceValue.width=1920;surfaceValue.height=1080;surfaceValue.events.clear();surfaceValue.stack.clear();inputValue={};commands.clear();
    readCalls=activations=deactivations=clears=buyCloses=characterCloses=0;Open();Layout();
}
void LayoutTests()
{
    for(const auto resolution:std::array{std::pair{1920,1080},std::pair{1280,720},std::pair{800,600},std::pair{640,480}}){
        Reset(16);surfaceValue.width=resolution.first;surfaceValue.height=resolution.second;Layout();
        const float expected=std::min({1.f,float(resolution.first)/932,float(resolution.second)/510});
        Check(std::abs(scale-expected)<.0001f,"fixed-size on large screen, proportional shrink on small screen");
        Check(listPanel->bounds[0]==X(presentation.list.x)&&listPanel->bounds[1]==Y(presentation.list.y),"clipping panel and hit transform share origin");
        scroll=MaxScroll();const auto last=Card(15);
        Check(last.y+last.h==presentation.list.y+presentation.list.h,"bottom scroll aligns final card with viewport");
        Check(Hit(X(last.x+30),Y(last.y+30))==15,"bottom-row pointer hits actual final map after resize");
        Check(Hit(X(last.x+30),Y(presentation.list.y-10))==-1,"cards above viewport cannot intercept input");
    }
    Reset(16);const auto thumb=Thumb();inputValue.x=X(thumb.x+5);inputValue.y=Y(thumb.y+5);inputValue.down[0]=true;
    Check(Scrollbar().x==presentation.list.x+623&&Scrollbar().w==14&&Scrollbar().h==431,"original14px scrollbar at list x623");
    ScenarioMapVoteFrame();inputValue.y+=int((Scrollbar().h-thumb.h)*scale);ScenarioMapVoteFrame();
    Check(dragging&&scroll==MaxScroll(),"dragging native-style thumb reaches final row");
    inputValue.down[0]=false;ScenarioMapVoteFrame();Check(!dragging,"mouse release ends thumb ownership");
}
void PaintTests()
{
    Reset();selection=state.choices[0].id;state.eligible=3;state.choices[0].votes=1;Paint();
    Check(std::any_of(surfaceValue.events.begin(),surfaceValue.events.end(),[](const auto& e){return e.text==L"已投票：1 / 3";}),"votes display actual ballots separately from electorate");
    Check(readCalls==0,"prepared Paint performs zero resource reads");
    Check(surfaceValue.stack.empty(),"Paint restores caller panel state");
    const int thumbnail=textures.at(presentation.maps[0].thumbnail.path),overlay=textures.at(presentation.cards[1][0].path);
    std::size_t imageIndex=surfaceValue.events.size(),overlayIndex=imageIndex;
    for(std::size_t i=0;i<surfaceValue.events.size();++i){const auto& e=surfaceValue.events[i];
        if(e.kind=="image"&&e.texture==thumbnail&&imageIndex==surfaceValue.events.size())imageIndex=i;
        if(e.kind=="image"&&e.texture==overlay&&overlayIndex==surfaceValue.events.size())overlayIndex=i;
    }
    Check(imageIndex<overlayIndex&&surfaceValue.events[imageIndex].panel==2&&surfaceValue.events[overlayIndex].panel==2,"selection border is emitted after thumbnail inside clipped list");
    const auto reads=readCalls;surfaceValue.events.clear();Paint();Check(readCalls==reads,"repeat Paint retains preloaded textures");
}
void OwnershipTests()
{
    Reset();Close();buyVisible=characterVisible=true;Open();
    Check(visible&&!buyVisible&&!characterVisible&&buyCloses==2&&characterCloses==2,"opening vote closes both custom menus");
    buyVisible=true;const auto old=activations;Close();Check(activations==old&&!restoreMouse,"closing vote cannot reacquire gameplay from buy menu");
    Reset();consoleVisible=true;const auto before=activations;Close();Check(activations==before,"console retains cursor ownership");
    usercmd_t cmd{IN_ATTACK|IN_ATTACK2,1,2,3,{}};ScenarioMapVoteCreateMove(&cmd,1);
    Check(!(cmd.buttons&(IN_ATTACK|IN_ATTACK2)),"dismiss click cannot become attack");
    consoleVisible=false;ScenarioMapVoteCreateMove(&cmd,1);Check(activations==before+1,"gameplay reacquired after console closes");
    Reset();gameUiVisible=true;Close();const auto ui=activations;RestoreMouse();Check(activations==ui,"game UI retains mouse ownership");
    Reset();clockTime=21;ScenarioMapVoteFrame();Check(!visible&&activations==1,"deadline closes and restores gameplay");
    Reset();Click(Confirm);Check(!visible&&commands.size()==1&&commands[0]=="csol_scenario_vote 1 74\n","confirmation sends only generation and chosen server ID");
    Reset();characterVisible=true;ScenarioMapVoteFrame();Check(!visible&&activations==0,"new character menu preempts vote without mouse steal");
    Reset();auto terminal=state;terminal.phase=mv::Phase::Cancelled;terminal.seconds=0;++terminal.revision;
    auto bytes=mv::Encode(terminal);Receive(mv::MessageName,int(bytes.size()),bytes.data());
    Check(!visible&&activations==1&&Remaining()==0,"server cancellation packet closes menu and restores gameplay immediately");
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);std::string json{std::istreambuf_iterator<char>(file),{}};
    original=mv::ParsePresentation(json);LayoutTests();PaintTests();OwnershipTests();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
