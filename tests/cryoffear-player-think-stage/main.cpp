// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "goldcraft/player_think_stage_api.hpp"
#include "player_think_stage.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

server_t g_psv{};
server_static_t g_psvs{};
globalvars_t gGlobalVariables{};
DLL_FUNCTIONS gEntityInterface{};
edict_t* sv_player{};
client_t* host_client{};
namespace
{
unsigned checks{},calls{},nativeCalls{},preCalls{},freeCalls{},mutation{};
std::size_t allocations{};
float frametime{};
std::uint64_t token{};
goldcraft::IPlayerThinkStage* service{};
edict_t player{};
client_t client{};
unsigned order[8]{},orderCount{};
float observedFrame{};
double observedBase{};
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"stage check %u failed\n",checks);std::exit(2);}}
void Observe(unsigned value){Check(orderCount<8);order[orderCount++]=value;}
void Callback(void* context,edict_t* entity,float frame,double base) noexcept
{
    Check(context==&client&&entity==&player);++calls;Observe(3);
    observedFrame=frame;observedBase=base;
    if(mutation==6)GoldCraftPlayerThinkStage(entity,frame,base,std::uint32_t(g_psvs.spawncount),entity->serialnumber);
    if(mutation==7){Check(service->Release(token));Check(!service->Register(context,Callback));}
    if(mutation==8){GoldCraftPlayerThinkStageReset();Check(!service->Current(token));}
}
void Initialize()
{
    GoldCraftPlayerThinkStageReset();player={};client={};
    player.serialnumber=17;g_psv.state=ss_active;g_psvs.spawncount=5;
    client.edict=&player;client.svtimebase=10.12500000001;
    sv_player=&player;host_client=&client;frametime=.01F;gGlobalVariables.time=0;
    calls=nativeCalls=preCalls=freeCalls=mutation=orderCount=0;
    gEntityInterface={};
    gEntityInterface.pfnPlayerPreThink=[](edict_t* ent){Check(ent==&player);++preCalls;Observe(1);};
    gEntityInterface.pfnThink=[](edict_t* ent)
    {
        Check(ent==&player);++nativeCalls;Observe(2);
        if(mutation==1)ent->free=TRUE;
        if(mutation==2)++ent->serialnumber;
        if(mutation==3)++g_psvs.spawncount;
        if(mutation==4){host_client->svtimebase+=100;sv_player=nullptr;}
        if(mutation==5)ent->v.flags|=FL_KILLME;
    };
    token=service->Register(&client,Callback);Check(token&&service->Current(token));
}
}
void* operator new(std::size_t size){++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void ED_Free(edict_t* ent){++freeCalls;ent->free=TRUE;++ent->serialnumber;}
#include "native-player-think.inc"
namespace
{
void UsercmdPrefix()
{
#include "native-usercmd-think.inc"
}
}
int main()
{
    service=static_cast<goldcraft::IPlayerThinkStage*>(CreateInterface(goldcraft::PlayerThinkStageVersion,nullptr));
    Check(service!=nullptr&&!service->Current(0)&&!service->Release(0));
    Check(!service->Register(&client,Callback));
    Initialize();Check(!service->Register(&client,Callback)&&!service->Register(&client,nullptr));
    const auto first=token;Check(!service->Release(token+1));
    UsercmdPrefix();Check(preCalls==1&&nativeCalls==0&&calls==1&&orderCount==2&&order[0]==1&&order[1]==3);
    Check(observedFrame==frametime&&std::bit_cast<std::uint64_t>(observedBase)==std::bit_cast<std::uint64_t>(client.svtimebase));
    for(unsigned which=0;which<=8;++which)
    {
        Initialize();mutation=which;player.v.nextthink=10;
        const double base=client.svtimebase;UsercmdPrefix();
        Check(preCalls==1&&nativeCalls==1&&order[0]==1&&order[1]==2&&player.v.nextthink==0);
        const bool skipped=which==1||which==2||which==3||which==5;
        Check(calls==unsigned(!skipped)&&orderCount==unsigned(skipped?2:3));
        if(!skipped)Check(order[2]==3&&std::bit_cast<std::uint64_t>(observedBase)==std::bit_cast<std::uint64_t>(base));
        if(which==5)Check(freeCalls==1);
    }
    for(int flags:{FL_KILLME,FL_DORMANT})
    {
        Initialize();player.v.flags=flags;UsercmdPrefix();Check(calls==0&&nativeCalls==0);
    }
    Initialize();GoldCraftPlayerThinkStage(&player,NAN,10,5,17);
    GoldCraftPlayerThinkStage(&player,0,INFINITY,5,17);
    GoldCraftPlayerThinkStage(&player,-1,10,5,17);
    GoldCraftPlayerThinkStage(&player,0,10,6,17);
    GoldCraftPlayerThinkStage(&player,0,10,5,18);Check(!calls);
    player.free=TRUE;GoldCraftPlayerThinkStage(&player,0,10,5,17);Check(!calls);
    Initialize();g_psv.state=ss_loading;UsercmdPrefix();Check(calls==0);
    g_psv.state=ss_active;Check(service->Release(token)&&!service->Current(token));
    token=service->Register(&client,Callback);Check(token&&token!=first);
    ++g_psvs.spawncount;Check(!service->Current(token));
    const auto next=service->Register(&client,Callback);Check(next&&next!=token&&!service->Release(token));
    Initialize();const auto before=allocations;
    for(unsigned n=0;n<10000;++n){orderCount=0;GoldCraftPlayerThinkStage(&player,.01F,10.125,5,17);}
    Check(calls==10000&&allocations==before);
    GoldCraftPlayerThinkStageReset();Check(!service->Current(token));
    orderCount=0;GoldCraftPlayerThinkStage(&player,.01F,10.125,5,17);Check(calls==10000);
    std::printf("%u actual ReHLDS SDK checks; native Think/PreThink preserved; 10000 dispatches zero new; no live gameplay\n",checks);
}
