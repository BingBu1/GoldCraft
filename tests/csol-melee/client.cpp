// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the complete production client against the real SDK declarations.
#include "../../native/csol/client/melee.cpp"
#include "wire.hpp"
#include <iostream>
#include <limits>

// The historical implementation has no draw-boundary entry. A no-op adapter
// lets the same behavior suite run against its unchanged source as a negative.
#ifdef CSOL_MELEE_LEGACY_DRAW_ENTRY
namespace csol::client { void PrepareMeleeEffectDraw(cl_entity_t*) {} }
#endif

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
model_t effectModel{}, recycledModel{};
TEMPENTITY* allocatedTemp = &temp;
bool haveViewModel = true;
pfnUserMsgHook receiver{};
void Check(bool good, const char* text)
{ ++checks; if (!good) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
bool Near(float a, float b) { return std::abs(a-b) < .0001f; }
void Pose(cl_entity_t& entity, float x, float y, float z, float pitch, float yaw, float roll)
{
    entity.origin[0]=x;entity.origin[1]=y;entity.origin[2]=z;
    entity.angles[0]=pitch;entity.angles[1]=yaw;entity.angles[2]=roll;
}
void CheckPose(const cl_entity_t& actual, const cl_entity_t& expected, const char* message, float zOffset=0)
{
    for(int i=0;i<3;++i) {
        Check(Near(actual.origin[i],expected.origin[i]+(i==2?zOffset:0)),message);
        Check(Near(actual.angles[i],expected.angles[i]),message);
        // Actual host StudioSetUpTransform reads these for MOVETYPE_NONE.
        Check(Near(actual.curstate.angles[i],expected.angles[i]),message);
    }
}
template<class T> auto Bytes(const T& value)
{
    std::array<unsigned char,sizeof(T)> bytes{};
    std::memcpy(bytes.data(),&value,sizeof(T));return bytes;
}
void Setup()
{
    local.index=1; viewModel.index=91;viewModel.curstate.body=4;
    Pose(viewModel,17,-31,112,-28,47,6);
    view.vieworg[0]=-101;view.vieworg[1]=203;view.vieworg[2]=9;
    view.viewangles[0]=42;view.viewangles[1]=-65;view.viewangles[2]=-13;
    engine.GetLocalPlayer=[](){return &local;}; engine.GetViewModel=[](){return haveViewModel?&viewModel:nullptr;};
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
    effects.R_TempModel=[](float* origin,float* velocity,float* angles,float life,int index,int bounce){
        ++temporaryCalls;Check(Near(life,.35f)&&index==7&&!bounce,"original temp model lifetime");
        for(int i=0;i<3;++i) {
            Check(Near(origin[i],viewModel.origin[i]+(i==2?16:0)),"original initial viewmodel origin with one z offset");
            Check(!velocity[i]&&!angles[i],"original allocation has zero velocity and initial angle argument");
        }
        if(!allocatedTemp)return static_cast<TEMPENTITY*>(nullptr);
        *allocatedTemp={};allocatedTemp->entity.model=&effectModel;
        allocatedTemp->entity.curstate.angles[0]=91;
        allocatedTemp->entity.curstate.angles[1]=-121;
        allocatedTemp->entity.curstate.angles[2]=37;
        std::copy_n(origin,3,allocatedTemp->entity.origin);
        std::copy_n(velocity,3,allocatedTemp->entity.baseline.origin);
        allocatedTemp->die=timeNow+life;
        return allocatedTemp;};
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
void EffectLifecycle()
{
    // Original client 02563390/02563560 and hw 37830540 select the viewmodel;
    // the draw refresh below is the host ordering adaptation, not a depth test.
    std::array<TEMPENTITY,3> pool{};
    State(melee::DragonSwordId);rightHand=1;haveViewModel=true;
    Pose(viewModel,120,-90,44,-34,82,-7);
    for(int i=0;i<2;++i) {
        allocatedTemp=&pool[i];
        Check(Complete(melee::DragonSwordId,100+i,1,0)==1,"independent authoritative effect accepted");
        CheckPose(pool[i].entity,viewModel,"initial pose uses actual viewmodel",16);
        Check(pool[i].clientIndex==viewModel.index && pool[i].clientIndex!=local.index,
            "temp client index belongs to viewmodel rather than local player");
    }
    const auto untouched=Bytes(pool[1]);
    pool[0].callback(&pool[0],.01f,timeNow);
    CheckPose(pool[0].entity,viewModel,"callback removes creation-only offset");
    Check(Bytes(pool[1])==untouched,"one callback does not move another effect");
    const float lifetime=pool[0].die;
    const float animationTime=pool[0].entity.curstate.animtime;
    const int alpha=pool[0].entity.curstate.renderamt;
    // TempEntUpdate sees the old pose; CalcRefdef then supplies a jump/turn.
    Pose(viewModel,128,-86,91,-51,119,13);
    PrepareMeleeEffectDraw(&pool[0].entity);
    CheckPose(pool[0].entity,viewModel,"draw refresh sees this frame's jump and complete angles");
    Check(Bytes(pool[1])==untouched,"draw refresh updates only the selected temp entity");
    PrepareMeleeEffectDraw(&pool[1].entity);
    CheckPose(pool[1].entity,viewModel,"each live effect refreshes independently");
    for(float z:{134.f,80.f,-12.f}) {
        Pose(viewModel,140-z,-65+z,z,17-z,125+z,9-z);
        pool[0].callback(&pool[0],.02f,timeNow);
        CheckPose(pool[0].entity,viewModel,"callback follows rising and falling viewmodel pose");
        PrepareMeleeEffectDraw(&pool[0].entity);
        CheckPose(pool[0].entity,viewModel,"same-frame redraw does not accumulate offset");
    }
    Check(Near(pool[0].die,lifetime)&&Near(pool[0].entity.curstate.animtime,animationTime)&&
        pool[0].entity.curstate.renderamt==alpha,"pose refresh preserves lifetime and animation/fade state");
    cl_entity_t unrelated=pool[0].entity;const auto unrelatedBefore=Bytes(unrelated);
    PrepareMeleeEffectDraw(&unrelated);PrepareMeleeEffectDraw(nullptr);
    Check(Bytes(unrelated)==unrelatedBefore,"copied model identity is not a temp-pool lease");

    for(int mutation=0;mutation<4;++mutation) {
        State(melee::DragonSwordId);
        for(int i=0;i<2;++i) {
            allocatedTemp=&pool[i];Complete(melee::DragonSwordId,200+i,1,0);
        }
        if(mutation==0)pool[0].callback=[](TEMPENTITY*,float,float){};
        if(mutation==1)pool[0].entity.model=&recycledModel;
        if(mutation==2)++pool[0].entity.curstate.iuser4;
        if(mutation==3)pool[0].flags&=~FTENT_CLIENTCUSTOM;
        const auto recycled=Bytes(pool[0]);
        Pose(viewModel,100+mutation,-100-mutation,250+mutation,-60,131,21);
        PrepareMeleeEffectDraw(&pool[0].entity);
        Check(Bytes(pool[0])==recycled,"changed callback/model/session/flags cannot retain ownership");
        PrepareMeleeEffectDraw(&pool[1].entity);
        CheckPose(pool[1].entity,viewModel,"recycling one pool slot preserves other effects");
        allocatedTemp=&pool[0];Complete(melee::DragonSwordId,202,1,0);
        Pose(viewModel,500,600,700,80,-150,-30);
        PrepareMeleeEffectDraw(&pool[0].entity);
        CheckPose(pool[0].entity,viewModel,"reallocated slot receives exactly its new lease");
        PrepareMeleeEffectDraw(&pool[1].entity);
        CheckPose(pool[1].entity,viewModel,"new lease preserves other valid pool records");
    }

    for(int missing=0;missing<2;++missing) {
        State(melee::DragonSwordId);allocatedTemp=&pool[0];
        const auto getter=engine.GetViewModel;
        if(missing==0)haveViewModel=false;else engine.GetViewModel=nullptr;
        const int calls=temporaryCalls;
        Complete(melee::DragonSwordId,300,1,0);
        Check(temporaryCalls==calls,"missing viewmodel cannot allocate a camera-based effect");
        haveViewModel=true;engine.GetViewModel=getter;
        Complete(melee::DragonSwordId,301,1,0);
        if(missing==0)haveViewModel=false;else engine.GetViewModel=nullptr;
        PrepareMeleeEffectDraw(&pool[0].entity);
        Check(Near(pool[0].die,timeNow)&&pool[0].entity.curstate.renderamt==0,
            "missing viewmodel retires an already-owned effect");
        haveViewModel=true;engine.GetViewModel=getter;
    }
    State(melee::DragonSwordId);allocatedTemp=nullptr;
    const int allocationAttempts=temporaryCalls;
    Complete(melee::DragonSwordId,400,1,0);
    Check(temporaryCalls==allocationAttempts+1,"allocation failure is a completed safe attempt");

    for(int boundary=0;boundary<4;++boundary) {
        auto state=State(melee::DragonSwordId);state=Predict(state,0);
        allocatedTemp=&pool[0];Complete(melee::DragonSwordId,500,1,0);
        const auto before=Bytes(pool[0]);
        if(boundary==0){current={29,1,-1,0,0};ReceiveMeleeState(current);}
        if(boundary==1){current.state=0;ReceiveMeleeState(current);}
        if(boundary==2){state.client.health=0;Predict(state,0);}
        if(boundary==3)ResetMeleeWeapons();
        PrepareMeleeEffectDraw(&pool[0].entity);
        Check(Bytes(pool[0])==before,"cleared leases cannot modify stale pool storage at draw time");
        pool[0].callback(&pool[0],.01f,timeNow);
        Check(Near(pool[0].die,timeNow)&&pool[0].entity.curstate.renderamt==0,
            "switch/death/reset invalidates callback and removes old effect");
    }
    allocatedTemp=&temp;haveViewModel=true;rightHand=1;State(melee::DragonSwordId);
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
    Pose(viewModel,23,-11,105,-24,42,7);temp.callback(&temp,0,timeNow);
    CheckPose(temp.entity,viewModel,"effect follows actual viewmodel each frame");
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
    EffectLifecycle();
    std::cout<<"melee client "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
