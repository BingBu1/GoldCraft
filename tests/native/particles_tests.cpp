#include "goldcraft/particles.hpp"
#include <cassert>
#include <bit>
#include <cstdio>
#include <limits>

using namespace goldcraft;
namespace {
void batch(Writer& out,const ParticleMaterial& m,unsigned count=4){
    out.u32(m.texture);out.u32(m.flags);
    for(auto v:{m.src_rgb,m.dst_rgb,m.src_alpha,m.dst_alpha,m.equation_rgb,m.equation_alpha,m.depth_func,m.color_mask})out.u32(v);
    out.f32(m.alpha_cutoff);for(auto v:m.color_mod)out.f32(v);out.u32(count);
    for(unsigned i=0;i<count;++i){out.f32(float(i));out.f32(64);out.f32(-2);out.f32(0);out.f32(1);out.u32(0x80102030);}
}
Writer frame(unsigned count,unsigned groups){Writer out;out.u64(9);out.u64(2);out.u64(5);out.u32(7);out.u32(count);out.u32(groups);return out;}
ParticleSnapshot decode(const Bytes& bytes){Reader in(bytes);return read_particles(in);}
void reject(const Bytes& bytes){bool failed=false;try{decode(bytes);}catch(const ProtocolError&){failed=true;}assert(failed);}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(8*i));}
}
int main(){
    ParticleMaterial m;m.texture=particle_untextured;
    auto data=frame(1,1);batch(data,m);assert(data.data.size()==36+64+4*24);
    auto decoded=decode(data.data);assert(decoded.batches.size()==1&&decoded.batches[0].material==m);
    assert(decoded.batches[0].vertices[3].position.x==3&&decoded.batches[0].vertices[0].color==0x80102030);
    for(std::size_t size=0;size<data.data.size();++size)reject(Bytes(data.data.begin(),data.data.begin()+size));
    auto extra=data.data;extra.push_back(0);reject(extra);
    for(unsigned field:{0u,8u,16u,24u}){auto bad=data.data;put(bad,field,0);reject(bad);}
    auto check_invalid=[&](unsigned offset,std::uint32_t value){auto bad=data.data;put(bad,36+offset,value);reject(bad);};
    check_invalid(0,65);check_invalid(0,0xfffffffe);check_invalid(4,128);check_invalid(4,16|32);
    for(unsigned offset:{8u,12u,16u,20u})for(unsigned value:{2u,0x0309u,0x8001u,0xffffffffu})check_invalid(offset,value);
    for(unsigned offset:{12u,16u,20u})check_invalid(offset,0x0308);
    for(unsigned offset:{24u,28u})for(unsigned value:{0u,0x8007u,0x8008u,0x8009u})check_invalid(offset,value);
    check_invalid(32,0x01ff);check_invalid(32,0x0208);check_invalid(36,16);
    for(unsigned value:{std::bit_cast<unsigned>(-.01f),std::bit_cast<unsigned>(1.01f),0x7f800000u,0xff800000u,0x7fc00000u})check_invalid(40,value);
    for(unsigned offset:{44u,48u,52u,56u})for(unsigned value:{std::bit_cast<unsigned>(-.01f),std::bit_cast<unsigned>(4.01f),0x7fc00000u})check_invalid(offset,value);
    for(unsigned value:{0u,3u,65540u,0xffffffffu})check_invalid(60,value);
    for(unsigned offset:{64u,68u,72u,76u,80u})check_invalid(offset,0x7fc00000);
    for(unsigned depth=0x0200;depth<=0x0207;++depth){m.depth_func=depth;assert(valid_particle_material(m));}
    for(unsigned factor:{0u,1u,0x0300u,0x0301u,0x0302u,0x0303u,0x0304u,0x0305u,0x0306u,0x0307u}){
        m.src_rgb=m.dst_rgb=m.src_alpha=m.dst_alpha=factor;assert(valid_particle_material(m));
    }
    m.src_rgb=0x0308;assert(valid_particle_material(m));
    for(unsigned equation:{0x8006u,0x800au,0x800bu}){m.equation_rgb=m.equation_alpha=equation;assert(valid_particle_material(m));}
    for(unsigned flags=0;flags<=127;++flags){m.flags=flags;assert(valid_particle_material(m)==((flags&48)!=48));}
    m=ParticleMaterial{};m.texture=particle_untextured;m.flags=particle_depth_write|particle_depth_test;m.color_mask=0;
    auto equal=m;equal.flags=particle_depth_test;equal.depth_func=0x0202;equal.color_mask=15;
    auto ordered=frame(5,5);batch(ordered,m);batch(ordered,m);batch(ordered,equal);batch(ordered,m);batch(ordered,m);
    auto stream=decode(ordered.data);assert(stream.batches.size()==3);
    assert(stream.batches[0].material==m&&stream.batches[0].vertices.size()==8);
    assert(stream.batches[1].material==equal&&stream.batches[1].vertices.size()==4);
    assert(stream.batches[2].material==m&&stream.batches[2].vertices.size()==8);
    auto empty=frame(0,0);auto cleared=decode(empty.data);assert(cleared.batches.empty());
    assert(current_particles(cleared,9,2,4,7));
    assert(!current_particles(cleared,10,2,4,7)&&!current_particles(cleared,9,2,4,8));
    assert(!current_particles(cleared,9,3,0,7)&&!current_particles(cleared,9,2,5,7));
    assert(current_particles(cleared,9,1,99,7));
    auto bad_empty=frame(1,0);reject(bad_empty.data);bad_empty=frame(0,1);reject(bad_empty.data);
    auto too_many=frame(1,max_particle_batches+1);reject(too_many.data);
    auto oversized=frame(max_particle_count+1,1);reject(oversized.data);
    std::puts("{\"effectsProtocol23\":true,\"headerBytes\":64,\"orderedCoalescing\":true,\"invalidMaterialsRejected\":true,\"clearAndLife\":true}");
}
