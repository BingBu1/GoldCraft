#include "goldcraft/combat.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace goldcraft;
int main(){
    unsigned checks=0;
    auto check=[&](bool v){++checks;assert(v);};
    MobDamage d;d.epoch=77;d.event=1;d.source_key=88;d.slot=2;d.serial=3;d.life=4;d.kind=MobDamageKind::skill;d.amount=25;
    d.source={0,0,36};d.target={384,0,36};d.target_tick=9;
    auto encode=[](const MobDamage& d){Writer w;w.u64(d.epoch);w.u64(d.event);w.u64(d.source_key);w.u32(d.slot);w.u32(d.serial);w.u32(d.life);w.u32(static_cast<unsigned>(d.kind));w.f32(d.amount);w.f32(d.source.x);w.f32(d.source.y);w.f32(d.source.z);w.u32(d.attacker);w.u32(d.attacker_serial);w.u32(d.attacker_spawn);if(d.kind==MobDamageKind::skill){w.u64(d.target_tick);w.f32(d.target.x);w.f32(d.target.y);w.f32(d.target.z);}return w.data;};
    auto bad=[&](const Bytes& b){try{read_mob_damage(b);check(false);}catch(const ProtocolError&){check(true);}};
    auto bytes=encode(d);check(bytes.size()==88);auto decoded=read_mob_damage(bytes);check(decoded.target_tick==9&&decoded.target.x==384);
    bytes.pop_back();bad(bytes);bytes=encode(d);bytes.push_back(0);bad(bytes);
    for(int field=0;field<7;field++){
        auto v=d;
        if(field==0)v.target_tick=0;if(field==1)v.target.x=16385;if(field==2)v.target.y=std::numeric_limits<float>::quiet_NaN();
        if(field==3)v.source.z=std::numeric_limits<float>::infinity();if(field==4)v.slot=65;if(field==5)v.life=0;if(field==6)v.kind=static_cast<MobDamageKind>(5);
        // Writer also rejects nonfinite input; exercise malformed bytes below.
        if(field==2||field==3){auto b=encode(d);const auto offset=field==2?80:52;std::uint32_t bits=field==2?0x7fc00000:0x7f800000;for(int i=0;i<4;i++)b[offset+i]=static_cast<std::uint8_t>(bits>>(8*i));bad(b);}
        else bad(encode(v));
    }
    for(unsigned kind=1;kind<=3;kind++){auto v=d;v.kind=static_cast<MobDamageKind>(kind);auto b=encode(v);check(b.size()==68&&read_mob_damage(b).kind==v.kind);}
    SkillDamageSnapshots history;history.begin(77,9,10);history.actor(9,2,3,4,{384,0,36});
    check(history.matches(d,10.05f,{384,0,36}));check(history.matches(d,11,{448,0,36}));
    check(!history.matches(d,11.001f,{384,0,36}));check(!history.matches(d,9.999f,{384,0,36}));check(!history.matches(d,10.05f,{448.1f,0,36}));
    for(int field=0;field<7;field++){
        auto v=d;if(field==0)v.epoch++;if(field==1)v.serial++;if(field==2)v.life++;if(field==3)v.target_tick++;if(field==4)v.target.x+=2;if(field==5)v.slot++;if(field==6)v.kind=MobDamageKind::melee;
        check(!history.matches(v,10.1f,{384,0,36}));
    }
    check(current_mob_damage(d,77,0,3,4));check(!current_mob_damage(d,77,1,3,4));check(!current_mob_damage(d,78,0,3,4));
    history.begin(77,41,10.5f);check(!history.matches(d,10.6f,{384,0,36}));
    history.begin(77,9,10);history.actor(9,2,3,4,{384,0,36});history.clear();check(!history.matches(d,10.1f,{384,0,36}));
    std::cout<<"skill damage checks="<<checks<<"\n";
}
