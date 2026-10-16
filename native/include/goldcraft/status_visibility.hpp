#pragma once
#include "goldcraft/wire.hpp"

namespace goldcraft::status_visibility {
constexpr std::uint32_t invisible=1;
struct State {
    std::uint64_t epoch=0,revision=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,flags=0;
};
inline State decode(std::span<const std::uint8_t> bytes){
    Reader r(bytes);State s;
    s.epoch=r.u64();s.revision=r.u64();s.slot=r.u32();s.serial=r.u32();
    s.spawn=r.u32();s.life=r.u32();s.flags=r.u32();r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||(s.flags&~invisible))
        throw ProtocolError("Invalid status visibility state");
    return s;
}
}
