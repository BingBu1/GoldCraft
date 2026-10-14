#include "goldcraft/gameplay_rules.hpp"
#include <cassert>

int main() {
    using namespace goldcraft;
    GameplayRules policy;policy.reset(19);
    assert(!policy.clear_blocks&&policy.drop_items&&policy.clear_revision==0);
    policy.observe_round(true);policy.cleanup_round();
    assert(policy.clear_revision==0);
    policy.configure(true,false);
    policy.observe_round(true);const auto revision=policy.revision;
    policy.observe_round(true);policy.cleanup_round();
    assert(policy.clear_revision==1&&policy.revision==revision);
    // The next round may terminate before another frame observes false.
    policy.observe_round(true);assert(policy.clear_revision==2);
    policy.cleanup_round();policy.cleanup_round(); // Direct restart without a terminating frame.
    assert(policy.clear_revision==3);
    policy.configure(false,true);policy.observe_round(true);
    policy.configure(true,true);policy.observe_round(true);
    assert(policy.clear_revision==3); // Enabling midway through an ended round is not another end.
    auto payload=policy.encode();Reader r(payload);
    assert(r.u64()==19&&r.u64()==policy.revision&&r.u64()==3&&r.u32()==1&&r.u32()==1);r.finish();
    policy.reset(20);assert(policy.world==20&&policy.clear_revision==0&&policy.revision==1);
    const Key key{};
    HostHandshake host(key,key,Role::host_server,Role::fabric_server,33);
    Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(4);hello.u32(capability_gameplay_rules);
    host.accept({Type::hello,static_cast<std::uint32_t>(hello.data.size()),0,1},hello.data);
    auto welcome=host.welcome();Reader w(welcome);w.key();assert(w.u32()==3&&w.u32()==128);
    auto encoded=encode_frame(Type::gameplay_rules,33,2,payload);
    assert(decode_header(std::span(encoded).first(header_bytes)).type==Type::gameplay_rules);
}
