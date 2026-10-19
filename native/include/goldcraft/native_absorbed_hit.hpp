#pragma once
#include "goldcraft/combat.hpp"

namespace goldcraft {
// The authenticated MC server has paid this entire hit from absorption. It
// still needs native contact and damage-hook approval before receiving push.
struct AbsorbedHit {
    MobDamage damage;
    std::uint32_t life=0;
    std::uint64_t tick=0;
    Vec3 center{};
};
inline AbsorbedHit read_absorbed_hit(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=92&&bytes.size()!=112)throw ProtocolError("Invalid absorbed hit length");
    AbsorbedHit hit;hit.damage=read_mob_damage(bytes.first(bytes.size()-24));
    if(hit.damage.kind!=MobDamageKind::melee&&hit.damage.kind!=MobDamageKind::projectile&&hit.damage.kind!=MobDamageKind::skill)
        throw ProtocolError("Invalid absorbed hit kind");
    Reader r(bytes.last(24));hit.life=r.u32();hit.tick=r.u64();
    hit.center={r.f32(),r.f32(),r.f32()};r.finish();
    if(!hit.life||!hit.tick||std::abs(hit.center.x)>16384||std::abs(hit.center.y)>16384||std::abs(hit.center.z)>16384)
        throw ProtocolError("Invalid absorbed hit contact");
    return hit;
}
}
