#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>

namespace goldcraft {
enum class ArmorDamageKind : std::uint32_t {
    melee=1,projectile=2,explosion=3,fire=4,fall=5,drowning=6,magic=7
};
struct ArmorRequest {
    std::uint64_t epoch=0,event=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0;
    std::uint32_t attacker=0,attacker_serial=0,attacker_spawn=0;
    float damage=0;
    ArmorDamageKind kind=ArmorDamageKind::melee;
    std::array<float,3> position{}; // Native inflictor position, in GoldSrc units.
};
inline ArmorRequest read_armor_request(std::span<const std::uint8_t> bytes){
    Reader r(bytes);ArmorRequest q;
    q.epoch=r.u64();q.event=r.u64();q.slot=r.u32();q.serial=r.u32();q.spawn=r.u32();q.life=r.u32();
    q.attacker=r.u32();q.attacker_serial=r.u32();q.attacker_spawn=r.u32();
    q.damage=r.f32();q.kind=static_cast<ArmorDamageKind>(r.u32());
    for(auto& v:q.position)v=r.f32();r.finish();
    const auto kind=static_cast<std::uint32_t>(q.kind);
    if(!q.epoch||!q.event||!q.slot||q.slot>64||!q.serial||!q.spawn||!q.life||q.attacker>64
        ||(q.attacker?(!q.attacker_serial||!q.attacker_spawn):(q.attacker_serial||q.attacker_spawn))
        ||!std::isfinite(q.damage)||q.damage<=0||q.damage>500000||kind<1||kind>7)
        throw ProtocolError("Invalid native armor request");
    for(auto v:q.position)if(!std::isfinite(v)||std::abs(v)>1048576)
        throw ProtocolError("Invalid native armor position");
    return q;
}
inline Bytes write_armor_request(const ArmorRequest& q){
    Writer w;w.u64(q.epoch);w.u64(q.event);w.u32(q.slot);w.u32(q.serial);w.u32(q.spawn);w.u32(q.life);
    w.u32(q.attacker);w.u32(q.attacker_serial);w.u32(q.attacker_spawn);w.f32(q.damage);
    w.u32(static_cast<std::uint32_t>(q.kind));for(auto v:q.position)w.f32(v);
    read_armor_request(w.data);return w.data;
}
struct ArmorResult {
    std::uint64_t epoch=0,event=0;
    std::uint32_t slot=0,serial=0,spawn=0,life=0,status=0; // 0=calculated, 1=unavailable.
    float after_armor=0,enchantment_multiplier=1;
};
inline ArmorResult read_armor_result(std::span<const std::uint8_t> bytes){
    Reader r(bytes);ArmorResult a;
    a.epoch=r.u64();a.event=r.u64();a.slot=r.u32();a.serial=r.u32();a.spawn=r.u32();a.life=r.u32();
    a.status=r.u32();a.after_armor=r.f32();a.enchantment_multiplier=r.f32();r.finish();
    if(!a.epoch||!a.event||!a.slot||a.slot>64||!a.serial||!a.spawn||!a.life||a.status>1
        ||!std::isfinite(a.after_armor)||a.after_armor<0||a.after_armor>500000
        ||!std::isfinite(a.enchantment_multiplier)||a.enchantment_multiplier<0||a.enchantment_multiplier>1)
        throw ProtocolError("Invalid native armor result");
    return a;
}
}
