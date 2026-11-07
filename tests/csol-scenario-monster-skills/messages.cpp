// SPDX-License-Identifier: GPL-3.0-or-later
// Real scenario_messages.cpp and ReGameDLL ABI, controlled engine serialization only.
#include "precompiled.h"
#include "../../native/csol/server/scenario/core/scenario_messages.h"
#include "../../native/csol/server/damage_numbers.h"
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <source_location>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
int gmsgShake{111};
// Unrelated damage-number path is deliberately outside this effect suite.
bool CSOLWriteDamageNumber(int, edict_t*, CSOLDamageNumber)
{ throw std::runtime_error("unexpected floating damage message"); }
namespace
{
int checks{}, begins{}, ends{}, destination{}, type{}, nextId{96};
bool hasOrigin{};
edict_t* recipient{};
std::array<float, 3> pvs{};
std::vector<unsigned char> bytes;
std::vector<float> coordinates;
std::map<std::string, std::pair<int,int>> registrations;
std::string registrationFailure;
std::function<void()> onBegin, onEnd;
void Check(bool value, const std::source_location where = std::source_location::current())
{ ++checks; if (!value) throw std::runtime_error("monster-fx-messages line "+std::to_string(where.line())); }
int Register(const char* name, int size)
{
    if (registrationFailure == name) return 0;
    const int id = nextId++; registrations[name] = {id,size}; return id;
}
void Begin(int target, int message, const float* origin, edict_t* player)
{
    ++begins; destination=target;type=message;recipient=player;hasOrigin=origin!=nullptr;
    if(origin)std::copy_n(origin,3,pvs.begin());
    const auto action=onBegin;if(action)action();
}
void Byte(int value) { bytes.push_back(static_cast<unsigned char>(value)); }
void Short(int value) { Byte(value);Byte(static_cast<unsigned>(value)>>8); }
void Coord(float value)
{
    coordinates.push_back(value);
    // Engine boundary captures the stock 1/8 coordinate wire representation.
    Short(static_cast<int>(value*8));
}
void End() { ++ends;const auto action=onEnd;if(action)action(); }
void Clear() { begins=ends=destination=type=0;recipient=nullptr;hasOrigin=false;bytes.clear();coordinates.clear(); }
void Install()
{
    g_engfuncs={};g_engfuncs.pfnRegUserMsg=Register;g_engfuncs.pfnMessageBegin=Begin;
    g_engfuncs.pfnMessageEnd=End;g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;
    g_engfuncs.pfnWriteCoord=Coord;nextId=96;gmsgShake=111;
    registrations.clear();registrationFailure.clear();onBegin={};onEnd={};Clear();
}
void Payloads()
{
    Install(); CCSOLScenarioMessages messages;edict_t player{};
    const float origin[]{-12.25f, 100.5f, 43.125f};
    Check(!messages.SendMonsterDisarmImpact(origin)&&begins==0);
    Check(!messages.SendMonsterZombieBomb(origin)&&begins==0);
    Check(!messages.SendMonsterTrapDirection(&player,origin)&&begins==0);
    Check(messages.Register());
    Check(registrations.at("CsoMonFX").second==-1);
    Check(messages.SendMonsterDisarmImpact(origin));
    Check(begins==1&&ends==1&&destination==MSG_PVS&&type==registrations.at("CsoMonFX").first);
    Check(type>=64&&type!=SVC_TEMPENTITY&&hasOrigin&&!recipient);
    Check(pvs==std::array<float,3>{-12.25f,100.5f,43.125f});
    Check(coordinates==std::vector<float>{-12.25f,100.5f,23.125f});
    Check(bytes==std::vector<unsigned char>{1,158,255,36,3,185,0});
    Clear();Check(messages.SendMonsterZombieBomb(origin));
    Check(begins==1&&ends==1&&destination==MSG_PVS&&type==registrations.at("CsoMonFX").first);
    Check(hasOrigin&&!recipient&&pvs==std::array<float,3>{-12.25f,100.5f,43.125f});
    Check(coordinates==std::vector<float>{-12.25f,100.5f,43.125f});
    Check(bytes==std::vector<unsigned char>{3,158,255,36,3,89,1});
    for(const std::array<float,3> edge : {std::array<float,3>{-4096,4095.875f,0},std::array<float,3>{.125f,-.125f,4095.875f}})
    {
        Clear();Check(messages.SendMonsterZombieBomb(edge.data()));
        Check(coordinates==std::vector<float>(edge.begin(),edge.end()));
        Check(pvs==edge&&bytes.size()==7&&bytes[0]==3&&begins==1&&ends==1);
    }
    Clear();Check(messages.SendMonsterTrapDirection(&player,origin));
    Check(begins==1&&ends==1&&destination==MSG_ONE&&recipient==&player&&!hasOrigin);
    Check(bytes==std::vector<unsigned char>{2,158,255,36,3,89,1});
    Clear();Check(messages.SendMonsterSkillShake(&player));
    Check(type==gmsgShake&&destination==MSG_ONE&&recipient==&player&&!hasOrigin);
    Check(bytes==std::vector<unsigned char>{0,160,0,32,0,100}&&begins==1&&ends==1);
}
void Hits()
{
    Install();CCSOLScenarioMessages messages;float origin[]{-12.25f,100.5f,43.125f};
    Check(!messages.SendMonsterHit(origin,false,4)&&begins==0);Check(messages.Register());
    for(bool metal:{false,true})
    {
        Clear();Check(messages.SendMonsterHit(origin,metal,metal?16:4));
        Check(begins==1&&ends==1&&destination==MSG_PVS&&type==registrations.at("CsoMonFX").first);
        Check(hasOrigin&&!recipient&&pvs==std::array<float,3>{-12.25f,100.5f,43.125f});
        Check(bytes==std::vector<unsigned char>{static_cast<unsigned char>(metal?5:4),static_cast<unsigned char>(metal?16:4),158,255,36,3,89,1});
        Check(coordinates==std::vector<float>{-12.25f,100.5f,43.125f});
    }
    Clear();Check(!messages.SendMonsterHit(nullptr,false,4)&&begins==0);
    for(bool metal:{false,true})for(int value:{-1,0,metal?61:16,256})Check(!messages.SendMonsterHit(origin,metal,value)&&begins==0);
    for(int axis=0;axis<3;++axis)for(float value:{-4096.125f,4096.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        auto old=origin[axis];origin[axis]=value;Check(!messages.SendMonsterHit(origin,false,4)&&begins==0);origin[axis]=old;
    }
    const auto full=g_engfuncs;
    for(int missing=0;missing<4;++missing){g_engfuncs=full;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;if(missing==3)g_engfuncs.pfnWriteCoord=nullptr;
        Check(!messages.SendMonsterHit(origin,false,4)&&begins==0);
    }g_engfuncs=full;
    CCSOLScenarioMessages other;Check(other.Register());
    onBegin=[&]{Check(!other.SendMonsterHit(origin,false,4));origin[0]=300;messages={};};
    onEnd=[&]{Check(!other.SendMonsterHit(origin,true,16));};
    Check(messages.SendMonsterHit(origin,false,4));Check(begins==1&&ends==1&&coordinates==std::vector<float>{-12.25f,100.5f,43.125f});
    onBegin={};onEnd={};Clear();Check(other.SendMonsterHit(origin,false,4)&&begins==1&&ends==1);
}

void Invalid()
{
    Install();CCSOLScenarioMessages messages;edict_t player{};
    Check(messages.Register());float origin[]{1,2,3};
    Check(!messages.SendMonsterDisarmImpact(nullptr)&&begins==0);
    Check(!messages.SendMonsterZombieBomb(nullptr)&&begins==0);
    Check(!messages.SendMonsterTrapDirection(nullptr,origin)&&begins==0);
    Check(!messages.SendMonsterTrapDirection(&player,nullptr)&&begins==0);
    Check(!messages.SendMonsterSkillShake(nullptr)&&begins==0);
    player.free=1;
    Check(!messages.SendMonsterTrapDirection(&player,origin)&&begins==0);
    Check(!messages.SendMonsterSkillShake(&player)&&begins==0);player.free=0;
    for(int axis=0;axis<3;++axis)
        for(float value:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        {
            origin[axis]=value;
            Check(!messages.SendMonsterDisarmImpact(origin)&&begins==0);
            Check(!messages.SendMonsterZombieBomb(origin)&&begins==0);
            Check(!messages.SendMonsterTrapDirection(&player,origin)&&begins==0);
            origin[axis]=float(axis+1);
        }
    for(int id:{0,63,256,-1}){gmsgShake=id;Check(!messages.SendMonsterSkillShake(&player)&&begins==0);}
    gmsgShake=111;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<5;++missing)
    {
        g_engfuncs=complete;Clear();
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteCoord=nullptr;
        if(missing==4)g_engfuncs.pfnWriteShort=nullptr;
        if(missing<4)
        {
            Check(!messages.SendMonsterDisarmImpact(origin)&&begins==0);
            Check(!messages.SendMonsterZombieBomb(origin)&&begins==0);
            Check(!messages.SendMonsterTrapDirection(&player,origin)&&begins==0);
        }
        if(missing<2||missing==4)Check(!messages.SendMonsterSkillShake(&player)&&begins==0);
    }
    g_engfuncs=complete;registrationFailure="CsoMonFX";Clear();Check(!messages.Register());
    Check(!messages.SendMonsterDisarmImpact(origin)&&begins==0);
    Check(!messages.SendMonsterZombieBomb(origin)&&begins==0);
    Check(!messages.SendMonsterTrapDirection(&player,origin)&&begins==0);
}
void Reentry()
{
    Install();CCSOLScenarioMessages messages,other;edict_t player{};float origin[]{1,2,23};
    Check(messages.Register()&&other.Register());
    const auto rejectNested=[&]
    {
        const int before=begins;
        Check(!other.SendMonsterDisarmImpact(origin));
        Check(!other.SendMonsterZombieBomb(origin));
        Check(!other.SendMonsterTrapDirection(&player,origin));
        Check(!other.SendMonsterSkillShake(&player));
        Check(begins==before);
    };
    onBegin=[&]{rejectNested();origin[0]=1000;origin[2]=2000;};onEnd=rejectNested;
    Check(messages.SendMonsterDisarmImpact(origin));
    Check(begins==1&&ends==1&&coordinates==std::vector<float>{1,2,3});
    onBegin={};onEnd={};Clear();Check(messages.SendMonsterDisarmImpact(origin)&&begins==1&&ends==1);
    Clear();origin[0]=1;origin[2]=23;
    onBegin=[&]{rejectNested();origin[0]=1000;origin[2]=2000;};onEnd=rejectNested;
    Check(messages.SendMonsterZombieBomb(origin));
    Check(begins==1&&ends==1&&coordinates==std::vector<float>{1,2,23});
    onBegin={};onEnd={};Clear();Check(messages.SendMonsterZombieBomb(origin)&&begins==1&&ends==1);
}
}
int main()
{
    try { Payloads();Hits();Invalid();Reentry();std::cout<<"monster-fx-messages "<<checks<<" checks passed\n";return 0; }
    catch(const std::exception& error){std::cerr<<error.what()<<" after "<<checks<<" checks\n";return 1;}
}
