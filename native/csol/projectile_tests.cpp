// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/projectiles.hpp"
#include "client/death_notices.hpp"
#include "projectile_effects.hpp"
#include <r_efx.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csol::client
{
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
void* spriteListHook{};
void* spriteListOriginal{};
void Hook(void*, void* replacement, void** original)
{
    spriteListHook = replacement;
    *original = spriteListOriginal;
}
}
namespace
{
using namespace csol::client;
pfnUserMsgHook receive{};
double now{};
int allocations{}, loads{}, checks{}, accepts=1;
bool failAllocation{};
model_t knife{}, sprite{};
BEAM beam{};
efx_api_t effects{};
cl_entity_t entity{};
std::array<client_sprite_t, 2> originalSprites{};
void Check(bool value)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL check=%d\n", checks); std::exit(1); }
}
void Reset()
{
    ResetProjectiles(); now=10; allocations=loads=0; accepts=1; failAllocation=false;
    entity={}; entity.index=42; entity.model=&knife;
}
int Send(unsigned version=1, unsigned index=42, unsigned model=23, int length=5)
{
    std::array<unsigned char,5> message{static_cast<unsigned char>(version),
        static_cast<unsigned char>(index), static_cast<unsigned char>(index>>8),
        static_cast<unsigned char>(model), static_cast<unsigned char>(model>>8)};
    return receive(csol::KnifeTrailMessage,length,message.data());
}
void Add(float x)
{
    entity.origin[0]=x;
    ProjectileAddEntity(0,&entity,knife.name);
}
}
int main()
{
    using namespace csol::client;
    std::strcpy(knife.name,"models/tknifeb.mdl"); knife.type=mod_studio; sprite.type=mod_sprite;
    engine.GetClientTime=[]()->float{return static_cast<float>(now);};
    engine.pfnHookUserMsg=[](const char*,pfnUserMsgHook callback)->int{receive=callback;return 1;};
    engine.CL_LoadModel=[](const char* name,int* index)->model_t*{
        Check(std::strcmp(name,"sprites/smoke.spr")==0); ++loads; *index=23; return &sprite;
    };
    downstream.HUD_AddEntity=[](int,cl_entity_t*,const char*)->int{return accepts;};
    effects.R_BeamFollow=[](int index,int model,float life,float width,float r,float g,float b,float brightness)->BEAM*{
        ++allocations; Check(index==42 && model==23 && life==.1f && width==1 && r==1 && g==1 && b==1);
        Check(brightness==200.f/255); return failAllocation?nullptr:&beam;
    };
    engine.pEfxAPI=&effects; InitializeProjectiles();
    Reset();
    Check(!receive(csol::KnifeTrailMessage,5,nullptr));
    Check(!Send(1,42,23,4)); Check(!Send(1,42,23,6)); Check(!Send(2));
    Check(!Send(1,0)); Check(!Send(1,2048)); Check(!Send(1,42,0));
    Add(0); Add(30); Check(allocations==0 && loads==0);
    Reset(); Check(Send()==1); Add(0); now+=.15; Add(0); Check(allocations==0);
    Add(20); Check(allocations==1); Add(60); Check(allocations==1);
    Reset(); Send(); accepts=0; Add(0); Add(50); Check(allocations==0);
    accepts=1; Add(50); Add(70); Check(allocations==1);
    Reset(); Send(); Add(0); now+=1.1; Add(100); Check(allocations==0);
    Reset(); Send(); Add(0); ResetProjectiles(); Add(100); Check(allocations==0);
    Reset(); Send(1,42,65535); Add(0); Add(20); Check(loads==1 && allocations==0);
    Reset(); Send(); Add(0); failAllocation=true; Add(20); Add(40); Check(allocations==1);
    Reset(); Send(); entity.model=nullptr; Add(0); Add(20); Check(allocations==0);
    entity.model=&knife; Add(20); Add(40); Check(allocations==1);
    std::strcpy(originalSprites[0].szName,"d_skull");
    std::strcpy(originalSprites[0].szSprite,"640hud1");
    originalSprites[0].iRes=640; originalSprites[0].rc={0,16,0,16};
    originalSprites[1]=originalSprites[0]; std::strcpy(originalSprites[1].szName,"d_ak47");
    engine.pfnSPR_GetList=[](char*,int* count)->client_sprite_t*{*count=2;return originalSprites.data();};
    spriteListOriginal=reinterpret_cast<void*>(engine.pfnSPR_GetList);
    InitializeDeathNotices();
    auto list=reinterpret_cast<decltype(engine.pfnSPR_GetList)>(spriteListHook);
    char hudPath[]="sprites/hud.txt", weaponPath[]="sprites/weapon_ak47.txt";
    int count=0; Check(list(weaponPath,&count)==originalSprites.data() && count==2);
    auto* extended=list(hudPath,&count); Check(count==8);
    Check(std::memcmp(extended,originalSprites.data(),sizeof(originalSprites))==0);
    Check(std::strcmp(extended[3].szName,"d_tknifeex2")==0 && extended[3].iRes==640);
    Check(std::strcmp(extended[3].szSprite,"csol/640hud12")==0);
    Check(extended[3].rc.left==172 && extended[3].rc.top==32 &&
        extended[3].rc.right==220 && extended[3].rc.bottom==48);
    Check(std::strcmp(extended[5].szName,"d_beamgun")==0&&extended[5].rc.top==0);
    Check(std::strcmp(extended[7].szName,"d_beamgunle")==0&&extended[7].rc.top==16);
    Check(std::strcmp(extended[7].szSprite,"csol/640hud266")==0);
    for (int i=0;i<3;++i) Check(list(hudPath,&count)==extended && count==8);
    ResetProjectiles(); Check(list(hudPath,&count)==extended && count==8);
    InitializeDeathNotices(); Check(list(hudPath,&count)==extended && count==8);
    std::printf("PASS projectile checks=%d\n",checks);
}
