#pragma once
#include "goldcraft/view_distortion.hpp"
#include <algorithm>

namespace goldcraft::status_nausea {
constexpr const char* message_name="GCNausea";
constexpr const char* client_capability="_gc_nausea";
constexpr unsigned wire_bytes=48;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    float previous=0,current=0,angle=0,step=0;
};
struct Sample {float intensity=0,angle=0;};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.previous=r.f32();s.current=r.f32();s.angle=r.f32();s.step=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life)
        throw ProtocolError("Invalid nausea identity");
    if(!std::isfinite(s.previous)||s.previous<0||s.previous>1||!std::isfinite(s.current)||s.current<0||s.current>1
        ||!std::isfinite(s.angle)||std::abs(s.angle)>3.141593f||!std::isfinite(s.step)||s.step<0||s.step>.349066f)
        throw ProtocolError("Invalid nausea coefficient");
    return s;
}
inline Bytes encode(const State& s){
    Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(s.slot);w.u32(s.serial);w.u32(s.spawn);w.u32(s.life);
    w.f32(s.previous);w.f32(s.current);w.f32(s.angle);w.f32(s.step);return w.data;
}
class Receiver {
    State state_{};double received_=0;
public:
    void clear(){state_={};received_=0;}
    bool accept(const State& s,double now){
        if(!std::isfinite(now))return false;
        if(s.epoch==state_.epoch&&s.serial==state_.serial&&s.spawn==state_.spawn&&s.life==state_.life
            &&s.revision<=state_.revision)return false;
        state_=s;received_=now;return true;
    }
    Sample sample(std::uint64_t world,unsigned slot,unsigned serial,unsigned life,double now)const{
        if(!world||state_.epoch!=world||state_.slot!=slot||state_.serial!=serial||state_.life!=life
            ||!std::isfinite(now)||now<received_||now-received_>=.35)return {};
        const float t=static_cast<float>(std::clamp((now-received_)*20.,0.,1.));
        return {state_.previous+(state_.current-state_.previous)*t,state_.angle+state_.step*t};
    }
};
// A paired zero is intentional (accessibility); it must override the fallback.
inline Sample select(const view_distortion::Frame* paired,Sample fallback,bool paired_source=false){
    // An expired negotiated frame clears; it must not undo reduced motion.
    return paired?Sample{paired->intensity,paired->angle}:paired_source?Sample{}:fallback;
}
}
