#include "goldcraft/combat.hpp"
#include "goldcraft/status_hud.hpp"
#include <fstream>
#include <iostream>
#include <functional>
#include <limits>
using namespace goldcraft;
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("check "+std::to_string(checks));}
void reject(std::function<void()> fn){try{fn();}catch(const ProtocolError&){++checks;return;}throw std::runtime_error("accepted malformed packet");}
void u32(Bytes& bytes,std::size_t at,std::uint32_t v){for(unsigned i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(v>>(8*i));}
Bytes damage(unsigned kind=5,float amount=5,unsigned flags=9){
    Writer w;w.u64(1);w.u64(2);w.u64(kind==5?0:9);w.u32(3);w.u32(4);w.u32(5);w.u32(kind);w.f32(amount);
    w.f32(0);w.f32(0);w.f32(0);w.u32(0);w.u32(0);w.u32(0);
    if(kind>=4){w.u64(10);w.f32(0);w.f32(0);w.f32(0);}if(kind==5)w.u32(flags);return w.data;
}
int main(int argc,char** argv){try{
    check(argc==2);
    for(unsigned kind=1;kind<=4;++kind){auto p=damage(kind);check(p.size()==(kind==4?88u:68u));check(read_mob_damage(p).amount==5);reject([&]{read_mob_damage(damage(kind,-5));});}
    auto p=damage();check(p.size()==92);auto d=read_mob_damage(p);check(d.status_flags==9);
    check(read_mob_damage(damage(5,-5,9)).amount==-5);
    for(std::size_t n=0;n<p.size();++n)reject([&]{read_mob_damage(std::span(p).first(n));});
    auto extra=p;extra.push_back(0);reject([&]{read_mob_damage(extra);});
    for(unsigned bits:{0u,32u,17u,25u}){auto bad=p;u32(bad,88,bits);reject([&]{read_mob_damage(bad);});}
    for(unsigned bits:{0u,0x80000000u,0x7fc00000u,0x7f800000u,0xff800000u,0x459c4800u}){
        auto bad=p;u32(bad,40,bits);reject([&]{read_mob_damage(bad);});
    }
    reject([&]{read_mob_damage(damage(5,-5,11));});reject([&]{read_mob_damage(damage(5,-5,13));});
    auto source=p;u32(source,56,2);u32(source,60,8);u32(source,64,9);reject([&]{read_mob_damage(source);});
    u32(source,88,1);check(read_mob_damage(source).attacker==2);
    SkillDamageSnapshots snapshots;snapshots.begin(1,10,5);snapshots.actor(10,3,4,5,{0,0,0});
    check(snapshots.matches(d,5,{0,0,0}));check(!snapshots.matches(d,6.1f,{0,0,0}));check(!snapshots.matches(d,5,{65,0,0}));
    d.life=6;check(!snapshots.matches(d,5,{0,0,0}));d.life=5;d.target={2,0,0};check(!snapshots.matches(d,5,{0,0,0}));
    for(unsigned cap:{0u,8u,2u,6u,14u}){
        Key key{};HostHandshake h(key,key,Role::host_server,Role::fabric_server,1);
        Writer w;w.bytes(key);w.bytes(key);w.u32(4);w.u32(cap);h.accept({Type::hello,static_cast<unsigned>(w.data.size()),0,1},w.data);
        check(h.ready());auto welcome=h.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==(cap&12));
    }
    for(unsigned cap:{4u,12u,512u,9u}){
        Key key{};HostHandshake h(key,key,Role::host_server,Role::fabric_server,1);
        Writer w;w.bytes(key);w.bytes(key);w.u32(4);w.u32(cap);reject([&]{h.accept({Type::hello,static_cast<unsigned>(w.data.size()),0,1},w.data);});
    }
    // Capability16 is now assigned to status attributes. Keep the original
    // malformed-capability cases, using the still-unassigned512 as unknown.
    {
        Key key{};HostHandshake attributes(key,key,Role::host_server,Role::fabric_server,2);
        Writer request;request.bytes(key);request.bytes(key);request.u32(4);request.u32(capability_status_attributes);
        attributes.accept({Type::hello,static_cast<unsigned>(request.data.size()),0,1},request.data);
        check(attributes.ready()&&attributes.peer_capabilities()==capability_status_attributes);
        auto welcome=attributes.welcome();Reader reply(welcome);check(reply.key()==key);
        check(reply.u32()==3&&reply.u32()==16);
        check(reply.f32()==units_per_block&&reply.f32()==minecraft_y_offset);reply.finish();
    }
    Key key{};HostHandshake client(key,key,Role::host_client,Role::fabric_client,1);Writer hello;
    hello.bytes(key);hello.bytes(key);hello.u32(2);hello.u32(8);reject([&]{client.accept({Type::hello,40,0,1},hello.data);});
    std::ifstream input(argv[1],std::ios::binary);Bytes hud((std::istreambuf_iterator<char>(input)),{});
    auto s=read_status_hud(hud);check(s.slot==3&&s.entries.size()==2);check(s.entries[1].id=="efn:curse_of_blood");
    check(status_hud_text(s)=="minecraft:poison 2 3s\nefn:curse_of_blood 1 inf\n");
    for(std::size_t n=0;n<hud.size();++n)reject([&]{read_status_hud(std::span(hud).first(n));});
    auto clear=hud;clear.resize(32);u32(clear,28,0);check(read_status_hud(clear).entries.empty());
    for(unsigned badCount:{33u,0xffffffffu}){auto bad=hud;u32(bad,28,badCount);reject([&]{read_status_hud(bad);});}
    for(unsigned c=0;c<256;++c){if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-')continue;
        auto bad=hud;bad[34]=static_cast<std::uint8_t>(c);reject([&]{read_status_hud(bad);});}
    s.entries.clear();for(unsigned i=0;i<32;++i)s.entries.push_back({"x:"+std::string(94,'x'),255,2147483647});
    check(status_hud_text(s).size()<=484);
    std::cout<<"status native codec checks="<<checks<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
