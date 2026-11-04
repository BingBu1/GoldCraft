// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the complete production client against the real SDK declarations.
#include "../../native/csol/client/melee.cpp"
#include "wire.hpp"
#include <iostream>
#include <limits>

namespace csol::client {
cl_enginefunc_t engine{}; cl_exportfuncs_t downstream{}; ref_params_t view{};
WeaponDefinition definitions[2]{{melee::DragonSwordId,2,1,-1,-1,-1,-1,-1,0,"weapon_dragonsword"},
    {melee::SkullAxeId,2,1,-1,-1,-1,-1,-1,0,"weapon_skullaxe"}};
WeaponCurrent current{};
bool presentation = true;
const WeaponDefinition* FindWeaponDefinition(int id)
{ return id == melee::DragonSwordId ? &definitions[0] : id == melee::SkullAxeId ? &definitions[1] : nullptr; }
const WeaponCurrent& CurrentExtendedWeapon() { return current; }
bool WeaponPresentationAllowed(int id) { return presentation && current.id == id; }
}
using namespace csol;
using namespace csol::client;
namespace {
int checks{}, failures{}, animations{}, lastAnimation{-1}, temporaryCalls{}, pushes{}, pops{};
int wantedHull{}, recordedBody{};
float rightHand{1}, localPrediction{1}, timeNow{30};
bool hitWorld{};
efx_api_t effects{}; event_api_t events{}; TEMPENTITY temp{};
cl_entity_t local{}, viewModel{};
pfnUserMsgHook receiver{};
void Check(bool good, const char* text)
{ ++checks; if (!good) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
bool Near(float a, float b) { return std::abs(a-b) < .0001f; }
void Setup()
{
    local.index=1; viewModel.curstate.body=4;
    engine.GetLocalPlayer=[](){return &local;}; engine.GetViewModel=[](){return &viewModel;};
    engine.GetClientTime=[](){return timeNow;};
    engine.pfnGetCvarFloat=[](const char* name){return !std::strcmp(name,"cl_lw")?localPrediction:rightHand;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook callback){Check(!std::strcmp(name,"CSOMelee"),"independent message name");receiver=callback;return 1;};
    engine.pfnWeaponAnim=[](int anim,int body){++animations;lastAnimation=anim;recordedBody=body;};
    engine.pfnAngleVectors=[](const float*,float* f,float* r,float* u){f[0]=1;f[1]=f[2]=0;r[1]=-1;r[0]=r[2]=0;u[2]=1;u[0]=u[1]=0;};
    engine.pEfxAPI=&effects; engine.pEventAPI=&events;
    events.EV_FindModelIndex=[](const char* path){Check(!std::strcmp(path,"models/ef_dragonsword.mdl"),"original effect model");return 7;};
    events.EV_PushPMStates=[](){++pushes;};events.EV_PopPMStates=[](){++pops;};
    events.EV_SetSolidPlayers=[](int except){Check(except==0,"exclude local solid player");};
    events.EV_SetTraceHull=[](int hull){wantedHull=hull;};
    events.EV_PlayerTrace=[](float*,float*,int flags,int ignore,pmtrace_t* trace){
        Check(flags==PM_NORMAL && ignore==-1,"prediction trace contract");trace->fraction=hitWorld?.5f:1;trace->ent=0;};
    effects.R_TempModel=[](float* origin,float*,float*,float life,int index,int bounce){
        ++temporaryCalls;Check(Near(life,.35f)&&index==7&&!bounce,"original temp model lifetime");
        Check(Near(origin[2],view.vieworg[2]+16),"original initial view z offset");temp={};return &temp;};
    downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){
        Check(from->client.m_iId==0,"extended ID hidden from stock predictor");*to=*from;to->client.weaponanim=99;to->client.viewmodel=99;};
    InitializeMeleeWeapons();Check(receiver!=nullptr,"message callback installed");
}
local_state_t State(int id)
{
    ResetMeleeWeapons(); current={id,1,-1,0,0};
    local_state_t s{};s.client.m_iId=id;s.client.health=100;s.client.iuser3=1;
    s.client.viewmodel=12;s.client.weaponanim=3;s.client.view_ofs[2]=28;
    s.weapondata[0].m_iId=id;s.weapondata[0].m_iClip=-1;return s;
}
local_state_t Predict(local_state_t source,int buttons,int msec=10,int run=1)
{
    local_state_t out{};usercmd_t cmd{};cmd.buttons=short(buttons);cmd.msec=byte(msec);
    Check(PredictMeleeWeapon(&source,&out,&cmd,run,0,1),"melee predictor accepts exact registered identity");
    Check(source.client.m_iId==current.id && out.client.m_iId==current.id,"identity restored around downstream");
    Check(out.client.viewmodel==12,"viewmodel ownership preserved");return out;
}
int Complete(int id,int serial,int animation,int contact,int offset=0)
{
    unsigned char data[]{1,byte(id),byte(id>>8),byte(serial),byte(serial>>8),byte(animation),byte(contact),byte(offset),byte(offset>>8)};
    return receiver("CSOMelee",sizeof(data),data);
}
void Handedness()
{
    static cvar_t hand{};
    static const char* directory = "cstrike";
    model_t model{}; model.type = mod_studio; viewModel.model = &model;
    char setting[] = "1"; hand.string = setting; hand.flags = 7;
    engine.pfnGetCvarPointer = [](const char* name) { return !std::strcmp(name,"cl_righthand") ? &hand : nullptr; };
    engine.pfnGetGameDirectory = []() { return directory; };
    for (int id : {melee::DragonSwordId, melee::SkullAxeId}) {
        current = {id,1,-1,0,0};
        std::strcpy(model.name, melee::Resource(melee::WeaponVariant(id)).viewModel);
        for (float value : {0.f,1.f,.5f,-1.f,2.f}) for (int flags : {0,STUDIO_RENDER,STUDIO_EVENTS,STUDIO_RENDER|STUDIO_EVENTS}) {
            hand.value = value;
            {
                MeleeHandScope scope(&viewModel,flags);
                Check(hand.value == (flags ? (std::trunc(value)==0 ? 1.f : 0.f) : value),
                    "CSO knife handedness covers both render and event bone sampling");
                Check(hand.string==setting && hand.flags==7 && viewModel.model==&model,
                    "hand scope preserves cvar text, flags and model identity");
            }
            Check(hand.value==value,"hand scope restores exact user value");
            try { MeleeHandScope scope(&viewModel,flags); throw 5; }
            catch(int) { Check(hand.value==value,"hand scope restores after draw failure"); }
        }
        for (int boundary=0;boundary<6;++boundary) {
            hand.value=1;current={id,1,-1,0,0};directory="cstrike";
            std::strcpy(model.name,melee::Resource(melee::WeaponVariant(id)).viewModel);
            cl_entity_t other=viewModel;auto* entity=&viewModel;
            if(boundary==0)entity=&other;
            if(boundary==1)current.state=0;
            if(boundary==2)current.id=29;
            if(boundary==3)directory="czero";
            if(boundary==4)std::strcpy(model.name,"models/v_k3.mdl");
            if(boundary==5)entity=nullptr;
            { MeleeHandScope scope(entity,STUDIO_RENDER);Check(hand.value==1,"unrelated draw or stale identity never flips handedness"); }
            Check(hand.value==1,"excluded draw preserves setting");
        }
    }
    current={melee::DragonSwordId,1,-1,0,0};directory="cstrike";
    std::strcpy(model.name,melee::DragonSwordResources.viewModel);
    hand.value=std::numeric_limits<float>::quiet_NaN();
    { MeleeHandScope scope(&viewModel,STUDIO_RENDER);Check(std::isnan(hand.value),"invalid cvar is not rewritten"); }
    Check(std::isnan(hand.value),"invalid cvar remains unchanged");
    engine.pfnGetCvarPointer=nullptr;viewModel.model=nullptr;
}
}
int main()
{
    bool serials=true;
    for (unsigned serial=0;serial<65536;++serial) {
        weapon_data_t packet{};
        melee::WritePrediction({serial,std::uint16_t(serial),8,.3f},packet);
        const auto decoded=melee::ReadPrediction(wire::RoundTrip(packet));
        serials &= decoded.serial==serial && decoded.primaryOrdinal==(serial&1) &&
            decoded.pendingAnimation==8 && std::abs(decoded.pendingTime-.3f)<.000126f;
    }
    Check(serials,"all 16-bit serials and ordinal parity survive actual delta fields");
    weapon_data_t unsent{};unsent.iuser2=8;unsent.iuser3=65535;unsent.iuser1=300;
    const auto lost=wire::RoundTrip(unsent);
    Check(!lost.iuser2 && !lost.iuser3 && lost.iuser1!=300,"original unmapped fields and scaled ordinal are not wire-safe");
    Setup();
    Handedness();
    auto state=Predict(State(melee::DragonSwordId),IN_ATTACK);
    state.weapondata[0]=wire::RoundTrip(state.weapondata[0]);
    Check(lastAnimation==1 && recordedBody==4,"first dragon slash preserves character hands");
    Check(Near(state.weapondata[0].fuser1/8,.59f) && melee::ReadPrediction(state.weapondata[0]).primaryOrdinal==1,"first delayed hit and primary ordinal");
    const int first=animations;
    for(int i=0;i<3;++i)state=Predict(state,IN_ATTACK);
    Check(animations==first,"unrendered animation never restarts during pending hit");
    auto input=state;input.weapondata[0].fuser1=-.008f;
    auto silent=Predict(input,0,10,0);
    Check(!melee::ReadPrediction(silent.weapondata[0]).pendingAnimation,"silent replay still advances simulation");
    Check(animations==first,"replay is presentation silent");
    state=Predict(input,0);
    Check(!melee::ReadPrediction(state.weapondata[0]).pendingAnimation && Near(state.weapondata[0].m_flNextPrimaryAttack,.64f),"delayed miss completes with original residual cooldown");
    Check(pushes==pops,"prediction saves/restores player states");
    Check(Complete(melee::DragonSwordId,1,1,0)==1 && temporaryCalls==1,"authority creates dragon slash effect");
    Check(temp.entity.curstate.sequence==0 && temp.entity.curstate.renderamt==128 &&
        temp.entity.curstate.rendermode==kRenderTransAdd && Near(temp.fadeSpeed,3),"original additive/alpha/fade contract");
    Check(Complete(melee::DragonSwordId,1,1,0)==1 && temporaryCalls==1,"duplicate completion never duplicates effect");
    view.vieworg[0]=23;view.viewangles[1]=42;temp.callback(&temp,0,timeNow);
    Check(Near(temp.entity.origin[0],23)&&Near(temp.entity.angles[1],42),"effect follows current view each frame");
    state.weapondata[0].m_flNextPrimaryAttack=state.weapondata[0].m_flNextSecondaryAttack=-.001f;
    state=Predict(state,IN_ATTACK);Check(lastAnimation==2 && Near(state.weapondata[0].fuser1/8,.29f),"alternate slash uses original shorter hit delay");
    Check(Complete(melee::DragonSwordId,2,2,0)==1 && temporaryCalls==1,"alternate slash does not invent type84 effect");
    state.weapondata[0].m_iWeaponState&=~15;state.weapondata[0].m_flNextSecondaryAttack=-.001f;
    state=Predict(state,IN_ATTACK|IN_ATTACK2);Check(lastAnimation==4,"secondary input has priority");
    rightHand=0;
    Check(Complete(melee::DragonSwordId,3,4,0)==1 && lastAnimation==5 && temporaryCalls==2 && temp.entity.curstate.sequence==3,
        "left-hand thrust finishes with attack animation5 and effect3");
    const int before=temporaryCalls;current={29,1,-1,0,0};ReceiveMeleeState(current);temp.callback(&temp,0,timeNow);
    Check(Near(temp.die,timeNow)&&!temp.entity.curstate.renderamt,"switch cancels camera effect");
    current={melee::DragonSwordId,1,-1,0,0};ReceiveMeleeState(current);
    Check(Complete(melee::DragonSwordId,3,4,0)==1 && temporaryCalls==before,"switch back cannot replay retired completion");
    Check(Complete(melee::DragonSwordId,2,1,0)==0,"older serial rejected after switch");
    state=Predict(State(melee::SkullAxeId),IN_ATTACK);Check(lastAnimation==8,"SKULL slash start");
    state.weapondata[0].fuser1=-.008f;state=Predict(state,0);Check(lastAnimation==5,"SKULL empty slash completion");
    Check(Complete(melee::SkullAxeId,1,8,2)==1 && lastAnimation==1,"authority corrects lateral flesh hit without client damage");
    const int correctionStarts=animations;Complete(melee::SkullAxeId,1,8,2);
    Check(animations==correctionStarts,"repeated hit correction does not restart animation");
    state=State(melee::SkullAxeId);state.client.m_flNextAttack=.5f;
    state=Predict(state,IN_ATTACK);Check(!melee::ReadPrediction(state.weapondata[0]).pendingAnimation,"draw cooldown blocks attacks");
    state=State(melee::SkullAxeId);state.client.iuser3=0;
    Check(!melee::ReadPrediction(Predict(state,IN_ATTACK).weapondata[0]).pendingAnimation,"freeze blocks attacks");
    state=State(melee::SkullAxeId);state.client.health=0;
    Check(!melee::ReadPrediction(Predict(state,IN_ATTACK).weapondata[0]).pendingAnimation,"dead player cannot attack");
    localPrediction=0;state=Predict(State(melee::SkullAxeId),IN_ATTACK);
    Check(!melee::ReadPrediction(state.weapondata[0]).pendingAnimation,"cl_lw0 waits for server state");
    state.client.weaponanim=2;state=Predict(state,IN_ATTACK2);Check(lastAnimation==2,"cl_lw0 authority transition presents");
    const int authorityStarts=animations;Predict(state,IN_ATTACK2);Check(animations==authorityStarts,"stable authoritative state does not restart");
    localPrediction=1;presentation=false;state=State(melee::DragonSwordId);
    const int hidden=animations;Predict(state,IN_ATTACK);Check(animations==hidden,"stale predicted weapon cannot animate the new viewmodel");
    Check(Complete(melee::DragonSwordId,1,1,0)==0,"hidden weapon completion rejected");
    presentation=true;state=State(melee::DragonSwordId);
    Check(Complete(melee::DragonSwordId,1,8,0)==0 && Complete(melee::DragonSwordId,1,1,3)==0,"invalid animation/contact rejected");
    Check(receiver("CSOMelee",0,nullptr)==0,"bad packet rejected");
    std::cout<<"melee client "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
