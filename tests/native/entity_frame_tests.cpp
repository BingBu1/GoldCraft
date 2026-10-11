#include "goldcraft/entity_frame.hpp"
#include <bit>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace goldcraft;
namespace {
unsigned rejected=0;
void put(Bytes& bytes,std::size_t offset,std::uint32_t value){
    assert(offset+4<=bytes.size());for(unsigned i=0;i<4;++i)bytes[offset+i]=std::uint8_t(value>>(8*i));
}
void avatar(Writer& out,unsigned slot=1){out.u32(slot);out.u32(3);out.u32(4);Key uuid{};uuid[0]=5;out.bytes(uuid);}
Bytes base(std::uint64_t revision,float x,bool empty=false){
    Writer out;out.u64(9);out.u64(revision);out.u32(empty?0:1);out.u32(0);out.u32(empty?0:1);
    if(!empty){
        out.u32(1);out.u32(4);out.u32(4);
        for(unsigned i=0;i<4;++i){out.f32(x+float(i));out.f32(64);out.f32(-2);out.f32(0);out.f32(1);out.u32(0xffffffff);out.u32(0x00800000);}
    }
    out.u32(empty?0:1);if(!empty)avatar(out);return out.data;
}
Bytes effects(std::uint64_t revision,float x,bool empty=false,unsigned texture=3,unsigned groups=1){
    Writer out;out.u64(9);out.u64(2);out.u64(revision);out.u32(7);out.u32(empty?0:1);out.u32(empty?0:groups);
    for(unsigned group=0;group<(empty?0:groups);++group){
        out.u32(texture);out.u32(particle_blend|particle_depth_test|particle_repeat_linear|particle_view_offset);
        for(unsigned value:{0x0300u,1u,0u,1u,0x8006u,0x8006u,0x0202u,15u})out.u32(value);
        out.f32(0);for(unsigned i=0;i<4;++i)out.f32(1);out.u32(4);
        for(unsigned i=0;i<4;++i){out.f32(x+float(i));out.f32(64);out.f32(-2);out.f32(2);out.f32(-1);out.u32(0xffffffff);}
    }
    return out.data;
}
Bytes envelope(const Bytes& body,const Bytes& effect){
    Writer out;out.u32(static_cast<unsigned>(body.size()));out.bytes(body);out.u32(static_cast<unsigned>(effect.size()));out.bytes(effect);return out.data;
}
Bytes packet(std::uint64_t revision,float x,bool empty=false){return envelope(base(revision,x,empty),effects(revision,x,empty));}
EntityFrame decode(const Bytes& data){Reader reader(data);return read_entity_frame(reader);}
void reject(const Bytes& data){bool failed=false;try{decode(data);}catch(const ProtocolError&){failed=true;}assert(failed);++rejected;}
void check_pair(const EntityFrame& value,std::uint64_t revision,float x){
    assert(value.base.revision==revision&&value.effects.revision==revision&&value.base.epoch==value.effects.epoch);
    assert(value.base.batches.size()==1&&value.effects.batches.size()==1);
    assert(value.base.batches[0].vertices[0].position.x==x&&value.effects.batches[0].vertices[0].position.x==x);
    assert(value.base.batches[0].flags==4);
    assert(value.effects.batches[0].material.flags==(particle_blend|particle_depth_test|particle_repeat_linear|particle_view_offset));
    assert(value.effects.batches[0].material.depth_func==0x0202);
}
}
int main(int argc,char** argv){
    static_assert(protocol_version==27);assert(known_type(Type::entity_frame));
    const auto first=packet(11,1),second=packet(12,2);check_pair(decode(first),11,1);check_pair(decode(second),12,2);
    assert(decode(first).base.avatars.size()==1);
    for(std::size_t n=0;n<second.size();++n)reject(Bytes(second.begin(),second.begin()+n));
    auto extra=second;extra.push_back(0);reject(extra);
    reject(Bytes(max_payload+1));
    const auto body=base(12,2),effect=effects(12,2);
    for(unsigned size:{0u,31u,0xffffffffu}){auto bad=second;put(bad,0,size);reject(bad);}
    for(unsigned size:{0u,35u,0xffffffffu}){auto bad=second;put(bad,4+body.size(),size);reject(bad);}
    auto bad=body;bad.push_back(0);reject(envelope(bad,effect));
    bad=effect;bad.push_back(0);reject(envelope(body,bad));
    reject(envelope(base(11,1),effect));bad=effect;put(bad,0,10);reject(envelope(body,bad));
    for(unsigned offset:{0u,8u}){bad=body;put(bad,offset,0);reject(envelope(bad,effect));}
    for(unsigned offset:{16u,20u,24u}){bad=body;put(bad,offset,129);reject(envelope(bad,effect));}
    for(const auto& [offset,value]:std::initializer_list<std::pair<unsigned,unsigned>>{{28,257},{32,8},{36,3},{36,299596},{152,65},{156,0},{156,65},{160,0},{164,0}}){
        bad=body;put(bad,offset,value);reject(envelope(bad,effect));
    }
    bad=body;for(unsigned offset=168;offset<184;offset+=4)put(bad,offset,0);reject(envelope(bad,effect));
    bad=body;put(bad,16,0);reject(envelope(bad,effect));
    bad=body;put(bad,16,2);put(bad,152,2);Writer duplicate;avatar(duplicate);bad.insert(bad.end(),duplicate.data.begin(),duplicate.data.end());reject(envelope(bad,effect));
    for(unsigned offset:{40u,44u,48u,52u,56u})for(unsigned value:{0x7fc00000u,0x7f800000u,0xff800000u}){
        bad=body;put(bad,offset,value);reject(envelope(bad,effect));
    }
    for(unsigned offset:{40u,44u,48u}){bad=body;put(bad,offset,std::bit_cast<unsigned>(32000004.f));reject(envelope(bad,effect));}
    bad=effect;put(bad,40,512);reject(envelope(body,bad));
    bad=effect;put(bad,68,0x0208);reject(envelope(body,bad));
    bad=effect;put(bad,100,0x7fc00000);reject(envelope(body,bad));
    auto ordered=decode(envelope(body,effects(12,2,false,3,2)));assert(ordered.effects.batches.size()==2);
    auto empty=decode(packet(13,0,true));assert(empty.base.batches.empty()&&empty.effects.batches.empty()&&empty.base.avatars.empty());
    assert(empty.base.count==0&&empty.effects.count==0);

    const auto next=decode(second);
    assert(current_entity_frame(next,9,2,11,7));
    assert(!current_entity_frame(next,10,2,11,7)&&!current_entity_frame(next,9,2,11,8));
    assert(!current_entity_frame(next,9,3,11,7)&&!current_entity_frame(next,9,2,12,7));
    assert(!current_entity_frame(next,9,1,12,7)); // A new texture namespace does not reset entity revision ordering.
    auto base_exists=[](unsigned id){return id==1;};auto effect_exists=[](unsigned id){return id==3;};
    validate_entity_frame_textures(next,2,base_exists,effect_exists);
    const auto reject_texture=[&](const EntityFrame& frame,std::uint64_t generation,bool body_ready,bool effect_ready){
        bool failed=false;try{validate_entity_frame_textures(frame,generation,[&](unsigned){return body_ready;},[&](unsigned){return effect_ready;});}
        catch(const ProtocolError&){failed=true;}assert(failed);++rejected;
    };
    reject_texture(next,2,false,true);reject_texture(next,2,true,false);reject_texture(next,1,true,true);reject_texture(next,3,true,true);
    for(unsigned texture:{0u,particle_untextured}){
        auto built_in=decode(envelope(body,effects(12,2,false,texture)));validate_entity_frame_textures(built_in,1,base_exists,[](unsigned){return false;});
    }
    // Exercise the production decode/currentness/reference checks before committing test state.
    // This is not a substitute for a plugin GL/pixel integration test.
    auto visible=decode(first);
    const auto receive=[&](const Bytes& data,bool ready){
        try{auto candidate=decode(data);if(!current_entity_frame(candidate,9,2,visible.base.revision,7))return false;
            validate_entity_frame_textures(candidate,2,base_exists,[&](unsigned){return ready;});
            visible=std::move(candidate);return true;
        }catch(const ProtocolError&){return false;}
    };
    for(std::size_t n=0;n<second.size();++n){assert(!receive(Bytes(second.begin(),second.begin()+n),true));check_pair(visible,11,1);}
    assert(!receive(second,false));check_pair(visible,11,1);assert(!receive(first,true));check_pair(visible,11,1);
    assert(receive(second,true));check_pair(visible,12,2);assert(visible.base.avatars==next.base.avatars);
    assert(receive(packet(13,0,true),false));assert(visible.base.batches.empty()&&visible.effects.batches.empty()&&visible.base.avatars.empty());
    assert(fresh_entity_frame(12,7,2,2,7,7,0)&&fresh_entity_frame(12,7,2,2,7,7,.499));
    assert(!fresh_entity_frame(12,7,2,2,7,7,.5)&&!fresh_entity_frame(12,7,2,2,7,7,-.01));
    assert(!fresh_entity_frame(12,7,2,2,8,7,.1)&&!fresh_entity_frame(12,7,2,2,7,8,.1));
    assert(!fresh_entity_frame(12,7,2,3,7,7,.1)&&!fresh_entity_frame(0,7,2,2,7,7,.1));
    assert(!fresh_entity_frame(12,7,2,2,7,7,std::bit_cast<double>(std::uint64_t{0x7ff8000000000000})));
    if(argc==2){
        std::ifstream input(argv[1],std::ios::binary);assert(input.good());
        Bytes java_packet((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        auto java_frame=decode(java_packet);check_pair(java_frame,12,2);assert(java_frame.base.avatars.empty());
    }
    std::printf("{\"entityProtocol26\":true,\"rejectedMalformedOrMissingTexture\":%u,\"orderedEffects\":true,\"transactionPreservesOldPair\":true,\"dualLifeAndFreshness\":true,\"javaInterop\":%s}\n",rejected,argc==2?"true":"false");
}
