#pragma once
#include "goldcraft/wire.hpp"

namespace goldcraft::status_environment {
constexpr std::uint32_t fire_resistance=1, water_breathing=2, fall_protection=4;
struct State {
    std::uint64_t epoch=0, revision=0;
    std::uint32_t slot=0, serial=0, spawn=0, life=0, flags=0;
};
inline State decode(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); State s;
    s.epoch=r.u64(); s.revision=r.u64(); s.slot=r.u32(); s.serial=r.u32();
    s.spawn=r.u32(); s.life=r.u32(); s.flags=r.u32(); r.finish();
    if(!s.epoch||!s.revision||!s.slot||s.slot>64||!s.serial||!s.spawn||!s.life||(s.flags&~7u))
        throw ProtocolError("Invalid native environment state");
    return s;
}
// Damage flags can combine causes. Suppress only when every cause is protected;
// e.g. resistance to fire must not erase the blast in BURN | BLAST.
inline bool immune(std::uint32_t flags,int bits,int fire,int drowning,int falling,int qualifiers) {
    unsigned protected_bits=0;
    if(flags&fire_resistance)protected_bits|=static_cast<unsigned>(fire);
    if(flags&water_breathing)protected_bits|=static_cast<unsigned>(drowning);
    if(flags&fall_protection)protected_bits|=static_cast<unsigned>(falling);
    const unsigned causes=static_cast<unsigned>(bits)&~static_cast<unsigned>(qualifiers);
    return causes!=0&&(causes&~protected_bits)==0;
}
}
