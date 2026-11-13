// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/muzzle_flash.hpp"
#include <metahook.h>
#include <com_model.h>
#include <event_api.h>
#include <event_args.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <io.h>
using namespace guest::cof::client;
namespace {
cl_entity_t local, view, visible, localVisible, worldVisible;
model_t model;
dlight_t engineLight;
PointLightState pool;
cl_enginefunc_t engine{};
efx_api_t effects{};
event_api_t events{};
std::array<std::uint32_t, 9> sprite{};
std::array<float, 3> direction{};
std::array<std::uint32_t, 16> calls{};
unsigned callCount{}, timeIndex{}, randomIndex{};
float now{}, sizeRandom{}, rollRandom{};
bool ready{true};
void Call(unsigned);
void Angles(const float*,float*,float*,float*) { Call(9); }
TEMPENTITY* Shell(float* origin,float* velocity,float* angles,float life,int index,int sound) {
    Call(10);for(unsigned i=0;i<3;++i){sprite[i]=std::bit_cast<unsigned>(origin[i]);sprite[i+3]=std::bit_cast<unsigned>(velocity[i]);direction[i]=angles[i];}
    sprite[6]=std::bit_cast<unsigned>(life);sprite[7]=unsigned(index);sprite[8]=unsigned(sound);return nullptr;
}
void Call(unsigned id) { calls[callCount++] = id; }
float Time() { Call(1); return now + static_cast<float>(timeIndex++) * 0.00001F; }
cl_entity_t* Local() { Call(2); return &local; }
model_t* Model(const char*, int* index) { Call(3); *index=13; return &model; }
float Random(float, float) { Call(4); return randomIndex++ ? rollRandom : sizeRandom; }
int Visible(int, cl_entity_t* entity) { Call(5); visible=*entity; visible.model=nullptr; return 1; }
int Find(const char*) { Call(6); return 27; }
TEMPENTITY* Sprite(float*, float* dir, float scale, int index, int mode, int fx, float alpha, float life, int flags) {
    Call(7); sprite={1,1,std::bit_cast<unsigned>(scale),static_cast<unsigned>(index),static_cast<unsigned>(mode),static_cast<unsigned>(fx),std::bit_cast<unsigned>(alpha),std::bit_cast<unsigned>(life),static_cast<unsigned>(flags)};
    std::memcpy(direction.data(),dir,sizeof(direction)); return nullptr;
}
dlight_t* Dlight(int) { Call(8); return &engineLight; }
bool Ready(void*) noexcept { return ready; }
PointLight* Allocate(void*,int key) noexcept { return &AllocatePointLight(pool,key,Time()); }
cl_entity_t* Entity(int index) { return index==1 ? &view : index==2 ? &local : nullptr; }
template<class T> bool Read(T& x) { return std::fread(&x,sizeof(x),1,stdin)==1; }
template<class T> void Write(const T& x) { std::fwrite(&x,sizeof(x),1,stdout); }
}
int main() {
    _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
    static_assert(sizeof(cl_entity_t)==3000 && sizeof(dlight_t)==40);
    engine.GetLocalPlayer=Local; engine.CL_LoadModel=Model; engine.pfnRandomFloat=Random;
    engine.CL_CreateVisibleEntity=Visible; engine.GetClientTime=Time; engine.pEfxAPI=&effects;
    engine.pEventAPI=&events; engine.GetEntityByIndex=Entity;
    engine.pfnAngleVectors=Angles;effects.R_TempModel=Shell;effects.CL_AllocDlight=Dlight; effects.R_TempSprite=Sprite; events.EV_FindModelIndex=Find;
    MuzzleLatch latch{true,33,99,{1,2,3}};
    P345Muzzle latchConsumer({&engine,&localVisible,&worldVisible,nullptr,Ready,Allocate});
    event_args_t event{}; event.iparam1=7; event.entindex=1;
    event.angles[0]=5; event.angles[1]=6; event.angles[2]=7;
    view.curstate.weaponmodel=0;
    if(latchConsumer.Latch(latch,event)!=MuzzleResult::Ignored || latch.owner!=99) return 10;
    view.curstate.weaponmodel=3;
    if(latchConsumer.Latch(latch,event)!=MuzzleResult::Ignored || latch.owner!=99) return 11;
    view.curstate.weaponmodel=2;
    if(latchConsumer.Latch(latch,event)!=MuzzleResult::RequestsIssued || latch.owner!=1 || latch.type!=7 || latch.eventVector!=std::array<float,3>{5,6,7}) return 12;
    if(P345Muzzle::Take(latch,2) || !latch.pending || !P345Muzzle::Take(latch,1) || latch.pending || P345Muzzle::Take(latch,1)) return 13;
    local.curstate.weaponmodel=1; event.entindex=2;
    if(latchConsumer.Latch(latch,event)!=MuzzleResult::RequestsIssued || latch.owner!=2) return 14;
    event.iparam1=3;
    if(latchConsumer.Latch(latch,event)!=MuzzleResult::Unsupported || latch.owner!=2) return 15;
    std::uint32_t op, attachment;
    float scale,cheap;
    while(Read(op)) {
        if(!Read(now)||!Read(sizeRandom)||!Read(rollRandom)||!Read(scale)||!Read(cheap)||!Read(attachment)||!Read(local)||!Read(view)||!Read(pool)) return 2;
        timeIndex=randomIndex=callCount=0; calls={}; sprite={}; direction={}; visible={};
        std::memset(&engineLight,0xCD,sizeof(engineLight)); view.model=&model; ready=op!=6;
        P345Muzzle muzzle({&engine,&localVisible,&worldVisible,nullptr,Ready,Allocate});
        MuzzleResult result=MuzzleResult::RequestsIssued; int selected=-1;
        if(op==0 || op==6) result=muzzle.Local(view,attachment,scale,cheap);
        else if(op==1) result=muzzle.World({view.attachment[0][0],view.attachment[0][1],view.attachment[0][2]},scale);
        else if(op==7 || op==8) {
            GlockMuzzle glock({&engine,&localVisible,&worldVisible,nullptr,Ready,Allocate});
            if(op==7)result=glock.World({view.attachment[0][0],view.attachment[0][1],view.attachment[0][2]},
                {view.attachment[1][0],view.attachment[1][1],view.attachment[1][2]},
                {view.angles[0],view.angles[1],view.angles[2]},scale);
            else result=glock.Local(view,attachment,scale,cheap);
        }
        else if(op==2) selected=static_cast<int>(&AllocatePointLight(pool,static_cast<int>(attachment),now)-pool.lights.data());
        else if(op==3) AdvancePointLights(pool,now);
        else if(op==4) selected=HasPointLights(pool,now) ? 1 : 0;
        else if(op==5) ClearPointLights(pool);
        else return 3;
        Write(result); Write(selected); Write(visible); Write(engineLight); Write(pool); Write(sprite); Write(direction); Write(callCount); Write(calls);
    }
}
