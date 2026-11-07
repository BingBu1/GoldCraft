// SPDX-License-Identifier: GPL-3.0-or-later
#include "../client/weapons/projectiles.hpp"
#include "../client/scenario/core/death_notices.hpp"
#include "projectile_effects.hpp"
#include "weapon_resources.hpp"
#include <r_efx.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace csol::client
{
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
void* spriteListHook{};
void* spriteListOriginal{};
hook_t* Hook(void*, void* replacement, void** original)
{
    spriteListHook = replacement;
    *original = spriteListOriginal;
    return nullptr;
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
void Check(bool value, const char* reason = "")
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL check=%d %s\n", checks, reason); std::exit(1); }
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
struct IconContract
{
    const char* name;
    std::span<const char* const> resources;
    const char* originalTable{"sprites/hud.txt"};
    const char* originalRole{};
};
const IconContract contracts[]{
    {"d_tknifeex2",csol::TKnifeEx2Resources}, {"d_beamgun",csol::BeamgunResources,"sprites/weapon_beamgun.txt","kill"},
    {"d_beamgunle",csol::BeamgunLEResources,"sprites/weapon_beamgunle.txt","kill"}, {"d_cannon",csol::CannonResources},
    {"d_k3",csol::K3Resources}, {"d_skull1",csol::Skull1Resources}, {"d_skull2",csol::Skull2Resources},
    {"d_skull5",csol::Skull5Resources}, {"d_m249ex",csol::M249EXResources}, {"d_balrog11",csol::Balrog11Resources},
    {"d_dragonsword",csol::DragonSwordHudResources}, {"d_skullaxe",csol::SkullAxeHudResources}};

void CheckOriginalIcon(const std::filesystem::path& common, const IconContract& contract, const client_sprite_t& icon)
{
    std::ifstream table(common / contract.originalTable);
    Check(table.good(), contract.originalTable);
    const std::string resource="sprites/"+std::string(icon.szSprite)+".spr";
    const auto originalSource=csol::WeaponResourceSource(resource);
    std::string line;
    int matches{};
    while(std::getline(table,line))
    {
        std::istringstream row(line);
        std::string role,atlas;int resolution{},x{},y{},w{},h{};
        if(!(row>>role>>resolution>>atlas>>x>>y>>w>>h) || resolution!=640 ||
            role!=(contract.originalRole ? contract.originalRole : contract.name))continue;
        ++matches;
        Check(originalSource=="sprites/"+atlas+".spr",contract.name);
        Check(icon.rc.left==x && icon.rc.right==x+w && icon.rc.top==y && icon.rc.bottom==y+h,contract.name);
    }
    Check(matches==1,contract.name);
    Check(std::filesystem::is_regular_file(common/originalSource),contract.name);
}
}
int wmain(int argc,wchar_t** argv)
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
    const int expectedCount=int(originalSprites.size()+std::size(contracts)*2);
    auto* extended=list(hudPath,&count); Check(count==expectedCount);
    Check(std::memcmp(extended,originalSprites.data(),sizeof(originalSprites))==0);
    for(const auto& contract:contracts)for(int resolution:{320,640})
    {
        const auto matches=[&](const client_sprite_t& icon){return icon.iRes==resolution && !std::strcmp(icon.szName,contract.name);};
        Check(std::count_if(extended,extended+count,matches)==1,contract.name);
        const auto& icon=*std::find_if(extended,extended+count,matches);
        const std::string resource="sprites/"+std::string(icon.szSprite)+".spr";
        Check(std::any_of(contract.resources.begin(),contract.resources.end(),[&](auto path){return resource==path;}),contract.name);
        Check(icon.rc.right>icon.rc.left && icon.rc.bottom>icon.rc.top,contract.name);
        if(argc>1)CheckOriginalIcon(argv[1],contract,icon);
    }
    const auto stable=[&]{
        Check(list(hudPath,&count)==extended && count==expectedCount);
        Check(std::memcmp(extended,originalSprites.data(),sizeof(originalSprites))==0);
    };
    for(int i=0;i<3;++i)stable();
    ResetProjectiles();stable();
    InitializeDeathNotices();stable();
    std::printf("PASS projectile checks=%d\n",checks);
}
