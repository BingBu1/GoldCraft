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
    for(const auto& s:stages){w.u8(s.kind);w.i32(s.priority);w.u8(s.first_batch);w.u8(s.batch_count);for(unsigned i=0;i<post_effect_parameter_count(s.kind);++i)w.f32(s.params[i]);}
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
    for(unsigned kind:{0u,9u,255u}){bad=data;bad[1]=static_cast<std::uint8_t>(kind);reject(bad);}
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
    const PostEffectStage impact{7,9999,0,0,{2.25f,.65f,.42f,.85f,1,1,.45f,.12f,.2f,.8f,.03f,-.25f,
        .02f,.03f,.08f,.95f,.96f,1,.1f,1,.02f,17,.1f,.2f,.3f,.4f,.8f,.9f,.7f,.6f}};
    const PostEffectStage hsv{8,101,0,0,{1,.97f,.6f,.8f,1.2f,.3f,.5f,0}};
    const auto hsv_bytes=encode(std::span(&hsv,1),0);
    reject(hsv_bytes); // Legacy/unsolicited stages cannot be consumed.
    {Reader r(hsv_bytes);assert(read_post_effects(r,true).stages[0].params==hsv.params);}
    assert(hsv_bytes.size()==81);
    for(unsigned i=0;i<30;++i){auto invalid=hsv;invalid.params[i]=std::numeric_limits<float>::quiet_NaN();assert(!valid_post_effect_stage(invalid));}
    for(unsigned i=7;i<30;++i){auto invalid=hsv;invalid.params[i]=1;assert(!valid_post_effect_stage(invalid));}
    auto masked_hsv=hsv;masked_hsv.batch_count=1;assert(!valid_post_effect_stage(masked_hsv));
    const std::array<PostEffectStage,3> hsv_chain{hsv,impact,hsv};
    {const auto bytes=encode(hsv_chain,0);Reader r(bytes);const auto graph=read_post_effects(r,true);
     assert(graph.stages.size()==3&&graph.stages[0].kind==8&&graph.stages[1].kind==7&&graph.stages[2].kind==8);}
    // The reused bit has distinct meanings only inside authenticated roles.
    for(auto role:{Role::fabric_client,Role::fabric_server}){
        Key key{};HostHandshake handshake(key,key,role==Role::fabric_client?Role::host_client:Role::host_server,role,9);
        Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(role));hello.u32(capability_hsv_post);
        auto bytes=encode_frame(Type::hello,0,1,hello.data);
        handshake.accept(decode_header(std::span(bytes).first(header_bytes)),hello.data);
        auto response=handshake.welcome();Reader reader(response);reader.key();reader.u32();
        assert(reader.u32()==(role==Role::fabric_client?capability_hsv_post:capability_status_effects));
    }
    const auto impact_bytes=encode(std::span(&impact,1),0);
    assert(impact_bytes.size()==169&&decode(impact_bytes).stages[0].params==impact.params);
    for(unsigned i=0;i<30;++i){
        for(auto bits:{0x7fc00000u,0x7f800000u,0xff800000u,std::bit_cast<unsigned>(1000001.f),std::bit_cast<unsigned>(-1000001.f)}){
            auto invalid=impact_bytes;put(invalid,8+4*i,bits);reject(invalid);
        }
        auto changed=impact;changed.params[i]=i==21?-1000000.f:((i==2||i==8||i==9)?.875f:-.875f);
        assert(valid_post_effect_stage(changed));assert(decode(encode(std::span(&changed,1),0)).stages[0].params==changed.params);
    }
    for(unsigned i:{2u,8u,9u})for(float value:{-.001f,1.001f}){auto invalid=impact;invalid.params[i]=value;reject(encode(std::span(&invalid,1),0));}
    for(float value:{-1.5f,1.5f,17.25f}){auto invalid=impact;invalid.params[21]=value;reject(encode(std::span(&invalid,1),0));}
    for(float mode:{-2.f,0.f,.499f,.5f,1.f,2.f}){auto valid=impact;valid.params[5]=mode;assert(valid_post_effect_stage(valid));}
    for(float samples:{-1000000.f,0.f,1.f,16.f,32.f,33.f,1000000.f}){auto valid=impact;valid.params[21]=samples;assert(valid_post_effect_stage(valid));}
    for(unsigned i=8;i<30;++i){auto invalid=screen;invalid.params[i]=.01f;assert(!valid_post_effect_stage(invalid));}
    for(std::size_t size=0;size<impact_bytes.size();++size)reject(Bytes(impact_bytes.begin(),impact_bytes.begin()+size));
    auto malformed=impact_bytes;malformed.push_back(0);reject(malformed);
    malformed=impact_bytes;malformed.insert(malformed.begin()+128,4,0);reject(malformed);
    malformed=impact_bytes;malformed.erase(malformed.begin()+96,malformed.begin()+128);reject(malformed); // obsolete 22-float kind7
    auto invalid_impact=impact;invalid_impact.batch_count=1;reject(encode(std::span(&invalid_impact,1),1));
    auto interleaved=impact;interleaved.first_batch=1;
    const std::array<PostEffectStage,3> impact_mixed{stages[0],interleaved,stages[1]};
    const auto impact_frame=decode(encode(impact_mixed,2,reset));
    assert(impact_frame.stages[1].params==impact.params&&impact_frame.stages[1].first_batch==1);
    if(argc==3&&std::string(argv[1])=="--impact"){
        std::ifstream in(argv[2],std::ios::binary);assert(in.good());const Bytes fixture{std::istreambuf_iterator<char>(in),{}};
        const auto java=decode(fixture);assert(java.mesh.epoch==123&&java.mesh.generation==7&&java.mesh.revision==13&&java.mesh.life==2);
        for(std::size_t size=0;size<fixture.size();++size)reject(Bytes(fixture.begin(),fixture.begin()+size));
        auto extra_byte=fixture;extra_byte.push_back(0);reject(extra_byte);
        assert(java.stages.size()==3&&java.mesh.batches.size()==2&&java.depth_resets==std::vector<std::uint8_t>{0});
        assert(java.stages[0].kind==1&&java.stages[0].priority==20&&java.stages[0].batch_count==1);
        assert(java.stages[1].kind==7&&java.stages[1].priority==9999&&java.stages[1].first_batch==1&&java.stages[1].batch_count==0);
        assert(java.stages[2].kind==4&&java.stages[2].priority==-7&&java.stages[2].first_batch==1&&java.stages[2].batch_count==1);
        for(unsigned i=0;i<30;++i)assert(std::bit_cast<unsigned>(java.stages[1].params[i])==std::bit_cast<unsigned>(impact.params[i]));
        const std::array<float,8> first_params{0,1,0,1.65f,0,0,1.35f,.28f},last_params{.05f,1,.3f,.1f,0,0,0,0};
        for(unsigned i=0;i<8;++i){assert(java.stages[0].params[i]==first_params[i]);assert(java.stages[2].params[i]==last_params[i]);}
        for(const auto& batch:java.mesh.batches)assert(batch.vertices.size()==4&&batch.material.texture==particle_untextured);
        std::printf("{\"javaImpactInterop\":true,\"floatBitComparisons\":30,\"legacyParameterComparisons\":16,\"truncationsRejected\":%zu}\n",fixture.size());
    }
    if(argc==2){
        std::ifstream in(argv[1],std::ios::binary);assert(in.good());
        const Bytes fixture{std::istreambuf_iterator<char>(in),{}};const auto java=decode(fixture);
        assert(java.mesh.epoch==123&&java.mesh.generation==7&&java.mesh.revision==13&&java.mesh.life==2);
        assert(java.stages.size()==2&&java.mesh.batches.size()==2&&java.stages[1].first_batch==1);
    }
    std::puts("{\"postImpactCandidate\":true,\"impactFloatCount\":30,\"impactBytes\":169,\"allFieldFiniteBounds\":true,\"oldKindsEightFloats\":true,\"oldTailRejected\":true,\"atomicClearBytes\":42,\"screenBytes\":81,\"rangeCoverage\":true,\"depthResetOwnership\":true,\"truncationsRejected\":true,\"coalescingDisabled\":true}");
}
