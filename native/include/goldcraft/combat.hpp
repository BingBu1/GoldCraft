#pragma once
#include "goldcraft/wire.hpp"
#include <array>
#include <cmath>

namespace goldcraft {
enum class MobDamageKind:std::uint32_t {melee=1,projectile=2,explosion=3,skill=4,status=5};
struct MobDamage {
    std::uint64_t epoch=0,event=0,source_key=0;
    std::uint32_t slot=0,serial=0,life=0;
    std::uint32_t attacker=0,attacker_serial=0,attacker_spawn=0;
    MobDamageKind kind=MobDamageKind::melee;
    float amount=0;
    Vec3 source{};
    std::uint64_t target_tick=0;
    Vec3 target{};
    std::uint32_t status_flags=0;
};
inline MobDamage read_mob_damage(std::span<const std::uint8_t> bytes){
    Reader r(bytes);MobDamage d;
    d.epoch=r.u64();d.event=r.u64();d.source_key=r.u64();d.slot=r.u32();d.serial=r.u32();d.life=r.u32();
    const auto kind=r.u32();d.kind=static_cast<MobDamageKind>(kind);d.amount=r.f32();d.source={r.f32(),r.f32(),r.f32()};
    d.attacker=r.u32();d.attacker_serial=r.u32();d.attacker_spawn=r.u32();
    if(d.kind==MobDamageKind::skill||d.kind==MobDamageKind::status){d.target_tick=r.u64();d.target={r.f32(),r.f32(),r.f32()};}
    if(d.kind==MobDamageKind::status)d.status_flags=r.u32();
    r.finish();
    if(!d.epoch||!d.event||(!d.source_key&&!d.attacker&&d.kind!=MobDamageKind::status)||!d.slot||d.slot>64||!d.serial||!d.life||kind<1||kind>5||d.amount==0||std::abs(d.amount)>5000||(d.kind!=MobDamageKind::status&&d.amount<0)
        ||d.attacker>64||(d.attacker&&(!d.attacker_serial||!d.attacker_spawn))
        ||std::abs(d.source.x)>16384||std::abs(d.source.y)>16384||std::abs(d.source.z)>16384)throw ProtocolError("Invalid authoritative mob damage");
    if((d.kind==MobDamageKind::skill||d.kind==MobDamageKind::status)&&(!d.target_tick||std::abs(d.target.x)>16384||std::abs(d.target.y)>16384||std::abs(d.target.z)>16384))
        throw ProtocolError("Invalid authoritative skill contact");
    if(d.kind==MobDamageKind::status){
        const bool world=(d.status_flags&8)!=0;
        if((d.status_flags&~31u)||(d.amount<0&&(d.status_flags&6))||((d.status_flags&1)&&(d.status_flags&16))
            ||(world&&(d.source_key||d.attacker||d.attacker_serial||d.attacker_spawn))
            ||(!world&&!d.source_key&&!d.attacker)||(d.source_key&&d.attacker)
            ||(!d.attacker&&(d.attacker_serial||d.attacker_spawn)))throw ProtocolError("Invalid status damage flags/source");
    }
    return d;
}

// Epic owns its animation/effect collision on the authenticated MC server.
// ReHLDS verifies that the chosen target is a recent native snapshot and has
// not moved out of that contact, then retains its native trace/team/damage path.
// A skill contact does not grant additional reach to ordinary melee packets.
class SkillDamageSnapshots {
    struct Actor {std::uint32_t serial=0,spawn=0;Vec3 center{};};
    struct Frame {
        std::uint64_t epoch=0,tick=0;
        float time=0;
        std::array<Actor,65> actors{};
    };
    std::array<Frame,32> frames_{};
    static float distance2(Vec3 a,Vec3 b){const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;}
public:
    void clear(){frames_={};}
    void begin(std::uint64_t epoch,std::uint64_t tick,float time){
        auto& frame=frames_[tick%frames_.size()];frame={};frame.epoch=epoch;frame.tick=tick;frame.time=time;
    }
    void actor(std::uint64_t tick,std::uint32_t slot,std::uint32_t serial,std::uint32_t spawn,Vec3 center){
        if(slot&&slot<65)frames_[tick%frames_.size()].actors[slot]={serial,spawn,center};
    }
    bool matches(const MobDamage& damage,float time,Vec3 current)const{
        if((damage.kind!=MobDamageKind::skill&&damage.kind!=MobDamageKind::status)||!damage.target_tick||!damage.slot||damage.slot>=65)return false;
        const auto& frame=frames_[damage.target_tick%frames_.size()];
        const auto& actor=frame.actors[damage.slot];
        return frame.epoch==damage.epoch&&frame.tick==damage.target_tick&&time>=frame.time&&time-frame.time<=1.0f
            &&actor.serial==damage.serial&&actor.spawn==damage.life
            &&distance2(actor.center,damage.target)<=1.0f&&distance2(current,damage.target)<=64.0f*64.0f;
    }
};
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
