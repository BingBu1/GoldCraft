#include "goldcraft/native_armor.hpp"
#include <iostream>
#include <stdexcept>

using namespace goldcraft;
int main(){
    int checks=0;
    const auto check=[&](bool value){if(!value)throw std::runtime_error("native armor check "+std::to_string(checks+1));++checks;};
    ArmorRequest q;
    q.epoch=0x1020304050607080ULL;q.event=UINT64_MAX;q.slot=2;q.serial=7;q.spawn=9;q.life=11;
    q.attacker=3;q.attacker_serial=13;q.attacker_spawn=17;q.damage=40;q.kind=ArmorDamageKind::projectile;
    q.position={320,-160,36};
    const auto good=write_armor_request(q);
    check(good.size()==64&&good[0]==0x80&&good[7]==0x10&&good[8]==0xff);
    auto decoded=read_armor_request(good);
    check(decoded.event==UINT64_MAX&&decoded.attacker_spawn==17&&decoded.position[1]==-160);
    const auto reject=[&](const Bytes& packet){bool failed=false;try{read_armor_request(packet);}catch(const ProtocolError&){failed=true;}check(failed);};
    for(std::size_t size=0;size<good.size();++size)reject(Bytes(good.begin(),good.begin()+size));
    auto bad=good;bad.push_back(0);reject(bad);
    for(auto offset:{16,20,24,28,36,40}){
        bad=good;for(int i=0;i<4;++i)bad[offset+i]=0;reject(bad);
    }
    bad=good;bad[44]=0;bad[45]=0;bad[46]=0xc0;bad[47]=0x7f;reject(bad);
    Writer result;
    result.u64(q.epoch);result.u64(q.event);result.u32(2);result.u32(7);result.u32(9);result.u32(11);
    result.u32(0);result.f32(11.2f);result.f32(.36f);
    check(result.data.size()==44);
    const auto reply=read_armor_result(result.data);check(reply.after_armor==11.2f&&reply.enchantment_multiplier==.36f);
    for(auto roles:{std::array{Role::host_server,Role::fabric_server},std::array{Role::host_client,Role::fabric_client}}){
        for(auto caps:{capability_native_armor,capability_native_armor|capability_status_attributes}){
            Key session{},token{};HostHandshake handshake(session,token,roles[0],roles[1],9);
            Writer hello;hello.bytes(session);hello.bytes(token);hello.u32(static_cast<std::uint32_t>(roles[1]));hello.u32(caps);
            bool accepted=true;
            try{handshake.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);}
            catch(const ProtocolError&){accepted=false;}
            check(accepted==(roles[0]==Role::host_server&&caps==(capability_native_armor|capability_status_attributes)));
        }
    }
    std::cout<<checks<<" native armor wire/role checks passed\n";
}
