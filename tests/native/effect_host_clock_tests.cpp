// Exact engine adapter .inc with small engine authority/clock boundaries.
// No ReHLDS, Pawn callback, networking or physics is executed by this fixture.
#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "../../native/server/effect_service_provider.hpp"
#include <array>
#include <iostream>
#include <utility>
#include <type_traits>

// Keep the real provider/registry for mutations. This fixture-only facade can
// fail a read after successful registration, without adding production hooks.
namespace goldcraft::amxx_effects {
struct AnchorFixtureProvider {
    EffectServiceProvider real;
    static inline bool failReadAfterApply = false;
    static inline bool failRegistrationAfterPump = false;
    static inline unsigned readFailures = 0;
    static void ResetFaults() noexcept {
        failReadAfterApply = false;
        failRegistrationAfterPump = false;
        readFailures = 0;
    }
#define FORWARD_ANCHOR_PROVIDER(method) \
    template<class... Args> auto method(Args&&... args) noexcept { \
        return real.method(std::forward<Args>(args)...); \
    }
    FORWARD_ANCHOR_PROVIDER(BeginWorld)
    FORWARD_ANCHOR_PROVIDER(EndWorld)
    FORWARD_ANCHOR_PROVIDER(AbortWorld)
    FORWARD_ANCHOR_PROVIDER(ClockFault)
    FORWARD_ANCHOR_PROVIDER(ObserveClock)
    FORWARD_ANCHOR_PROVIDER(GetContext)
    FORWARD_ANCHOR_PROVIDER(InvalidateTarget)
    FORWARD_ANCHOR_PROVIDER(Pump)
    FORWARD_ANCHOR_PROVIDER(ReadLeasedSnapshot)
    FORWARD_ANCHOR_PROVIDER(CreateOwner)
    FORWARD_ANCHOR_PROVIDER(ResolveTarget)
    FORWARD_ANCHOR_PROVIDER(Update)
    FORWARD_ANCHOR_PROVIDER(Remove)
    FORWARD_ANCHOR_PROVIDER(RevokeOwner)
#undef FORWARD_ANCHOR_PROVIDER
    template<class... Args> auto Apply(Args&&... args) noexcept {
        const auto result = real.Apply(std::forward<Args>(args)...);
        if (result == ServiceStatus::ok && failReadAfterApply) {
            failReadAfterApply = false;
            ++readFailures;
        }
        return result;
    }
    ServiceStatus RegisterTarget(const ServiceContext& context, const effects::Target& target,
                                 std::uint64_t now) noexcept {
        if (failRegistrationAfterPump) {
            failRegistrationAfterPump = false;
            const auto result = real.Pump(context, now);
            return result == ServiceStatus::ok ? ServiceStatus::capacity_exhausted : result;
        }
        return real.RegisterTarget(context, target, now);
    }
    ServiceStatus ReadSnapshot(const ServiceContext& context, const effects::Target& target,
                               std::uint64_t now, effects::Snapshot* output) noexcept {
        if (readFailures) { --readFailures; return ServiceStatus::internal_error; }
        return real.ReadSnapshot(context, target, now, output);
    }
};
} // namespace goldcraft::amxx_effects


namespace {
using namespace goldcraft::amxx_effects;
namespace ec = goldcraft::effect_clock;
using Target = goldcraft::effects::Target;
constexpr int FL_KILLME = 1, DEAD_NO = 0, TERRORIST = 1, CT = 2, at_console = 1;
struct CBasePlayer { int m_iTeam = CT; } player;
struct NativeVec { float x = 0, y = 0, z = 0; };
struct Entvars { NativeVec origin{64, -96, 164}, mins{0, 0, -36}; int flags = 0, deadflag = DEAD_NO, iuser1 = 0; float health = 100; };
struct Edict { bool free = false; Entvars v; CBasePlayer* object = &player; } entities[3];
Edict* edicts = entities;
int max_clients = 1;
std::array<std::uint32_t, 65> player_lives{};
struct Control { bool active = true; float last_update = 100; };
std::array<Control, 65> controlled{};
std::array<bool, 65> minecraft_forms{};
struct Globals { float time = 100; } globals;
Globals* gpGlobals = &globals;
struct Link {
    bool online = true;
    std::uint64_t current = 9;
    bool connected() const noexcept { return online; }
    std::uint64_t generation() const noexcept { return current; }
} link;
std::uint64_t connection_generation = 9;
struct PairDirectory {
    struct Binding { std::uint32_t serial = 10; bool paired = true; std::array<std::uint8_t, 16> uuid{1}; } binding, secondBinding;
    std::uint64_t epoch = 7;
    std::uint64_t world() const noexcept { return epoch; }
    const Binding* get(unsigned slot) const noexcept { return slot == 1 ? &binding : slot == 2 ? &secondBinding : nullptr; }
} registry;
int logCalls = 0;
void ALERT(int, const char*, ...) { ++logCalls; }
#define GET_PRIVATE(entity) ((entity)->object)
#undef EXPOSE_SINGLE_INTERFACE_GLOBALVAR
#define EXPOSE_SINGLE_INTERFACE_GLOBALVAR(...)
#define EffectServiceProvider AnchorFixtureProvider
#include "../../native/server/effect_host.inc"
#undef EffectServiceProvider
#undef GET_PRIVATE
#undef EXPOSE_SINGLE_INTERFACE_GLOBALVAR

ec::RawSample raw{100000, 1000000};
bool clockOk = true;
std::int64_t clockStep = 0;
int clockReadCount = 0;
void (*clockReadHook)(int) noexcept = nullptr;
bool read_clock(ec::RawSample& output) noexcept { ++clockReadCount; if (clockReadHook) clockReadHook(clockReadCount);
    if (!clockOk) return false; output = raw; raw.counter += clockStep; return true; }
int checks = 0, failures = 0;
void expect(bool value, const char* expression, int line) {
    ++checks;
    if (!value) { ++failures; std::cerr << line << ": " << expression << '\n'; }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)
struct Fixture {
    HostEffects host{read_clock};
    ServiceContext context;
    Target target;
    std::uint64_t owner = 0, effect = 0;
    Fixture() {
        raw = {100000, 1000000}; clockOk = true; clockStep = 0; clockReadCount = 0; clockReadHook = nullptr;
        AnchorFixtureProvider::ResetFaults();
        max_clients = 1; registry.secondBinding = {};
        registry.binding = {}; link = {}; connection_generation = 9;
        minecraft_forms[1] = true; controlled[1] = {}; globals = {}; gpGlobals = &globals;
        registry.epoch = 7; registry.binding.serial = 10;
        player.m_iTeam = CT; entities[1] = {}; player_lives[1] = 2;
        host.Begin(7); host.Connected(1); host.Spawn(1);
        CHECK(host.GetContext(&context) == ServiceStatus::ok);
        CHECK(host.ResolveTarget(context, goldcraft::effects::TargetKind::player, 1, 100, &target) == ServiceStatus::ok);
        CHECK(host.CreateOwner(context, 100, &owner) == ServiceStatus::ok);
        CHECK(host.Apply(context, owner, target, {goldcraft::effects::wish_speed_cap, 0, 0, 80, 1},
                         10, 100, &effect) == ServiceStatus::ok);
    }
    void assert_closed() {
        ServiceContext untouched{88, 99};
        CHECK(host.GetContext(&untouched) == ServiceStatus::clock_failed);
        CHECK((untouched == ServiceContext{88, 99}));
        CHECK(host.Update(context, owner, effect, {}, 10, 0) == ServiceStatus::clock_failed);
        goldcraft::effects::Snapshot snapshot;
        CHECK(host.ReadSnapshot(1, &snapshot) == ServiceStatus::clock_failed);
        QualifiedLeasedSnapshot lease;
        CHECK(host.ReadLeasedSnapshot(1, 10, &lease) == ServiceStatus::clock_failed);
        clockOk = true; raw.counter = 200000;
        host.End(); registry.epoch = 8; host.Begin(8);
        CHECK(host.GetContext(&untouched) == ServiceStatus::clock_failed);
    }
};

void actual_host_qualifies_raw_duration() {
    Fixture f;
    QualifiedLeasedSnapshot lease;
    raw.counter = 107000;
    CHECK(f.host.ReadLeasedSnapshot(1, 100, &lease) == ServiceStatus::ok);
    CHECK(lease.native.remainingMs == 3 && lease.safeRemainingMs == 1 && lease.marginMs == 2);
    const auto before = lease;
    raw.counter = 108000;
    CHECK(f.host.ReadLeasedSnapshot(1, 100, &lease) == ServiceStatus::invalid_lease);
    CHECK(lease.native == before.native && lease.safeRemainingMs == before.safeRemainingMs);
    raw.counter = 110000;
    f.host.Frame(); // Independent host expiry, no AMXX call.
    goldcraft::effects::Snapshot snapshot;
    CHECK(f.host.ReadSnapshot(1, &snapshot) == ServiceStatus::ok && snapshot.movement == goldcraft::effects::MovementState{});
}

void actual_host_fault_boundaries() {
    for (int boundary = 0; boundary < 5; ++boundary) {
        Fixture f;
        const auto logBefore = logCalls;
        clockOk = false;
        switch (boundary) {
        case 0: f.host.Frame(); break;
        case 1: { goldcraft::effects::Snapshot value; CHECK(f.host.ReadSnapshot(1, &value) == ServiceStatus::clock_failed); break; }
        case 2: f.host.End(); break;
        case 3: clockOk = true; f.host.ClockFault(); break; // In-process adapter notification.
        case 4: clockOk = true; --raw.counter; f.host.Frame(); break; // Raw regression.
        }
        CHECK(logCalls == logBefore + 1);
        f.assert_closed();
        f.host.ClockFault();
        CHECK(logCalls == logBefore + 1); // No repeated frame/API log flood.
    }
}

void interleaved_adapter_host_clock_is_one_domain() {
    for (bool sameMillisecond : {false, true}) {
        Fixture f; // Host last raw = 100000 at 1MHz.
        const ec::RawSample adapterSample{sameMillisecond ? 100010 : 110000, 1000000};
        ec::Sample adapted;
        CHECK(ec::convert(adapterSample, adapted) == ec::Error::none);
        CHECK(f.host.ObserveClock(adapterSample) == ServiceStatus::ok);
        CHECK(f.host.ObserveClock(adapterSample) == ServiceStatus::ok); // Same raw is legitimate.
        std::uint64_t effect = 0;
        CHECK(f.host.Apply(f.context, f.owner, f.target, {}, 50, adapted.milliseconds, &effect) == ServiceStatus::ok);
        raw.counter = sameMillisecond ? 100005 : 105000;
        f.host.Frame(); // Was >= host-local 100000, but is < accepted adapter sample.
        f.assert_closed(); // Even later raw=200000 / new world cannot recover.
    }
    Fixture f;
    CHECK(f.host.ObserveClock({110000, 1000000}) == ServiceStatus::ok);
    raw.counter = 120000;
    f.host.Frame();
    ServiceContext context;
    CHECK(f.host.GetContext(&context) == ServiceStatus::ok && context == f.context);
    // A plain old caller timestamp isn't a new QPC observation. It is rejected
    // without declaring the real host clock failed.
    CHECK(f.host.Update(f.context, f.owner, f.effect, {}, 10, 119) == ServiceStatus::clock_regressed);
    CHECK(f.host.GetContext(&context) == ServiceStatus::ok);
}
HostEffects::ExecutionIdentity execution_identity(const Fixture& f) {
    return {f.context, f.target, registry.binding.uuid, connection_generation};
}

void coherent_execution_sample() {
    Fixture f;
    const auto expected = execution_identity(f);
    HostEffects::ExecutionSample sample;
    raw.counter = 105000;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
    CHECK(sample.identity == expected && sample.constraints.target == f.target);
    CHECK(sample.frequency == 1000000 && sample.issuedAtMs == 105 && sample.checkedAtMs == 105);
    CHECK(sample.deadlineMs == 108); // Raw expiry110, native conversion margin2.
    CHECK((sample.nativeFeet == std::array<float, 3>{64, -96, 128}));
    CHECK(sample.constraints.movement.wishSpeedCap == 80);
    const auto before = sample;
    auto stale = expected; ++stale.context.generation;
    CHECK(f.host.ReadExecutionSample(stale, 100, &sample) == ServiceStatus::stale_context);
    CHECK(sample == before);
    stale = expected; ++stale.target.life;
    CHECK(f.host.ReadExecutionSample(stale, 100, &sample) == ServiceStatus::invalid_target);
    CHECK(sample == before);
    stale = expected; ++stale.minecraftUuid[0];
    CHECK(f.host.ReadExecutionSample(stale, 100, &sample) == ServiceStatus::not_ready);
    CHECK(sample == before);
    stale = expected; ++stale.authorityConnection;
    CHECK(f.host.ReadExecutionSample(stale, 100, &sample) == ServiceStatus::stale_context);
    CHECK(sample == before);
    CHECK(f.host.ReadExecutionSample(expected, 0, &sample) == ServiceStatus::invalid_lease);
    CHECK(sample == before);
    CHECK(f.host.ReadExecutionSample(expected, 100, nullptr) == ServiceStatus::invalid_state);
}

void execution_requires_actual_control_and_pair() {
    for (int boundary = 0; boundary < 9; ++boundary) {
        Fixture f;
        const auto expected = execution_identity(f);
        HostEffects::ExecutionSample sample;
        CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
        const auto before = sample;
        switch (boundary) {
        case 0: link.online = false; break;
        case 1: ++link.current; break; // New transport not adopted by host yet.
        case 2: registry.binding.paired = false; break;
        case 3: minecraft_forms[1] = false; break;
        case 4: controlled[1].active = false; break;
        case 5: globals.time = 101.01f; break;
        case 6: globals.time = 99.99f; break;
        case 7: controlled[1].last_update = std::numeric_limits<float>::quiet_NaN(); break;
        case 8: globals.time = std::numeric_limits<float>::infinity(); break;
        }
        CHECK(f.host.ReadExecutionSample(expected, 100, &sample) != ServiceStatus::ok);
        CHECK(sample == before);
    }
    Fixture f;
    const auto expected = execution_identity(f);
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
    const auto before = sample;
    f.host.Forget(1);
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_target);
    CHECK(sample == before);
    ++player_lives[1]; f.host.Spawn(1);
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_target);
    CHECK(sample == before);
}

void execution_sample_time_and_coordinate_boundaries() {
    Fixture f;
    const auto expected = execution_identity(f);
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
    const auto before = sample;
    entities[1].v.origin.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    entities[1].v.origin.x = 64;
    entities[1].v.origin.z = entities[1].v.mins.z = std::numeric_limits<float>::max();
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    entities[1].v.origin.z = 164; entities[1].v.mins.z = -36;
    raw.counter = 107000; clockStep = 1000;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_lease);
    CHECK(sample == before); // Safe deadline108 elapsed at the second sample.
    clockStep = 0; raw.counter = 110000;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
    CHECK(sample.constraints.movement == goldcraft::effects::MovementState{});
    CHECK(sample.issuedAtMs == 110 && sample.deadlineMs == 208);
    const auto released = sample;
    clockOk = false;
    CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::clock_failed);
    CHECK(sample == released);
}

void execution_sample_signed_deadline_range() {
    Fixture environment;
    raw = {9214148664817920932LL, 999}; // floor(QPC*1000/F) == INT64_MAX-100.
    HostEffects host{read_clock};
    host.Begin(7); host.Connected(1); host.Spawn(1);
    ServiceContext context;
    CHECK(host.GetContext(&context) == ServiceStatus::ok);
    Target target;
    constexpr auto now = static_cast<std::uint64_t>(INT64_MAX) - 100;
    CHECK(host.ResolveTarget(context, goldcraft::effects::TargetKind::player, 1, now, &target) == ServiceStatus::ok);
    HostEffects::ExecutionIdentity expected{context, target, registry.binding.uuid, connection_generation};
    HostEffects::ExecutionSample sample;
    CHECK(host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
    CHECK(sample.issuedAtMs == now && sample.frequency == 999);
    CHECK(sample.deadlineMs == static_cast<std::uint64_t>(INT64_MAX) - 4); // margin=1+ceil(2000/999).
    const auto before = sample;
    raw.counter = 9214148664817921032LL; // Exactly INT64_MAX milliseconds.
    CHECK(host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::invalid_lease);
    CHECK(sample == before);
    CHECK(host.GetContext(&context) == ServiceStatus::ok); // Representation rejection is not a clock fault.
}

int secondReadMutation = 0;
void mutate_at_second_read(int count) noexcept {
    if (count != 2) return;
    switch (secondReadMutation) {
    case 0: ++registry.epoch; break;
    case 1: ++player_lives[1]; break;
    case 2: ++registry.binding.uuid[0]; break;
    case 3: ++link.current; break;
    case 4: minecraft_forms[1] = false; break;
    case 5: controlled[1].active = false; break;
    case 6: clockOk = false; break;
    case 7: ++raw.frequency; break;
    case 8: --raw.counter; break;
    }
}

void execution_revalidates_after_second_read() {
    for (int boundary = 0; boundary < 9; ++boundary) {
        Fixture f;
        const auto expected = execution_identity(f);
        HostEffects::ExecutionSample sample;
        CHECK(f.host.ReadExecutionSample(expected, 100, &sample) == ServiceStatus::ok);
        const auto before = sample;
        secondReadMutation = boundary; clockReadCount = 0; clockReadHook = mutate_at_second_read;
        CHECK(f.host.ReadExecutionSample(expected, 100, &sample) != ServiceStatus::ok);
        CHECK(clockReadCount == 2 && sample == before);
        clockReadHook = nullptr;
        if (boundary >= 6) {
            clockOk = true; raw = {200000, 1000000};
            ServiceContext unchanged{88, 99};
            CHECK(f.host.GetContext(&unchanged) == ServiceStatus::clock_failed);
            CHECK((unchanged == ServiceContext{88, 99}));
        }
    }
}

goldcraft::effects::MovementState hold_state(std::uint32_t axes) {
    return {goldcraft::effects::position_hold, 0, axes, 0, 1};
}

void anchors_capture_before_first_publication() {
    Fixture f;
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
    entities[1].v.origin = {96, -80, 180};
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK((sample.nativeFeet == std::array<float, 3>{96, -80, 144}));
    CHECK((sample.nativeAnchorFeet == std::array<float, 3>{64, -80, 144}));
    CHECK(f.host.Remove(f.context, f.owner, hold, 100) == ServiceStatus::ok);
    entities[1].v.origin.x = 128;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 160;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 128 && sample.nativeFeet[0] == 160);
}

void anchors_preserve_overlapping_owners_and_axes() {
    Fixture f;
    std::uint64_t first = 0, second = 0, owner = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &first) == ServiceStatus::ok);
    CHECK(f.host.CreateOwner(f.context, 100, &owner) == ServiceStatus::ok);
    entities[1].v.origin.x = 128;
    CHECK(f.host.Apply(f.context, owner, f.target, hold_state(1), 50, 100, &second) == ServiceStatus::ok);
    CHECK(f.host.Remove(f.context, f.owner, first, 100) == ServiceStatus::ok);
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 64);
    entities[1].v.origin.y = -32;
    CHECK(f.host.Update(f.context, owner, second, hold_state(3), 50, 100) == ServiceStatus::ok);
    entities[1].v.origin = {192, -16, 200};
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK((sample.nativeAnchorFeet == std::array<float, 3>{64, -32, 164}));
    CHECK(f.host.Update(f.context, owner, second, hold_state(1), 50, 100) == ServiceStatus::ok);
    CHECK(f.host.Update(f.context, owner, second, hold_state(3), 50, 100) == ServiceStatus::ok);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 64 && sample.nativeAnchorFeet[1] == -16);
    CHECK(f.host.RevokeOwner(f.context, owner, 100) == ServiceStatus::ok);
    entities[1].v.origin.z = 256;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(7), 50, 100, &first) == ServiceStatus::ok);
    entities[1].v.origin = {256, 32, 320};
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK((sample.nativeAnchorFeet == std::array<float, 3>{192, -16, 220}));
}

void anchors_observe_expiry_before_same_axis_apply() {
    Fixture f;
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 5, 100, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 128;
    raw.counter = 105000;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 105, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 192;
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 128 && sample.nativeFeet[0] == 192);
    CHECK(f.host.Update(f.context, f.owner + 999, hold, hold_state(3), 50, 105) == ServiceStatus::invalid_owner);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 128 && sample.constraints.movement.holdAxes == 1);
}

void anchors_never_recapture_nonfinite_acquisition() {
    Fixture f;
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    const auto before = sample;
    entities[1].v.origin.x = std::numeric_limits<float>::quiet_NaN();
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok && hold);
    entities[1].v.origin.x = 128;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    CHECK(f.host.Update(f.context, f.owner, hold, hold_state(1), 60, 100) == ServiceStatus::ok);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    CHECK(f.host.Update(f.context, f.owner, hold, hold_state(0), 60, 100) == ServiceStatus::ok);
    CHECK(f.host.Update(f.context, f.owner, hold, hold_state(1), 60, 100) == ServiceStatus::ok);
    entities[1].v.origin.x = 192;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 128);
}

void anchors_renew_and_expire_without_publication() {
    Fixture f;
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 10, 100, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 128;
    raw.counter = 104000;
    CHECK(f.host.Update(f.context, f.owner, hold, hold_state(1), 60, 104) == ServiceStatus::ok);
    raw.counter = 112000; // The original lease and unrelated speed cap expired.
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 64 && sample.constraints.movement.holdAxes == 1);
    raw.counter = 164000;
    f.host.Frame();
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet == sample.nativeFeet && sample.constraints.movement.holdAxes == 0);
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 164, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 192;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 128);
}

void anchors_clear_at_real_identity_boundaries() {
    for (unsigned boundary = 0; boundary < 4; ++boundary) {
        Fixture f;
        const auto oldIdentity = execution_identity(f);
        std::uint64_t hold = 0;
        CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
        HostEffects::ExecutionSample sample;
        CHECK(f.host.ReadExecutionSample(oldIdentity, 100, &sample) == ServiceStatus::ok);
        const auto before = sample;
        entities[1].v.origin.x = 256;
        switch (boundary) {
        case 0: // Actual form/relocation adapter retires before its life bump.
            f.host.Retire(1); ++player_lives[1]; f.host.Birth(1); break;
        case 1: // A new connection serial may restart its life counter.
            f.host.ResetSlot(1); ++registry.binding.serial; player_lives[1] = 1;
            f.host.Connected(1); f.host.Spawn(1); break;
        case 2: // Frame invalidation must not retain anchors until respawn.
            entities[1].v.deadflag = 1; f.host.Frame();
            entities[1].v.deadflag = DEAD_NO; ++player_lives[1]; f.host.Spawn(1); break;
        case 3:
            f.host.End(); ++registry.epoch; f.host.Begin(registry.epoch);
            f.host.Connected(1); f.host.Spawn(1); break;
        }
        CHECK(f.host.ReadExecutionSample(oldIdentity, 100, &sample) != ServiceStatus::ok);
        CHECK(sample == before);
        CHECK(f.host.GetContext(&f.context) == ServiceStatus::ok);
        CHECK(f.host.ResolveTarget(f.context, goldcraft::effects::TargetKind::player, 1, 100, &f.target) == ServiceStatus::ok);
        CHECK(f.host.CreateOwner(f.context, 100, &f.owner) == ServiceStatus::ok);
        CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
        entities[1].v.origin.x = 320;
        CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
        CHECK(sample.nativeAnchorFeet[0] == 256);
    }
}

void anchor_read_fault_preserves_successful_registration() {
    Fixture f;
    HostEffects::ExecutionSample sample;
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    const auto before = sample;
    AnchorFixtureProvider::failReadAfterApply = true;
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok && hold);
    goldcraft::effects::Snapshot snapshot;
    CHECK(f.host.ReadSnapshot(1, &snapshot) == ServiceStatus::ok && snapshot.movement.holdAxes == 1);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    CHECK(f.host.Remove(f.context, f.owner, hold, 100) == ServiceStatus::ok);
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::invalid_state);
    ++player_lives[1]; f.host.Spawn(1);
    CHECK(f.host.ResolveTarget(f.context, goldcraft::effects::TargetKind::player, 1, 100, &f.target) == ServiceStatus::ok);
    entities[1].v.origin.x = 256;
    CHECK(f.host.Apply(f.context, f.owner, f.target, hold_state(1), 50, 100, &hold) == ServiceStatus::ok);
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 256);
}

void failed_birth_refreshes_other_target_history() {
    Fixture f;
    max_clients = 2; entities[2] = {}; player_lives[2] = 1;
    f.host.Connected(2);
    // Admission's provider pump expires the first target's old cap. Inject a
    // lost snapshot during that rejected admission, then remove the injection:
    // execution must remain ineligible, even after subsequent reads recover.
    raw.counter = 110000;
    AnchorFixtureProvider::failRegistrationAfterPump = true;
    AnchorFixtureProvider::readFailures = 1;
    f.host.Spawn(2);
    AnchorFixtureProvider::ResetFaults();
    HostEffects::ExecutionSample sample;
    const auto before = sample;
    goldcraft::effects::Snapshot snapshot;
    CHECK(f.host.ReadSnapshot(1, &snapshot) == ServiceStatus::ok);
    CHECK(snapshot.movement == goldcraft::effects::MovementState{});
    CHECK(f.host.ReadExecutionSample(execution_identity(f), 100, &sample) == ServiceStatus::invalid_state);
    CHECK(sample == before);
    Target second;
    CHECK(f.host.ResolveTarget(f.context, goldcraft::effects::TargetKind::player, 2, 110, &second) == ServiceStatus::invalid_target);
}

// Inactive bootstrap and active execution are distinct bridge-private types.
// Engine/clock substitutes only: this does not exercise HandlePose or networking.
static_assert(!std::is_convertible_v<HostEffects::BootstrapSample, HostEffects::ExecutionSample>);

void bootstrap_samples_inactive_control_without_acquiring() {
    Fixture f;
    const auto expected = execution_identity(f);
    HostEffects::BootstrapSample bootstrap;
    const auto empty = bootstrap;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &bootstrap) == ServiceStatus::not_ready);
    CHECK(bootstrap == empty);
    controlled[1].active = false;
    controlled[1].last_update = -200;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &bootstrap) == ServiceStatus::ok);
    CHECK(bootstrap.identity == expected && bootstrap.constraints.target == f.target);
    CHECK(bootstrap.frequency == 1000000 && bootstrap.issuedAtMs == 100 && bootstrap.deadlineMs == 108);
    CHECK((bootstrap.nativeFeet == std::array<float, 3>{64, -96, 128}));
    CHECK(bootstrap.nativeAnchorFeet == bootstrap.nativeFeet);
    CHECK(!controlled[1].active && controlled[1].last_update == -200);
    CHECK(entities[1].v.origin.x == 64 && entities[1].v.origin.y == -96 && entities[1].v.origin.z == 164);
    HostEffects::ExecutionSample execution;
    const auto untouched = execution;
    CHECK(f.host.ReadExecutionSample(expected, 100, &execution) == ServiceStatus::not_ready);
    CHECK(execution == untouched);
    // Pre-acquisition pose age is undefined. Sampling must neither inspect it
    // as a received-pose timestamp nor repair it as a side effect.
    controlled[1].last_update = std::numeric_limits<float>::quiet_NaN();
    CHECK(f.host.ReadBootstrapSample(expected, 100, &bootstrap) == ServiceStatus::ok);
    CHECK(std::isnan(controlled[1].last_update) && !controlled[1].active);
    controlled[1] = {true, globals.time}; // Fixture alone simulates acquisition.
    CHECK(f.host.ReadExecutionSample(expected, 100, &execution) == ServiceStatus::ok);
    const auto before = bootstrap;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &bootstrap) == ServiceStatus::not_ready);
    CHECK(bootstrap == before);
    globals.time += 1.01f;
    CHECK(f.host.ReadExecutionSample(expected, 100, &execution) == ServiceStatus::not_ready);
}

void bootstrap_requires_exact_current_authority() {
    for (int boundary = 0; boundary < 16; ++boundary) {
        Fixture f;
        auto expected = execution_identity(f);
        controlled[1].active = false;
        HostEffects::BootstrapSample sample;
        CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::ok);
        const auto before = sample;
        switch (boundary) {
        case 0: ++expected.context.generation; break;
        case 1: ++expected.target.life; break;
        case 2: ++expected.minecraftUuid[0]; break;
        case 3: ++expected.authorityConnection; break;
        case 4: link.online = false; break;
        case 5: ++link.current; break;
        case 6: registry.binding.paired = false; break;
        case 7: minecraft_forms[1] = false; break;
        case 8: entities[1].v.health = 0; break;
        case 9: entities[1].v.iuser1 = 1; break;
        case 10: gpGlobals = nullptr; break;
        case 11: globals.time = std::numeric_limits<float>::infinity(); break;
        case 12: entities[1].v.origin.x = std::numeric_limits<float>::quiet_NaN(); break;
        case 13: entities[1].v.origin.z = entities[1].v.mins.z = std::numeric_limits<float>::max(); break;
        case 14: f.host.Forget(1); break;
        case 15: ++player_lives[1]; f.host.Spawn(1); break;
        }
        CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) != ServiceStatus::ok);
        CHECK(sample == before && !controlled[1].active && controlled[1].last_update == 100);
    }
    Fixture f;
    controlled[1].active = false;
    const auto expected = execution_identity(f);
    HostEffects::BootstrapSample sample;
    CHECK(f.host.ReadBootstrapSample(expected, 100, nullptr) == ServiceStatus::invalid_state);
    CHECK(f.host.ReadBootstrapSample(expected, 0, &sample) == ServiceStatus::invalid_lease);
    CHECK(sample == HostEffects::BootstrapSample{});
}

void mutate_bootstrap_at_second_read(int count) noexcept {
    if (count != 2) return;
    if (secondReadMutation == 5) controlled[1].active = true;
    else mutate_at_second_read(count);
}

void bootstrap_revalidates_after_second_read() {
    for (int boundary = 0; boundary < 9; ++boundary) {
        Fixture f;
        controlled[1].active = false;
        const auto expected = execution_identity(f);
        HostEffects::BootstrapSample sample;
        CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::ok);
        const auto before = sample;
        secondReadMutation = boundary; clockReadCount = 0; clockReadHook = mutate_bootstrap_at_second_read;
        CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) != ServiceStatus::ok);
        CHECK(clockReadCount == 2 && sample == before);
        CHECK(controlled[1].last_update == 100);
        clockReadHook = nullptr;
    }
}

void bootstrap_preserves_anchor_and_clock_boundaries() {
    Fixture f;
    controlled[1].active = false;
    const auto expected = execution_identity(f);
    std::uint64_t hold = 0;
    CHECK(f.host.Apply(f.context, f.owner, f.target,
        {goldcraft::effects::position_hold, 0, 1, 0, 1}, 100, 100, &hold) == ServiceStatus::ok);
    entities[1].v.origin.x = 96;
    HostEffects::BootstrapSample sample;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 64 && sample.nativeFeet[0] == 96);
    CHECK(sample.constraints.movement.holdAxes == 1 && !controlled[1].active);
    const auto before = sample;
    raw.counter = 107000; clockStep = 1000;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::invalid_lease);
    CHECK(sample == before);
    clockStep = 0; raw.counter = 110000;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::ok);
    CHECK(sample.nativeAnchorFeet[0] == 64 && sample.constraints.movement.holdAxes == 1);
    CHECK(sample.constraints.movement.wishSpeedCap == 0); // Expired separate cap.
    const auto after = sample;
    clockOk = false;
    CHECK(f.host.ReadBootstrapSample(expected, 100, &sample) == ServiceStatus::clock_failed);
    CHECK(sample == after && !controlled[1].active);

    Fixture fault;
    controlled[1].active = false;
    AnchorFixtureProvider::failReadAfterApply = true;
    CHECK(fault.host.Apply(fault.context, fault.owner, fault.target,
        {goldcraft::effects::position_hold, 0, 1, 0, 1}, 100, 100, &hold) == ServiceStatus::ok);
    HostEffects::BootstrapSample rejected;
    CHECK(fault.host.ReadBootstrapSample(execution_identity(fault), 100, &rejected) == ServiceStatus::invalid_state);
    CHECK(rejected == HostEffects::BootstrapSample{});
}

} // namespace

int main() {
    anchors_capture_before_first_publication();
    anchors_preserve_overlapping_owners_and_axes();
    anchors_observe_expiry_before_same_axis_apply();
    anchors_never_recapture_nonfinite_acquisition();
    anchors_renew_and_expire_without_publication();
    anchors_clear_at_real_identity_boundaries();
    anchor_read_fault_preserves_successful_registration();
    failed_birth_refreshes_other_target_history();
    bootstrap_samples_inactive_control_without_acquiring();
    bootstrap_requires_exact_current_authority();
    bootstrap_revalidates_after_second_read();
    bootstrap_preserves_anchor_and_clock_boundaries();
    coherent_execution_sample();
    execution_requires_actual_control_and_pair();
    execution_sample_time_and_coordinate_boundaries();
    execution_sample_signed_deadline_range();
    execution_revalidates_after_second_read();
    actual_host_qualifies_raw_duration();
    actual_host_fault_boundaries();
    interleaved_adapter_host_clock_is_one_domain();
    std::cout << "actual host adapter with boundary stubs: " << checks << " assertions, " << failures
              << " failures; no loaded ReHLDS or physical execution\n";
    return failures ? 1 : 0;
}
