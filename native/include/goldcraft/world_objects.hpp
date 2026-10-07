#pragma once
#include "goldcraft/wire.hpp"
#include <set>

namespace goldcraft {
// Server-authoritative bounds, in GoldSrc units. Fixed-width wire fields, never edict pointers.
constexpr std::uint32_t object_block=1,object_mob=2,object_usable=4;
constexpr std::size_t max_world_objects=512;
struct WorldObject {
    std::uint64_t key=0;
    std::uint32_t flags=0;
    float health=0,min[3]{},max[3]{};
};
struct WorldObjects {
    std::uint64_t epoch=0,revision=0;
    std::vector<WorldObject> objects;
};
inline WorldObjects decode_world_objects(std::span<const std::uint8_t> payload) {
    Reader r(payload);WorldObjects result;result.epoch=r.u64();result.revision=r.u64();auto count=r.u32();
    if(!result.epoch||!result.revision||count>max_world_objects||r.remaining()!=count*40u)throw ProtocolError("World object snapshot bounds");
    std::set<std::uint64_t> keys;result.objects.reserve(count);
    for(std::uint32_t n=0;n<count;++n){
        WorldObject o;o.key=r.u64();o.flags=r.u32();o.health=r.f32();
        for(auto& v:o.min)v=r.f32();for(auto& v:o.max)v=r.f32();
        if(!o.key||!keys.insert(o.key).second||(o.flags&~7u)||((o.flags&3u)!=1&&(o.flags&3u)!=2)||o.health<0||o.health>1000000)throw ProtocolError("World object identity");
        for(int i=0;i<3;++i)if(o.min[i]<-16384||o.max[i]>16384||o.min[i]>=o.max[i])throw ProtocolError("World object bounds");
        result.objects.push_back(o);
    }
    r.finish();return result;
}
}
