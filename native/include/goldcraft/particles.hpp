#pragma once
#include "wire.hpp"
#include <cmath>

namespace goldcraft {
constexpr unsigned max_particle_count=4096, max_particle_vertices=65536, max_particle_batches=128, max_particle_textures=64;
constexpr std::uint32_t particle_untextured=0xffffffffu;
constexpr unsigned particle_depth_write=1, particle_blend=2, particle_depth_test=4,
    particle_cull=8, particle_cutoff_before_mod=16, particle_cutoff_texture_only=32,
    particle_discard_zero_alpha=64, particle_repeat_linear=128, particle_view_offset=256;
struct ParticleMaterial {
    std::uint32_t texture=0,flags=particle_blend|particle_depth_test;
    std::uint32_t src_rgb=0x0302,dst_rgb=0x0303,src_alpha=1,dst_alpha=0x0303;
    std::uint32_t equation_rgb=0x8006,equation_alpha=0x8006,depth_func=0x0203,color_mask=15;
    float alpha_cutoff=0.1f;
    std::array<float,4> color_mod{1,1,1,1};
    bool operator==(const ParticleMaterial&) const = default;
};
inline bool valid_particle_material(const ParticleMaterial& m) {
    const auto blend=[](std::uint32_t value,bool source_rgb){
        return value<=1||(value>=0x0300&&value<=0x0307)||(source_rgb&&value==0x0308);
    };
    const auto equation=[](std::uint32_t value){return value==0x8006||value==0x800a||value==0x800b;};
    if((m.texture>max_particle_textures&&m.texture!=particle_untextured)||(m.flags&~511u)||
       (m.flags&(particle_cutoff_before_mod|particle_cutoff_texture_only))==(particle_cutoff_before_mod|particle_cutoff_texture_only)||
       !blend(m.src_rgb,true)||!blend(m.dst_rgb,false)||!blend(m.src_alpha,false)||!blend(m.dst_alpha,false)||
       !equation(m.equation_rgb)||!equation(m.equation_alpha)||m.depth_func<0x0200||m.depth_func>0x0207||m.color_mask>15||
       !std::isfinite(m.alpha_cutoff)||m.alpha_cutoff<0||m.alpha_cutoff>1)return false;
    for(float value:m.color_mod)if(!std::isfinite(value)||value<0||value>4)return false;
    return true;
}
struct ParticleVertex {Vec3 position;float u,v;std::uint32_t color;};
struct ParticleBatch {ParticleMaterial material;std::vector<ParticleVertex> vertices;};
struct ParticleSnapshot {
    std::uint64_t epoch,generation,revision;
    unsigned life,count;
    std::vector<ParticleBatch> batches;
};
inline bool current_particles(const ParticleSnapshot& snapshot,std::uint64_t epoch,std::uint64_t generation,std::uint64_t revision,unsigned life){
    return snapshot.epoch==epoch&&snapshot.life==life&&snapshot.generation>=generation
        &&(snapshot.generation!=generation||snapshot.revision>revision);
}
inline ParticleSnapshot read_particles(Reader& r,bool coalesce=true) {
    ParticleSnapshot result{r.u64(),r.u64(),r.u64(),r.u32(),r.u32(),{}};
    const auto groups=r.u32();
    if(!result.epoch||!result.generation||!result.revision||!result.life||result.count>max_particle_count||groups>max_particle_batches)
        throw ProtocolError("Invalid particle snapshot identity/count");
    if((result.count==0)!=(groups==0))throw ProtocolError("Invalid empty particle snapshot");
    unsigned total=0;result.batches.reserve(groups);
    for(unsigned i=0;i<groups;++i){
        // Protocol23: 64-byte material/count header followed by 24-byte vertices.
        ParticleMaterial material;
        material.texture=r.u32();material.flags=r.u32();
        material.src_rgb=r.u32();material.dst_rgb=r.u32();material.src_alpha=r.u32();material.dst_alpha=r.u32();
        material.equation_rgb=r.u32();material.equation_alpha=r.u32();material.depth_func=r.u32();material.color_mask=r.u32();
        material.alpha_cutoff=r.f32();for(auto& value:material.color_mod)value=r.f32();
        const auto count=r.u32();
        if(!valid_particle_material(material)||!count||count%4||count>max_particle_vertices-total||std::uint64_t(count)*24>r.remaining())
            throw ProtocolError("Particle mesh exceeds bounds");
        // Only adjacent, identical materials coalesce. In particular depth-only
        // followed by EQUAL is an ordered operation, never a sorting key.
        // Post plans address exact wire batch ranges, so their decoder disables
        // coalescing. Ordinary effect snapshots retain adjacent batching.
        if(!coalesce||result.batches.empty()||result.batches.back().material!=material)result.batches.push_back({material,{}});
        auto& batch=result.batches.back();
        total+=count;batch.vertices.reserve(batch.vertices.size()+count);
        for(unsigned j=0;j<count;++j){
            ParticleVertex v{{r.f32(),r.f32(),r.f32()},r.f32(),r.f32(),r.u32()};
            // Reader::f32 rejects nonfinite values. Finite UVs are interpolants,
            // not texture indices; young Epic trails can legitimately exceed16.
            if(std::abs(v.position.x)>32000000||std::abs(v.position.y)>32000000||std::abs(v.position.z)>32000000)
                throw ProtocolError("Invalid particle coordinate");
            batch.vertices.push_back(v);
        }
    }
    r.finish();return result;
}
}
