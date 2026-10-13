#pragma once
#include "wire.hpp"
#include <algorithm>
#include <memory>

namespace goldcraft {
constexpr unsigned max_particle_texture_dimension=4096;
constexpr std::size_t max_particle_texture_chunk_bytes=256u*1024u;
constexpr std::size_t max_particle_texture_bytes=4096u*4096u*4u;
constexpr std::size_t max_particle_texture_resident_bytes=128u*1024u*1024u;
struct ParticleTextureChunk {
    std::uint64_t epoch=0,generation=0;
    std::uint32_t life=0,key=0;
    std::uint64_t revision=0;
    std::uint32_t width=0,height=0,offset=0,total_bytes=0;
    std::span<const std::uint8_t> data;
};
inline ParticleTextureChunk read_particle_texture_chunk(Reader& r) {
    ParticleTextureChunk c;
    c.epoch=r.u64();c.generation=r.u64();c.life=r.u32();c.key=r.u32();c.revision=r.u64();
    c.width=r.u32();c.height=r.u32();c.offset=r.u32();c.total_bytes=r.u32();
    if(!c.epoch||!c.generation||!c.life||!c.key||c.key>64||!c.revision||!c.width||!c.height||
       c.width>max_particle_texture_dimension||c.height>max_particle_texture_dimension||
       std::uint64_t(c.width)*c.height*4!=c.total_bytes||c.offset>=c.total_bytes||c.offset%(c.width*4)||
       !r.remaining()||r.remaining()>max_particle_texture_chunk_bytes||r.remaining()%(c.width*4)||
       r.remaining()>c.total_bytes-c.offset)
        throw ProtocolError("Invalid particle texture chunk");
    c.data=r.bytes(r.remaining());r.finish();return c;
}

/** A single at-most64MiB CPU staging image; published textures have a separate128MiB budget. */
class ParticleTextureAssembler {
    ParticleTextureChunk header_{};
    std::unique_ptr<std::uint8_t[]> pixels_;
    std::size_t received_=0,retained_=0;
    std::uint64_t allocations_=0,reuses_=0;
    [[noreturn]] void reject(const char* message){reset();throw ProtocolError(message);}
public:
    // A successful publication may reuse this storage on the next transfer.
    // Session changes and rejected transfers must use reset() to release it.
    void finish() noexcept {header_={};received_=0;}
    void reset() noexcept {finish();pixels_.reset();retained_=0;}
    const ParticleTextureChunk& header() const noexcept{return header_;}
    std::span<const std::uint8_t> pixels() const noexcept{
        return header_.total_bytes&&received_==header_.total_bytes?
            std::span<const std::uint8_t>(pixels_.get(),header_.total_bytes):std::span<const std::uint8_t>{};
    }
    std::size_t pending_bytes() const noexcept{return header_.total_bytes;}
    std::size_t retained_bytes() const noexcept{return retained_;}
    std::size_t received_bytes() const noexcept{return received_;}
    // Cumulative diagnostics survive reset so callers can measure intervals.
    std::uint64_t allocation_count() const noexcept{return allocations_;}
    std::uint64_t reuse_count() const noexcept{return reuses_;}
    bool append(const ParticleTextureChunk& c) {
        // Validate direct callers as well as the wire decoder before allocating.
        if(!c.epoch||!c.generation||!c.life||!c.key||c.key>64||!c.revision||!c.width||!c.height||
           c.width>max_particle_texture_dimension||c.height>max_particle_texture_dimension||
           std::uint64_t(c.width)*c.height*4!=c.total_bytes||c.offset>=c.total_bytes||c.offset%(c.width*4)||
           c.data.empty()||c.data.size()>max_particle_texture_chunk_bytes||c.data.size()%(c.width*4)||
           c.data.size()>c.total_bytes-c.offset)reject("Invalid particle texture staging chunk");
        // A capture canceled on the producer can start a newer transfer in the
        // same life. Never expose the abandoned partial image or accept its tail.
        if(c.offset==0&&header_.total_bytes&&c.epoch==header_.epoch&&c.generation==header_.generation&&
           c.life==header_.life&&c.revision>header_.revision)finish();
        if(!header_.total_bytes) {
            if(c.offset)reject("Particle texture starts after byte zero");
            if(c.total_bytes>retained_){
                // Release before growth: even transient staging stays within
                // the single-image 64 MiB bound. Every byte is filled by chunks.
                pixels_.reset();retained_=0;
                try{pixels_=std::make_unique_for_overwrite<std::uint8_t[]>(c.total_bytes);}
                catch(...){reset();throw;}
                retained_=c.total_bytes;++allocations_;
            }else ++reuses_;
            header_=c;header_.data={};
        }
        if(c.epoch!=header_.epoch||c.generation!=header_.generation||c.life!=header_.life||c.key!=header_.key||
           c.revision!=header_.revision||c.width!=header_.width||c.height!=header_.height||c.total_bytes!=header_.total_bytes||
           c.offset!=received_)reject("Interleaved or reordered particle texture chunks");
        std::copy(c.data.begin(),c.data.end(),pixels_.get()+received_);received_+=c.data.size();
        return received_==header_.total_bytes;
    }
};
}
