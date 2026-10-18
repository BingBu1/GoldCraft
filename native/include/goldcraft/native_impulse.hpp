#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>

namespace goldcraft::native_impulse {
struct Request {
    std::uint64_t epoch=0,event=0,tick=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    Vec3 center{},delta{};
};
inline Request decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);Request q;
    q.epoch=r.u64();q.event=r.u64();q.tick=r.u64();
    q.slot=r.u32();q.serial=r.u32();q.spawn=r.u32();q.life=r.u32();
    q.center={r.f32(),r.f32(),r.f32()};q.delta={r.f32(),r.f32(),r.f32()};r.finish();
    if(!q.epoch||!q.event||!q.tick||!q.slot||q.slot>64||!q.serial||!q.spawn||!q.life)
        throw ProtocolError("Invalid native impulse identity");
    for(float x:{q.center.x,q.center.y,q.center.z,q.delta.x,q.delta.y,q.delta.z})
        if(!std::isfinite(x)||std::abs(x)>65536)throw ProtocolError("Invalid native impulse vector");
    return q;
}
}
