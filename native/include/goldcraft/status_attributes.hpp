#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <deque>

namespace goldcraft {
// Values are authored by the authenticated Minecraft server, never a client.
struct StatusAttributes {
    std::uint64_t epoch=0,revision=0,ack=0,shield=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,flags=0;
    float speed=1,resistance=1,absorption=0,durability=0;
};
inline StatusAttributes read_status_attributes(std::span<const std::uint8_t> bytes){
    Reader r(bytes);StatusAttributes s;
    s.epoch=r.u64();s.revision=r.u64();s.ack=r.u64();s.shield=r.u64();
    s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();s.flags=r.u32();
    s.speed=r.f32();s.resistance=r.f32();s.absorption=r.f32();s.durability=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||(s.flags&~3u)
        ||s.speed<0||s.speed>16||s.resistance<0||s.resistance>1||s.absorption<0||s.absorption>100000
        ||s.durability<0||s.durability>10000000||((s.flags&1)!=0&&!s.shield))
        throw ProtocolError("Invalid native status attributes");
    return s;
}
struct StatusSpend {
    std::uint64_t event=0,shield=0;
    float absorption=0,durability=0;
};
// Snapshot acknowledgements cannot replenish an unacknowledged local spend.
// The queue survives form changes; the native spawn, connection and world fence it.
class StatusBudget {
public:
    StatusAttributes state;
    std::uint64_t sequence=0;
    std::deque<StatusSpend> pending;
    bool update(const StatusAttributes& next){
        if(next.revision<=state.revision||next.ack<state.ack||next.ack>sequence)return false;
        state=next;
        while(!pending.empty()&&pending.front().event<=next.ack)pending.pop_front();
        return true;
    }
    float absorption()const{
        float amount=state.absorption;
        for(const auto& p:pending)amount-=p.absorption;
        return std::max(0.0f,amount);
    }
    float durability()const{
        float amount=state.durability;
        for(const auto& p:pending)if(p.shield==state.shield)amount-=p.durability;
        return std::max(0.0f,amount);
    }
    bool available()const{return pending.size()<128&&sequence!=UINT64_MAX;}
    void commit(float absorption,float durability){pending.push_back({++sequence,state.shield,absorption,durability});}
};
}
