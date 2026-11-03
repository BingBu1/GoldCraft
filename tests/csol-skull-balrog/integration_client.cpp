// SPDX-License-Identifier: GPL-3.0-or-later
// Full production weapon dispatcher, inventory, predictors and death-icon TUs.
// Engine and unrelated UI services are controlled at their SDK interfaces.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons.hpp"
#include "../../native/csol/client/weapon_inventory.hpp"
#include "../../native/csol/client/m249ex.hpp"
#include "../../native/csol/client/balrog11.hpp"
#include "../../native/csol/client/death_notices.hpp"
#include "../../native/csol/client/hud_style.hpp"
#include "../../native/csol/m249ex.hpp"
#include "../../native/csol/balrog11.hpp"
#include <event_api.h>
#include <r_efx.h>
#include <pmtrace.h>
#include <pm_defs.h>
#include <in_buttons.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <source_location>

namespace csol::client {
cl_enginefunc_t engine{}; cl_exportfuncs_t downstream{}; ref_params_t view{}; metahook_api_t* api{};
void ClearScene(){} void ForgetSceneEntity(const cl_entity_t*){} void ResetCamera(){} void ResetCharacters(){}
void ResetStatusIcons(){} void ResetProjectiles(){} void ResetBeamScope(){} void DrawBeamScope(int,int,int,float){}
int CameraIsThirdPerson(){return 0;}
void ResetSpray(){} void ResetCharacterSkillEffects(){} void ResetHudStyle(){} void ResetEliteVisuals(){}
void ResetScenarioRoundHud(){}
bool CharacterSkillInfiniteAmmoFor(int){return false;}
bool DrawCharacterSkillInfiniteAmmo(int,int,int,int,int,int){return false;}
bool TryActivateScenarioAbility(){return false;} bool TryScenarioHealingSlot(int){return false;}
void BeginHudStyle(int,int){} void ConstrainHudStyle(bool,bool,bool){}
bool HudStyleVitalsVisible(){return false;} bool HudStyleWeaponVisible(){return false;} bool HudStyleMoneyVisible(){return false;}
hud_style::Vitals HudStyleVitalsState(){return {};} hud_style::Money HudStyleMoneyState(){return {};}
float HudStyleScale(){return 1;} void DrawHudStyle(){}
bool Exists(const char*){return false;} std::vector<std::byte> ReadResource(const char*){return {};}
hook_t* Hook(void* address,void* replacement,void** original){
    *original=address;
    if(address==reinterpret_cast<void*>(engine.pfnSPR_GetList))engine.pfnSPR_GetList=reinterpret_cast<decltype(engine.pfnSPR_GetList)>(replacement);
    return nullptr;
}
}
namespace {
using namespace csol;using namespace csol::client;
int checks{},failures{},animCount{},lastAnim{},soundCount{},traceCount{},pushes{},pops{},stockCalls{},forwarded{};
float now=100,localPrediction=1;
std::string level="maps/cs_assault.bsp",sent,selectedCommand;
std::map<std::string,pfnUserMsgHook> messages;
std::map<std::string,xcommand_t> commands;
std::map<std::string,void(*)(event_args_t*)> events;
cl_entity_t local{},model{};event_api_t eventApi{};efx_api_t effectApi{};physent_t physical{};TEMPENTITY temp{};cvar_t protocol{};
void Check(bool value,const char* reason,std::source_location where=std::source_location::current()){
    ++checks;if(!value){++failures;std::cerr<<"FAIL "<<where.line()<<' '<<reason<<'\n';}}
int Native(const char*,int,void*){++forwarded;return 17;}
void Fallback(){}
void Send(const char* name,std::vector<std::byte> bytes){Check(messages.contains(name),"production message hook installed");messages.at(name)(name,int(bytes.size()),bytes.data());}
void Register(){
    for(auto d:std::initializer_list<WeaponDefinition>{
        {m249ex::Id,0,1,24,-1,m249ex::Reserve,-1,m249ex::Clip,0,m249ex::Classname},
        {balrog11::Id,0,1,53,52,balrog11::Reserve,4,balrog11::Clip,0,balrog11::Classname}})
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(d));
    std::vector<std::byte> stock;for(char ch:std::string("weapon_knife"))stock.push_back(std::byte(ch));stock.push_back(std::byte{});
    for(int value:{255,255,255,255,2,0,29,0})stock.push_back(std::byte(value));Send("WeaponList",stock);
    WeaponOwnership owned{};owned[m249ex::Id]=owned[balrog11::Id]=owned[29]=true;
    for(const auto& part:EncodeWeaponInventory(owned,1))Send(WeaponInventoryMessage,part);
}
void Select(int id,int clip,int reserve,int charge=0){Send(WeaponCurrentMessage,EncodeWeaponCurrent({id,1,clip,reserve,charge}));}
local_state_t State(int id,int clip,int reserve,int charge=0){
    local_state_t s{};s.client.m_iId=id;s.client.health=100;s.client.iuser3=1;s.client.flags=FL_ONGROUND;
    s.client.viewmodel=12;s.client.fov=90;s.client.weaponanim=id==m249ex::Id?int(m249ex::Draw):int(balrog11::Draw);s.client.vuser4[1]=float(reserve);
    auto& w=s.weapondata[0];w.m_iId=id;w.m_iClip=clip;w.fuser1=.25f;
    w.m_flNextPrimaryAttack=w.m_flNextSecondaryAttack=w.m_flTimeWeaponIdle=-.001f;
    if(id==balrog11::Id)w.m_iWeaponState=balrog11::PackCharge({0,charge});return s;
}
local_state_t Dispatch(local_state_t from,int buttons,int run=1){
    local_state_t to{};usercmd_t cmd{};cmd.buttons=short(buttons);cmd.msec=10;
    WeaponPostRunCmd(&from,&to,&cmd,run,now,123);return to;
}
void Setup(){
    static metahook_api_t hooks{};api=&hooks;
    hooks.HookUserMsg=[](const char* name,pfnUserMsgHook fn)->pfnUserMsgHook{messages[name]=fn;return Native;};
    hooks.HookCmd=[](const char* name,xcommand_t fn)->xcommand_t{commands[name]=fn;return Fallback;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook fn){messages[name]=fn;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t fn){commands[name]=fn;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &protocol;};
    engine.Cvar_SetValue=[](const char*,float value){protocol.value=value;};
    engine.GetClientTime=[](){return now;};engine.pfnGetLevelName=[](){return level.c_str();};
    engine.pfnGetCvarFloat=[](const char* name){return !std::strcmp(name,"cl_lw")?localPrediction:!std::strcmp(name,"hud_fastswitch")?0.f:1.f;};
    engine.pfnGetCvarString=[](const char*)->char*{static char value[]="small";return value;};
    engine.Cmd_Argv=[](int){return selectedCommand.data();};engine.pfnClientCmd=[](const char* command){sent=command;return 1;};
    engine.pfnServerCmd=[](const char* command){sent=command;return 1;};
    engine.pfnGetScreenInfo=[](SCREENINFO* screen){screen->iWidth=1280;screen->iHeight=720;return 1;};
    engine.pfnSPR_Load=[](const char*){return 1;};
    engine.pfnSPR_Set=[](int,int,int,int){};
    engine.pfnSPR_DrawAdditive=[](int,int,int,const wrect_t*){};
    engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){};
    engine.pfnHookEvent=[](const char* path,void(*callback)(event_args_t*)){events[path]=callback;};
    engine.pfnWeaponAnim=[](int seq,int){++animCount;lastAnim=seq;};
    local.index=1;model.curstate.body=7;engine.GetLocalPlayer=[](){return &local;};engine.GetViewModel=[](){return &model;};
    engine.pfnAngleVectors=[](const float*,float* f,float* r,float* u){f[0]=1;f[1]=f[2]=0;r[1]=-1;r[0]=r[2]=0;u[2]=1;u[0]=u[1]=0;};
    engine.pfnRandomLong=[](int lo,int){return lo;};engine.pfnRandomFloat=[](float lo,float hi){return (lo+hi)/2;};
    engine.pEventAPI=&eventApi;engine.pEfxAPI=&effectApi;
    eventApi.EV_IsLocal=[](int index){return int(index==0);};eventApi.EV_FindModelIndex=[](const char*){return 15;};
    eventApi.EV_PlaySound=[](int,float*,int,const char*,float,float,int,int){++soundCount;};
    eventApi.EV_LocalPlayerViewheight=[](float* h){h[0]=h[1]=0;h[2]=28;};
    eventApi.EV_SetUpPlayerPrediction=[](int,int){};eventApi.EV_PushPMStates=[](){++pushes;};eventApi.EV_PopPMStates=[](){++pops;};
    eventApi.EV_SetSolidPlayers=[](int){};eventApi.EV_SetTraceHull=[](int){};
    eventApi.EV_PlayerTrace=[](float*,float* end,int,int,pmtrace_t* tr){++traceCount;tr->fraction=1;std::copy_n(end,3,tr->endpos);};
    eventApi.EV_GetPhysent=[](int){return &physical;};effectApi.R_TempModel=[](float*,float*,float*,float,int,int){return &temp;};
    downstream.HUD_PostRunCmd=[](local_state_t* f,local_state_t* t,usercmd_t*,int,double,unsigned){++stockCalls;Check(f->client.m_iId<32,"stock table never sees extended ID");if(f->client.m_iId)*t=*f;};
    engine.pfnSPR_GetList=[](char*,int* count)->client_sprite_t*{static client_sprite_t one{};one.iRes=640;std::strcpy(one.szName,"ordinary");*count=1;return &one;};
    view.health=100;InitializeWeapons();InitializeWeaponInventory();InitializeM249EX();InitializeBalrog11();
    SynchronizeFrameSession();ResetWeapons(true);Register();
}
void Run(){
    Check(events.contains(m249ex::Event)&&events.contains(balrog11::Event),"both actual event callbacks installed");
    for(const int id:{m249ex::Id,balrog11::Id}){
        const int capacity=id==m249ex::Id?120:7,reserve=id==m249ex::Id?240:32;
        Select(id,capacity,reserve,id==balrog11::Id?7:0);
        auto hud=CurrentWeaponHudState();Check(hud.id==id&&hud.kind==hud_style::AmmoKind::Magazine&&!hud.specialAmmo,"extended gun uses ordinary magazine HUD");
        Check(hud.clip==capacity&&hud.reserve==reserve&&hud.secondary==(id==balrog11::Id?7:0),"actual reliable message reaches HUD without secondary reserve clamp");
        commands.at("slot1")();usercmd_t selection{};selection.buttons=IN_ATTACK;sent.clear();WeaponInventoryCreateMove(.01f,&selection,1);
        Check(!(selection.buttons&IN_ATTACK)&&!sent.empty(),"actual slot selection consumes confirm click and issues gun command");
        auto result=Dispatch(State(id,capacity,reserve,id==balrog11::Id?7:0),IN_ATTACK);
        Check(result.weapondata[0].m_iClip==capacity-1,"dispatcher reaches the correct actual predictor");
        const auto animations=animCount;Dispatch(result,IN_ATTACK);Check(animCount==animations,"cooldown cannot restart attack animation");
        Select(29,-1,0);const int before=animCount;Dispatch(State(id,capacity,reserve),IN_ATTACK);
        Check(animCount==before,"late prediction after knife selection cannot animate knife");
        event_args_t args{};args.entindex=1;args.bparam2=1;args.fparam1=1;args.fparam2=.0735f;
        events.at(id==m249ex::Id?m249ex::Event:balrog11::Event)(&args);
        Check(animCount==before,"late original event cannot animate a different selected gun");
        Select(id,capacity-1,reserve,id==balrog11::Id?6:0);hud=CurrentWeaponHudState();
        Check(hud.clip==capacity-1&&hud.secondary==(id==balrog11::Id?6:0),"reliable switch-back invalidates old predicted clip and charge");
    }
    Select(balrog11::Id,7,32,7);auto state=State(balrog11::Id,7,32,7);auto fired=Dispatch(state,IN_ATTACK2);
    Check(CurrentWeaponHudState().secondary==6&&CurrentWeaponHudState().clip==7&&balrog11::UnpackCharge(fired.weapondata[0].m_iWeaponState).charges==6,"flame consumes internal charge but not shotgun shell through public HUD");
    Select(balrog11::Id,7,32,0);Check(CurrentWeaponHudState().secondary==0,"server correction clears final predicted charge");
    ResetWeapons(false);Check(CurrentWeaponHudState().id==0&&M249EXPredictedAmmo()==-1&&Balrog11PredictedCharges()==-1,"round reset clears every new prediction owner");
    Check(FindWeaponDefinition(m249ex::Id)&&FindWeaponDefinition(balrog11::Id),"round reset retains map-scoped registration");
    level="maps/de_dust2.bsp";SynchronizeFrameSession();
    Check(!FindWeaponDefinition(m249ex::Id)&&!FindWeaponDefinition(balrog11::Id),"map lifecycle discards registrations and inventory");
    InitializeDeathNotices();char path[]="sprites/hud.txt";int count{};auto* icons=engine.pfnSPR_GetList(path,&count);
    for(const auto& [name,x,y]:{std::tuple{"d_m249ex",122,240},std::tuple{"d_balrog11",172,144}})for(int res:{320,640}){
        auto* it=std::find_if(icons,icons+count,[&](const auto& a){return a.iRes==res&&!std::strcmp(a.szName,name);});
        Check(it!=icons+count&&it->rc.left==x&&it->rc.top==y&&it->rc.right==x+48&&it->rc.bottom==y+16,"original death atlas rectangle registered at both resolutions");}
    Check(engine.pfnSPR_GetList(path,&count)==icons,"death HUD sprite pointer remains stable");
    Check(pushes==pops,"prediction collision stack balanced across current and late callbacks");
}
}
int main(){Setup();Run();std::cout<<"skull_balrog_integration checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
