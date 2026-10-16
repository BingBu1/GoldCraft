#include "goldcraft/combat.hpp"
#include <functional>
#include <iostream>
using namespace goldcraft;
namespace {
unsigned checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("check "+std::to_string(checks));}
void reject(const std::function<void()>& action){try{action();}catch(const ProtocolError&){++checks;return;}throw std::runtime_error("malformed request accepted");}
void u32(Bytes& b,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));}
Bytes packet(){
    Writer w;w.u64(77);w.u64(2);w.u64(0);w.u32(26);w.u32(3);w.u32(4);w.u32(6);w.f32(125);
    w.f32(64);w.f32(0);w.f32(36);w.u32(0);w.u32(0);w.u32(0);w.u64(9);w.f32(0);w.f32(0);w.f32(36);return w.data;
}
}
int main(){try{
    auto bytes=packet();auto d=read_mob_damage(bytes);check(bytes.size()==88&&d.kind==MobDamageKind::tnt);
    check(d.source_key==0&&d.attacker==0&&d.amount==125&&d.target_tick==9);
    auto native=bytes;u32(native,56,27);u32(native,60,8);u32(native,64,9);
    check(read_mob_damage(native).attacker==27);
    auto mob=bytes;u32(mob,16,99);check(read_mob_damage(mob).source_key==99);
    auto both=native;u32(both,16,99);reject([&]{read_mob_damage(both);});
    for(auto at:{60u,64u}){auto bad=bytes;u32(bad,at,1);reject([&]{read_mob_damage(bad);});}
    for(auto at:{60u,64u}){auto bad=native;u32(bad,at,0);reject([&]{read_mob_damage(bad);});}
    for(std::size_t n=0;n<bytes.size();++n)reject([&]{read_mob_damage(std::span(bytes).first(n));});
    auto extra=bytes;extra.push_back(0);reject([&]{read_mob_damage(extra);});
    for(auto at:{0u,8u,24u,28u,32u,68u}){auto bad=bytes;u32(bad,at,0);reject([&]{read_mob_damage(bad);});}
    for(auto at:{40u,44u,48u,52u,76u,80u,84u})for(auto bits:{0x7fc00000u,0x7f800000u,0xff800000u}){
        auto bad=bytes;u32(bad,at,bits);reject([&]{read_mob_damage(bad);});
    }
    for(auto bits:{0u,0x80000000u,0xbf800000u,0x459c4800u}){auto bad=bytes;u32(bad,40,bits);reject([&]{read_mob_damage(bad);});}
    for(auto at:{44u,76u}){auto bad=bytes;u32(bad,at,0x46800200);reject([&]{read_mob_damage(bad);});}
    auto badSlot=bytes;u32(badSlot,24,65);reject([&]{read_mob_damage(badSlot);});
    auto badOwner=native;u32(badOwner,56,65);reject([&]{read_mob_damage(badOwner);});
    for(unsigned kind=1;kind<=4;++kind){auto bad=bytes;u32(bad,36,kind);if(kind<4)bad.resize(68);reject([&]{read_mob_damage(bad);});}
    SkillDamageSnapshots history;history.begin(77,9,10);history.actor(9,26,3,4,{0,0,36});
    check(history.matches(d,10.05f,{0,0,36}));check(history.matches(d,11,{64,0,36}));
    check(!history.matches(d,11.001f,{0,0,36}));check(!history.matches(d,9.999f,{0,0,36}));
    check(!history.matches(d,10.05f,{64.1f,0,36}));
    for(unsigned field=0;field<6;++field){auto bad=d;
        if(field==0)bad.epoch++;if(field==1)bad.serial++;if(field==2)bad.life++;
        if(field==3)bad.target_tick++;if(field==4)bad.target.x+=2;if(field==5)bad.slot++;
        check(!history.matches(bad,10.1f,{0,0,36}));
    }
    check(current_mob_damage(d,77,1,3,4));check(!current_mob_damage(d,77,2,3,4));
    check(!current_mob_damage(d,78,1,3,4));check(!current_mob_damage(d,77,1,4,4));check(!current_mob_damage(d,77,1,3,5));
    history.begin(77,41,10.5f);check(!history.matches(d,10.6f,{0,0,36}));
    history.begin(77,9,10);history.actor(9,26,3,4,{0,0,36});history.clear();check(!history.matches(d,10.1f,{0,0,36}));
    constexpr auto old=capability_status_effects|capability_status_attributes|capability_native_armor|
        capability_fluid_volumes|capability_gameplay_rules|capability_host_explosions;
    for(auto caps:{0u,old,capability_tnt_damage,old|capability_tnt_damage}) {
        Key key{};HostHandshake host(key,key,Role::host_server,Role::fabric_server,31);
        Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(4);hello.u32(caps);
        host.accept({Type::hello,40,0,1},hello.data);check(host.ready()&&host.peer_capabilities()==caps);
        auto response=host.welcome();Reader reply(response);check(reply.key()==key&&reply.u32()==3&&reply.u32()==caps);
        check(reply.f32()==units_per_block&&reply.f32()==minecraft_y_offset);reply.finish();
    }
    for(unsigned variant=0;variant<4;++variant){Key key{},token{};if(variant==3)token[0]=1;
        HostHandshake host(key,key,variant==1?Role::host_client:Role::host_server,variant==1?Role::fabric_client:Role::fabric_server,32);
        Writer hello;hello.bytes(key);hello.bytes(token);hello.u32(variant==1||variant==2?2:4);
        hello.u32(variant==0?capability_tnt_damage|(1u<<31):capability_tnt_damage);
        reject([&]{host.accept({Type::hello,40,0,1},hello.data);});check(!host.ready()&&host.peer_capabilities()==0);
    }
    std::cout<<"TNT codec/identity/capability checks="<<checks<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
