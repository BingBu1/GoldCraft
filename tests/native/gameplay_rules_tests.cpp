#include "goldcraft/gameplay_rules.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>

// Keep assertion failures visible to CTest instead of a Windows CRT dialog.
#undef assert
#define assert(condition) do { if(!(condition)) { std::fprintf(stderr,"gameplay rules assertion at line %d: %s\n",__LINE__,#condition);std::abort(); } } while(false)

// Keep expected protocol exceptions in one boundary without payload ownership.
// In the pinned optimized x86 build, the inlined rejection loop faults in
// Writer cleanup; an isolated exception boundary also passes with ThinLTO.
__declspec(noinline) bool accepts(goldcraft::HostHandshake& host,const goldcraft::Bytes& payload) {
    try {host.accept({goldcraft::Type::hello,static_cast<std::uint32_t>(payload.size()),0,1},payload);return true;}
    catch(const goldcraft::ProtocolError&){return false;}
}

void test_round_start_policy() {
    using namespace goldcraft;
    GameplayRules policy;policy.reset(19);
    assert(!policy.clear_blocks&&policy.drop_items&&policy.clear_revision==0);
    policy.cleanup_round();
    assert(policy.clear_revision==0);
    policy.configure(true,false);
    const auto revision=policy.revision;
    assert(policy.clear_revision==0); // Configuration alone never clears the completed round.
    policy.cleanup_round();
    assert(policy.clear_revision==1&&policy.revision==revision+1);
    // Every actual new round counts, even with no intervening active frame.
    policy.cleanup_round();policy.cleanup_round(); // Direct restart without a terminating frame.
    assert(policy.clear_revision==3);
    policy.configure(false,true);policy.cleanup_round();
    policy.configure(true,true);
    assert(policy.clear_revision==3); // Enabling during the end delay waits for actual cleanup.
    auto payload=policy.encode();Reader r(payload);
    assert(r.u64()==19&&r.u64()==policy.revision&&r.u64()==3&&r.u32()==1&&r.u32()==1);r.finish();
    policy.reset(20);assert(policy.world==20&&policy.clear_revision==0&&policy.revision==1);
    policy.configure(true,false);policy.cleanup_round();assert(policy.clear_revision==1&&!policy.drop_items);
    policy.reset(0);policy.configure(true,false);policy.cleanup_round();assert(policy.clear_revision==0);
}

void test_handshake() {
    using namespace goldcraft;
    GameplayRules policy;policy.reset(19);policy.configure(true,false);policy.cleanup_round();
    const auto payload=policy.encode();
    const Key key{};
    HostHandshake host(key,key,Role::host_server,Role::fabric_server,33);
    Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(4);hello.u32(capability_gameplay_rules);
    host.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);
    auto welcome=host.welcome();Reader w(welcome);w.key();assert(w.u32()==3&&w.u32()==128);
    auto encoded=encode_frame(Type::gameplay_rules,33,2,payload);
    assert(decode_header(std::span(encoded).first(header_bytes)).type==Type::gameplay_rules);

    // All server features must survive one combined handshake. Native armor
    // still requires status attributes, and no server-only bit is a client cap.
    constexpr auto server_caps=capability_effect_position_hold|capability_status_effects|
        capability_status_attributes|capability_fluid_volumes|capability_native_armor|capability_gameplay_rules;
    for(auto caps:{server_caps|capability_local_qpc_v1,
                   (server_caps|capability_local_qpc_v1)&~capability_status_attributes,
                   server_caps}) {
        HostHandshake combined(key,key,Role::host_server,Role::fabric_server,34);
        Writer request;request.bytes(key);request.bytes(key);request.u32(4);request.u32(caps);
        const bool accepted=accepts(combined,request.data);
        assert(accepted==(caps==(server_caps|capability_local_qpc_v1)));
        if(accepted){auto response=combined.welcome();Reader reply(response);reply.key();
            assert(reply.u32()==3&&reply.u32()==server_caps);}
    }
    // Bit 3 is now explicitly scoped as HSV on client links; other server bits stay forbidden.
    for(auto cap:{capability_status_attributes,capability_fluid_volumes,
                  capability_native_armor,capability_gameplay_rules,server_caps}) {
        HostHandshake client(key,key,Role::host_client,Role::fabric_client,35);
        Writer request;request.bytes(key);request.bytes(key);request.u32(2);request.u32(cap);
        assert(!accepts(client,request.data));
    }

    // Keep every original rules/handshake assertion above. The current85 TNT
    // capability must also coexist with attributes, fluids and native armor.
    constexpr auto combined_caps=server_caps|capability_host_explosions;
    struct CombinedCase {std::uint32_t caps,role;bool bad_token,accepted;};
    for(const auto test:{
        CombinedCase{combined_caps|capability_local_qpc_v1,4,false,true},
        CombinedCase{(combined_caps|capability_local_qpc_v1)&~capability_status_attributes,4,false,false},
        CombinedCase{combined_caps,4,false,false}, // effect hold still requires QPC
        CombinedCase{combined_caps|capability_local_qpc_v1|(1u<<31),4,false,false},
        CombinedCase{combined_caps|capability_local_qpc_v1,2,false,false},
        CombinedCase{combined_caps|capability_local_qpc_v1,4,true,false}}) {
        HostHandshake combined(key,key,Role::host_server,Role::fabric_server,36);
        auto token=key;if(test.bad_token)token[0]^=1;
        Writer request;request.bytes(key);request.bytes(token);request.u32(test.role);request.u32(test.caps);
        const bool accepted=accepts(combined,request.data);
        assert(accepted==test.accepted&&combined.ready()==test.accepted);
        if(accepted){
            assert(combined.peer_capabilities()==test.caps);
            auto response=combined.welcome();Reader reply(response);assert(reply.key()==key);
            assert(reply.u32()==3&&reply.u32()==combined_caps); // QPC proof bit is not echoed
            assert(reply.f32()==units_per_block&&reply.f32()==minecraft_y_offset);reply.finish();
        }else assert(combined.peer_capabilities()==0);
    }
    // TNT alone and the full server union remain forbidden on a client link.
    for(auto caps:{capability_host_explosions,combined_caps}) {
        HostHandshake client(key,key,Role::host_client,Role::fabric_client,37);
        Writer request;request.bytes(key);request.bytes(key);request.u32(2);request.u32(caps);
        assert(!accepts(client,request.data)&&!client.ready()&&client.peer_capabilities()==0);
    }
    // Retain a valid client capability as the positive control for role checks.
    HostHandshake shared_client(key,key,Role::host_client,Role::fabric_client,38);
    Writer shared_hello;shared_hello.bytes(key);shared_hello.bytes(key);shared_hello.u32(2);shared_hello.u32(capability_shared_bulk_v1);
    shared_client.accept({Type::hello,static_cast<std::uint32_t>(shared_hello.data.size()),0,1},shared_hello.data);
    assert(shared_client.ready()&&shared_client.peer_capabilities()==capability_shared_bulk_v1);
    auto shared_welcome=shared_client.welcome();Reader shared_reply(shared_welcome);assert(shared_reply.key()==key);
    assert(shared_reply.u32()==1&&shared_reply.u32()==0);
    assert(shared_reply.f32()==units_per_block&&shared_reply.f32()==minecraft_y_offset);shared_reply.finish();
}

int main() {
    test_round_start_policy();
    test_handshake();
    std::puts("Gameplay rules: round-start policy and server/client handshake cases passed");
}
