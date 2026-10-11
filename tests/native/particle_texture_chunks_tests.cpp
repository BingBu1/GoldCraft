#include "goldcraft/particle_texture_chunks.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace goldcraft;
namespace {
Bytes encode(const ParticleTextureChunk& c){
    Writer w;w.u64(c.epoch);w.u64(c.generation);w.u32(c.life);w.u32(c.key);w.u64(c.revision);
    w.u32(c.width);w.u32(c.height);w.u32(c.offset);w.u32(c.total_bytes);w.bytes(c.data);return w.data;
}
ParticleTextureChunk decode(const Bytes& bytes){Reader r(bytes);return read_particle_texture_chunk(r);}
void reject_wire(const Bytes& bytes){bool failed=false;try{decode(bytes);}catch(const ProtocolError&){failed=true;}assert(failed);}
void reject_append(ParticleTextureAssembler& a,const ParticleTextureChunk& c){
    bool failed=false;try{a.append(c);}catch(const ProtocolError&){failed=true;}assert(failed&&a.pending_bytes()==0&&a.pixels().empty());
}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(8*i));}
}
int main(){
    Bytes small(24);for(unsigned i=0;i<24;++i)small[i]=static_cast<std::uint8_t>(i);
    ParticleTextureChunk c{0x0102030405060708ull,9,10,11,12,3,2,0,24,small};
    const auto golden=encode(c);assert(golden.size()==72);
    const std::string expected="080706050403020109000000000000000a0000000b0000000c0000000000000003000000020000000000000018000000";
    const char* hex="0123456789abcdef";std::string actual;
    for(unsigned i=0;i<48;++i){actual+=hex[golden[i]>>4];actual+=hex[golden[i]&15];}assert(actual==expected);
    ParticleTextureAssembler a;assert(a.append(decode(golden)));assert(std::ranges::equal(a.pixels(),small));a.reset();
    for(unsigned size=0;size<=48;++size)reject_wire(Bytes(golden.begin(),golden.begin()+size));
    auto bad=golden;bad.push_back(0);reject_wire(bad);
    for(unsigned at:{0u,8u,16u,20u,24u,32u,36u}){bad=golden;for(unsigned i=0;i<(at==0||at==8||at==24?8u:4u);++i)bad[at+i]=0;reject_wire(bad);}
    bad=golden;put(bad,20,65);reject_wire(bad);bad=golden;put(bad,32,4097);reject_wire(bad);
    for(unsigned offset:{1u,4u,24u,0xffffffffu}){bad=golden;put(bad,40,offset);reject_wire(bad);}
    bad=golden;put(bad,44,25);reject_wire(bad);

    // No published pixels until every row has arrived, even across interrupted transfers.
    c.data=std::span(small).first(12);assert(!a.append(c));assert(a.pending_bytes()==24&&a.pixels().empty());
    auto next=c;next.offset=12;next.data=std::span(small).subspan(12);assert(a.append(next));assert(std::ranges::equal(a.pixels(),small));a.reset();
    assert(!a.append(c));reject_append(a,c); // Duplicate start is not a new transfer.
    assert(!a.append(c));next=c;next.offset=12;next.life++;reject_append(a,next);
    assert(!a.append(c));next=c;next.revision++;assert(!a.append(next));
    auto stale=c;stale.offset=12;reject_append(a,stale); // Canceled upload cannot contribute old rows.
    assert(!a.append(c));a.reset();next=c;next.life++;assert(!a.append(next));a.reset();
    next=c;next.offset=12;reject_append(a,next);

    // The exact live4096 atlas needs256 chunks, at most64MiB staging, and publishes atomically.
    Bytes large(max_particle_texture_bytes);
    for(std::size_t i=0;i<large.size();++i)large[i]=static_cast<std::uint8_t>(i*31+(i>>16));
    c={7,8,9,10,11,4096,4096,0,static_cast<unsigned>(large.size()),{}};
    unsigned chunks=0;
    for(std::size_t at=0;at<large.size();at+=max_particle_texture_chunk_bytes){
        c.offset=static_cast<unsigned>(at);c.data=std::span(large).subspan(at,max_particle_texture_chunk_bytes);
        const auto packet=encode(c);const bool complete=a.append(decode(packet));++chunks;
        assert(complete==(at+max_particle_texture_chunk_bytes==large.size()));
        assert(a.pending_bytes()==max_particle_texture_bytes);if(!complete)assert(a.pixels().empty());
    }
    assert(chunks==256&&std::ranges::equal(a.pixels(),large));a.reset();assert(a.pending_bytes()==0);
    std::puts("{\"particleTextureChunks\":true,\"atlas4096\":true,\"chunks\":256,\"atomicPublication\":true,\"staleAndReorderedRejected\":true,\"maxStagingBytes\":67108864}");
}
