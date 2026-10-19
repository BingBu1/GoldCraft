#pragma once
#include "goldcraft/native_impulse.hpp"
#include <array>

namespace goldcraft::native_knockback {
constexpr std::size_t max_steps=32;
// Units/second in GoldSrc axes. Vanilla steps halve horizontal velocity;
// value.z is the resisted strength used by its grounded vertical clamp.
struct Step { std::uint32_t kind=0; Vec3 value{}; };
struct Steps {
    native_impulse::Request impulse;
    std::uint64_t damage_event=0;
    std::uint32_t count=0;
    std::array<Step,max_steps> steps{};
};
inline Steps decode_steps(std::span<const std::uint8_t> bytes){
    if(bytes.size()<92||bytes.size()>76+16*max_steps)throw ProtocolError("Invalid knockback steps length");
    Steps q;q.impulse=native_impulse::decode(bytes.first(64));
    if(q.impulse.delta.x!=0||q.impulse.delta.y!=0||q.impulse.delta.z!=0)
        throw ProtocolError("Unexpected knockback steps delta");
    Reader r(bytes.subspan(64));q.damage_event=r.u64();q.count=r.u32();
    if(!q.damage_event||!q.count||q.count>max_steps||bytes.size()!=76+16*q.count)
        throw ProtocolError("Invalid knockback steps count");
    for(std::uint32_t i=0;i<q.count;++i){
        auto& step=q.steps[i];step.kind=r.u32();step.value={r.f32(),r.f32(),r.f32()};
        if(step.kind>1||(step.kind==1&&step.value.z<=0))throw ProtocolError("Invalid knockback step kind/strength");
        for(float x:{step.value.x,step.value.y,step.value.z})
            if(!std::isfinite(x)||std::abs(x)>65536)throw ProtocolError("Invalid knockback step vector");
    }
    r.finish();return q;
}
}
