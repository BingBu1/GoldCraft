#include "goldcraft/lights.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace goldcraft;
int main(){
    unsigned checks=0;
    const auto check=[&](bool value){++checks;assert(value);};
    const auto decode=[](const Bytes& bytes){Reader reader(bytes);return read_scene_lights(reader);};
    Writer empty;empty.u32(0);check(decode(empty.data).empty());
    Writer many;many.u32(1025);
    for(unsigned i=0;i<1025;++i){many.f32(float(i));many.f32(64);many.f32(-float(i));many.f32(480);many.u32(0xff9cd6ff);}
    const auto lights=decode(many.data);check(lights.size()==1025);
    for(unsigned i=0;i<lights.size();++i){
        // Independent wire-coordinate expectation: MC X,-Z,Y-64 map to GS XYZ.
        check(lights[i].origin==std::array<float,3>{float(i)*32,float(i)*32,0});
        check(lights[i].radius==480&&lights[i].color==0xff9cd6ff);
    }
    const auto rejected=[&](const Bytes& bytes){try{(void)decode(bytes);check(false);}catch(const ProtocolError&){++checks;}};
    for(std::size_t i=0;i<many.data.size();++i){Bytes truncated(many.data.begin(),many.data.begin()+i);rejected(truncated);}
    auto extra=many.data;extra.push_back(0);rejected(extra);
    Writer excessive;excessive.u32(max_scene_lights+1);rejected(excessive.data);
    Writer maximum;maximum.u32(max_scene_lights);
    for(unsigned i=0;i<max_scene_lights;++i){maximum.f32(0);maximum.f32(64);maximum.f32(0);maximum.f32(32);maximum.u32(0xffffffff);}
    check(decode(maximum.data).size()==max_scene_lights);
    for(const float bad:{0.f,-1.f,769.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        Writer badRadius;badRadius.u32(1);badRadius.f32(0);badRadius.f32(64);badRadius.f32(0);
        // Writer rejects nonfinite values too; construct the hostile bits directly.
        auto bytes=badRadius.data;const auto* raw=reinterpret_cast<const std::uint8_t*>(&bad);
        bytes.insert(bytes.end(),raw,raw+4);bytes.insert(bytes.end(),4,255);rejected(bytes);
    }
    Writer overflow;overflow.u32(1);overflow.f32(std::numeric_limits<float>::max());overflow.f32(64);overflow.f32(0);overflow.f32(480);overflow.u32(0xffffffff);rejected(overflow.data);
    // Failed parsing does not replace a previously accepted complete snapshot.
    auto retained=lights;try{retained=decode(excessive.data);}catch(const ProtocolError&){}
    check(retained==lights);
    std::printf("Scene light decoding: %u checks passed\n",checks);
}
