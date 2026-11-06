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
bool pendingOpen{};
#ifdef CSOL_MAP_VOTE_TEST_RESET
HANDLE privateFont{};
#endif
int left{},top{},fontHeight{},scroll{},hover{-1},dragY{},dragStart{},font{};
float scale{1},angles[3]{};
std::uint16_t selection{};
unsigned rejected{},draws{},submitted{},restores{},readCalls{},activations{},deactivations{},clears{},buyCloses{},characterCloses{};
constexpr int Confirm=100,CloseButton=101,ScrollTrack=102,IN_ATTACK=1,IN_ATTACK2=2048;
bool focused{true},buyVisible{},characterVisible{},consoleVisible{};
double clockTime{};
std::string engineLevel="maps/zs_deadend.bsp";
bool separateVoteClock{};double voteClockTime{};
ULONGLONG TestTickCount64(){return ULONGLONG((separateVoteClock?voteClockTime:clockTime)*1000.);}
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
    void DeleteTextureByID(int){}
}surfaceValue,*surface=&surfaceValue;
struct Client {void HideAllVGUIMenu(){}}client,*clientUI=&client;
int extensionValue{};int* extension=&extensionValue;
struct Engine {
    double GetClientTime(){return clockTime;}
    const char*(*pfnGetLevelName)()=[](){return engineLevel.c_str();};
    int(*Con_IsVisible)()=[](){return int(consoleVisible);};
    void GetViewAngles(float* data){std::fill_n(data,3,0);}
    void SetViewAngles(float*){}
    void pfnServerCmd(char* text){commands.emplace_back(text);}
    void pfnClientCmd(char* text){commands.emplace_back(text);}
}engine;
struct Downstream {
    void(*HUD_Frame)(double)=nullptr;
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
void SynchronizeSession();
void SynchronizeFrameSession();
bool Load(){if(!loaded){presentation=original;loaded=true;}return loaded;}
std::vector<std::byte> ReadResource(const char*){++readCalls;throw std::runtime_error("draw attempted resource IO");}
#define GetTickCount64 TestTickCount64
#include "actual_client.inc"
#undef GetTickCount64
#ifdef CSOL_MAP_VOTE_TEST_RESET
// Controlled boundary with signatures from the pinned IVGUI2Extension.h;
// callback bodies themselves are extracted unchanged from the production TU.
using CreateInterfaceFn=void*(*)(const char*,int*);
struct cl_enginefuncs_s;
enum class VGUI2Extension_Result { UNSET,IGNORED,HANDLED,OVERRIDE,SUPERCEDE,SUPERCEDE_SKIP_PLUGINS };
struct VGUI2Extension_CallbackContext {
    VGUI2Extension_Result Result{};void* pPluginReturnValue{};void* pRealReturnValue{};bool IsPost{};
};
struct IVGUI2Extension_BaseUICallbacks {
    virtual int GetAltitude()const=0;
    virtual void Initialize(CreateInterfaceFn*,int)=0;
    virtual void Start(cl_enginefuncs_s*,int)=0;
    virtual void Shutdown()=0;
    virtual void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)=0;
    virtual void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)=0;
    virtual void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)=0;
    virtual void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext*)=0;
    virtual void HideGameUI(VGUI2Extension_CallbackContext*)=0;
    virtual void ActivateGameUI(VGUI2Extension_CallbackContext*)=0;
    virtual void HideConsole(VGUI2Extension_CallbackContext*)=0;
    virtual void ShowConsole(VGUI2Extension_CallbackContext*)=0;
};
enum class OverlayLayer {MapVote};
void QueueOverlayPaint(OverlayLayer,void(*)()){}
namespace csol::client {using ::Paint;}
#include "actual_overlay.inc"
Overlay overlay;
#endif

std::string level="maps/zs_deadend.bsp";
double frameTime=-1;
unsigned clockCorrections{},largeClockCorrections{};
double largestCorrection{};
void ClearScene(){}
void ResetWeapons(bool){}
void ResetHudStyle(){}
void ResetCamera(){}
void ResetProjectiles(){}
void ResetCharacters(){}
void ResetStatusIcons(){}
void ResetWallStatus(){}
void ResetBuyMenu(){}
void ResetDamageDisplay(){}
void ResetSpray(){}
void ResetScenarioKill(){}
void ResetScenarioEffects(){}
void ResetCharacterSkillEffects(){}
void ResetScenarioSmoke(){}
void ResetEliteVisuals(){}
void ResetScenarioUpgrades(){}
void ResetScenarioAbilities(){}
void ResetBossHealth(){}
void ResetScenarioRoundHud(){}
void ResetScenarioRespawnUi(){}
void ResetPlayerClips(){}
void ResetRenderSettings(){}
void ScenarioEffectsFrame(){}
void ScenarioMusicFrame(){}
void ScenarioKillFrame(){}
void ScenarioUpgradesFrame(){}
void ScenarioAbilitiesFrame(){}
void CharacterSkillDisplayFrame(){}
void CharacterSkillEffectsFrame(){}
void BossHealthFrame(){}
void ScenarioRoundHudFrame(){}
void ScenarioRespawnUiFrame(){}
void EliteVisualsFrame(){}
void Balrog11FlameFrame(){}
void RenderSettingsFrame(){}
void SceneFrame(){}
void CharacterFrame(){}
void StatusIconsFrame(){}
void DamageDisplayFrame(){}
void SprayFrame(){}
void BuyMenuFrame(){}
void HudStyleFrame(){}
// Other subsystem boundaries stay enabled; their implementations are outside this UI regression.
bool ResourceReadsAllowed(){return true;}
void BossImpactCreateMove(int){}
void CharacterCreateMove(float,usercmd_t*,int){}
void BuyMenuCreateMove(usercmd_t*,int){}
#include "actual_session.inc"

void Reset(unsigned count=7)
{
    separateVoteClock=false;voteClockTime=0;engineLevel=level="maps/zs_deadend.bsp";frameTime=-1;
    presentation=original;state={};state.generation=1;state.revision=1;state.phase=mv::Phase::Open;state.seconds=20;state.count=std::uint8_t(count);
    for(unsigned i=0;i<count;++i){
        if(i>=presentation.maps.size()){auto map=presentation.maps[0];map.id=std::uint16_t(200+i);map.mapName="test_"+std::to_string(i);presentation.maps.push_back(map);}
        state.choices[i].id=presentation.maps[i].id;
    }
    textures.clear();int id{};for(const auto& path:mv::Resources(presentation))if(path.ends_with(".tga"))textures.emplace(path,++id);
    countdown.Reset();clockTime=1;countdown.Observe(state,clockTime);loaded=true;visible=false;pendingOpen=false;mouseHeld=restoreMouse=swallowMouse=dragging=gameUiVisible=listPainting=false;
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
    Reset();Click(Confirm);Check(visible&&commands.size()==1&&commands[0]=="csol_scenario_vote 1 74\n","confirmation sends only generation and chosen server ID and keeps ballot open");
    Reset();characterVisible=true;ScenarioMapVoteFrame();Check(!visible&&activations==0,"new character menu preempts vote without mouse steal");
    Reset();auto terminal=state;terminal.phase=mv::Phase::Cancelled;terminal.seconds=0;++terminal.revision;
    auto bytes=mv::Encode(terminal);Receive(mv::MessageName,int(bytes.size()),bytes.data());
    Check(!visible&&activations==1&&Remaining()==0,"server cancellation packet closes menu and restores gameplay immediately");
}
void Deliver(const mv::Snapshot& next)
{
    auto bytes=mv::Encode(next);Check(!bytes.empty(),"automatic-open fixture sends a valid server snapshot");
    Check(Receive(mv::MessageName,int(bytes.size()),bytes.data())==1,"valid automatic-open snapshot accepted");
}
mv::Snapshot BeginBlockedVote()
{
    auto next=state;++next.generation;next.revision=1;Deliver(next);return next;
}
void AutoOpenTests()
{
    for(int blocker=0;blocker<3;++blocker){
        Reset();Close(false);
        consoleVisible=blocker==0;gameUiVisible=blocker==1;focused=blocker!=2;
        auto next=BeginBlockedVote();
        Check(!visible&&pendingOpen&&deactivations==1,"blocked first snapshot defers display without taking mouse");
        // These callbacks may call Close even though the vote has never opened.
        Close(false);ScenarioMapVoteFrame();
        Check(!visible&&pendingOpen&&deactivations==1,"repeated host blocking preserves the first presentation request");
        ++next.revision;Deliver(next);
        Check(!visible&&pendingOpen,"same-generation tally does not lose pending presentation");
        consoleVisible=gameUiVisible=false;focused=true;ScenarioMapVoteFrame();
        Check(visible&&!pendingOpen&&state.generation==next.generation,"next focused unblocked frame opens the pending vote exactly once");
        Click(CloseButton);++next.revision;Deliver(next);ScenarioMapVoteFrame();
        Check(visible&&!pendingOpen,"close button cannot dismiss active ballot across tally and frame updates");
        Open();Check(visible&&!pendingOpen,"explicit open command is idempotent");
        Open();Check(visible&&!pendingOpen,"repeated open command cannot toggle active ballot off");
    }
    Reset();Close(false);consoleVisible=true;auto next=BeginBlockedVote();
    gameUiVisible=true;consoleVisible=false;ScenarioMapVoteFrame();
    Check(!visible&&pendingOpen,"one remaining UI blocker still defers display");
    gameUiVisible=false;focused=false;ScenarioMapVoteFrame();
    Check(!visible&&pendingOpen,"foreground focus remains required after UI closes");
    focused=true;ScenarioMapVoteFrame();Click(Confirm);++next.revision;Deliver(next);ScenarioMapVoteFrame();
    Check(visible&&!pendingOpen&&commands.size()==1,"submitted ballot stays visible through acknowledgement");
    for(const auto phase:std::array{mv::Phase::Cancelled,mv::Phase::Finished}){
        Reset();Close(false);consoleVisible=true;next=BeginBlockedVote();
        next.phase=phase;next.seconds=0;++next.revision;
        if(phase==mv::Phase::Finished)next.winner=next.choices[0].id;
        Deliver(next);consoleVisible=false;ScenarioMapVoteFrame();
        Check(!visible&&!pendingOpen&&Remaining()==0,"terminal snapshot consumes a deferred presentation");
    }
    Reset();Close(false);consoleVisible=true;next=BeginBlockedVote();clockTime=21;ScenarioMapVoteFrame();
    Check(!visible&&!pendingOpen,"local deadline expires a never-shown vote while blocked");
    consoleVisible=false;++next.revision;Deliver(next);ScenarioMapVoteFrame();
    Check(!visible&&!pendingOpen&&Remaining()==0,"late tally cannot revive an expired presentation");
    Reset();Close(false);consoleVisible=true;next=BeginBlockedVote();
    auto newer=next;++newer.generation;newer.revision=1;Deliver(newer);consoleVisible=false;ScenarioMapVoteFrame();
    Check(visible&&!pendingOpen&&state.generation==newer.generation,"new generation replaces a deferred older vote");
    Click(CloseButton);Deliver(next);ScenarioMapVoteFrame();
    Check(visible&&!pendingOpen&&state.generation==newer.generation,"stale generation cannot replace persistent current ballot");
    Reset();focused=false;ScenarioMapVoteFrame();focused=true;next=state;++next.revision;Deliver(next);ScenarioMapVoteFrame();
    Check(visible&&!pendingOpen,"focus recovery resumes an already displayed ballot");
#ifdef CSOL_MAP_VOTE_TEST_RESET
    Reset();Close(false);consoleVisible=true;BeginBlockedVote();
    Check(pendingOpen,"session-reset case starts with a genuine deferred vote");
    ResetScenarioMapVote();consoleVisible=false;ScenarioMapVoteFrame();
    Check(!visible&&!pendingOpen&&state.generation==0&&Remaining()==0,"actual session reset clears deferred vote and countdown");
#endif
}
#ifdef CSOL_MAP_VOTE_TEST_RESET
void Key(int key,const char* binding=nullptr,int down=1,bool post=false)
{
    int handled{};VGUI2Extension_CallbackContext context;context.IsPost=post;context.pPluginReturnValue=&handled;
    overlay.Key_Event(down,key,binding,&context);
    Check(post?context.Result==VGUI2Extension_Result::UNSET:
        context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS&&handled==1,
        "only pre callback consumes vote key before engine and other plugins");
}
void PersistentTests()
{
    Reset(16);selection=state.choices[3].id;scroll=MaxScroll();const auto chosen=selection;const auto offset=scroll;
    for(const int key:std::array<int,4>{27,242,'w',32}){Key(key);Check(visible,"Escape, secondary attack and movement cannot dismiss ballot");}
    Key(27,nullptr,0);Key(27,nullptr,1,true);
    Open();Click(CloseButton);ScenarioMapVoteFrame();
    Check(visible&&selection==chosen&&scroll==offset,"command and close button preserve selection and scroll");
    Key(13);Check(visible&&commands.size()==1&&commands.back()=="csol_scenario_vote 1 77\n","Enter submits selected ID without releasing gameplay");
    Check(activations==0,"submission never reacquires weapon mouse");
    usercmd_t command{IN_ATTACK|IN_ATTACK2,10,20,30,{1,2,3}};ScenarioMapVoteCreateMove(&command,1);
    Check(command.buttons==0&&command.forwardmove==0&&command.sidemove==0&&command.upmove==0,"visible ballot suppresses attacks and movement");
    auto acknowledgement=state;++acknowledgement.revision;acknowledgement.selection=chosen;acknowledgement.choices[3].votes=1;acknowledgement.eligible=1;
    Deliver(acknowledgement);ScenarioMapVoteFrame();
    Check(visible&&selection==chosen&&scroll==offset&&commands.size()==1,"acknowledgement does not reset ballot or resubmit");
    for(int blocker=0;blocker<5;++blocker){
        Reset(16);selection=state.choices[3].id;scroll=MaxScroll();
        VGUI2Extension_CallbackContext context;void* window{};unsigned message=WM_KILLFOCUS,wparam=0;long lparam{};
        if(blocker==0){consoleVisible=true;overlay.ShowConsole(&context);}
        if(blocker==1)overlay.ActivateGameUI(&context);
        if(blocker==2){focused=false;overlay.CallEngineSurfaceWndProc(window,message,wparam,lparam,&context);}
        if(blocker==3){focused=false;message=WM_ACTIVATEAPP;overlay.CallEngineSurfaceWndProc(window,message,wparam,lparam,&context);}
        if(blocker==4){characterVisible=true;ScenarioMapVoteFrame();}
        const auto held=deactivations;
        for(int frame=0;frame<3;++frame){
            ScenarioMapVoteFrame();command={IN_ATTACK|IN_ATTACK2,10,20,30,{}};ScenarioMapVoteCreateMove(&command,1);
            Check(!visible&&pendingOpen&&deactivations==held&&activations==0,"blocked frames neither steal focus nor hand input to weapons");
            Check(command.buttons==0&&command.forwardmove==0&&command.sidemove==0&&command.upmove==0,"suspended ballot suppresses queued attack and movement input");
        }
        context.IsPost=true;overlay.HideGameUI(&context);overlay.HideConsole(&context);
        focused=true;consoleVisible=characterVisible=false;ScenarioMapVoteFrame();
        Check(visible&&!pendingOpen&&selection==chosen&&scroll==offset,"host UI and focus restoration resumes same ballot selection and scroll");
    }
    Reset();Key('`',"toggleconsole");
    Check(!visible&&pendingOpen&&commands.size()==1&&commands[0]=="toggleconsole\n","console binding suspends rather than dismisses ballot");
    consoleVisible=true;ScenarioMapVoteFrame();consoleVisible=false;ScenarioMapVoteFrame();
    Check(visible&&!pendingOpen,"console binding resumes ballot on return");
    Key(0,"snapshot");Check(visible&&commands.back()=="snapshot\n","snapshot preserves ballot");
    for(int reason=0;reason<4;++reason){
        Reset();consoleVisible=true;VGUI2Extension_CallbackContext context;overlay.ShowConsole(&context);
        if(reason<2){auto end=state;end.phase=reason==0?mv::Phase::Cancelled:mv::Phase::Finished;end.seconds=0;++end.revision;if(reason)end.winner=end.choices[0].id;Deliver(end);}
        if(reason==2){clockTime=21;ScenarioMapVoteFrame();}
        if(reason==3)ResetScenarioMapVote();
        consoleVisible=false;ScenarioMapVoteFrame();command={IN_ATTACK|IN_ATTACK2,0,0,0,{}};
        ScenarioMapVoteCreateMove(&command,1);
        Check(!visible&&!pendingOpen&&Remaining()==0,"finish, cancel, expiry and session reset retire suspended ballot");
        Check(reason==3?activations==0:activations==1,"retirement restores gameplay once except session teardown");
    }
    Reset();inputValue.down[0]=true;clockTime=21;ScenarioMapVoteFrame();
    for(int frame=0;frame<2;++frame){command={IN_ATTACK|IN_ATTACK2,0,0,0,{}};ScenarioMapVoteCreateMove(&command,1);Check(command.buttons==0&&swallowMouse,"held confirmation cannot fire weapon after deadline");}
    inputValue.down[0]=false;ScenarioMapVoteCreateMove(&command,1);Check(!swallowMouse,"release clears the post-ballot mouse latch");
    command.buttons=IN_ATTACK;ScenarioMapVoteCreateMove(&command,1);Check(command.buttons==IN_ATTACK,"fresh attack works after ballot closes and release");
    Reset();VGUI2Extension_CallbackContext post;post.IsPost=true;overlay.ActivateGameUI(&post);overlay.ShowConsole(&post);
    Check(visible&&!gameUiVisible&&!pendingOpen,"post UI callbacks do not repeat ownership transitions");
}
void ReplacementLifecycleTests()
{
    for(const auto phase:std::array{mv::Phase::Finished,mv::Phase::Cancelled})for(int blocked=0;blocked<3;++blocked){
        Reset();auto terminal=state;++terminal.generation;terminal.revision=1;terminal.phase=phase;terminal.seconds=0;
        if(phase==mv::Phase::Finished)terminal.winner=terminal.choices[0].id;
        if(blocked){VGUI2Extension_CallbackContext context;if(blocked==1){consoleVisible=true;overlay.ShowConsole(&context);}else{focused=false;ScenarioMapVoteFrame();}}
        Deliver(terminal);
        Check(!visible&&!pendingOpen&&state.generation==terminal.generation&&Remaining()==0,"new-generation terminal retires old ballot");
        Check(activations==(blocked?0:1),"new-generation terminal retains old mouse-release ownership");
        focused=true;consoleVisible=false;ScenarioMapVoteFrame();
        usercmd_t command{IN_ATTACK|IN_ATTACK2,0,0,0,{}};ScenarioMapVoteCreateMove(&command,1);
        Check(activations==1,"new-generation terminal restores once after host blocker clears");
        ScenarioMapVoteFrame();ScenarioMapVoteCreateMove(&command,1);
        Check(activations==1,"terminal repeated frames never reacquire twice");
    }
    for(int blocker=0;blocker<3;++blocker){
        Reset();auto next=state;++next.generation;next.revision=1;
        // A new packet can arrive before the host's ShowConsole/focus callback.
        consoleVisible=blocker==0;gameUiVisible=blocker==1;focused=blocker!=2;
        Deliver(next);Check(!visible&&pendingOpen&&activations==0,"new open generation transfers old ownership to a blocked ballot");
        clockTime=21;ScenarioMapVoteFrame();Check(!visible&&!pendingOpen,"replacement can expire before ever becoming visible");
        consoleVisible=gameUiVisible=false;focused=true;ScenarioMapVoteFrame();
        usercmd_t command{};ScenarioMapVoteCreateMove(&command,1);
        Check(activations==1,"never-shown replacement still releases old vote mouse after expiry");
    }
    for(int suspended=0;suspended<2;++suspended){
        Reset();if(suspended){focused=false;ScenarioMapVoteFrame();}
        ResetScenarioMapVote();focused=true;
        Check(!visible&&!pendingOpen&&!restoreMouse&&!swallowMouse&&state.generation==0&&Remaining()==0,"actual session reset clears visible and suspended vote ownership");
        usercmd_t command{};ScenarioMapVoteFrame();ScenarioMapVoteCreateMove(&command,1);
        Check(activations==0,"session teardown does not reacquire gameplay mouse on later frame");
        loaded=true;presentation=original;mv::Snapshot first;first.generation=1;first.revision=1;first.phase=mv::Phase::Open;first.seconds=20;first.count=1;first.choices[0].id=74;
        Deliver(first);Check(visible&&!pendingOpen&&state.generation==1,"fresh session can open lower generation after real reset");
    }
}
#endif

void CrossMapClockTests()
{
    for(bool messageFirst:std::array{false,true})for(double skew:std::array{.01,.2,4.0}){
        Reset();separateVoteClock=true;voteClockTime=1000;clockTime=500;
        auto finished=state;finished.phase=mv::Phase::Finished;finished.seconds=0;finished.winner=finished.choices[0].id;++finished.revision;
        Deliver(finished);Check(!visible,"first-map terminal closes before actual map transition");
        engineLevel="maps/zs_lostcity.bsp";clockTime=10;
        if(!messageFirst){Frame(clockTime);Check(!visible&&state.generation==0,"actual frame session clears previous map ballot");}
        mv::Snapshot next;next.generation=2;next.revision=1;next.phase=mv::Phase::Open;next.seconds=20;next.count=2;next.choices[0].id=74;next.choices[1].id=98;
        clockTime=10+skew;voteClockTime=1001;Deliver(next);
        Check(visible&&!pendingOpen&&state.generation==2&&level==engineLevel,"real receive resets new-map session and opens second ballot");
        const auto oldActivations=activations;clockTime=10;voteClockTime=1001.01;Frame(clockTime);
        usercmd_t command{IN_ATTACK|IN_ATTACK2,10,20,30,{}};CreateMove(.01f,&command,1);
        Check(visible&&!pendingOpen&&Remaining()==20,"first interpolated frame cannot dismiss freshly received second-map vote");
        Check(activations==oldActivations&&command.buttons==0&&command.forwardmove==0,"first frame retains ballot mouse and suppresses gameplay input");
        // Same generation AND revision; a retransmission at another packet/render
        // time must neither cancel ownership nor reset the fixed local deadline.
        clockTime=10+skew/2;voteClockTime=1001.02;Deliver(next);Frame(10);
        Check(visible&&state.generation==2&&state.revision==1&&Remaining()==20,"same-revision snapshot survives packet render clock rollback");
        ++next.revision;next.seconds=19;clockTime=11;voteClockTime=1002;Deliver(next);Frame(11);
        Check(visible&&Remaining()==19,"tally revision advances without resetting presentation");
        clockTime=-500;voteClockTime=1005;Frame(clockTime);
        Check(visible&&Remaining()==16,"large negative render jump neither cancels nor extends deadline");
        clockTime=100000;voteClockTime=1006;Frame(clockTime);
        Check(visible&&Remaining()==15,"large positive render jump cannot expire real ballot");
        voteClockTime=1021;Frame(clockTime);
        Check(!visible&&!pendingOpen&&Remaining()==0&&activations==oldActivations+1,"monotonic elapsed deadline closes once after exactly its budget");
        voteClockTime=1022;++next.revision;next.seconds=20;Deliver(next);Frame(clockTime);
        Check(!visible&&Remaining()==0,"late same-generation packet cannot extend expired wall-clock deadline");
        engineLevel="";Frame(clockTime);Check(state.generation==0&&!visible&&!pendingOpen,"actual disconnect session resets ballot");
        engineLevel="maps/zs_deadend.bsp";clockTime=1;voteClockTime=1030;next.generation=1;next.revision=1;next.seconds=20;Deliver(next);
        Check(visible&&state.generation==1,"new connection accepts lower generation after genuine reset");
        next.phase=mv::Phase::Cancelled;next.seconds=0;++next.revision;clockTime=-100;voteClockTime=1031;Deliver(next);
        Check(!visible&&!pendingOpen&&Remaining()==0,"authoritative cancellation closes despite unrelated render clock rollback");
    }
}

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);std::string json{std::istreambuf_iterator<char>(file),{}};
    original=mv::ParsePresentation(json);LayoutTests();PaintTests();OwnershipTests();AutoOpenTests();
#ifdef CSOL_MAP_VOTE_TEST_RESET
    PersistentTests();
    ReplacementLifecycleTests();
    CrossMapClockTests();
#endif
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
