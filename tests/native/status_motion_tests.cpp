#include "goldcraft/fluid_volumes.hpp"
#include "goldcraft/status_motion.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
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
void PM_CheckVelocity(){}
#include "motion_gravity_actual.inc"
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
void setup(playermove_t& m,bool duck=false){
    std::memset(&m,0,sizeof(m));m.PM_PointContents=point;m.PM_TruePointContents=truth;
    m.PM_PlayerTrace=trace;m.PM_PlaySound=playSound;m.RandomLong=randomLong;
    m.movetype=MOVETYPE_WALK;m.usehull=duck?1:0;m.onground=0;
    for(int i=0;i<2;++i){m.player_mins[i][0]=m.player_mins[i][1]=-16;m.player_maxs[i][0]=m.player_maxs[i][1]=16;}
    m.player_mins[0][2]=-36;m.player_maxs[0][2]=36;
    m.player_mins[1][2]=-18;m.player_maxs[1][2]=18;
    m.origin[2]=duck?18.0f:36.0f;m.view_ofs[2]=duck?12.0f:28.0f;
    m.friction=1;m.maxspeed=250;m.frametime=.016f;m.cmd.msec=16;
    m.forward[0]=1;m.right[1]=-1;movement.gravity=800;movement.maxvelocity=2000;movement.friction=4;movement.accelerate=10;movement.stepsize=18;m.movevars=&movement;
}
}
using Scope=goldcraft::status_motion::MovementScope<playermove_t>;
using State=goldcraft::status_motion::State;
void cancelJump(){}
void collide(){
    float end[3];VectorMA(pmove->origin,pmove->frametime,pmove->velocity,end);
    const auto t=pmove->PM_PlayerTrace(pmove->origin,end,0,-1);VectorCopy(t.endpos,pmove->origin);
    if(t.fraction<1)pmove->velocity[2]=0;
}
void airStep(playermove_t& m,const State* state,bool jump=false){
    pmove=&m;Scope scope(m,state);Scope::categorize(PM_CategorizePosition);
    PM_AddCorrectGravity();if(jump)Scope::jump(PM_Jump);
    if(m.onground!=-1)m.velocity[2]=0;collide();
    Scope::categorize(PM_CategorizePosition);PM_FixupGravityVelocity();
    if(m.onground!=-1)m.velocity[2]=0;
}
int main() try {
    using namespace goldcraft;using namespace status_motion;
    // The matched SDK embeds large physent/touch arrays (~325 KiB per move).
    // Keep fixture copies off the default 1 MiB Windows thread stack.
    struct Moves {playermove_t base,m,normal,old,a,c,control;};
    auto moves=std::make_unique<Moves>();
    auto& base=moves->base;auto& m=moves->m;auto& normal=moves->normal;auto& old=moves->old;
    auto& a=moves->a;auto& c=moves->c;auto& control=moves->control;
    State s{1,1,1,2,3,4,slow_falling,0,0};
    auto packet=[&](){Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(s.slot);w.u32(s.serial);w.u32(s.spawn);w.u32(s.life);w.u32(s.flags);w.f32(s.jump);w.f32(s.levitation_target);return w.data;};
    check(packet().size()==44&&decode(packet()).flags==slow_falling,"exact motion layout");
    auto rejects=[&](Bytes b){try{decode(b);check(false,"malformed motion accepted");}catch(const ProtocolError&){check(true,"rejected");}};
    auto b=packet();b.push_back(0);rejects(b);b=packet();b.pop_back();rejects(b);
    for(unsigned flags:{8u,levitation}){s.flags=flags;rejects(packet());}s.flags=slow_falling;
    for(float bad:{-1.f,26.f,INFINITY,NAN}){
        auto malformed=packet();std::memcpy(malformed.data()+36,&bad,sizeof(bad));rejects(malformed);
    }
    Receiver receiver;check(receiver.accept(s,1),"new state");check(!receiver.accept(s,1.1),"duplicate no lease renewal");
    check(receiver.active(1,1,2,4,1.34)!=nullptr,"live lease");
    for(auto now:{.9,1.35,2.0})check(!receiver.active(1,1,2,4,now),"expired/backwards clock");
    check(!receiver.active(2,1,2,4,1.1)&&!receiver.active(1,2,2,4,1.1)&&!receiver.active(1,1,3,4,1.1)&&!receiver.active(1,1,2,5,1.1),"world/slot/connection/life isolation");
    nativeKind=CONTENTS_EMPTY;ceiling=100000;
    setup(base);base.origin[2]=10000;base.onground=-1;base.velocity[2]=-160;base.cmd.msec=50;base.frametime=.05f;
    m=base;airStep(m,nullptr);check(std::abs(m.velocity[2]+200)<.001f,"native gravity negative control");
    m=base;airStep(m,&s);check(std::abs(m.velocity[2]-(-160-6.4f)*.98f)<.001f,"actual halves yield vanilla slowfall velocity");
    check(m.origin[2]>base.origin[2]-10&&m.movevars==&movement&&m.gravity==0,"slower fall and temporary gravity restored");
    for(float vz:{-800.f,-160.f,0.f,100.f}){
        m=base;m.velocity[2]=vz;a=m;airStep(a,&s);c=m;airStep(c,nullptr);
        check(vz<=0?a.velocity[2]>c.velocity[2]:a.velocity[2]==c.velocity[2],"slowfall only descending");
    }
    for(int blocked=0;blocked<7;++blocked){
        m=base;if(blocked==0)m.dead=1;if(blocked==1)m.deadflag=1;if(blocked==2)m.spectator=1;if(blocked==3)m.iuser1=1;
        if(blocked==4)m.movetype=MOVETYPE_NOCLIP;if(blocked==5)m.flags|=1<<24;if(blocked==6)m.flags|=1<<12;
        control=m;airStep(m,&s);airStep(control,nullptr);check(m.velocity[2]==control.velocity[2],"inactive movement untouched");
    }
    setup(normal);m=normal;airStep(m,&s,true);airStep(normal,nullptr,true);
    check(m.velocity[2]==normal.velocity[2],"slowfall must not lengthen jump ascent");
    s.flags=levitation;s.levitation_target=.05f;
    setup(m);const float floor=m.origin[2];airStep(m,&s);
    check(m.origin[2]>floor&&m.velocity[2]>0&&m.onground==-1,"levitation lifts off actual categorize ground snap");
    m=base;m.velocity[2]=0;airStep(m,&s);check(std::abs(m.velocity[2]-.196f*32)<.001f,"level-one levitation 20Hz velocity");
    setup(m);ceiling=72;for(int n=0;n<20;++n)airStep(m,&s);check(m.origin[2]<=36,"levitation keeps ceiling hull collision");ceiling=100000;
    for(float dt:{.001f,.008f,.016f,.05f,.1f,.255f}){
        const float v=vertical_target(-100,800,dt,s),half=vertical_target(vertical_target(-100,800,dt/2,s),800,dt/2,s);
        check(std::abs(v-half)<.001f,"fractional-tick levitation command partition");
    }
    s.flags=0;s.levitation_target=0;s.jump=.1f;setup(m);normal=m;airStep(m,&s,true);airStep(normal,nullptr,true);
    check(std::abs(m.velocity[2]-normal.velocity[2]-64)<.001f,"jump boost actual successful PM jump");
    setup(m);pmove=&m;{Scope scope(m,&s);Scope::jump(cancelJump);}check(m.velocity[2]==0&&m.onground==0,"ReAPI-cancelled jump stays cancelled");
    setup(m);m.onground=-1;pmove=&m;{Scope scope(m,&s);Scope::jump(PM_Jump);}check(m.velocity[2]==0,"no midair bonus");
    s.jump=0;s.flags=dolphins_grace;nativeKind=CONTENTS_WATER;nativeHeight=100000;
    setup(m);pmove=&m;PM_CheckWater();m.velocity[0]=400;m.cmd.forwardmove=0;m.frametime=.05f;
    old=m;{Scope scope(m,&s);Scope::water(PM_WaterMove);}pmove=&old;PM_WaterMove();
    check(std::abs(m.velocity[0]-384)<.001f&&m.velocity[0]>old.velocity[0],"actual water move 0.96 drag");
    check(m.movevars==&movement&&movement.friction==4&&movement.accelerate==10&&m.maxspeed==250&&m.cmd.forwardmove==0,"water command/movevars restoration");
    setup(m);m.watertype=CONTENTS_LAVA;m.waterlevel=3;m.velocity[0]=400;old=m;pmove=&m;
    {Scope scope(m,&s);Scope::water(PM_WaterMove);}pmove=&old;PM_WaterMove();check(m.velocity[0]==old.velocity[0],"dolphin does not change lava");
    nativeKind=CONTENTS_EMPTY;setup(m);pmove=&m;
    {Scope outer(m,&s);auto* temporary=m.movevars;{Scope inner(m,nullptr);Scope::categorize(PM_CategorizePosition);}check(m.movevars==temporary,"nested callback scope restored");}
    check(m.movevars==&movement,"outer movement vars restored");
    s.flags=levitation;s.levitation_target=.05f;m=base;pmove=&m;
    {
        Scope outer(m,&s);Scope::categorize(PM_CategorizePosition);
        auto* temporary=m.movevars;const float temporary_gravity=m.gravity;
        check(temporary->gravity!=movement.gravity,"outer command has temporary gravity");
        const float velocity=m.velocity[2];
        {
            Scope inner(m,&s);
            check(m.movevars->gravity==movement.gravity&&m.gravity==0,"reentrant command uses native gravity");
            PM_AddCorrectGravity();PM_FixupGravityVelocity();
            check(std::abs(m.velocity[2]-(velocity-40))<.001f,"reentrant gravity halves exclude outer effect");
        }
        check(m.movevars==temporary&&m.gravity==temporary_gravity,"reentrant command restores outer gravity");
    }
    check(m.movevars==&movement&&m.gravity==0,"nested modified gravity fully restored");
    std::cout<<checks<<" motion checks: matched PM gravity/categorize/jump/water, collisions, lifetimes and native controls\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"motion check "<<checks<<" failed: "<<error.what()<<'\n';
    return 1;
}
