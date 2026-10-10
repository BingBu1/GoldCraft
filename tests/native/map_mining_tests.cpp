#include "goldcraft/map_mining.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace goldcraft;
using namespace goldcraft::mining;
template<class F> void rejected(F&& action) {
    bool threw=false;try{action();}catch(const ProtocolError&){threw=true;}assert(threw);
}
int main(){
    for(float value:{-1.0f,0.0f,0.5f,1.5f,2.1f,3.0f,1e30f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        assert(cvar_mode(value)==Mode::disabled);
    assert(cvar_mode(1)==Mode::damageable_entities&&cvar_mode(2)==Mode::all_geometry);
    for(Mode mode:{Mode::disabled,Mode::damageable_entities,Mode::all_geometry}){
        assert(!permits(mode,Target::none));
        assert(permits(mode,Target::geometry)==(mode==Mode::all_geometry));
        assert(permits(mode,Target::damageable_entity)==(mode!=Mode::disabled));
    }
    Policy policy{0xfedcba9876543210ull,0x8000000000000001ull,Mode::damageable_entities,entity_damage};
    auto bytes=encode_policy(policy);assert(bytes.size()==24&&decode_policy(bytes)==policy);
    for(std::size_t n=0;n<bytes.size();++n)rejected([&]{decode_policy(std::span(bytes).first(n));});
    for(auto offset:{0,8}){auto invalid=bytes;std::fill_n(invalid.begin()+offset,8,0);rejected([&]{decode_policy(invalid);});}
    for(auto offset:{16,20}){auto invalid=bytes;invalid[offset]=4;rejected([&]{decode_policy(invalid);});}
    Request q{policy.epoch,policy.revision,17,1,3,4,parse_key("00112233445566778899aabbccddeeff"),72,9,2,{10,-20,30},15,144};
    bytes=encode_request(q);assert(bytes.size()==84);auto parsed=decode_request(bytes);
    assert(parsed.epoch==q.epoch&&parsed.uuid==q.uuid&&parsed.target==72&&parsed.target_serial==9&&parsed.model==2&&parsed.point.y==-20&&parsed.damage==15&&parsed.reach==144);
    for(std::size_t n=0;n<bytes.size();++n)rejected([&]{decode_request(std::span(bytes).first(n));});
    auto trailing=bytes;trailing.push_back(0);rejected([&]{decode_request(trailing);});
    for(float invalid:{-1.0f,0.0f,5001.0f}){auto v=q;v.damage=invalid;rejected([&]{decode_request(encode_request(v));});}
    for(float invalid:{0.0f,193.0f}){auto v=q;v.reach=invalid;rejected([&]{decode_request(encode_request(v));});}
    auto wrong=q;wrong.slot=65;rejected([&]{decode_request(encode_request(wrong));});
    wrong=q;wrong.point.x=16385;rejected([&]{decode_request(encode_request(wrong));});
    wrong=q;wrong.target=0;rejected([&]{decode_request(encode_request(wrong));});
    wrong.target_serial=wrong.model=0;assert(decode_request(encode_request(wrong)).target==0);
    wrong=q;wrong.model=0;rejected([&]{decode_request(encode_request(wrong));});
    auto nonfinite=bytes;nonfinite[76]=0;nonfinite[77]=0;nonfinite[78]=0x80;nonfinite[79]=0x7f;
    rejected([&]{decode_request(nonfinite);});
    Result result{policy.epoch,17,policy.revision,1,3,4,72,9,Status::applied,20,-10};
    bytes=encode_result(result);assert(bytes.size()==56);auto response=decode_result(bytes);
    assert(response.before==20&&response.after==-10&&response.status==Status::applied&&response.target_serial==9);
    bytes[44]=10;rejected([&]{decode_result(bytes);});
    Surface surface{result,Target::damageable_entity,Material::glass,{10,-20,30},{0,0,1}};
    bytes=encode_surface(surface);assert(bytes.size()==88);
    auto sample=decode_surface(bytes);
    assert(sample.material==Material::glass&&sample.point.y==-20&&sample.normal.z==1&&sample.result.event==17);
    for(std::size_t n=0;n<bytes.size();++n)rejected([&]{decode_surface(std::span(bytes).first(n));});
    for(auto offset:{56,60}){auto invalid=bytes;invalid[offset]=255;rejected([&]{decode_surface(invalid);});}
    surface.normal={0,0,0};rejected([&]{decode_surface(encode_surface(surface));});
    surface.result.status=Status::obstructed;surface.kind=Target::none;
    assert(decode_surface(encode_surface(surface)).result.status==Status::obstructed);
    for(auto type:{Type::map_mining_policy,Type::map_mining_request,Type::map_mining_result,
                  Type::map_mining_sample,Type::map_mining_surface}){
        auto frame=encode_frame(type,2,3,{});assert(decode_header(frame).type==type);
    }
    std::puts("{\"mining_policy_codec\":\"passed\",\"truncated_request_lengths\":84,\"authority\":\"runtime test required\"}");
}
