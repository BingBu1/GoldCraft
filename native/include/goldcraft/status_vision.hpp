#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>

namespace goldcraft::status_vision {
constexpr const char* message_name="GCVision";
constexpr const char* client_capability="_gc_vision";
constexpr unsigned wire_bytes=48;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    float night=0,blindness=0,darkness=0,pulse=0;
};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.night=r.f32();s.blindness=r.f32();s.darkness=r.f32();s.pulse=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life)
        throw ProtocolError("Invalid vision identity");
    for(float v:{s.night,s.blindness,s.darkness,s.pulse})
        if(!std::isfinite(v)||v<0||v>1)throw ProtocolError("Invalid vision coefficient");
    if(s.pulse>s.darkness*.45f+.00001f)throw ProtocolError("Invalid darkness pulse");
    return s;
}
class Receiver {
    State state_{};double received_=0;
public:
    void clear(){state_={};received_=0;}
    bool accept(const State& s,double now){
        if(!std::isfinite(now))return false;
        if(s.epoch==state_.epoch&&s.serial==state_.serial&&s.revision<=state_.revision)return false;
        state_=s;received_=now;return true;
    }
    const State* active(std::uint64_t world,unsigned slot,unsigned serial,unsigned life,double now)const{
        return world&&state_.epoch==world&&state_.slot==slot&&state_.serial==serial&&state_.life==life
            &&now>=received_&&now-received_<.35?&state_:nullptr;
    }
};
}
