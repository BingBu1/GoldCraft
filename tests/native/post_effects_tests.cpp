#include "goldcraft/post_effects.hpp"
#include <cassert>
#include <bit>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace goldcraft;
namespace {
Writer mesh(unsigned groups){
    Writer w;w.u64(9);w.u64(2);w.u64(5);w.u32(7);w.u32(groups);w.u32(groups);
    for(unsigned i=0;i<groups;++i){
        ParticleMaterial m;m.texture=particle_untextured;
        w.u32(m.texture);w.u32(m.flags);
        for(auto v:{m.src_rgb,m.dst_rgb,m.src_alpha,m.dst_alpha,m.equation_rgb,m.equation_alpha,m.depth_func,m.color_mask})w.u32(v);
        w.f32(m.alpha_cutoff);for(auto v:m.color_mod)w.f32(v);w.u32(4);
        for(unsigned j=0;j<4;++j){w.f32(float(j));w.f32(64);w.f32(-2);w.f32(0);w.f32(1);w.u32(0x80102030);}
    }
    return w;
}
Bytes encode(std::span<const PostEffectStage> stages,unsigned groups,std::span<const std::uint8_t> resets={}){
    Writer w;w.u8(static_cast<std::uint8_t>(stages.size()));
    for(const auto& s:stages){w.u8(s.kind);w.i32(s.priority);w.u8(s.first_batch);w.u8(s.batch_count);for(float v:s.params)w.f32(v);}
    w.u8(static_cast<std::uint8_t>(resets.size()));w.bytes(resets);
    const auto m=mesh(groups);w.u32(static_cast<unsigned>(m.data.size()));w.bytes(m.data);return w.data;
}
PostEffectFrame decode(const Bytes& bytes){Reader r(bytes);return read_post_effects(r);}
void reject(const Bytes& bytes){bool failed=false;try{decode(bytes);}catch(const ProtocolError&){failed=true;}assert(failed);}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(8*i));}
}
int main(int argc,char** argv){
    const auto clear=encode({},0);assert(clear.size()==42&&decode(clear).stages.empty());
    const PostEffectStage screen{5,1,0,0,{.1f,0,0,.2f,.3f,.4f,0,0}};
    const auto fullscreen=encode(std::span(&screen,1),0);assert(fullscreen.size()==81&&decode(fullscreen).stages.size()==1);
    const PostEffectStage dispersion{6,100,0,0,{1.15f,1.075f,1,.25f,.75f,.7f,.7f,1.1f}};
    const auto dispersion_bytes=encode(std::span(&dispersion,1),0);const auto dispersion_frame=decode(dispersion_bytes);
    assert(dispersion_bytes.size()==81&&dispersion_frame.stages.size()==1&&dispersion_frame.mesh.batches.empty());
    assert(dispersion_frame.stages[0].params==dispersion.params);
    for(unsigned field:{0u,1u,2u})for(float invalid:{0.f,-1.f}){auto invalid_stage=dispersion;invalid_stage.params[field]=invalid;reject(encode(std::span(&invalid_stage,1),0));}
    auto masked_dispersion=dispersion;masked_dispersion.batch_count=1;reject(encode(std::span(&masked_dispersion,1),1));
    const std::array<PostEffectStage,2> stages{{{1,2,0,1,{0,1,0,1,1,0,1,0}},{2,2,1,1,{0,1,0,.2f,0,0,0,0}}}};
    const auto data=encode(stages,2);const auto frame=decode(data);
    assert(frame.stages.size()==2&&frame.mesh.batches.size()==2); // Equal materials must keep wire indices.
    assert(frame.stages[0].kind==1&&frame.stages[1].kind==2&&frame.stages[1].first_batch==1);
    for(std::size_t size=0;size<data.size();++size)reject(Bytes(data.begin(),data.begin()+size));
    auto bad=data;bad.push_back(0);reject(bad);
    bad=data;bad[0]=33;reject(bad);
    for(unsigned kind:{0u,7u,255u}){bad=data;bad[1]=static_cast<std::uint8_t>(kind);reject(bad);}
    for(auto priority:{-32769,32768}){bad=data;put(bad,2,std::bit_cast<unsigned>(priority));reject(bad);}
    for(unsigned field:{6u,7u}){bad=data;bad[field]=129;reject(bad);}
    bad=data;bad[7]=0;reject(bad); // A non-screen pass needs a mask.
    bad=fullscreen;bad[7]=1;reject(bad);
    for(unsigned value:{0x7fc00000u,0x7f800000u,std::bit_cast<unsigned>(1000001.f)}){bad=data;put(bad,8,value);reject(bad);}
    auto overlap=stages;overlap[1].first_batch=0;reject(encode(overlap,2));
    auto outside=stages;outside[1].first_batch=2;reject(encode(outside,2));
    reject(encode(std::span(stages.data(),1),2));reject(encode({},1));
    bad=data;put(bad,2+2*39,0);reject(bad);
    const std::array<std::uint8_t,1> reset{0};assert(decode(encode(stages,2,reset)).depth_resets.size()==1);
    const std::array<std::uint8_t,2> repeated{0,0};reject(encode(stages,2,repeated));
    const std::array<std::uint8_t,1> wrong_kind{1},bad_index{2};reject(encode(stages,2,wrong_kind));reject(encode(stages,2,bad_index));
    auto screen_between=dispersion;screen_between.first_batch=1;
    const std::array<PostEffectStage,3> mixed{stages[0],screen_between,stages[1]};
    const auto mixed_frame=decode(encode(mixed,2,reset));
    assert(mixed_frame.stages.size()==3&&mixed_frame.stages[1].kind==6&&mixed_frame.mesh.batches.size()==2&&mixed_frame.depth_resets[0]==0);
    assert(current_particles(frame.mesh,9,2,4,7));
    assert(!current_particles(frame.mesh,10,2,4,7)&&!current_particles(frame.mesh,9,2,4,8));
    assert(!current_particles(frame.mesh,9,3,0,7)&&!current_particles(frame.mesh,9,2,5,7));
    assert(known_type(Type::post_effect_plan));
    auto packet=encode_frame(Type::post_effect_plan,9,1,data);assert(decode_header(std::span(packet).first(header_bytes)).type==Type::post_effect_plan);
    if(argc==2){
        std::ifstream in(argv[1],std::ios::binary);assert(in.good());
        const Bytes fixture{std::istreambuf_iterator<char>(in),{}};const auto java=decode(fixture);
        assert(java.mesh.epoch==123&&java.mesh.generation==7&&java.mesh.revision==13&&java.mesh.life==2);
        assert(java.stages.size()==2&&java.mesh.batches.size()==2&&java.stages[1].first_batch==1);
    }
    std::puts("{\"postProtocol24\":true,\"atomicClearBytes\":42,\"screenBytes\":81,\"rangeCoverage\":true,\"depthResetOwnership\":true,\"truncationsRejected\":true,\"coalescingDisabled\":true}");
}
