#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>

namespace goldcraft::status_melee {
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    float add=0,base_multiplier=0,total_multiplier=1;
};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();s.spawn=r.u32();s.life=r.u32();
    s.add=r.f32();s.base_multiplier=r.f32();s.total_multiplier=r.f32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life)
        throw ProtocolError("Invalid melee attribute identity");
    for(float v:{s.add,s.base_multiplier,s.total_multiplier})
        if(!std::isfinite(v)||std::abs(v)>1000000)throw ProtocolError("Invalid melee attribute coefficient");
    return s;
}
inline float apply(const State& s,float native_damage){
    // The weapon owns its base and later head/body/backstab/armor/plugin rules.
    // Do not revive a zero/vetoed hit or clamp an unchanged native weapon.
    if(!std::isfinite(native_damage)||native_damage<=0||(s.add==0&&s.base_multiplier==0&&s.total_multiplier==1))return native_damage;
    const double base=native_damage/5.0+s.add;
    const double result=(base+base*s.base_multiplier)*s.total_multiplier;
    return static_cast<float>(std::clamp(result,0.0,2048.0)*5.0);
}
}
