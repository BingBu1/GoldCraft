#include "goldcraft/effect_grant.hpp"
#include <cassert>
#include <bit>
#include <iostream>

using namespace goldcraft;
using namespace goldcraft::effects;
int main() {
    Grant g;
    g.serviceGeneration=2; g.nativeConnection=3;
    g.constraints={{1,TargetKind::player,4,5,6},7,{position_hold,0,hold_x|hold_z,0,1}};
    for(unsigned i=0;i<16;++i)g.minecraftUuid[i]=static_cast<std::uint8_t>(i+1);
    g.stream=8;g.publication=9;g.frequency=10000000;g.issuedAtMs=1000;g.deadlineMs=1750;g.bootstrap=true;
    g.nativeFeet={32,-64,96};g.nativeAnchorFeet={16,-64,80};
    const Bytes bytes=encode_grant(g);
    assert(bytes.size()==164 && decode_grant(bytes)==g);
    // Golden bytes follow the independently packed schema, not the codec implementation.
    const Bytes golden={1,0,0,0,1,0,0,0,2,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,4,0,0,0,0,0,0,0,5,0,0,0,6,0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,8,0,0,0,0,0,0,0,9,0,0,0,0,0,0,0,7,0,0,0,0,0,0,0,128,150,152,0,0,0,0,0,232,3,0,0,0,0,0,0,214,6,0,0,0,0,0,0,1,0,0,0,0,0,0,66,0,0,128,194,0,0,192,66,0,0,128,65,0,0,128,194,0,0,160,66,2,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,128,63};
    assert(bytes==golden);
    auto rejects=[](Bytes b){try{(void)decode_grant(b);return false;}catch(const ProtocolError&){return true;}};
    for(std::size_t n=0;n<bytes.size();++n)assert(rejects(Bytes(bytes.begin(),bytes.begin()+n)));
    Bytes changed=bytes;changed.push_back(0);assert(rejects(changed));
    auto overwrite=[&](std::size_t offset,std::uint64_t value,unsigned count){
        auto b=bytes;for(unsigned i=0;i<count;++i)b[offset+i]=static_cast<std::uint8_t>(value>>(8*i));return b;
    };
    // Independent schema offsets, including signed clock bounds and canonical movement fields.
    for(const auto offset:{0u,4u,8u,16u,24u,36u,44u,48u,68u,76u,84u,92u})assert(rejects(overwrite(offset,0,4)));
    assert(rejects(overwrite(32,2,4))); // Object grants are not player execution grants.
    assert(rejects(overwrite(36,65,8)));
    assert(rejects(overwrite(116,2,4)));
    assert(rejects(overwrite(108,1000,8))); // No zero or negative lifetime.
    assert(rejects(overwrite(108,61001,8)));
    assert(rejects(overwrite(92,UINT64_MAX,8)));
    assert(rejects(overwrite(120,0x7fc00000u,4)));
    assert(rejects(overwrite(120,std::bit_cast<std::uint32_t>(16385.0f),4)));
    assert(rejects(overwrite(144,16,4)));
    assert(rejects(overwrite(148,1,4)));
    assert(rejects(overwrite(152,8,4)));
    assert(rejects(overwrite(156,std::bit_cast<std::uint32_t>(1.0f),4)));
    assert(rejects(overwrite(160,0,4)));
    Writer pose;pose.u32(grant_schema);for(auto v:{2ULL,3ULL,8ULL,9ULL})pose.u64(v);
    pose.data.resize(effect_pose_bytes);
    Reader r(pose.data);auto proof=decode_pose_proof(r);
    assert(proof.serviceGeneration==2&&proof.nativeConnection==3&&proof.stream==8&&proof.publication==9&&r.remaining()==92);
    pose.data[4]=0;bool rejected=false;
    try{Reader p(pose.data);(void)decode_pose_proof(p);}catch(const ProtocolError&){rejected=true;}
    assert(rejected);
    for(std::size_t i=4;i<36;++i)pose.data[i]=0;
    Reader empty(pose.data);assert(decode_pose_proof(empty).empty()&&empty.remaining()==92);
    Writer reopen;reopen.u32(1);reopen.u64(1);reopen.u32(4);reopen.u32(5);reopen.u32(6);
    reopen.bytes(g.minecraftUuid);reopen.u64(2);reopen.u64(3);reopen.u64(8);
    assert(reopen.data.size()==64);Reader rr(reopen.data);const auto decoded=decode_reopen(rr);
    assert(decoded.world==1&&decoded.slot==4&&decoded.stream==8&&decoded.minecraftUuid==g.minecraftUuid);
    // Server effect bit requires clock bit; client role must never negotiate it.
    auto hello=[&](Role peer,std::uint32_t caps){Writer h;h.bytes(Key{});h.bytes(Key{});h.u32(static_cast<std::uint32_t>(peer));h.u32(caps);return h.data;};
    for(const auto caps:{0u,capability_local_qpc_v1,capability_local_qpc_v1|capability_effect_position_hold}){
        HostHandshake hs(Key{},Key{},Role::host_server,Role::fabric_server,17);
        const auto h=hello(Role::fabric_server,caps);hs.accept({Type::hello,static_cast<std::uint32_t>(h.size()),0,1},h);
        const auto welcome=hs.welcome();Reader wr(welcome);wr.bytes(20);
        assert(wr.u32()==(caps&capability_effect_position_hold));
    }
    for(const auto peer:{Role::fabric_server,Role::fabric_client}){
        HostHandshake hs(Key{},Key{},peer==Role::fabric_server?Role::host_server:Role::host_client,peer,17);
        const auto h=hello(peer,capability_effect_position_hold);bool bad=false;
        try{hs.accept({Type::hello,static_cast<std::uint32_t>(h.size()),0,1},h);}catch(const ProtocolError&){bad=true;}
        assert(bad);
    }
    std::cout<<"effect grant codec, proof and role negotiation checks passed\n";
}
