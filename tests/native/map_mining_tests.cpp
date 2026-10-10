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
    for (const float boundary : {-64.0f, -32.0f, 0.0f, 32.0f, 64.0f}) {
        for (int axis = 0; axis < 3; ++axis) {
            for (float sign : {-1.0f, 1.0f}) {
                std::array<float,3> from{5,5,5}, to{5,5,5}, normal{};
                from[axis]=boundary+sign*16;to[axis]=boundary-sign;normal[axis]=sign;
                auto cell=surface_cell({from[0],from[1],from[2]}, {to[0],to[1],to[2]},
                                       {normal[0],normal[1],normal[2]},sign*boundary);
                const int expected=static_cast<int>(boundary/32)-(sign>0?1:0);
                const int values[]{cell.x,cell.y,cell.z};
                assert(values[axis]==expected);
                auto box=cell_box(cell);assert(box.max[axis]-box.min[axis]==32);
                for(int other=0;other<3;++other)if(other!=axis)assert(values[other]==0);
            }
        }
    }
    assert((surface_cell({10,10,5},{-1,-1,5},{std::sqrt(.5f),std::sqrt(.5f),0},0)==Cell{-1,-1,0}));
    assert((surface_cell({-20,5,5},{-32.002f,5,5},{1,0,0},-32.001f)==Cell{-2,0,0}));
    rejected([]{surface_cell({0,0,0},{0,1,0},{1,0,0},0);});
    rejected([]{surface_cell({0,0,0},{1,0,0},{1,0,0},.5f);});
    rejected([]{surface_cell({1,0,0},{0,0,0},{1,0,0},2);});
    rejected([]{surface_cell({1,0,0},{0,0,0},{0,0,0},0);});
    rejected([]{cell_box({1024,0,0});});
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
    q.sample=16;
    bytes=encode_request(q);assert(bytes.size()==92);auto parsed=decode_request(bytes);assert(parsed.sample==16);
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
    bytes=encode_surface(surface);assert(bytes.size()==108);
    auto sample=decode_surface(bytes);
    assert(sample.material==Material::glass&&sample.point.y==-20&&sample.normal.z==1&&sample.result.event==17);
    for(std::size_t n=0;n<bytes.size();++n)rejected([&]{decode_surface(std::span(bytes).first(n));});
    for(auto offset:{56,60}){auto invalid=bytes;invalid[offset]=255;rejected([&]{decode_surface(invalid);});}
    surface.normal={0,0,0};rejected([&]{decode_surface(encode_surface(surface));});
    surface.result.status=Status::obstructed;surface.kind=Target::none;
    assert(decode_surface(encode_surface(surface)).result.status==Status::obstructed);
    surface.result.status=Status::applied;surface.normal={0,0,1};surface.kind=Target::geometry;
    surface.cell={-1,90,12};surface.edit_revision=55;
    auto selected=decode_surface(encode_surface(surface));assert(selected.cell==surface.cell&&selected.edit_revision==55);
    surface.kind=Target::damageable_entity;rejected([&]{decode_surface(encode_surface(surface));});
    surface.kind=Target::geometry;surface.cell.x=1024;rejected([&]{decode_surface(encode_surface(surface));});
    for(auto type:{Type::map_mining_policy,Type::map_mining_request,Type::map_mining_result,
                  Type::map_mining_sample,Type::map_mining_surface}){
        auto frame=encode_frame(type,2,3,{});assert(decode_header(frame).type==type);
    }
    std::puts("{\"mining_policy_codec\":\"passed\",\"truncated_request_lengths\":92,\"native_surface_grid\":true,\"authority\":\"runtime test required\"}");
}
