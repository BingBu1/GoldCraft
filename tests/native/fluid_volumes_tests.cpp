#include "goldcraft/fluid_volumes.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace goldcraft;
namespace {
int checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("fluid check "+std::to_string(checks));}
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const ProtocolError&){caught=true;}check(caught);}
struct PM {
    int (*PM_PointContents)(float*,int*);int (*PM_TruePointContents)(float*);
    int movetype=3,dead=0,deadflag=0,spectator=0,iuser1=0,usehull=0;
    float origin[3]{},player_mins[4][3]{},player_maxs[4][3]{};
};
int native=-1,calls=0;
int point(float*,int* real){++calls;if(real)*real=native;return native;}
int truth(float*){return native;}
Bytes fragment(const fluids::Snapshot& s,std::size_t offset,std::size_t length){
    auto data=fluids::encode(s);Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(static_cast<unsigned>(offset));w.u32(static_cast<unsigned>(data.size()));w.bytes(std::span(data).subspan(offset,length));return w.data;
}
Bytes lease(const fluids::Snapshot& s){Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(0);w.u32(0);return w.data;}
}
int main(){
    fluids::Snapshot s{9,1,{{fluids::water,{-32,-32,0},{32,32,28}},{fluids::lava,{16,16,0},{48,48,32}}}};
    auto bytes=fluids::encode(s);auto decoded=fluids::decode(bytes);check(decoded.volumes==s.volumes);
    bytes.pop_back();rejects([&]{fluids::decode(bytes);});
    auto bad=s;bad.volumes[0].contents=-2;rejects([&]{fluids::decode(fluids::encode(bad));});
    bad=s;bad.volumes[0].min[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{fluids::decode(fluids::encode(bad));});
    bad=s;bad.volumes[0].max[1]=-40;rejects([&]{fluids::decode(fluids::encode(bad));});
    bad=s;bad.epoch=0;rejects([&]{fluids::decode(fluids::encode(bad));});
    fluids::Field f;check(f.assign(s));float p[3]{0,0,1};check(f.contents(p)==fluids::water);
    p[0]=20;p[1]=20;check(f.contents(p)==fluids::lava);p[0]=0;p[1]=0;p[2]=28;check(f.contents(p)==fluids::empty);
    p[2]=std::numeric_limits<float>::quiet_NaN();check(f.contents(p)==fluids::empty);p[2]=1;
    PM pm{point,truth};int real=0;
    {fluids::MovementScope scope(pm,&f);check(pm.PM_PointContents(p,&real)==fluids::water&&real==fluids::water);
        check(pm.PM_TruePointContents(p)==fluids::water);native=-2;check(pm.PM_PointContents(p,&real)==-2&&real==-2);
        native=-4;check(pm.PM_PointContents(p,&real)==-4&&real==-4);native=-1;
        {fluids::MovementScope nested(pm,&f);check(pm.PM_PointContents(p,nullptr)==fluids::water);}
        check(pm.PM_PointContents(p,nullptr)==fluids::water);
        {fluids::MovementScope disabled(pm,nullptr);check(pm.PM_PointContents(p,nullptr)==fluids::empty);}
        check(pm.PM_PointContents(p,nullptr)==fluids::water);
        PM other{point,truth};{fluids::MovementScope nested(other,&f);check(other.PM_PointContents(p,nullptr)==fluids::water);}
        check(pm.PM_PointContents(p,nullptr)==fluids::water);
    }
    check(pm.PM_PointContents==point&&pm.PM_TruePointContents==truth);
    try{fluids::MovementScope scope(pm,&f);throw 1;}catch(int){}check(pm.PM_PointContents==point);
    {fluids::MovementScope scope(pm,nullptr);check(pm.PM_PointContents==point);}
    fluids::Receiver r;auto first=fragment(s,0,32);check(!r.accept(8,first,0));check(!r.accept(9,first,0));check(!r.active(9,0));
    rejects([&]{r.accept(9,fragment(s,33,10),0);});check(!r.active(9,0));
    r.accept(9,first,0);check(r.accept(9,fragment(s,32,44),0));check(r.active(9,.5)&&r.active(9,.5)->contents(p)==fluids::water);
    check(!r.active(9,1.0)&&!r.active(8,.5));check(r.accept(9,lease(s),.8));check(r.active(9,1.2));
    auto clear=s;clear.revision=2;clear.volumes.clear();check(r.accept(9,fragment(clear,0,20),1.3));check(r.active(9,1.4)->contents(p)==-1);
    check(!r.accept(9,lease(s),1.5));check(!r.accept(9,fragment(s,0,76),1.5));r.clear();check(!r.active(9,1.5));
    bad=s;bad.volumes.clear();rejects([&]{f.assign(bad);});check(f.contents(p)==fluids::water);
    // Compare BVH with an independent linear oracle at random points, including
    // overlaps and exact boundaries of large liquid collections.
    std::mt19937 random(8);std::uniform_real_distribution<float> location(-500,500);
    fluids::Snapshot many{9,2,{}};
    for(int i=0;i<8192;++i){fluids::Volume v;v.contents=i%3?fluids::water:fluids::lava;
        for(int a=0;a<3;++a){v.min[a]=location(random);v.max[a]=v.min[a]+32;}many.volumes.push_back(v);}
    f.assign(many);
    for(int i=0;i<4000;++i){for(auto& a:p)a=location(random);int expected=-1;
        for(const auto& v:many.volumes)if(v.contains(p)){if(v.contents==fluids::lava){expected=fluids::lava;break;}expected=fluids::water;}
        check(f.contents(p)==expected);
    }
    f.clear();check(f.contents(p)==-1);
    many.volumes.push_back(many.volumes.front());rejects([&]{fluids::decode(fluids::encode(many));});
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_fluid_volumes);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,8);
    handshake.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);
    auto welcome=handshake.welcome();Reader reader(welcome);reader.key();reader.u32();check(reader.u32()==capability_fluid_volumes);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,8);
    hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
    rejects([&]{client.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);});
    std::cout<<checks<<" fluid codec, authority lease, fragment, BVH and real callback scope checks passed\n";
}
