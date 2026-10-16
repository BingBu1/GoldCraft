// Includes the actual publisher, HostEffects and provider/registry; engine,
// Endpoint delivery and QPC are explicit boundary substitutes. This fixture
// proves no ReHLDS/Metamod/Pawn execution or live Minecraft physics acceptance.
#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "goldcraft/effect_grant.hpp"
#include "../../native/server/effect_service_provider.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace goldcraft;
using goldcraft::amxx_effects::ServiceStatus;
constexpr int FL_KILLME = 1, DEAD_NO = 0, TERRORIST = 1, CT = 2, at_console = 1;
struct CBasePlayer { int m_iTeam = CT; } player;
struct NativeVec { float x = 0, y = 0, z = 0; };
struct Entvars { NativeVec origin{64, -96, 164}, mins{0, 0, -36}; int flags = 0, deadflag = DEAD_NO, iuser1 = 0; float health = 100; };
struct Edict { bool free = false; Entvars v; CBasePlayer* object = &player; } entities[3];
Edict* edicts = entities;
int max_clients = 1;
std::array<std::uint32_t, 65> player_lives{};
struct Control { bool active = false; float last_update = 100; };
std::array<Control, 65> controlled{};
std::array<bool, 65> minecraft_forms{};
struct Globals { float time = 100; } globals;
Globals* gpGlobals = &globals;
struct Link {
    bool online = true, negotiated = true, accept = true;
    std::uint64_t current = 9, frequency = 1000000;
    std::vector<effects::Grant> attempts, queued;
    bool connected() const noexcept { return online; }
    std::uint64_t generation() const noexcept { return current; }
    bool effect_position_hold() const noexcept { return negotiated; }
    std::uint64_t local_qpc_frequency() const noexcept { return frequency; }
    bool send(Type type, std::span<const std::uint8_t> bytes) {
        if (type != Type::effect_grant) throw ProtocolError("unexpected fixture delivery");
        attempts.push_back(effects::decode_grant(bytes));
        if (!accept) return false;
        queued.push_back(attempts.back()); return true;
    }
} link;
std::uint64_t connection_generation = 9;
struct PairDirectory {
    struct Binding { std::uint32_t serial = 10; bool paired = true; Key uuid{1}; } binding;
    std::uint64_t epoch = 7;
    std::uint64_t world() const noexcept { return epoch; }
    const Binding* get(unsigned slot) const noexcept { return slot == 1 ? &binding : nullptr; }
} registry;
void ALERT(int, const char*, ...) {}
#define GET_PRIVATE(entity) ((entity)->object)
#undef EXPOSE_SINGLE_INTERFACE_GLOBALVAR
#define EXPOSE_SINGLE_INTERFACE_GLOBALVAR(...)
#include "../../native/server/effect_host.inc"
#include "../../native/server/effect_publisher.inc"
#undef GET_PRIVATE
#undef EXPOSE_SINGLE_INTERFACE_GLOBALVAR

effect_clock::RawSample raw{100000, 1000000};
bool clock_ok = true;
bool read_clock(effect_clock::RawSample& output) noexcept { output = raw; return clock_ok; }
int checks = 0, failures = 0;
void expect(bool value, const char* expression, int line) {
    ++checks;
    if (!value) { ++failures; std::cerr << line << ": " << expression << '\n'; }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

effects::PoseProof proof(const effects::Grant& g) {
    return {g.serviceGeneration, g.nativeConnection, g.stream, g.publication};
}
effects::Reopen reopen(const effects::Grant& g) {
    const auto& t = g.constraints.target;
    return {t.world, static_cast<std::uint32_t>(t.key), t.serial, t.life,
            g.minecraftUuid, g.serviceGeneration, g.nativeConnection, g.stream};
}
std::array<float, 3> placement(const effects::Grant& g) {
    auto result = g.nativeFeet;
    for (unsigned axis = 0; axis < result.size(); ++axis)
        if (g.constraints.movement.holdAxes & (std::uint32_t{1} << axis)) result[axis] = g.nativeAnchorFeet[axis];
    return result;
}
struct Fixture {
    HostEffects host{read_clock};
    EffectPublisher publisher;
    amxx_effects::ServiceContext context;
    effects::Target target;
    std::uint64_t owner = 0, effect = 0;
    explicit Fixture(std::uint64_t firstStream = 1, std::uint64_t firstPublication = 1)
        : publisher(host, firstStream, firstPublication) {
        raw = {100000, 1000000}; clock_ok = true;
        max_clients = 1; edicts = entities; entities[1] = {}; player = {};
        registry = {}; link = {}; connection_generation = 9;
        player_lives = {}; player_lives[1] = 2;
        minecraft_forms = {}; minecraft_forms[1] = true;
        controlled = {}; globals = {}; gpGlobals = &globals;
        host.Begin(7); host.Connected(1); host.Spawn(1);
        CHECK(host.GetContext(&context) == ServiceStatus::ok);
        CHECK(host.ResolveTarget(context, effects::TargetKind::player, 1, now(), &target) == ServiceStatus::ok);
        CHECK(host.CreateOwner(context, now(), &owner) == ServiceStatus::ok);
    }
    std::uint64_t now() const { return static_cast<std::uint64_t>(raw.counter / 1000); }
    void advance(std::uint64_t ms) { raw.counter += static_cast<std::int64_t>(ms * 1000); globals.time += static_cast<float>(ms) / 1000.0f; }
    void apply(effects::MovementState movement, std::uint32_t duration = 5000) {
        CHECK(host.Apply(context, owner, target, movement, duration, now(), &effect) == ServiceStatus::ok);
    }
    void update(effects::MovementState movement, std::uint32_t duration = 5000) {
        CHECK(host.Update(context, owner, effect, movement, duration, now()) == ServiceStatus::ok);
    }
    effects::Grant publish() {
        const auto count = link.queued.size(); publisher.Frame();
        CHECK(link.queued.size() == count + 1);
        return link.queued.empty() ? effects::Grant{} : link.queued.back();
    }
    bool admit(const effects::Grant& grant, std::array<float, 3> submitted) {
        return publisher.Admit(1, proof(grant), submitted);
    }
    // Simulate only the caller's immediately following state commit. Tests of
    // the real HandlePose commit boundary are still required in ReHLDS.
    bool commit(const effects::Grant& grant, std::array<float, 3> submitted) {
        if (!admit(grant, submitted)) return false;
        controlled[1].active = true; controlled[1].last_update = globals.time;
        entities[1].v.origin = {submitted[0], submitted[1], submitted[2] - entities[1].v.mins.z};
        return true;
    }
};
const effects::MovementState hold_x{effects::position_hold, 0, effects::hold_x, 0, 1};
const effects::MovementState hold_y{effects::position_hold, 0, effects::hold_y, 0, 1};

void publication_does_not_acquire_and_old_live_proof_is_accepted() {
    Fixture f; f.apply(hold_x);
    const float last = controlled[1].last_update;
    const auto first = f.publish();
    CHECK(first.operation == effects::GrantOperation::open && first.bootstrap);
    CHECK(!controlled[1].active && controlled[1].last_update == last);
    CHECK(first.deadlineMs > first.issuedAtMs && first.deadlineMs - first.issuedAtMs <= 750);
    auto wrong = placement(first); wrong[0] += 1;
    CHECK(!f.admit(first, wrong));
    CHECK(!controlled[1].active && controlled[1].last_update == last);
    CHECK(f.commit(first, placement(first)));
    f.advance(100); const auto second = f.publish();
    CHECK(second.operation == effects::GrantOperation::update && !second.bootstrap);
    CHECK(second.stream == first.stream && second.publication > first.publication);
    auto next = placement(first); next[1] += 12; next[2] += 3;
    CHECK(f.commit(first, next)); // Old OPEN proof after acquire, unheld axes move.
    CHECK(f.commit(second, next));
}

void failed_queue_never_mints_an_admissible_publication() {
    Fixture f; f.apply(hold_x);
    link.accept = false; f.publisher.Frame();
    CHECK(link.queued.empty() && link.attempts.size() == 1);
    const auto failed = link.attempts.back();
    CHECK(!f.admit(failed, placement(failed)));
    link.accept = true; const auto sent = f.publish();
    CHECK(sent.publication > failed.publication);
    CHECK(!f.admit(failed, placement(failed)));
    CHECK(f.commit(sent, placement(sent)));
    f.advance(100); link.accept = false; f.publisher.Frame();
    const auto failed_renewal = link.attempts.back();
    CHECK(!f.admit(failed_renewal, placement(failed_renewal)));
    CHECK(f.commit(sent, placement(sent))); // Failed queue did not discard history.
}

void bootstrap_checks_both_submitted_and_current_unheld_axes() {
    for (bool move_native : {false, true}) {
        Fixture f; f.apply(hold_x);
        const auto first = f.publish(); auto submitted = placement(first);
        if (move_native) entities[1].v.origin.y += 2;
        else submitted[1] += 2;
        const float last = controlled[1].last_update;
        CHECK(!f.admit(first, submitted));
        CHECK(!controlled[1].active && controlled[1].last_update == last);
        CHECK(!f.admit(first, placement(first))); // Mismatch retired that stream.
        const auto replacement = f.publish();
        CHECK(replacement.stream > first.stream);
        CHECK(f.commit(replacement, placement(replacement)));
        CHECK(!f.admit(first, placement(first)));
    }
    Fixture f; f.apply(hold_x);
    const auto first = f.publish();
    entities[1].v.origin.x += 10; // Held axis must return to captured native anchor.
    CHECK(f.commit(first, placement(first)));
    CHECK(entities[1].v.origin.x == first.nativeAnchorFeet[0]);
}

void semantic_changes_are_not_partial_capability_downgrades() {
    Fixture f; f.apply(hold_x);
    const auto first = f.publish(); CHECK(f.commit(first, placement(first)));
    f.advance(1); f.update(hold_y);
    CHECK(!f.admit(first, placement(first)));
    const auto changed = f.publish();
    CHECK(changed.stream == first.stream && changed.constraints.revision > first.constraints.revision);
    CHECK(f.commit(changed, placement(changed)));
    for (const auto unsupported : {
            effects::MovementState{effects::input_lock | effects::position_hold, effects::lock_attack, effects::hold_x, 0, 1},
            effects::MovementState{effects::wish_speed_cap, 0, 0, 50, 1},
            effects::MovementState{effects::gravity_factor, 0, 0, 0, 0.5f}}) {
        f.advance(1); f.update(unsupported);
        const auto count = link.queued.size(); f.publisher.Frame();
        CHECK(link.queued.size() == count);
        CHECK(!f.admit(changed, placement(changed)));
    }
    f.advance(1); f.update({}); const auto unrestricted = f.publish();
    CHECK(unrestricted.stream > changed.stream && unrestricted.constraints.movement.capability == 0);
    CHECK(f.commit(unrestricted, placement(unrestricted)));
}

void expiry_and_reopen_preserve_control_until_real_timeout() {
    Fixture f; f.apply(hold_x);
    const auto first = f.publish(); CHECK(f.commit(first, placement(first)));
    const float last = controlled[1].last_update;
    f.advance(first.deadlineMs - f.now());
    CHECK(!f.admit(first, placement(first)));
    CHECK(controlled[1].active && controlled[1].last_update == last);
    const auto reopened = f.publish();
    CHECK(reopened.stream > first.stream && reopened.operation == effects::GrantOperation::open && !reopened.bootstrap);
    CHECK(controlled[1].active && controlled[1].last_update == last);
    f.publisher.Reopen(reopen(first));
    CHECK(f.commit(reopened, placement(reopened))); // Old request cannot retire replacement.
    auto wrong = reopen(reopened); ++wrong.serial; f.publisher.Reopen(wrong);
    CHECK(f.commit(reopened, placement(reopened)));
    f.publisher.Reopen(reopen(reopened));
    const float reopened_last = controlled[1].last_update;
    CHECK(controlled[1].active && controlled[1].last_update == reopened_last);
    CHECK(!f.admit(reopened, placement(reopened)));
    const auto next = f.publish(); CHECK(next.stream > reopened.stream);
    CHECK(f.commit(next, placement(next)));
}

void old_expired_entry_does_not_retire_a_live_renewed_stream() {
    Fixture f; f.apply(hold_x);
    const auto first = f.publish(); CHECK(f.commit(first, placement(first)));
    f.advance(500); const auto second = f.publish();
    CHECK(f.commit(second, placement(second)));
    f.advance(first.deadlineMs - f.now());
    CHECK(!f.admit(first, placement(first)));
    CHECK(f.commit(second, placement(second)));
}

void lifecycle_identity_and_clock_are_reread_at_admission() {
    for (unsigned change = 0; change < 13; ++change) {
        Fixture f; f.apply(hold_x); const auto grant = f.publish();
        switch (change) {
        case 0: ++player_lives[1]; break;
        case 1: ++registry.binding.serial; break;
        case 2: ++registry.binding.uuid[0]; break;
        case 3: registry.binding.paired = false; break;
        case 4: minecraft_forms[1] = false; break;
        case 5: entities[1].v.health = 0; break;
        case 6: entities[1].v.deadflag = 1; break;
        case 7: entities[1].free = true; break;
        case 8: player.m_iTeam = 0; break;
        case 9: entities[1].v.iuser1 = 1; break;
        case 10: ++link.current; break;
        case 11: link.frequency = 0; break;
        case 12: clock_ok = false; break;
        }
        const float last = controlled[1].last_update;
        CHECK(!f.admit(grant, placement(grant)));
        CHECK(!controlled[1].active && controlled[1].last_update == last);
    }
    Fixture f; const auto grant = f.publish();
    auto wrong = proof(grant); ++wrong.publication;
    CHECK(!f.publisher.Admit(1, wrong, placement(grant)));
    wrong = proof(grant); ++wrong.nativeConnection;
    CHECK(!f.publisher.Admit(1, wrong, placement(grant)));
    CHECK(!f.publisher.Admit(1, {}, placement(grant)));
    CHECK(f.commit(grant, placement(grant)));
}

void channel_reset_and_counter_exhaustion_never_reuse_streams() {
    Fixture f; const auto first = f.publish();
    f.publisher.Reset(); const auto second = f.publish(); CHECK(second.stream > first.stream);
    link.frequency = 0; f.publisher.Frame(); CHECK(!f.admit(second, placement(second)));
    link.frequency = 1000000; const auto third = f.publish(); CHECK(third.stream > second.stream);
    link.negotiated = false; const auto count = link.queued.size(); f.publisher.Frame(); CHECK(link.queued.size() == count);
    link.negotiated = true; ++link.current; connection_generation = link.current;
    const auto fourth = f.publish(); CHECK(fourth.stream > third.stream);
    CHECK(fourth.nativeConnection != third.nativeConnection);
    CHECK(!f.admit(third, placement(third)));
    {
        Fixture exhausted(static_cast<std::uint64_t>(INT64_MAX));
        const auto final = exhausted.publish(); CHECK(final.stream == static_cast<std::uint64_t>(INT64_MAX));
        exhausted.publisher.Retire(1); const auto before = link.queued.size(); exhausted.publisher.Frame();
        CHECK(link.queued.size() == before && !exhausted.admit(final, placement(final)));
    }
    {
        Fixture exhausted(1, static_cast<std::uint64_t>(INT64_MAX));
        const auto final = exhausted.publish(); CHECK(final.publication == static_cast<std::uint64_t>(INT64_MAX));
        exhausted.advance(100); const auto before = link.queued.size(); exhausted.publisher.Frame();
        CHECK(link.queued.size() == before && !exhausted.admit(final, placement(final)));
    }
}

void normal_pose_timeout_still_requires_actual_release() {
    Fixture f; f.apply(hold_x);
    const auto first = f.publish(); CHECK(f.commit(first, placement(first)));
    const float last = controlled[1].last_update;
    f.advance(1001);
    CHECK(!f.admit(first, placement(first)));
    const auto count = link.queued.size(); f.publisher.Frame();
    CHECK(link.queued.size() == count);
    CHECK(controlled[1].active && controlled[1].last_update == last);
    // The real bridge's unchanged >1 s timeout releases control. Publishing
    // cannot impersonate that release or refresh the native movement lease.
    controlled[1].active = false; f.publisher.Retire(1);
    const auto next = f.publish();
    CHECK(next.stream > first.stream && next.bootstrap);
    CHECK(f.commit(next, placement(next)));
}

void admitted_relocation_preserves_holds_bounds_and_lifecycle() {
    for (std::uint32_t axes = 0; axes < 8; ++axes) {
        Fixture f;
        if (axes) f.apply({effects::position_hold, 0, axes, 0, 1});
        const auto first = f.publish();
        CHECK(f.commit(first, placement(first)));
        f.advance(100); const auto renewal = f.publish();
        CHECK(renewal.operation == effects::GrantOperation::update);
        auto destination = placement(renewal);
        for (unsigned axis = 0; axis < destination.size(); ++axis)
            if (!(axes & (std::uint32_t{1} << axis))) destination[axis] += 4096.0f;
        CHECK(f.commit(renewal, destination));
        CHECK(entities[1].v.origin.x == destination[0]);
        CHECK(entities[1].v.origin.y == destination[1]);
        CHECK(entities[1].v.origin.z == destination[2] + 36);
        const auto committed = entities[1].v.origin;
        const auto last = controlled[1].last_update;
        for (unsigned axis = 0; axis < destination.size(); ++axis) {
            for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(), 16385.0f, -16385.0f}) {
                auto wrong = destination; wrong[axis] = invalid;
                CHECK(!f.commit(renewal, wrong));
            }
            if (axes & (std::uint32_t{1} << axis)) {
                auto wrong = destination; wrong[axis] += 1;
                CHECK(!f.commit(renewal, wrong));
            }
        }
        CHECK(entities[1].v.origin.x == committed.x && entities[1].v.origin.y == committed.y &&
              entities[1].v.origin.z == committed.z && controlled[1].last_update == last);
        CHECK(f.commit(renewal, destination));
        f.advance(renewal.deadlineMs - f.now());
        CHECK(!f.commit(renewal, destination));
    }
    for (unsigned change = 0; change < 6; ++change) {
        Fixture f; const auto first = f.publish(); CHECK(f.commit(first, placement(first)));
        f.advance(100); const auto renewal = f.publish();
        auto destination = placement(renewal); destination[0] += 4096;
        switch (change) {
        case 0: ++player_lives[1]; break;
        case 1: ++registry.binding.serial; break;
        case 2: ++registry.binding.uuid[0]; break;
        case 3: minecraft_forms[1] = false; break;
        case 4: ++registry.epoch; break;
        case 5: ++link.current; break;
        }
        CHECK(!f.commit(renewal, destination));
    }
}
} // namespace

int main() {
    publication_does_not_acquire_and_old_live_proof_is_accepted();
    failed_queue_never_mints_an_admissible_publication();
    bootstrap_checks_both_submitted_and_current_unheld_axes();
    semantic_changes_are_not_partial_capability_downgrades();
    expiry_and_reopen_preserve_control_until_real_timeout();
    old_expired_entry_does_not_retire_a_live_renewed_stream();
    lifecycle_identity_and_clock_are_reread_at_admission();
    channel_reset_and_counter_exhaustion_never_reuse_streams();
    normal_pose_timeout_still_requires_actual_release();
    admitted_relocation_preserves_holds_bounds_and_lifecycle();
    std::cout << "actual effect publisher with boundary substitutes: " << checks << " assertions, "
              << failures << " failures; no ReHLDS/Pawn/live-Minecraft acceptance\n";
    return failures ? 1 : 0;
}
