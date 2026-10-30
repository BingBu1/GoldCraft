// SPDX-License-Identifier: GPL-3.0-or-later
// Actual consumer and TEMPENTITY callbacks; engine allocation/lookup are controlled.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario_effects.hpp"
#include "../../native/csol/scenario_healing.hpp"
#include <r_efx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <source_location>

namespace fixture
{
std::map<std::string,pfnUserMsgHook> receivers;
std::map<int,cl_entity_t> owners;
std::array<TEMPENTITY, 600> temps{};
model_t ownerModel{}, sprite{};
efx_api_t effects{};
int checks{}, forwards{}, fallbacks{}, allocations{}, loads{}, synced{}, forcedSlot{-1};
bool existing{}, failModel{}, failAlloc{};
float now{10};
std::string loaded;
std::array<float,3> allocatedPosition{};
void Check(bool value,const std::source_location location=std::source_location::current())
{ ++checks; if(!value)throw std::runtime_error("healing client line "+std::to_string(location.line())); }
int Previous(const char*,int,void*){++forwards;return 73;}
int Send(std::vector<unsigned char> bytes)
{return receivers.at("MPToCL")("MPToCL",int(bytes.size()),bytes.data());}
int Heal(int entity,bool self=false)
{return Send({4,static_cast<unsigned char>(entity),static_cast<unsigned char>(entity>>8),
    static_cast<unsigned char>(self?13:9)});}
void Alien(TEMPENTITY*,float,float){}
TEMPENTITY* Alloc(float* origin,model_t* model)
{
    Check(model==&sprite);++allocations;
    std::copy_n(origin,3,allocatedPosition.begin());
    if(failAlloc)return nullptr;
    const int index=forcedSlot>=0?forcedSlot:allocations-1;
    Check(index>=0&&index<int(temps.size()));
    temps[index]={};temps[index].entity.model=model;
    std::copy_n(origin,3,temps[index].entity.origin);
    return &temps[index];
}
void AddOwner(int index)
{
    auto& owner=owners[index];owner={};owner.index=index;owner.model=&ownerModel;
    owner.origin[0]=3;owner.origin[1]=4;owner.origin[2]=5;
}
}
namespace csol::client
{
metahook_api_t* api{};cl_enginefunc_t engine{};
void SynchronizeSession(){++fixture::synced;}
}
int main(int argc,char**)
{
    using namespace fixture;using namespace csol::client;
    try
    {
        existing=argc==1;sprite.type=mod_sprite;sprite.numframes=8;
        if(existing)for(const char* name:{"MPToCL","InitHUD"})receivers[name]=Previous;
        static metahook_api_t host;api=&host;
        host.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook
        {auto it=receivers.find(name);if(it==receivers.end())return nullptr;auto old=it->second;it->second=hook;return old;};
        engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook hook)->int
        {++fallbacks;receivers[name]=hook;return 1;};
        engine.GetEntityByIndex=[](int index)->cl_entity_t*
        {auto it=owners.find(index);return it==owners.end()?nullptr:&it->second;};
        engine.CL_LoadModel=[](const char* path,int* index)->model_t*
        {++loads;loaded=path;*index=41;return failModel?nullptr:&sprite;};
        engine.GetClientTime=[]()->float{return now;};
        effects.CL_TempEntAlloc=Alloc;engine.pEfxAPI=&effects;
        InitializeScenarioEffects();InitializeScenarioEffects();Check(fallbacks==(existing?0:2));
        int before=forwards;
        Check(Send({27,2,1,1,0})==(existing?73:1));Check(forwards==before+(existing?1:0));
        before=forwards;Check(Send({4,1,0,7,88})==(existing?73:1));
        Check(forwards==before+(existing?1:0));Check(allocations==0);
        before=forwards;Send({4,1});Send({4,1,0,9,88});Check(forwards==before&&allocations==0);
        AddOwner(300);Heal(300);Check(allocations==1&&loaded==csol::healing::Sprites[0]);
        auto& first=temps[0];
        Check(first.clientIndex==300&&first.callback&&first.tentOffset[2]==40);
        Check(allocatedPosition==std::array<float,3>{3,4,45});
        Check(first.die==11&&first.fadeSpeed==1&&first.frameMax==8);
        Check(first.entity.curstate.rendermode==kRenderTransAdd&&first.entity.curstate.renderfx==14);
        Check(first.entity.curstate.renderamt==254&&first.entity.baseline.renderamt==254);
        Check(first.entity.curstate.framerate==10&&first.entity.curstate.frame==0&&first.entity.curstate.scale==1);
        Check(first.flags==(FTENT_FADEOUT|FTENT_SPRANIMATE|FTENT_PERSIST|FTENT_SPRANIMATELOOP|FTENT_CLIENTCUSTOM));
        owners[300].origin[0]=13;owners[300].origin[2]=15;first.callback(&first,.1f,10.1f);
        Check(first.entity.origin[0]==13&&first.entity.origin[1]==4&&first.entity.origin[2]==55);
        AddOwner(32767);Heal(32767,true);Check(allocations==2&&temps[1].clientIndex==32767);
        Check(loaded==csol::healing::Sprites[1]&&temps[1].tentOffset[2]==25);
        Check(allocatedPosition==std::array<float,3>{3,4,30});
        Heal(0);Heal(32768);Heal(65535);Heal(301);Check(allocations==2);
        owners[300].model=nullptr;first.callback(&first,.1f,10.2f);
        Check(first.die==10.2f&&(first.flags&FTENT_NOMODEL));owners[300].model=&ownerModel;
        failModel=true;Heal(300);failModel=false;Check(allocations==2);
        sprite.type=mod_studio;Heal(300);sprite.type=mod_sprite;sprite.numframes=0;Heal(300);
        Check(allocations==2);sprite.numframes=8;failAlloc=true;Heal(300);failAlloc=false;Check(allocations==3);
        ResetScenarioEffects();Check(temps[1].die==-1&&(temps[1].flags&FTENT_NOMODEL));
        Heal(300);auto& foreign=temps[3];foreign.callback=Alien;foreign.die=123;foreign.flags=0;
        ResetScenarioEffects();Check(foreign.die==123&&foreign.flags==0);
        Heal(300);auto& tokenChanged=temps[4];++tokenChanged.entity.curstate.iuser4;
        tokenChanged.die=321;tokenChanged.flags=0;ResetScenarioEffects();
        Check(tokenChanged.die==321&&tokenChanged.flags==0);
        Heal(300);forcedSlot=5;Heal(32767,true);forcedSlot=-1;
        Check(temps[5].clientIndex==32767);ResetScenarioEffects();
        Check(temps[5].die==-1&&(temps[5].flags&FTENT_NOMODEL));
        Heal(300);before=forwards;
        const auto result=receivers.at("InitHUD")("InitHUD",0,nullptr);
        Check(result==(existing?73:1)&&forwards==before+(existing?1:0));
        Check(temps[7].die==-1&&(temps[7].flags&FTENT_NOMODEL));
        Heal(300);ShutdownScenarioEffects();Check(temps[8].die==-1);
        InitializeScenarioEffects();Heal(300);Check(temps[9].die==11);ShutdownScenarioEffects();
        Check(synced>0);
        std::cout<<"monster-healing-client "<<(existing?"existing":"missing")<<": "<<checks<<" checks passed\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
