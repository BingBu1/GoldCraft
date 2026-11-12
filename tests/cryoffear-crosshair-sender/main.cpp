// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/crosshair_dispatch.hpp"
#include "native/guest/cof/server/engine_message.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
enginefuncs_t g_engfuncs{};
globalvars_t globals{};globalvars_t* gpGlobals=&globals;
bool g_bAllowedCSBot{};
const Vector g_vecZero{};
namespace
{
using namespace guest::cof;
unsigned checks{},failures{},calls{},cancelAt{},recycleAt{},begins{},ends{},written{};
bool valid=true,packetOpen{},nested{};
std::array<edict_t,2> edicts;
std::array<CBasePlayer,2> players;
CrosshairPacket packet;
CrosshairSnapshot snapshot{{4,5,6},7,8,3};
void Check(bool value,int line){++checks;if(!value){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
void Edge(){++calls;if(calls==cancelAt)valid=false;if(calls==recycleAt)++edicts[1].serialnumber;}
bool Current(void*) noexcept{return valid;}
void Begin(int kind,int message,const float* origin,edict_t* recipient)
{
    CHECK(kind==MSG_ONE_UNRELIABLE&&message==77&&!origin&&recipient==&edicts[1]&&!packetOpen);
    packetOpen=true;++begins;Edge();
    nested=SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Busy;
}
void Byte(int byte){CHECK(packetOpen&&written<packet.size());if(written<packet.size())packet[written++]=std::byte(byte);Edge();}
void End(){CHECK(packetOpen&&written==CrosshairBytes);packetOpen=false;++ends;Edge();}
void Reset()
{
    calls=cancelAt=recycleAt=begins=ends=written=0;valid=true;packetOpen=nested=false;packet={};
    edicts[1]={};edicts[1].serialnumber=9;edicts[1].pvPrivateData=&players[1];
    edicts[1].v.pContainingEntity=&edicts[1];players[1].pev=&edicts[1].v;
    g_engfuncs={};g_engfuncs.pfnMessageBegin=Begin;g_engfuncs.pfnMessageEnd=End;g_engfuncs.pfnWriteByte=Byte;
}
}
#include "sdk-abort-virtuals.inc"
int main()
{
    Reset();CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Sent);
    CHECK(begins==1&&ends==1&&nested&&!packetOpen);
    CrosshairSnapshot read{};CHECK(DecodeCrosshair(packet,read)&&read==snapshot);
    for(unsigned edge=1;edge<=24;++edge)
    {
        Reset();cancelAt=edge;
        CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Cancelled);
        CHECK(begins==1&&ends==1&&written==CrosshairBytes&&!packetOpen&&nested);
        CHECK(DecodeCrosshair(packet,read)&&read==snapshot);
        Reset();recycleAt=edge;
        CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Cancelled);
        CHECK(begins==1&&ends==1&&written==CrosshairBytes&&!packetOpen);
    }
    Reset();valid=false;CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Cancelled&&!begins);
    Reset();CHECK(SendCrosshairSnapshot(players[1],snapshot,0,{nullptr,Current})==CrosshairSend::MissingBackend&&!begins);
    Reset();g_engfuncs.pfnWriteByte=nullptr;CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::MissingBackend&&!begins);
    Reset();auto invalid=snapshot;invalid.sequence=0;CHECK(SendCrosshairSnapshot(players[1],invalid,77,{nullptr,Current})==CrosshairSend::Invalid&&!begins);
    Reset();edicts[1].free=true;CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Cancelled&&!begins);
    Reset();guest::cof::detail::messageOpen=true;
    CHECK(SendCrosshairSnapshot(players[1],snapshot,77,{nullptr,Current})==CrosshairSend::Busy&&!begins);
    guest::cof::detail::messageOpen=false;
    std::printf("actual crosshair sender SDK checks=%u failures=%u; full packet after retirement; no gameplay/performance\n",checks,failures);
    return failures?1:0;
}
