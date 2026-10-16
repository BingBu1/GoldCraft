#include "goldcraft/status_attributes.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace goldcraft;
static Bytes packet(float speed=1,float resistance=1,float absorption=4){
    Writer w;w.u64(1);w.u64(2);w.u64(0);w.u64(8);
    w.u32(1);w.u32(3);w.u32(5);w.u32(6);w.u32(1);
    w.f32(speed);w.f32(resistance);w.f32(absorption);w.f32(336);return w.data;
}
int main(){
    int checks=0;
    auto check=[&](bool value){if(!value)throw std::runtime_error("check "+std::to_string(checks+1));++checks;};
    auto reject=[&](const Bytes& b){try{read_status_attributes(b);}catch(const ProtocolError&){check(true);return;}check(false);};
    auto bytes=packet(0.85f,0.8f,4);auto state=read_status_attributes(bytes);
    check(bytes.size()==68&&state.speed==0.85f&&state.resistance==0.8f&&state.absorption==4);
    for(std::size_t n=0;n<bytes.size();++n)reject(Bytes(bytes.begin(),bytes.begin()+n));
    bytes.push_back(0);reject(bytes);
    reject(packet(-1));reject(packet(17));reject(packet(1,-0.1f));reject(packet(1,1.1f));reject(packet(1,1,-1));
    bytes=packet();bytes[55]=0x7f;bytes[54]=0xc0;reject(bytes); // NaN speed
    StatusBudget budget;check(budget.update(state));check(budget.absorption()==4);
    budget.commit(3,0);check(budget.absorption()==1);
    state.revision++;check(budget.update(state)&&budget.absorption()==1); // old pool cannot refill
    budget.commit(1,0);check(budget.absorption()==0);
    state.revision++;state.ack=1;state.absorption=1;
    check(budget.update(state)&&budget.absorption()==0&&budget.pending.size()==1);
    state.revision++;state.ack=2;state.absorption=0;
    check(budget.update(state)&&budget.absorption()==0&&budget.pending.empty());
    check(!budget.update(state)); // repeat
    state.revision++;state.ack=3;check(!budget.update(state)); // never committed
    state.ack=1;check(!budget.update(state)); // regressing acknowledgement
    state.ack=2;state.absorption=4;state.life++;
    check(budget.update(state)&&budget.absorption()==4); // actual reapplication, same spawn
    budget.commit(0,330);check(budget.durability()==6);
    state.revision++;check(budget.update(state)&&budget.durability()==6);
    budget.commit(0,7);check(budget.durability()==0); // last hit breaks; next hit not blocked
    state.revision++;state.shield=9;state.durability=100;
    check(budget.update(state)&&budget.durability()==100); // old item cannot consume replacement
    state.revision++;state.ack=4;state.durability=98;
    check(budget.update(state)&&budget.pending.empty()&&budget.durability()==98);
    for(int i=0;i<128;++i)budget.commit(0,1);
    check(!budget.available());
    check(known_type(Type::status_attributes)&&known_type(Type::status_spend)
        &&static_cast<unsigned>(Type::status_attributes)<wire_type_count
        &&static_cast<unsigned>(Type::status_spend)<wire_type_count);
    Key session{},token{};HostHandshake server(session,token,Role::host_server,Role::fabric_server,9);
    Writer hello;hello.bytes(session);hello.bytes(token);hello.u32(4);hello.u32(capability_status_attributes);
    server.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);
    auto welcome=server.welcome();Reader r(welcome);r.bytes(16);check(r.u32()==3&&r.u32()==16);
    HostHandshake client(session,token,Role::host_client,Role::fabric_client,9);
    hello.data.clear();hello.bytes(session);hello.bytes(token);hello.u32(2);hello.u32(capability_status_attributes);
    bool refused=false;try{client.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);}catch(const ProtocolError&){refused=true;}
    check(refused);
    std::cout<<checks<<" status attribute/consumption checks passed\n";
}
