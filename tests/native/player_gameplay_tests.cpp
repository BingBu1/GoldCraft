#include "goldcraft/player_gameplay.hpp"
#include <cassert>
#include <iostream>
using namespace goldcraft;
Bytes packet(PlayerGameplay s){Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(s.slot);w.u32(s.serial);w.u32(s.spawn);w.u32(s.life);w.bytes(s.uuid);w.u32(s.flags);return w.data;}
int main(){
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
    ActivePlayerAttack attack{2,3,4,1,11,12,3,4,5,6,7,8};
    assert(attack.match(2,3,1,11,12)==4&&attack.current(3,4,5,6,7,8));
    assert(!attack.match(2,3,2,11,12)&&!attack.match(2,3,1,11,13)&&!attack.match(2,3,1,12,12));
    assert(!attack.match(3,2,1,11,12));
    assert(!attack.current(4,4,5,6,7,8)&&!attack.current(3,5,5,6,7,8)&&!attack.current(3,4,6,6,7,8));
    assert(!attack.current(3,4,5,7,7,8)&&!attack.current(3,4,5,6,8,8)&&!attack.current(3,4,5,6,7,9));
    std::cout<<"PASS authenticated gameplay leases and active-hit depth/world/authority/connection/life fences\n";
}
