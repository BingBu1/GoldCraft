#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>

namespace goldcraft::status_health {
constexpr float maximum = 10000000;
inline bool valid_limit(float value) { return std::isfinite(value) && value >= 1 && value <= maximum; }
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    float base=0,effective=0;
};
inline State decode(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.base=r.f32();s.effective=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life
        ||!valid_limit(s.base)||!valid_limit(s.effective))throw ProtocolError("Invalid native health limit");
    return s;
}
// Removing a modifier caps its own extra hearts, but retains native rewards
// already above the previous limit. This is a cap change, never an injury.
inline float reduce(float health,float previous,float next) {
    if(!std::isfinite(health)||health<=0||next>=previous)return health;
    return std::min(health,next+std::max(0.0f,health-previous));
}
}
