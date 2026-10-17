#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>

namespace goldcraft::status_fall {
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    float safe_distance=3,multiplier=1;
};
inline State decode(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.safe_distance=r.f32();s.multiplier=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life
        ||!std::isfinite(s.safe_distance)||s.safe_distance< -1024||s.safe_distance>1024
        ||!std::isfinite(s.multiplier)||s.multiplier<0||s.multiplier>100)
        throw ProtocolError("Invalid native fall state");
    return s;
}
// Retain the native velocity-based fall curve. Only the difference from MC's
// default three-block allowance changes its equivalent ballistic distance.
inline float velocity(float native_velocity,float gravity,const State& state) {
    if(state.safe_distance==3||native_velocity<=0||!std::isfinite(native_velocity)
        ||!std::isfinite(gravity)||gravity<=0)return native_velocity;
    const double squared=double(native_velocity)*native_velocity
        -2.0*gravity*(state.safe_distance-3)*units_per_block;
    return static_cast<float>(std::sqrt(std::max(0.0,squared)));
}
}
