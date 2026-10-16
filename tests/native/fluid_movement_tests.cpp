#include "goldcraft/fluid_volumes.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Matched SDK layout; no game DLL or client is launched by this test.
using byte=unsigned char;
using qboolean=int;
using vec_t=float;
using vec3_t=float[3];
using int32=std::int32_t;
#define Q_snprintf std::snprintf
#define BIT(n) (1<<(n))
#include "pm_defs.h"
#include "pm_movevars.h"
#include "in_buttons.h"
#define PLAY_GAMEDLL
#include "bspfile.h"

#define REGAMEDLL_FIXES
#define EXT_FUNC
#define __API_HOOK(name) name
void VectorScale(const float* a,double s,float* b){for(int i=0;i<3;++i)b[i]=float(a[i]*s);}
void VectorMA(const float* a,double s,const float* b,float* c){for(int i=0;i<3;++i)c[i]=float(a[i]+s*b[i]);}
float Length(const float* a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
float VectorNormalize(float* a){const float n=Length(a);if(n)for(int i=0;i<3;++i)a[i]/=n;return n;}

playermove_t* pmove;
struct MovementVersion {bool IsAtLeast(int,int){return false;}};
struct Player {MovementVersion m_MovementVersion;} player;
Player* pmoveplayer=&player;
int flyCalls=0,sounds=0;
void PM_FlyMove(){++flyCalls;}
void PM_CatagorizeTextureType(){}
void PM_PreventMegaBunnyJumping(){}
int PM_MapTextureTypeStepType(char){return 0;}
void PM_PlayStepSound(int,float){}
double PM_JumpHeight(bool){return std::sqrt(2.0*800*45);}
void PM_FixupGravityVelocity(){pmove->velocity[2]-=400*pmove->frametime;}
void PM_AddToTouched(pmtrace_t,float*){}
#include "fluid_movement_actual.inc"

namespace {
int checks=0,nativeKind=CONTENTS_EMPTY;
float nativeHeight=0,ceiling=1000;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int point(float* p,int* real){const int value=p[2]<nativeHeight?nativeKind:CONTENTS_EMPTY;if(real)*real=value;return value;}
int truth(float* p){return point(p,nullptr);}
int randomLong(int,int){return 0;}
void playSound(int,const char*,float,float,int,int){++sounds;}
pmtrace_t trace(float* start,float* end,int,int){
    pmtrace_t t{};t.fraction=1;t.ent=-1;VectorCopy(end,t.endpos);
    const float low=-pmove->player_mins[pmove->usehull][2];
    const float high=ceiling-pmove->player_maxs[pmove->usehull][2];
    if(end[2]<low){t.endpos[2]=low;t.ent=0;t.plane.normal[2]=1;t.fraction=0;}
    if(end[2]>high){t.endpos[2]=high;t.ent=0;t.plane.normal[2]=-1;t.fraction=0;}
    if(start[2]>high&&end[2]>high)t.startsolid=t.allsolid=1;
    return t;
}
movevars_t movement;
playermove_t setup(bool duck=false){
    playermove_t m{};m.PM_PointContents=point;m.PM_TruePointContents=truth;
    m.PM_PlayerTrace=trace;m.PM_PlaySound=playSound;m.RandomLong=randomLong;
    m.movetype=MOVETYPE_WALK;m.usehull=duck?1:0;m.onground=0;
    for(int i=0;i<2;++i){m.player_mins[i][0]=m.player_mins[i][1]=-16;m.player_maxs[i][0]=m.player_maxs[i][1]=16;}
    m.player_mins[0][2]=-36;m.player_maxs[0][2]=36;
    m.player_mins[1][2]=-18;m.player_maxs[1][2]=18;
    m.origin[2]=duck?18.0f:36.0f;m.view_ofs[2]=duck?12.0f:28.0f;
    m.friction=1;m.maxspeed=250;m.frametime=.016f;m.cmd.msec=16;
    m.forward[0]=1;m.right[1]=-1;movement.friction=4;movement.accelerate=10;movement.stepsize=18;m.movevars=&movement;
    return m;
}
goldcraft::fluids::Field water(float height,int contents=CONTENTS_WATER){
    goldcraft::fluids::Field f;f.assign({1,1,{{contents,{-100,-100,0},{100,100,height}}}});return f;
}
struct Result {int waterlevel,watertype;float z,vz,x;};
Result step(playermove_t& m,const goldcraft::fluids::Field* f,bool jump=true){
    pmove=&m;goldcraft::fluids::MovementScope scope(m,f);
    PM_CategorizePosition();
    if(m.waterlevel>=2){if(jump)PM_Jump();PM_WaterMove();PM_CategorizePosition();}
    return {m.waterlevel,m.watertype,m.origin[2],m.velocity[2],m.origin[0]};
}
}
int main(){
    // Old behavior reproduced through the actual PM functions, without a bridge:
    // one source-block height is wading; native deep water already swims.
    nativeKind=CONTENTS_WATER;nativeHeight=32*8.0f/9;
    auto m=setup();auto old=step(m,nullptr);
    check(old.waterlevel==1&&old.z==36,"native shallow-water negative control");
    auto shallow=water(nativeHeight);nativeKind=CONTENTS_EMPTY;
    m=setup();auto fixed=step(m,&shallow);
    check(fixed.waterlevel==2&&fixed.z>36&&fixed.vz>0,"one MC block must allow jump swimming");
    check(m.PM_PointContents==point&&m.PM_TruePointContents==truth,"callback restoration");
    m=setup();pmove=&m;
    {goldcraft::fluids::MovementScope scope(m,&shallow);
        float waist[3]{0,0,36},eye[3]{0,0,64},nearWaist[3]{.1f,0,36};
        check(m.PM_PointContents(waist,nullptr)==CONTENTS_WATER,"buoyancy waist sample");
        check(m.PM_TruePointContents(waist)==CONTENTS_EMPTY,"true contents remains physical");
        check(m.PM_PointContents(eye,nullptr)==CONTENTS_EMPTY,"eyes remain above real water");
        check(m.PM_PointContents(nearWaist,nullptr)==CONTENTS_EMPTY,"no inflated liquid volume");
    }
    for(float h:{1.0f,8.0f,12.8f,12.81f,28.44f,40.0f,80.0f}){
        auto f=water(h);m=setup();pmove=&m;
        {goldcraft::fluids::MovementScope scope(m,&f);PM_CheckWater();}
        check(m.waterlevel==(h<=1?0:h<=12.8f?1:h<=64?2:3),"depth threshold/eye classification");
    }
    auto deep=water(100);
    for(bool jump:{false,true})for(float forward:{0.0f,150.0f})for(float up:{0.0f,100.0f}){
        nativeKind=CONTENTS_WATER;nativeHeight=100;m=setup();m.cmd.forwardmove=forward;m.cmd.upmove=up;
        auto expected=step(m,nullptr,jump);
        nativeKind=CONTENTS_EMPTY;m=setup();m.cmd.forwardmove=forward;m.cmd.upmove=up;
        auto actual=step(m,&deep,jump);
        check(actual.waterlevel==expected.waterlevel&&actual.z==expected.z&&actual.vz==expected.vz&&actual.x==expected.x,"deep native/MC parity");
    }
    m=setup(true);auto ducked=step(m,&shallow);check(ducked.waterlevel==2&&ducked.z>18,"crouching shallow swim");
    auto lava=water(28.44f,CONTENTS_LAVA);m=setup();auto hot=step(m,&lava);
    check(hot.watertype==CONTENTS_LAVA&&hot.z>36&&hot.vz<fixed.vz,"lava retains native slower buoyancy");
    for(int state=0;state<5;++state){
        m=setup();if(state==0)m.dead=1;if(state==1)m.deadflag=1;if(state==2)m.spectator=1;if(state==3)m.iuser1=1;if(state==4)m.movetype=MOVETYPE_NOCLIP;
        pmove=&m;goldcraft::fluids::MovementScope scope(m,&shallow);float p[3]{0,0,36};
        check(m.PM_PointContents(p,nullptr)==CONTENTS_EMPTY,"inactive movement must not gain buoyancy");
    }
    nativeKind=CONTENTS_SOLID;nativeHeight=100;m=setup();pmove=&m;
    {goldcraft::fluids::MovementScope scope(m,&shallow);float p[3]{0,0,36};check(m.PM_PointContents(p,nullptr)==CONTENTS_SOLID,"native solid precedence");}
    nativeKind=CONTENTS_WATER;nativeHeight=28.44f;m=setup();
    goldcraft::fluids::Field empty;auto untouched=step(m,&empty);
    check(untouched.waterlevel==1&&untouched.z==36,"native BSP shallow water unchanged");
    nativeKind=CONTENTS_EMPTY;m=setup();ceiling=72;
    auto blocked=step(m,&shallow);check(blocked.z<=36&&flyCalls>0,"actual water movement still uses collision trace");ceiling=1000;
    m=setup();pmove=&m;
    {goldcraft::fluids::MovementScope outer(m,&shallow);
        {goldcraft::fluids::MovementScope inner(m,nullptr);PM_CheckWater();check(m.waterlevel==0,"disabled nested scope");}
        PM_CheckWater();check(m.waterlevel==2,"nested scope restored");
    }
    check(sounds>0,"native swimming sound branch executed");
    std::cout<<checks<<" checks passed: actual matched PM water classification, swim jump, movement and collision callbacks\n";
}
