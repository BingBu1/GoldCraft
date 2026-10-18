#include "goldcraft/view_distortion.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace goldcraft;
namespace {
unsigned checks=0;
void check(bool ok){++checks;assert(ok);}
template<class F>void bad(F f){bool rejected=false;try{f();}catch(const ProtocolError&){rejected=true;}check(rejected);}
Bytes packet(view_distortion::Frame f){Writer w;w.u64(f.epoch);w.u64(f.revision);w.u32(f.serial);w.u32(f.life);w.f32(f.intensity);w.f32(f.angle);return w.data;}
}
int main(){
    view_distortion::Frame f{99,1,7,3,.5f,1};auto bytes=packet(f);check(bytes.size()==32);
    Reader r(bytes);auto got=view_distortion::read(r);check(got.intensity==.5f&&got.serial==7);
    for(unsigned i=0;i<32;++i){bad([&]{Reader truncated(std::span<const std::uint8_t>(bytes).first(i));view_distortion::read(truncated);});}
    bytes.push_back(0);bad([&]{Reader extended(bytes);view_distortion::read(extended);});
    for(float value:{-1.f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        auto invalid=f;invalid.intensity=value;bad([&]{auto b=packet(invalid);Reader x(b);view_distortion::read(x);});
    }
    for(float value:{-3.15f,3.15f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        auto invalid=f;invalid.angle=value;bad([&]{auto b=packet(invalid);Reader x(b);view_distortion::read(x);});
    }
    for(unsigned i=0;i<4;++i){auto invalid=f;if(i==0)invalid.epoch=0;if(i==1)invalid.revision=0;if(i==2)invalid.serial=0;if(i==3)invalid.life=0;
        bad([&]{auto b=packet(invalid);Reader x(b);view_distortion::read(x);});}
    view_distortion::Receiver frames;
    check(!frames.accept(f,100,7,3,1));check(!frames.accept(f,99,8,3,1));check(!frames.accept(f,99,7,4,1));
    check(frames.accept(f,99,7,3,1));check(!frames.accept(f,99,7,3,1.1));check(frames.active(99,7,3,1.349));
    check(!frames.active(99,7,3,1.35));check(!frames.active(99,7,3,.9));check(!frames.active(99,7,4,1.1));
    check(!frames.active(100,7,3,1.1));check(!frames.active(99,8,3,1.1));
    f.life=4;check(frames.accept(f,99,7,4,1.4));check(frames.active(99,7,4,1.5));frames.clear();check(!frames.active(99,7,4,1.5));
    const auto session=parse_key("00112233445566778899aabbccddeeff"),token=parse_key("ffeeddccbbaa99887766554433221100");
    auto hello=[&](Role role,unsigned caps){Writer w;w.bytes(session);w.bytes(token);w.u32(static_cast<unsigned>(role));w.u32(caps);return w.data;};
    for(unsigned caps:{0u,capability_shared_bulk_v1,capability_view_distortion,capability_shared_bulk_v1|capability_view_distortion}){
        HostHandshake h(session,token,Role::host_client,Role::fabric_client,1);auto b=hello(Role::fabric_client,caps);
        h.accept({Type::hello,static_cast<unsigned>(b.size()),0,1},b);check(h.ready());auto welcome=h.welcome();Reader x(welcome);
        x.key();check(x.u32()==1);check(x.u32()==(caps&capability_view_distortion));
    }
    auto forbidden=hello(Role::fabric_server,capability_view_distortion);HostHandshake server(session,token,Role::host_server,Role::fabric_server,1);
    bad([&]{server.accept({Type::hello,static_cast<unsigned>(forbidden.size()),0,1},forbidden);});
    auto frame=encode_frame(Type::view_distortion,1,2,packet(f));check(decode_header(std::span<const std::uint8_t>(frame).first(header_bytes)).type==Type::view_distortion);
    check(static_cast<unsigned>(Type::view_distortion)<wire_type_count);
    std::printf("%u view distortion wire/session/lease/role checks passed\n",checks);
}
