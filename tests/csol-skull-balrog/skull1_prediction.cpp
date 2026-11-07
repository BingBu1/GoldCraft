// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the complete production predictor at the real HLSDK boundary.
// Recoil expectations come from the original MP instruction oracle, not a
// second copy of the production arithmetic. CRT input is controlled here.
#include "../../native/csol/client/weapons/skull1.cpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace prediction_test {
unsigned checks{}, failures{}, animationCalls{}, soundCalls{}, angleCalls{}, crtCalls{};
int lastAnimation{-1};
float clWeapons{1};
std::array<int,6> randomValues{};
std::array<float,3> lastAngles{};
cl_entity_t view{}, player{};
event_api_t events{};
void Check(bool value,const char* description)
{
    ++checks;
    if(!value){++failures;std::cerr<<"FAIL "<<description<<'\n';}
}
bool Bits(float a,float b){return std::bit_cast<unsigned>(a)==std::bit_cast<unsigned>(b);}
}

namespace csol::client {
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
WeaponDefinition definition{skull1::Id,1,1,32,-1,skull1::Reserve,-1,skull1::Clip,0,skull1::Classname};
const WeaponDefinition* FindWeaponDefinition(int id){return id==skull1::Id?&definition:nullptr;}
bool WeaponPresentationAllowed(int id){return id==skull1::Id;}
void InitializeGunSmoke(){}
bool GunSmokeEnabled(){return false;}
WeaponModuleRegistration::WeaponModuleRegistration(const WeaponModule&) noexcept {}
float SharedRandomFloat(unsigned,float low,float high){return (low+high)*.5f;}
int ClientCRTRandom()
{
    using namespace prediction_test;
    Check(crtCalls<randomValues.size(),"CRT consumer stays inside six original draws");
    return randomValues[(crtCalls++)%randomValues.size()];
}
}

namespace prediction_test {
using namespace csol;
using namespace csol::client;

void Setup()
{
    player.index=1;view.curstate.body=9;
    engine.GetViewModel=[](){return &view;};engine.GetLocalPlayer=[](){return &player;};
    engine.pfnGetCvarFloat=[](const char* name){return std::strcmp(name,"cl_lw")==0?clWeapons:1.f;};
    engine.pfnWeaponAnim=[](int sequence,int body){++animationCalls;lastAnimation=sequence;Check(body==9,"character body retained");};
    engine.pfnAngleVectors=[](const float* angles,float* forward,float* right,float* up){
        ++angleCalls;std::copy_n(angles,3,lastAngles.begin());std::fill_n(forward,3,0);std::fill_n(right,3,0);std::fill_n(up,3,0);
        forward[0]=1;right[1]=-1;up[2]=1;
    };
    engine.pfnRandomLong=[](int low,int){return low;};
    engine.pfnRandomFloat=[](float low,float high){return (low+high)*.5f;};
    engine.pEventAPI=&events;
    events.EV_IsLocal=[](int index){return int(index==0);};
    events.EV_PlaySound=[](int,float*,int,const char*,float,float,int,int){++soundCalls;};
    downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){
        Check(from->client.m_iId==0,"host prediction does not receive extended weapon id");
        *to=*from;
        // Distinguish the actual from-state input from a stale downstream value.
        to->client.punchangle[0]=71;to->client.punchangle[1]=72;to->client.punchangle[2]=73;
        to->client.weaponanim=99;to->client.viewmodel=99;
    };
}

local_state_t State(int clip=7,int reserve=35)
{
    local_state_t state{};
    state.client.m_iId=skull1::Id;state.client.health=100;state.client.iuser3=1;
    state.client.flags=FL_ONGROUND;state.client.viewmodel=12;state.client.weaponanim=skull1::Draw;
    state.client.vuser4[1]=float(reserve);state.client.fov=90;
    auto& weapon=state.weapondata[ActiveWeaponSlot];weapon.m_iId=skull1::Id;weapon.m_iClip=clip;
    weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=-.001f;
    weapon.m_flTimeWeaponIdle=1;weapon.fuser1=.95f;
    return state;
}

local_state_t Predict(local_state_t state,int buttons,int run=1)
{
    local_state_t to{};usercmd_t command{};command.buttons=short(buttons);command.msec=10;
    Check(PredictSkull1(&state,&to,&command,run,100,123),"SKULL1 predictor handled state");
    Check(state.client.m_iId==skull1::Id&&to.client.m_iId==skull1::Id&&to.client.viewmodel==12,"host id/model adaptation restored");
    return to;
}

void PrimaryAndLocks()
{
    for(int run:{0,1}){
        ResetSkull1();auto state=State();state.client.punchangle[0]=1.25f;state.client.punchangle[1]=-2;state.client.punchangle[2]=3;
        const auto sounds=soundCalls,animations=animationCalls;crtCalls=0;
        const auto out=Predict(state,IN_ATTACK,run);
        Check(Bits(out.client.punchangle[0],-.75f)&&Bits(out.client.punchangle[1],-2)&&Bits(out.client.punchangle[2],3),"primary predicts original minus-two pitch and preserves yaw/roll");
        Check(out.weapondata[ActiveWeaponSlot].m_iClip==6&&crtCalls==0,"primary consumes one round without rapid CRT draws");
        Check(run?(soundCalls==sounds+1&&animationCalls==animations+1):(soundCalls==sounds&&animationCalls==animations),"primary effects run once and replay does not present");
        if(run)Check(lastAngles[0]==1.25f&&lastAngles[1]==-2&&lastAngles[2]==0,"fire event uses pre-shot punch input");
    }
    for(int buttons:{IN_ATTACK,IN_ATTACK2})for(int mode=0;mode<7;++mode){
        ResetSkull1();auto state=State();state.client.punchangle[0]=1.25f;state.client.punchangle[1]=-2;state.client.punchangle[2]=3;
        switch(mode){
        case 0:state.client.health=0;break;
        case 1:state.client.deadflag=1;break;
        case 2:state.client.iuser3=0;break;
        case 3:state.client.iuser3=3;break;
        case 4:state.client.m_flNextAttack=.5f;break;
        case 5:state.weapondata[ActiveWeaponSlot].m_iClip=0;break;
        case 6:state.weapondata[ActiveWeaponSlot].m_flNextPrimaryAttack=state.weapondata[ActiveWeaponSlot].m_flNextSecondaryAttack=.5f;break;
        }
        crtCalls=0;const auto clip=state.weapondata[ActiveWeaponSlot].m_iClip;
        const auto out=Predict(state,buttons);
        Check(crtCalls==0&&out.weapondata[ActiveWeaponSlot].m_iClip==clip,"locked or empty attack does not consume recoil RNG/ammo");
        for(int axis=0;axis<3;++axis)Check(Bits(out.client.punchangle[axis],state.client.punchangle[axis]),"locked or empty attack preserves source punch");
    }
    clWeapons=0;ResetSkull1();auto state=State();state.client.punchangle[0]=4;crtCalls=0;
    const auto out=Predict(state,IN_ATTACK2);Check(crtCalls==0&&out.weapondata[0].m_iClip==7&&out.client.punchangle[0]==4,"cl_lw0 retains authoritative recoil and ammo");clWeapons=1;
}

void OriginalRecoil(const std::filesystem::path& path)
{
    std::ifstream file(path);Check(bool(file),"original recoil oracle is available");
    std::string line;unsigned rows{};
    while(std::getline(file,line)){
        if(!line.starts_with("R "))continue;
        std::istringstream input(line);char tag;int ground,moving,duck,shots;unsigned pitch,yaw,expectedPitch,expectedYaw;
        input>>tag>>ground>>moving>>duck>>shots>>pitch>>yaw;
        for(auto& value:randomValues)input>>value;
        input>>expectedPitch>>expectedYaw;Check(bool(input),"original recoil row parses");
        for(int run:{0,1}){
            ResetSkull1();auto state=State();state.client.flags=(ground?FL_ONGROUND:0)|(duck?FL_DUCKING:0);
            state.client.velocity[0]=moving?100.f:0;state.client.punchangle[0]=std::bit_cast<float>(pitch);
            state.client.punchangle[1]=std::bit_cast<float>(yaw);state.client.punchangle[2]=1.5f;
            state.weapondata[0].fuser3=skull1::PackShots(shots-1);crtCalls=0;
            const auto sounds=soundCalls,animations=animationCalls;
            const auto out=Predict(state,IN_ATTACK2,run);
            Check(crtCalls==6,"rapid prediction consumes exactly six original CRT values");
            Check(std::bit_cast<unsigned>(out.client.punchangle[0])==expectedPitch&&std::bit_cast<unsigned>(out.client.punchangle[1])==expectedYaw,"full predictor matches original recoil instruction output bits");
            Check(out.client.punchangle[2]==1.5f&&out.weapondata[0].m_iClip==6&&skull1::UnpackShots(out.weapondata[0].fuser3)==shots,"rapid adapter preserves roll and original shot ordinal");
            Check(run?(soundCalls==sounds+1&&animationCalls==animations+1):(soundCalls==sounds&&animationCalls==animations),"rapid replay changes state without presentation");
        }
        ++rows;
    }
    Check(rows==300,"all original recoil movement/count/clamp contexts exercised");
}

void ReloadPresentation()
{
    randomValues={7,3,123,32765,0,1};ResetSkull1();crtCalls=0;
    auto state=Predict(State(1),IN_ATTACK2);Check(state.weapondata[0].m_iClip==0,"last rapid round consumed");
    state.weapondata[0].m_flNextPrimaryAttack=state.weapondata[0].m_flNextSecondaryAttack=-.001f;
    crtCalls=0;state=Predict(state,IN_ATTACK2);
    Check(!state.weapondata[0].m_fInReload&&crtCalls==0,"held empty secondary remains dryfire, not invented autoreload");
    for(int frame=0;frame<30&&!state.weapondata[0].m_fInReload;++frame)state=Predict(state,0,0);
    Check(state.weapondata[0].m_fInReload&&state.client.weaponanim==skull1::Reload,"released empty secondary enters original reload in replay");
    const auto before=animationCalls;state=Predict(state,0);
    Check(animationCalls==before+1&&lastAnimation==skull1::Reload,"replay-started reload receives missing actual animation");
    const auto once=animationCalls;
    for(int frame=0;frame<10;++frame)state=Predict(state,IN_ATTACK2);
    Check(animationCalls==once&&state.weapondata[0].m_fInReload,"unchanged reload does not restart while holding secondary");
    state.client.m_flNextAttack=-.001f;state=Predict(state,0);
    Check(state.weapondata[0].m_iClip==7&&state.client.vuser4[1]==28&&!state.weapondata[0].m_fInReload,"reload transfers original seven-round magazine after delay");

    ResetSkull1();crtCalls=0;Predict(State(),IN_ATTACK2);
    auto authoritative=State(0);authoritative.client.weaponanim=skull1::Reload;
    authoritative.client.m_flNextAttack=2;authoritative.weapondata[0].m_fInReload=1;
    const auto prior=animationCalls;authoritative=Predict(authoritative,0);
    Check(animationCalls==prior+1&&lastAnimation==skull1::Reload,"source reload6 with no locally generated animation is reconciled");
    const auto stable=animationCalls;Predict(authoritative,0);Check(animationCalls==stable,"same authoritative reload only presents once");
}
}

int wmain(int argc,wchar_t** argv)
{
    if(argc!=2)return 2;
    prediction_test::Setup();prediction_test::PrimaryAndLocks();
    prediction_test::OriginalRecoil(argv[1]);prediction_test::ReloadPresentation();
    std::cout<<"SKULL1 prediction: "<<prediction_test::checks<<" checks, "<<prediction_test::failures<<" failures\n";
    return prediction_test::failures?1:0;
}
