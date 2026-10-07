#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>

namespace goldcraft {
enum class MobDamageKind:std::uint32_t {melee=1,projectile=2,explosion=3};
struct MobDamage {
    std::uint64_t epoch=0,event=0,source_key=0;
    std::uint32_t slot=0,serial=0,life=0;
    std::uint32_t attacker=0,attacker_serial=0,attacker_spawn=0;
    MobDamageKind kind=MobDamageKind::melee;
    float amount=0;
    Vec3 source{};
};
inline MobDamage read_mob_damage(std::span<const std::uint8_t> bytes){
    Reader r(bytes);MobDamage d;
    d.epoch=r.u64();d.event=r.u64();d.source_key=r.u64();d.slot=r.u32();d.serial=r.u32();d.life=r.u32();
    const auto kind=r.u32();d.kind=static_cast<MobDamageKind>(kind);d.amount=r.f32();d.source={r.f32(),r.f32(),r.f32()};
    d.attacker=r.u32();d.attacker_serial=r.u32();d.attacker_spawn=r.u32();r.finish();
    if(!d.epoch||!d.event||(!d.source_key&&!d.attacker)||!d.slot||d.slot>64||!d.serial||!d.life||kind<1||kind>3||d.amount<=0||d.amount>5000
        ||d.attacker>64||(d.attacker&&(!d.attacker_serial||!d.attacker_spawn))
        ||std::abs(d.source.x)>16384||std::abs(d.source.y)>16384||std::abs(d.source.z)>16384)throw ProtocolError("Invalid authoritative mob damage");
    return d;
}
inline bool current_mob_damage(const MobDamage& d,std::uint64_t epoch,std::uint64_t last,std::uint32_t serial,std::uint32_t life){
    // The final identity field is the real spawn, independent of form/pose life.
    return d.epoch==epoch&&d.event>last&&d.serial==serial&&d.life==life;
}

// A net vanilla health change from one authoritative MC server tick. This is
// never a client position/health claim. Real spawn, unlike pose life, survives
// form changes and native teleports so final queued damage is not discarded.
struct VitalsDelta {
    std::uint64_t epoch=0,event=0;
    std::uint32_t slot=0,serial=0,spawn=0;
    Key uuid{};
    float delta=0;
    std::uint32_t attacker=0,attacker_serial=0,attacker_spawn=0,kind=0;
    std::uint64_t source_key=0;
};
inline VitalsDelta read_vitals_delta(std::span<const std::uint8_t> bytes){
    Reader r(bytes);VitalsDelta d;
    d.epoch=r.u64();d.event=r.u64();d.slot=r.u32();d.serial=r.u32();d.spawn=r.u32();d.uuid=r.key();d.delta=r.f32();
    d.attacker=r.u32();d.attacker_serial=r.u32();d.attacker_spawn=r.u32();d.kind=r.u32();d.source_key=r.u64();r.finish();
    if(!d.epoch||!d.event||!d.slot||d.slot>64||!d.serial||!d.spawn||d.delta==0||std::abs(d.delta)>5000||d.attacker>64||d.kind>3
        ||(d.attacker&&(!d.attacker_serial||!d.attacker_spawn)))throw ProtocolError("Invalid authoritative vitals delta");
    return d;
}
inline bool current_vitals_delta(const VitalsDelta& d,std::uint64_t epoch,std::uint64_t acknowledged,
                                std::uint32_t serial,std::uint32_t spawn,const Key& uuid){
    return d.epoch==epoch&&d.event>acknowledged&&d.serial==serial&&d.spawn==spawn&&d.uuid==uuid;
}
}
