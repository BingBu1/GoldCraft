#pragma once
#include "wire.hpp"
#include <cmath>

namespace goldcraft {
constexpr unsigned max_particle_count=4096, max_particle_vertices=65536, max_particle_batches=128, max_particle_textures=64;
struct ParticleVertex {Vec3 position;float u,v;std::uint32_t color;};
struct ParticleBatch {unsigned texture,flags;std::vector<ParticleVertex> vertices;};
struct ParticleSnapshot {
    std::uint64_t epoch,generation,revision;
    unsigned life,count;
    std::vector<ParticleBatch> batches;
};
inline bool current_particles(const ParticleSnapshot& snapshot,std::uint64_t epoch,std::uint64_t generation,std::uint64_t revision,unsigned life){
    return snapshot.epoch==epoch&&snapshot.life==life&&snapshot.generation>=generation
        &&(snapshot.generation!=generation||snapshot.revision>revision);
}
inline ParticleSnapshot read_particles(Reader& r) {
    ParticleSnapshot result{r.u64(),r.u64(),r.u64(),r.u32(),r.u32(),{}};
    const auto groups=r.u32();
    if(!result.epoch||!result.generation||!result.revision||!result.life||result.count>max_particle_count||groups>max_particle_batches)
        throw ProtocolError("Invalid particle snapshot identity/count");
    if((result.count==0)!=(groups==0))throw ProtocolError("Invalid empty particle snapshot");
    unsigned total=0;result.batches.reserve(groups);
    for(unsigned i=0;i<groups;++i){
        ParticleBatch batch{r.u32(),r.u32(),{}};const auto count=r.u32();
        // Texture zero reuses the already generation-checked block atlas. Bit 0 is depth-write.
        if(batch.texture>max_particle_textures||(batch.flags&~1u)||!count||count%4||count>max_particle_vertices-total||std::uint64_t(count)*24>r.remaining())
            throw ProtocolError("Particle mesh exceeds bounds");
        total+=count;batch.vertices.reserve(count);
        for(unsigned j=0;j<count;++j){
            ParticleVertex v{{r.f32(),r.f32(),r.f32()},r.f32(),r.f32(),r.u32()};
            if(std::abs(v.position.x)>32000000||std::abs(v.position.y)>32000000||std::abs(v.position.z)>32000000||std::abs(v.u)>16||std::abs(v.v)>16)
                throw ProtocolError("Invalid particle coordinate");
            batch.vertices.push_back(v);
        }
        result.batches.push_back(std::move(batch));
    }
    r.finish();return result;
}
}
