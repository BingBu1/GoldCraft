#include "goldcraft/player_gameplay.hpp"
#include <cassert>
#include <iostream>
using namespace goldcraft;
Bytes packet(PlayerGameplay s){Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(s.slot);w.u32(s.serial);w.u32(s.spawn);w.u32(s.life);w.bytes(s.uuid);w.u32(s.flags);return w.data;}
int main(){
    const auto handshake=[](Role role,unsigned capabilities){
        Key key{};Writer payload;payload.bytes(key);payload.bytes(key);payload.u32(static_cast<unsigned>(role));payload.u32(capabilities);
        auto frame=encode_frame(Type::hello,0,1,payload.data);auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
        HostHandshake host(key,key,role==Role::fabric_server?Role::host_server:Role::host_client,role,1);
        host.accept(header,payload.data);return host.welcome();
    };
    auto accepted=handshake(Role::fabric_server,capability_player_gameplay|capability_skill_guard);
    Reader reply(accepted);reply.key();reply.u32();assert(reply.u32()==(capability_player_gameplay|capability_skill_guard));
    for(auto role:{Role::fabric_server,Role::fabric_client}){
        bool refused=false;try{handshake(role,capability_skill_guard);}catch(const ProtocolError&){refused=true;}assert(refused);
    }
    PlayerGameplay s{11,1,2,3,4,5,Key{7},7};
    auto decoded=read_player_gameplay(packet(s));assert(decoded.flags==7&&decoded.uuid==s.uuid);
    for(unsigned bad:{2u,4u,6u,8u}){auto invalid=s;invalid.flags=bad;bool refused=false;try{read_player_gameplay(packet(invalid));}catch(const ProtocolError&){refused=true;}assert(refused);}
    PlayerGameplayLease lease;assert(lease.update(s,10));
    assert(lease.flags(11,3,4,5,s.uuid,10.5f,true)==7);
    // Identity/authority expiry never grants armor/creative; form transitions
    // keep only presence while waiting for the new life's equipment snapshot.
    assert(lease.flags(11,3,4,6,s.uuid,10.5f,true)==1);
    assert(lease.flags(11,3,5,5,s.uuid,10.5f,true)==1);
    assert(lease.flags(11,4,4,5,s.uuid,10.5f,true)==0);
    assert(lease.flags(12,3,4,5,s.uuid,10.5f,true)==0);
    assert(lease.flags(11,3,4,5,Key{8},10.5f,true)==0);
    assert(lease.flags(11,3,4,5,s.uuid,11,true)==0);
    assert(lease.flags(11,3,4,5,s.uuid,10.5f,false)==0);
    assert(!lease.update(s,10.8f));s.revision=2;s.flags=0;assert(lease.update(s,10.9f));
    assert(lease.flags(11,3,4,5,s.uuid,11,true)==0);
    s.revision=3;s.flags=5;assert(lease.update(s,11.1f));assert(lease.flags(11,3,4,5,s.uuid,11.2f,true)==5);
    s.revision=4;s.flags=player_connected|player_skill_guard;
    bool legacy_refused=false;try{read_player_gameplay(packet(s));}catch(const ProtocolError&){legacy_refused=true;}assert(legacy_refused);
    assert(read_player_gameplay(packet(s),true).flags==9);
    assert(lease.update(s,20));assert(lease.flags(11,3,4,5,s.uuid,20.1f,true)==9);
    assert(lease.flags(11,3,4,5,s.uuid,20.16f,true)==1);
    assert(!lease.update(s,20.17f));assert(lease.flags(11,3,4,5,s.uuid,20.18f,true)==1);
    assert(lease.flags(11,3,4,5,s.uuid,19.9f,true)==0);
    ++s.revision;assert(lease.update(s,21));assert(lease.flags(11,3,4,6,s.uuid,21.01f,true)==1);
    assert(lease.flags(11,3,4,5,s.uuid,21.01f,false)==0);
    ++s.revision;s.flags=1;assert(lease.update(s,21.02f));assert(lease.flags(11,3,4,5,s.uuid,21.03f,true)==1);
    for(unsigned flags:{8u,16u,17u}){auto bad=s;bad.flags=flags;bool refused=false;try{read_player_gameplay(packet(bad),true);}catch(const ProtocolError&){refused=true;}assert(refused);}
    ActivePlayerAttack attack{2,3,4,1,11,12,3,4,5,6,7,8};
    assert(attack.match(2,3,1,11,12)==4&&attack.current(3,4,5,6,7,8));
    assert(!attack.match(2,3,2,11,12)&&!attack.match(2,3,1,11,13)&&!attack.match(2,3,1,12,12));
    assert(!attack.match(3,2,1,11,12));
    assert(!attack.current(4,4,5,6,7,8)&&!attack.current(3,5,5,6,7,8)&&!attack.current(3,4,6,6,7,8));
    assert(!attack.current(3,4,5,7,7,8)&&!attack.current(3,4,5,6,8,8)&&!attack.current(3,4,5,6,7,9));
    std::cout<<"PASS authenticated gameplay leases and active-hit depth/world/authority/connection/life fences\n";
}
