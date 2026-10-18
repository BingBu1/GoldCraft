#pragma once
#include "avatar.hpp"
#include "particles.hpp"

namespace goldcraft {
constexpr unsigned entity_outline=8u,entity_outline_cull=16u;
inline bool valid_entity_batch_flags(unsigned flags,bool outlines) noexcept {
    return flags&entity_outline ? outlines&&!(flags&~(entity_outline|entity_outline_cull)) : !(flags&~7u);
}
struct EntityFrameVertex {Vec3 position;float u=0,v=0;std::uint32_t color=0,normal=0;};
struct EntityFrameBatch {unsigned texture=0,flags=0;std::vector<EntityFrameVertex> vertices;};
struct EntityBaseSnapshot {
    std::uint64_t epoch=0,revision=0;
    unsigned count=0,blocks=0;
    std::vector<EntityFrameBatch> batches;
    std::vector<PlayerIdentity> avatars;
};
struct EntityFrame {EntityBaseSnapshot base;ParticleSnapshot effects;};

inline EntityBaseSnapshot read_entity_base(Reader& r,bool outlines=false) {
    EntityBaseSnapshot base;
    base.epoch=r.u64();base.revision=r.u64();base.count=r.u32();base.blocks=r.u32();const auto groups=r.u32();
    if(!base.epoch||!base.revision||base.count>128||base.blocks>128||groups>128)
        throw ProtocolError("Invalid entity frame counts/identity");
    base.batches.reserve(groups);unsigned total=0;
    for(unsigned i=0;i<groups;++i) {
        EntityFrameBatch batch;batch.texture=r.u32();batch.flags=r.u32();const auto count=r.u32();
        if(batch.texture>256||!valid_entity_batch_flags(batch.flags,outlines)||count%4||count>299592-total||std::uint64_t(count)*28>r.remaining())
            throw ProtocolError("Invalid entity frame vertices");
        total+=count;batch.vertices.reserve(count);
        for(unsigned j=0;j<count;++j) {
            EntityFrameVertex vertex{{r.f32(),r.f32(),r.f32()},r.f32(),r.f32(),r.u32(),r.u32()};
            if(std::abs(vertex.position.x)>32000000||std::abs(vertex.position.y)>32000000||std::abs(vertex.position.z)>32000000)
                throw ProtocolError("Invalid entity frame coordinate");
            batch.vertices.push_back(vertex);
        }
        base.batches.push_back(std::move(batch));
    }
    base.avatars=read_rendered_players(r);
    if(base.avatars.size()>base.count)throw ProtocolError("Avatar count exceeds entity frame");
    return base;
}
inline EntityFrame read_entity_frame(Reader& r,bool outlines=false) {
    if(r.remaining()>max_payload)throw ProtocolError("Entity frame byte budget");
    const auto base_size=r.u32();
    if(base_size<32||base_size>r.remaining())throw ProtocolError("Invalid entity base length");
    Reader base_reader(r.bytes(base_size));auto base=read_entity_base(base_reader,outlines);
    const auto effect_size=r.u32();
    if(effect_size!=r.remaining())throw ProtocolError("Invalid entity effect length");
    Reader effect_reader(r.bytes(effect_size));auto effects=read_particles(effect_reader,false);r.finish();
    if(base.epoch!=effects.epoch||base.revision!=effects.revision)throw ProtocolError("Entity base/effect identity mismatch");
    return {std::move(base),std::move(effects)};
}
inline bool current_entity_frame(const EntityFrame& frame,std::uint64_t epoch,std::uint64_t texture_generation,
                                 std::uint64_t last_revision,unsigned life) noexcept {
    return frame.base.epoch==epoch&&frame.effects.life==life&&frame.effects.generation>=texture_generation&&
        frame.base.revision>last_revision;
}
template<class BaseTextureExists,class EffectTextureExists>
void validate_entity_frame_textures(const EntityFrame& frame,std::uint64_t texture_generation,
                                   BaseTextureExists base_exists,EffectTextureExists effect_exists) {
    for(const auto& batch:frame.base.batches)if(batch.texture&&!base_exists(batch.texture))
        throw ProtocolError("Missing entity frame base texture");
    for(const auto& batch:frame.effects.batches){
        const auto key=batch.material.texture;
        if(key!=0&&key!=particle_untextured&&(frame.effects.generation!=texture_generation||!effect_exists(key)))
            throw ProtocolError("Missing current-generation entity effect texture");
    }
}
inline bool fresh_entity_frame(std::uint64_t revision,unsigned life,std::uint64_t generation,std::uint64_t texture_generation,
                               unsigned minecraft_life,unsigned server_life,double age) noexcept {
    return revision&&life==minecraft_life&&life==server_life&&generation==texture_generation&&age>=0&&age<0.5;
}
}
