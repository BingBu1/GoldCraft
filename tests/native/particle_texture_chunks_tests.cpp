#include "goldcraft/particle_texture_chunks.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include "allocation_faults.hpp"

using namespace goldcraft;
namespace {
Bytes encode(const ParticleTextureChunk& c){
    Writer w;w.u64(c.epoch);w.u64(c.generation);w.u32(c.life);w.u32(c.key);w.u64(c.revision);
    w.u32(c.width);w.u32(c.height);w.u32(c.offset);w.u32(c.total_bytes);w.bytes(c.data);return w.data;
}
ParticleTextureChunk decode(const Bytes& bytes){Reader r(bytes);return read_particle_texture_chunk(r);}
void reject_wire(const Bytes& bytes){bool failed=false;try{decode(bytes);}catch(const ProtocolError&){failed=true;}assert(failed);}
void idle(const ParticleTextureAssembler& a,std::size_t retained=0){
    assert(a.pending_bytes()==0&&a.received_bytes()==0&&a.pixels().empty()&&a.retained_bytes()==retained);
    const auto& h=a.header();
    assert(!h.epoch&&!h.generation&&!h.life&&!h.key&&!h.revision&&!h.width&&!h.height&&!h.offset&&!h.total_bytes&&h.data.empty());
}
void reject_append(ParticleTextureAssembler& a,const ParticleTextureChunk& c){
    bool failed=false;try{a.append(c);}catch(const ProtocolError&){failed=true;}assert(failed);idle(a);
}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(8*i));}
unsigned append_image(ParticleTextureAssembler& a,ParticleTextureChunk c,std::span<const std::uint8_t> source,bool through_wire=false){
    assert(source.size()==c.total_bytes);
    const auto chunk_bytes=max_particle_texture_chunk_bytes/(c.width*4)*(c.width*4);
    unsigned chunks=0;
    for(std::size_t at=0;at<source.size();at+=chunk_bytes){
        const auto bytes=std::min(chunk_bytes,source.size()-at);
        c.offset=static_cast<unsigned>(at);c.data=source.subspan(at,bytes);
        const auto packet=through_wire?encode(c):Bytes{};
        const bool complete=through_wire?a.append(decode(packet)):a.append(c);++chunks;
        assert(complete==(at+bytes==source.size()));
        assert(a.pending_bytes()==source.size()&&a.received_bytes()==at+bytes);
        assert(a.retained_bytes()>=source.size()&&a.retained_bytes()<=max_particle_texture_bytes);
        assert(a.header().data.empty());
        if(!complete)assert(a.pixels().empty());
    }
    assert(std::ranges::equal(a.pixels(),source));return chunks;
}
void reject_allocation(ParticleTextureAssembler& a,const ParticleTextureChunk& c){
    bool failed=false;
    try{allocations::Fault fault(0);a.append(c);}catch(const std::bad_alloc&){failed=true;}
    assert(failed&&allocations::injected);idle(a);
}
}
int main(){
    Bytes small(24);for(unsigned i=0;i<24;++i)small[i]=static_cast<std::uint8_t>(i);
    ParticleTextureChunk c{0x0102030405060708ull,9,10,11,12,3,2,0,24,small};
    const auto golden=encode(c);assert(golden.size()==72);
    const std::string expected="080706050403020109000000000000000a0000000b0000000c0000000000000003000000020000000000000018000000";
    const char* hex="0123456789abcdef";std::string actual;
    for(unsigned i=0;i<48;++i){actual+=hex[golden[i]>>4];actual+=hex[golden[i]&15];}assert(actual==expected);
    ParticleTextureAssembler a;idle(a);
    assert(a.append(decode(golden)));assert(std::ranges::equal(a.pixels(),small));a.reset();idle(a);
    for(unsigned size=0;size<=48;++size)reject_wire(Bytes(golden.begin(),golden.begin()+size));
    auto bad=golden;bad.push_back(0);reject_wire(bad);
    for(unsigned at:{0u,8u,16u,20u,24u,32u,36u}){bad=golden;for(unsigned i=0;i<(at==0||at==8||at==24?8u:4u);++i)bad[at+i]=0;reject_wire(bad);}
    bad=golden;put(bad,20,65);reject_wire(bad);bad=golden;put(bad,32,4097);reject_wire(bad);
    for(unsigned offset:{1u,4u,24u,0xffffffffu}){bad=golden;put(bad,40,offset);reject_wire(bad);}
    bad=golden;put(bad,44,25);reject_wire(bad);

    // No published pixels until every row has arrived, even across interrupted transfers.
    c.data=std::span(small).first(12);assert(!a.append(c));assert(a.pending_bytes()==24&&a.retained_bytes()==24&&a.pixels().empty());
    auto next=c;next.offset=12;next.data=std::span(small).subspan(12);assert(a.append(next));assert(std::ranges::equal(a.pixels(),small));a.reset();idle(a);
    assert(!a.append(c));reject_append(a,c); // Duplicate start is not a new transfer.
    assert(!a.append(c));next=c;next.offset=12;next.life++;reject_append(a,next);
    assert(!a.append(c));next=c;next.revision++;assert(!a.append(next));
    auto stale=c;stale.offset=12;reject_append(a,stale); // Canceled upload cannot contribute old rows.
    assert(!a.append(c));a.reset();idle(a);next=c;next.life++;assert(!a.append(next));a.reset();idle(a);
    next=c;next.offset=12;reject_append(a,next);

    // Allocation failure leaves no partial identity or buffer, and the same
    // valid first chunk can recover once memory is available.
    reject_allocation(a,c);assert(!a.append(c));a.reset();idle(a);

    // The exact live4096 atlas needs256 chunks, at most64MiB staging, and publishes atomically.
    Bytes large(max_particle_texture_bytes);
    for(std::size_t i=0;i<large.size();++i)large[i]=static_cast<std::uint8_t>(i*31+(i>>16));
    c={7,8,9,10,11,4096,4096,0,static_cast<unsigned>(large.size()),{}};
    assert(append_image(a,c,large,true)==256);
    const auto* storage=a.pixels().data();
    const auto cold_allocations=a.allocation_count(),cold_reuses=a.reuse_count();
    a.finish();idle(a,max_particle_texture_bytes);

    // Every pixel changes on a repeated maximum-size transfer. Faulting every
    // allocation proves that retaining storage removes hot-transfer allocation;
    // partial chunks still cannot reveal either old or uninitialized bytes.
    for(auto& byte:large)byte^=0xa5;
    ++c.revision;
    const auto hot_allocations=allocations::count;
    {
        allocations::Fault fault(0);
        assert(append_image(a,c,large)==256);
        assert(a.pixels().data()==storage);
        a.finish();idle(a,max_particle_texture_bytes);

        // Shrinking publishes exactly the new dimensions, without exposing the
        // old atlas tail. Returning to 4096 also reuses the same bounded block.
        auto smaller=c;smaller.key++;smaller.revision++;smaller.width=3;smaller.height=2;smaller.total_bytes=24;
        assert(append_image(a,smaller,small)==1);
        assert(a.pixels().size()==small.size()&&a.pixels().data()==storage);
        a.finish();idle(a,max_particle_texture_bytes);
        c.revision=smaller.revision+1;
        assert(append_image(a,c,large)==256&&a.pixels().data()==storage);
    }
    assert(allocations::count==hot_allocations&&!allocations::injected);
    assert(a.allocation_count()==cold_allocations&&a.reuse_count()>cold_reuses);

    // An interrupted hot transfer can be replaced by a newer revision, even
    // with different dimensions/key. Its abandoned rows must never survive.
    a.finish();idle(a,max_particle_texture_bytes);
    c.revision++;c.offset=0;c.data=std::span(large).first(max_particle_texture_chunk_bytes);
    assert(!a.append(c));
    auto smaller=c;smaller.key++;smaller.revision++;smaller.width=3;smaller.height=2;smaller.total_bytes=24;
    smaller.data=std::span(small).first(12);
    const auto cancel_allocations=allocations::count;
    {
        allocations::Fault fault(0);
        assert(!a.append(smaller));assert(a.pixels().empty()&&a.pending_bytes()==24);
        next=smaller;next.offset=12;next.data=std::span(small).subspan(12);
        assert(a.append(next)&&std::ranges::equal(a.pixels(),small)&&a.pixels().data()==storage);
    }
    assert(allocations::count==cancel_allocations&&!allocations::injected);
    stale=c;stale.offset=max_particle_texture_chunk_bytes;reject_append(a,stale);

    // A life/session reset releases the retained atlas and permits its new
    // identity. A duplicate start or changed identity always rejects and frees.
    assert(append_image(a,c,large)==256);a.finish();idle(a,max_particle_texture_bytes);
    a.reset();idle(a);
    smaller.life++;smaller.revision=3;
    assert(!a.append(smaller));reject_append(a,smaller);
    for(unsigned changed=0;changed<5;++changed){
        assert(!a.append(smaller));next=smaller;next.offset=12;next.data=std::span(small).subspan(12);
        if(changed==0)next.epoch++;
        else if(changed==1)next.generation++;
        else if(changed==2)next.life++;
        else if(changed==3)next.key++;
        else next.revision--;
        reject_append(a,next);
    }

    // Growing after either a completed or canceled smaller image releases the
    // old block before requesting a replacement. Failed growth is fully reset.
    Bytes medium(1024,0x6d);
    auto grow=smaller;grow.width=16;grow.height=16;grow.total_bytes=static_cast<unsigned>(medium.size());
    grow.revision++;grow.data=std::span(medium).first(256);
    assert(append_image(a,smaller,small)==1);a.finish();idle(a,small.size());
    reject_allocation(a,grow);
    assert(!a.append(smaller));assert(!a.append(grow));
    assert(a.retained_bytes()==medium.size()&&a.pending_bytes()==medium.size()&&a.pixels().empty());
    next=c;next.epoch=grow.epoch;next.generation=grow.generation;next.life=grow.life;next.revision=grow.revision+1;
    reject_allocation(a,next);
    assert(append_image(a,grow,medium)==1);a.finish();idle(a,medium.size());
    next=grow;next.data={};reject_append(a,next); // Malformed input also frees an idle retained block.
    assert(!a.append(smaller));a.reset();idle(a);
    std::puts("{\"particleTextureChunks\":true,\"atlas4096\":true,\"chunks\":256,\"atomicPublication\":true,\"staleAndReorderedRejected\":true,\"maxStagingBytes\":67108864,\"hotTransferAllocations\":0,\"resizeReuse\":true,\"allocationFailureRecovery\":true,\"resetReleasesStorage\":true}");
}
