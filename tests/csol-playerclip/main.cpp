// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK structures and production wire/injection helpers. This does not
// execute the engine's hull walker or claim live collision verification.
#include "precompiled.h"
#include "../../native/csol/playerclip_movement.hpp"
#include "../../native/csol/scenario/round_clock.hpp"
#include <iostream>
#include <limits>
#include <memory>

// Matched SDK global used by cs_bot_manager.h during static initialization.
// The helper does not construct bots or exercise their navigation.
bool g_bAllowedCSBot = false;

namespace
{
int checks{}, failures{};
void Check(bool ok, const char* reason)
{ ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << reason << '\n'; } }

void Wire()
{
    using namespace csol::playerclip;
    std::array<std::uint8_t,30> bytes;
    const Descriptor d{349,120,{1.25f,-2048,0},{0,90,-0.5f}};
    const auto size = Write({Operation::Set,d},bytes);
    auto decoded = Read(bytes);
    Check(size == 30 && decoded && decoded->clip == d && decoded->operation == Operation::Set,
        "descriptor carries exact entity/model and raw float vectors");
    Check(bytes[0]==1 && bytes[1]==1 && bytes[2]==93 && bytes[3]==1 && bytes[4]==120 && bytes[5]==0,
        "little endian exact header");
    for (std::size_t n=0;n<bytes.size();++n)
        Check(!Read(std::span(bytes).first(n)),"truncated set cannot change current clip table");
    auto extra = std::array<std::uint8_t,31>{};std::copy(bytes.begin(),bytes.end(),extra.begin());
    Check(!Read(extra),"trailing data rejected");
    auto invalid=bytes;invalid[0]=2;Check(!Read(invalid),"unknown protocol rejected");
    invalid=bytes;invalid[2]=invalid[3]=0;Check(!Read(invalid),"world entity rejected");
    invalid=bytes;invalid[4]=invalid[5]=0;Check(!Read(invalid),"world model rejected");
    invalid=bytes;invalid[6]=0;invalid[7]=0;invalid[8]=128;invalid[9]=127;
    Check(!Read(invalid),"infinite origin rejected");
    Check(Write({Operation::Remove,d},bytes)==4 && Read(std::span(bytes).first(4))->clip.entity==349,
        "remove identifies only the existing clip");
    Check(Write({Operation::Reset,{}},bytes)==2 && Read(std::span(bytes).first(2))->operation==Operation::Reset,
        "reset is explicit and needs no fake descriptor");
}

void Prediction()
{
    using namespace csol::playerclip;
    auto move = std::make_unique<playermove_t>();
    move->numphysent=2;move->physents[0].info=0;move->physents[1].info=7;move->onground=-1;
    const physent_t native=move->physents[1];
    std::array<CollisionModel,2> clips{
        CollisionModel{Descriptor{349,120,{1,2,3},{}},reinterpret_cast<model_s*>(0x1000)},
        CollisionModel{Descriptor{711,361,{4,5,6},{}},reinterpret_cast<model_s*>(0x2000)}};
    Movement movement;
    Check(movement.Inject(*move,clips) && move->numphysent==4,"both real brush pointers appended after native order");
    Check(move->physents[1].info==native.info && move->physents[2].info==349 &&
        move->physents[2].model==clips[0].model && move->physents[2].solid==SOLID_NOT &&
        move->physents[2].skin==0,"brush hull and SOLID_NOT preserved without content flag hacks");
    move->onground=3;
    Check(move->numphysent==4 && move->physents[move->onground].info==711,
        "ground and touch consumers retain appended indices after helper returns");
    Check(movement.Inject(*move,clips) && move->numphysent==4 && move->onground==3,
        "multiple prediction commands do not accumulate duplicates or shift stable indices");
    Check(movement.Inject(*move,{}) && move->numphysent==2 && move->onground==-1,
        "removed snapshot clears only old verified tail and invalid ground index");
    move->numphysent=3;move->physents[2]={};move->physents[2].info=99;
    Check(movement.Inject(*move,clips) && move->numphysent==5 && move->physents[2].info==99,
        "new engine-built native physents are preserved");
    move->numphysent=MAX_PHYSENTS;
    for (int i=0;i<MAX_PHYSENTS;++i) {move->physents[i]={};move->physents[i].info=1000+i;}
    Check(!movement.Inject(*move,clips) && move->numphysent==MAX_PHYSENTS,
        "physent limit refuses whole append without overflowing the engine array");
    move->numphysent=1;
    Check(movement.Inject(*move,clips) && move->numphysent==3,"next engine rebuild recovers after capacity failure");
}

void Clock()
{
    using namespace csol::scenario;
    // Expected raw words captured by the original MP11385F80 instruction
    // oracle: map-playerclip-freeze-20261031/oracle.json, seven clock cases.
    struct Original { float start, now; unsigned round,total;std::uint32_t bits; };
    const Original cases[]={{10,10,1,2,0},{10,30,1,2,1101004800},{10,123.25f,2,2,1122140160},
        {10,5,2,2,0},{0,20,1,2,1101004800},{1000.125f,1000.375f,2,2,1048576000},
        {.125f,1.25f,255,255,1066401792}};
    for(const auto& c:cases)
    {
        RoundClock clock;
        Check(clock.BeginRound(c.round-1,c.total,c.start),"valid original round begins");
        const auto snapshot=clock.Snapshot(c.now);
        Check(snapshot && snapshot->round==c.round && snapshot->total==c.total &&
            std::bit_cast<std::uint32_t>(snapshot->elapsed)==c.bits,"clock matches original packet float32 bits");
    }
    RoundClock clock;Check(!clock.Snapshot(1),"no invented round before BeginRound");
    Check(clock.BeginRound(0,2,10) && clock.BeginRound(1,2,100) && clock.Snapshot(120)->elapsed==110,
        "stage change preserves full-game clock");
    Check(!clock.BeginRound(2,2,200) && clock.Snapshot(120)->round==2,"invalid stage leaves previous valid snapshot");
    Check(!clock.BeginRound(0,256,200),"round byte cannot silently wrap");
    Check(!clock.Snapshot(std::numeric_limits<float>::infinity()),"nonfinite time cannot enter a packet");
    clock.Reset();Check(!clock.Snapshot(200),"new map or complete reset clears old game clock");
    Check(clock.BeginRound(0,2,0) && clock.BeginRound(1,2,10) && clock.Snapshot(20)->elapsed==10,
        "original zero start sentinel is retained");
}
}
int main()
{
    Wire();Prediction();Clock();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures ? 1 : 0;
}
