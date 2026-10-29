// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the production consumer/session against the real resource catalog.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/damage_display.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <source_location>

namespace fixture
{
std::filesystem::path common;
std::string level="maps/cs_assault.bsp";
float now=10;
cvar_t selected{},enabled{};
cl_entity_t entity{};
model_t model{};
pfnUserMsgHook receiver{};
int checks{},forwarded{},prepared{},graphicsResets{};
void Check(bool ok,const std::source_location where=std::source_location::current())
{++checks;if(!ok)throw std::runtime_error("client check line "+std::to_string(where.line()));}
int Previous(const char*,int,void*){++forwarded;return 73;}
void Quiet(const char*,...){}
int Send(int target=2,int style=0,int flags=0,int value=123)
{
    std::array<std::byte,9> bytes{std::byte{206},std::byte(target&255),std::byte((target>>8)&255),std::byte(style),std::byte(flags)};
    for(int i=0;i<4;++i)bytes[5+i]=std::byte((std::uint32_t(value)>>(8*i))&255);
    return receiver("MPToCL",9,bytes.data());
}
void Frame(){csol::client::SynchronizeFrameSession();csol::client::DamageDisplayFrame();}
}
namespace csol::client
{
cl_enginefunc_t engine{};
metahook_api_t* api{};
void ClearScene(){}
void ResetWeapons(bool){}
void ResetCamera(){}
void ResetProjectiles(){}
void ResetCharacters(){}
void ResetStatusIcons(){}
void ResetWallStatus(){}
void ResetBuyMenu(){}
void PrepareDamageGraphics(int){++fixture::prepared;}
void ResetDamageGraphics(){++fixture::graphicsResets;}
void ShutdownDamageOptions(){}
std::vector<std::byte> ReadResource(const char* name)
{
    std::ifstream stream(fixture::common/name,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("fixture resource missing");
    std::vector<std::byte> bytes(std::size_t(stream.tellg()));stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());return bytes;
}
}
int wmain(int argc,wchar_t** argv)
{
    using namespace fixture;using namespace csol::client;
    try
    {
        Check(argc==2);common=argv[1];
        static metahook_api_t host{};api=&host;
        host.HookUserMsg=[](const char*,pfnUserMsgHook callback)->pfnUserMsgHook{receiver=callback;return Previous;};
        engine.pfnGetCvarPointer=[](const char*)->cvar_t*{return nullptr;};
        engine.pfnRegisterVariable=[](const char* name,const char*,int flags)->cvar_t*{
            Check((flags&FCVAR_ARCHIVE)!=0);auto* c=std::strcmp(name,"cl_dmg_skin")==0?&selected:&enabled;c->value=1;return c;};
        engine.Cvar_SetValue=[](const char* name,float value){(std::strcmp(name,"cl_dmg_skin")==0?selected:enabled).value=value;};
        engine.GetClientTime=[](){return now;};
        engine.pfnGetLevelName=[]()->const char*{return level.c_str();};
        engine.GetEntityByIndex=[](int index)->cl_entity_t*{return index==2?&entity:nullptr;};
        engine.pfnRandomLong=[](int low,int)->int{return low;};
        engine.Con_Printf=engine.Con_DPrintf=Quiet;
        model.type=mod_studio;entity.model=&model;entity.origin[0]=100;entity.origin[2]=80;
        InitializeDamageDisplay();Frame();Check(DamageSkins().size()==46);
        Check(Send()==1 && DamageEntries().size()==1);Check(Send()==1 && DamageEntries().size()==2);
        Check(!DamageEntries()[0].newest && DamageEntries()[1].newest);
        Check(DamageEntries()[0].anchor.x==100 && DamageEntries()[0].anchor.z==100);
        Check(DamageEntries()[0].offsetX==-40 && DamageEntries()[0].offsetY==-5);
        entity.origin[0]=999;Check(DamageEntries()[0].anchor.x==100);
        std::byte other[]{std::byte{177}};Check(receiver("MPToCL",1,other)==73 && forwarded==1);
        std::byte broken[]{std::byte{206}};Check(receiver("MPToCL",1,broken)==1 && forwarded==1);
        Send(-1);Send(9);Send(2,250);Check(DamageEntries().size()==2);
        now+=.1f;Frame();Check(DamageEntries()[0].skipped && !DamageEntries()[1].skipped);
        now+=1;Frame();Check(DamageEntries().empty());
        Send();level="maps/de_dust2.bsp";Frame();Check(DamageEntries().empty());
        Send();now-=2;Frame();Check(DamageEntries().empty());
        Send();ApplyDamageSettings(false,2);Check(!DamageEnabled() && DamageEntries().empty() && selected.value==2);
        Send();Check(DamageEntries().empty());ApplyDamageSettings(true,2);Send(2,6,4,42);
        Check(DamageEntries().size()==1 && DamageEntries()[0].message.value==42);
        selected.value=3;Frame();Check(DamageEntries().empty());
        Send();DamageDisplayVideoInit();Check(DamageEntries().empty() && graphicsResets==1);
        ApplyDamageSettings(true,0);Send();now+=.1f;Send();Frame();
        Check(DamageEntries().size()==2 && !DamageEntries()[0].skipped);
        Check(DamageEntries()[0].anchor.z==120 && DamageEntries()[0].animation.skipTime==0);
        now+=.8f;Frame();Check(DamageEntries().size()==2);
        now+=.3f;Frame();Check(DamageEntries().empty());
        ShutdownDamageDisplay();Check(DamageSkins().empty());
        std::cout<<checks<<" production consumer/session checks passed; no VGUI/GL/live acceptance\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
