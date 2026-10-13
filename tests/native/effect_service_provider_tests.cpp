// Tests of the actual provider and registry implementation.
// No fake IEffectService, AMXX VM, engine lifecycle, physical executor or receipt.
//
// Matched x86 build from the repository root (initialize the pinned VS/SDK first):
// clang-cl --target=i686-pc-windows-msvc /std:c++20 /EHsc /MT /utf-8 /W3
//   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /clang:-O3 /clang:-flto=thin
//   -fuse-ld=lld /fp:precise /arch:AVX2
//   /Inative/include
//   /Iexternal/ReGameDLL_CS/regamedll/public
//   native/server/effect_service_provider.cpp
//   tests/native/effect_service_provider_tests.cpp
//   /Fobuild/
//   /Febuild/effect_service_provider_tests.exe
//   /link /MACHINE:X86
// Compile both source files above; this test uses the ordinary provider header.
// Neither SDK interface.cpp nor AMXX amxxmodule.cpp is required. The executable's
// tiny CreateInterface below is its host factory and routes to the real provider.

#include <cstdlib>
#include <array>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>

#include "allocation_faults.hpp"

#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "../../native/server/effect_service_provider.hpp"

using namespace goldcraft::amxx_effects;
using namespace goldcraft::effects;
static_assert(sizeof(void*) == 4, "the service ABI must be tested with matched x86");
static_assert(!std::is_copy_constructible_v<EffectServiceProvider>);
static_assert(!std::is_move_constructible_v<EffectServiceProvider>);
static_assert(noexcept(std::declval<IEffectService&>().Apply({}, 0, {}, {}, 1, 0, nullptr)));

namespace {
EffectServiceProvider* executableProvider = nullptr;
int checks = 0, failures = 0;
void expect(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "line " << line << ": " << expression << '\n';
    }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

template<class Operation>
ServiceStatus failing_after(std::size_t successfulAllocations, Operation&& operation) {
    allocations::Fault fault(successfulAllocations);
    return operation();
}

Target player(std::uint64_t world = 7, std::uint64_t slot = 1,
              std::uint32_t serial = 10, std::uint32_t life = 2) {
    return {world, TargetKind::player, slot, serial, life};
}
MovementState cap(float speed) { return {wish_speed_cap, 0, 0, speed, 1}; }
MovementState gravity(float value) { return {gravity_factor, 0, 0, 0, value}; }
MovementState locks(std::uint32_t mask) { return {input_lock, mask, 0, 0, 1}; }
ServiceContext context(EffectServiceProvider& service) {
    ServiceContext value{};
    if (service.GetContext(&value) != ServiceStatus::ok) std::abort();
    return value;
}
Snapshot snapshot(EffectServiceProvider& service, const Target& target, std::uint64_t now = 100) {
    Snapshot value{};
    if (service.ReadSnapshot(context(service), target, now, &value) != ServiceStatus::ok) std::abort();
    return value;
}
std::uint64_t owner(EffectServiceProvider& service, std::uint64_t now = 100) {
    std::uint64_t value = 0;
    if (service.CreateOwner(context(service), now, &value) != ServiceStatus::ok) std::abort();
    return value;
}
std::uint64_t apply(EffectServiceProvider& service, std::uint64_t source,
                    const Target& target, MovementState state, std::uint32_t lease = 1000,
                    std::uint64_t now = 100) {
    std::uint64_t value = 0;
    if (service.Apply(context(service), source, target, state, lease, now, &value) != ServiceStatus::ok)
        std::abort();
    return value;
}
void setup(EffectServiceProvider& service, const Target& target = player(), std::uint64_t now = 100) {
    CHECK(service.BeginWorld(target.world, now) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(context(service), target, now) == ServiceStatus::ok);
}
} // namespace

// The SDK declares this export. This is solely the standalone test executable's
// host entrypoint, backed by a real provider; production bridge is not linked.
extern "C" IBaseInterface* CreateInterface(const char* name, int* result) {
    if (executableProvider) return QueryEffectServiceInterface(*executableProvider, name, result);
    if (result) *result = IFACE_FAILED;
    return nullptr;
}

namespace {
void actual_interface_and_uninitialized_state() {
    EffectServiceProvider service;
    executableProvider = &service;
    int result = -1;
    CHECK(CreateInterface("GoldCraftServerControl001", &result) == nullptr && result == IFACE_FAILED);
    CHECK(CreateInterface(nullptr, &result) == nullptr && result == IFACE_FAILED);
    CHECK(CreateInterface("GoldCraftEffects001", nullptr) == nullptr);
    auto* api = static_cast<IEffectService*>(CreateInterface(service_version, &result));
    CHECK(api == &service && result == IFACE_OK);
    ServiceContext untouched{77, 88};
    CHECK(api->GetContext(&untouched) == ServiceStatus::not_ready);
    CHECK((untouched == ServiceContext{77, 88}));
    CHECK(api->GetContext(nullptr) == ServiceStatus::invalid_state);
    std::uint64_t token = 99;
    CHECK(api->CreateOwner({}, 100, &token) == ServiceStatus::not_ready && token == 99);
    CHECK(service.RegisterTarget({}, player(), 100) == ServiceStatus::not_ready);
    CHECK(service.BeginWorld(0, 100) == ServiceStatus::stale_context);
    setup(service);
    const auto ctx = context(service);
    CHECK(api->CreateOwner(ctx, 100, nullptr) == ServiceStatus::invalid_state);
    CHECK(api->ResolveTarget(ctx, TargetKind::player, 1, 100, nullptr) == ServiceStatus::invalid_state);
    CHECK(api->Apply(ctx, 1, player(), {}, 1, 100, nullptr) == ServiceStatus::invalid_state);
    CHECK(service.ReadSnapshot(ctx, player(), 100, nullptr) == ServiceStatus::invalid_state);
    CHECK(owner(service) == 1); // Bad output pointers never mint/discard tokens.
    executableProvider = nullptr;
}

void random_worlds_stale_context_and_nonreuse() {
    constexpr std::uint64_t highWorld = 0xFEDCBA9876543210ULL;
    constexpr std::uint64_t tokenSeed = 0x1122334455667788ULL;
    EffectServiceProvider service({}, tokenSeed, 0x8877665544332211ULL);
    const auto oldTarget = player(highWorld);
    setup(service, oldTarget);
    const auto oldContext = context(service);
    const auto oldOwner = owner(service);
    const auto oldEffect = apply(service, oldOwner, oldTarget, cap(80));
    CHECK(oldOwner == tokenSeed && oldEffect == tokenSeed + 1);
    CHECK(service.BeginWorld(2, 200) == ServiceStatus::ok); // Smaller random epoch is valid.
    const auto next = context(service);
    CHECK(next.world == 2 && next.generation == oldContext.generation + 1);
    CHECK(service.BeginWorld(highWorld, 200) == ServiceStatus::stale_context);
    CHECK(service.BeginWorld(2, 200) == ServiceStatus::stale_context);
    CHECK(context(service) == next);
    std::uint64_t output = 99;
    Target targetOutput = oldTarget;
    Snapshot snapshotOutput{oldTarget, 99, cap(33)};
    const Snapshot untouched = snapshotOutput;
    CHECK(service.CreateOwner(oldContext, 9000, &output) == ServiceStatus::stale_context && output == 99);
    CHECK(service.ResolveTarget(oldContext, TargetKind::player, 1, 9000, &targetOutput) == ServiceStatus::stale_context);
    CHECK(targetOutput == oldTarget);
    CHECK(service.Apply(oldContext, oldOwner, oldTarget, {}, 1, 9000, &output) == ServiceStatus::stale_context);
    CHECK(service.Update(oldContext, oldOwner, oldEffect, {}, 1, 9000) == ServiceStatus::stale_context);
    CHECK(service.Remove(oldContext, oldOwner, oldEffect, 9000) == ServiceStatus::stale_context);
    CHECK(service.RevokeOwner(oldContext, oldOwner, 9000) == ServiceStatus::stale_context);
    CHECK(service.Pump(oldContext, 9000) == ServiceStatus::stale_context);
    CHECK(service.RegisterTarget(oldContext, oldTarget, 9000) == ServiceStatus::stale_context);
    CHECK(service.InvalidateTarget(oldContext, oldTarget, 9000) == ServiceStatus::stale_context);
    CHECK(service.ReadSnapshot(oldContext, oldTarget, 9000, &snapshotOutput) == ServiceStatus::stale_context);
    CHECK(snapshotOutput == untouched);
    CHECK(service.Pump(next, 201) == ServiceStatus::ok); // Rejected old calls did not advance the clock.
    CHECK(service.ResolveTarget(next, TargetKind::player, 1, 201, &targetOutput) == ServiceStatus::invalid_target);
    const auto newOwner = owner(service, 201);
    CHECK(newOwner == oldEffect + 1);
    CHECK(service.Update(next, oldOwner, oldEffect, {}, 100, 201) == ServiceStatus::invalid_owner);
    CHECK(service.Update(next, newOwner, oldEffect, {}, 100, 201) == ServiceStatus::missing_effect);
    CHECK(service.RegisterTarget(next, player(2), 201) == ServiceStatus::ok);
    CHECK(snapshot(service, player(2), 201).revision == 1);
    CHECK(service.BeginWorld(9, 200) == ServiceStatus::clock_regressed);
    CHECK(context(service) == next);
    CHECK(service.BeginWorld(9, 201) == ServiceStatus::ok); // Failed transition did not reserve epoch.
}

void authoritative_targets_and_high_water_marks() {
    EffectServiceProvider service;
    CHECK(service.BeginWorld(7, 100) == ServiceStatus::ok);
    const auto ctx = context(service);
    Target found{99, TargetKind::object, 99, 99, 99};
    const auto untouched = found;
    const auto source = owner(service);
    std::uint64_t output = 123;
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::invalid_target);
    CHECK(found == untouched);
    CHECK(service.Apply(ctx, source, player(), cap(80), 1000, 100, &output) == ServiceStatus::invalid_target);
    CHECK(output == 123);
    CHECK(service.ResolveTarget(ctx, static_cast<TargetKind>(3), 1, 100, &found) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(7, 65), 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(7, 1, 0), 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(7, 1, 10, 0), 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(8), 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(), 100) == ServiceStatus::ok);
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::ok && found == player());
    const auto effect = apply(service, source, player(), cap(80));
    const auto before = snapshot(service, player());
    CHECK(before.revision == 2);
    CHECK(service.RegisterTarget(ctx, player(), 100) == ServiceStatus::ok);
    CHECK(snapshot(service, player()) == before); // Re-registration must not reset revision.
    const auto life3 = player(7, 1, 10, 3);
    CHECK(service.RegisterTarget(ctx, life3, 100) == ServiceStatus::ok);
    CHECK(service.Update(ctx, source, effect, cap(40), 1000, 100) == ServiceStatus::missing_effect);
    CHECK(snapshot(service, life3).revision == 1);
    CHECK(service.InvalidateTarget(ctx, player(), 100) == ServiceStatus::invalid_target);
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::ok && found == life3);
    CHECK(service.InvalidateTarget(ctx, life3, 100) == ServiceStatus::ok);
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, life3, 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(), 100) == ServiceStatus::invalid_target);
    const auto life4 = player(7, 1, 10, 4);
    CHECK(service.RegisterTarget(ctx, life4, 100) == ServiceStatus::ok);
    const auto nextConnection = player(7, 1, 11, 1);
    CHECK(service.RegisterTarget(ctx, nextConnection, 100) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(ctx, player(7, 1, 10, UINT32_MAX), 100) == ServiceStatus::invalid_target);
    CHECK(service.InvalidateTarget(ctx, life4, 100) == ServiceStatus::invalid_target);
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::ok && found == nextConnection);

    // Object identity is a host-provided incarnation, not inferred from its key.
    const Target object{7, TargetKind::object, 1, 50, 1}; // Same key as player, separate kind.
    const Target reissued{7, TargetKind::object, 1, 51, 1};
    CHECK(service.RegisterTarget(ctx, object, 100) == ServiceStatus::ok);
    const auto objectEffect = apply(service, source, object, gravity(0));
    CHECK(service.InvalidateTarget(ctx, object, 100) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(ctx, object, 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, reissued, 100) == ServiceStatus::ok);
    CHECK(service.Update(ctx, source, objectEffect, gravity(2), 1000, 100) == ServiceStatus::missing_effect);
    CHECK(service.ResolveTarget(ctx, TargetKind::object, 1, 100, &found) == ServiceStatus::ok && found == reissued);
    CHECK(service.ResolveTarget(ctx, TargetKind::player, 1, 100, &found) == ServiceStatus::ok && found == nextConnection);
    CHECK(service.Update(ctx, source, apply(service, source, nextConnection, cap(20)), cap(30), 1000, 100)
          == ServiceStatus::ok); // Native owners survive object-stream reset.
    const Target wideObject{7, TargetKind::object, 0xFEDCBA9876543210ULL, 70, 3};
    CHECK(service.RegisterTarget(ctx, wideObject, 100) == ServiceStatus::ok);
    CHECK(service.ResolveTarget(ctx, TargetKind::object, wideObject.key, 100, &found) == ServiceStatus::ok && found == wideObject);
    const auto maximum = player(7, 2, UINT32_MAX, UINT32_MAX);
    CHECK(service.RegisterTarget(ctx, maximum, 100) == ServiceStatus::ok);
    CHECK(service.InvalidateTarget(ctx, maximum, 100) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(ctx, maximum, 100) == ServiceStatus::invalid_target);
    CHECK(service.RegisterTarget(ctx, player(7, 2, 1, 1), 100) == ServiceStatus::invalid_target);
}

void deactivation_gates_every_operation_and_cannot_resume() {
    EffectServiceProvider service;
    setup(service);
    const auto old = context(service);
    const auto source = owner(service);
    const auto effect = apply(service, source, player(), cap(80), 1000);
    const auto before = snapshot(service, player());
    auto wrong = old; ++wrong.generation;
    CHECK(service.EndWorld(wrong, 9000) == ServiceStatus::stale_context);
    CHECK(service.EndWorld(old, 99) == ServiceStatus::clock_regressed);
    CHECK(context(service) == old && snapshot(service, player()) == before);
    CHECK(service.EndWorld(old, 150) == ServiceStatus::ok);
    ServiceContext current{77, 88};
    CHECK(service.GetContext(&current) == ServiceStatus::not_ready);
    CHECK((current == ServiceContext{77, 88}));
    CHECK(service.EndWorld(old, 150) == ServiceStatus::not_ready);
    CHECK(service.BeginWorld(old.world, 150) == ServiceStatus::stale_context);
    Target resolved = player();
    Snapshot read = before;
    std::uint64_t output = 123;
    CHECK(service.CreateOwner(old, 9000, &output) == ServiceStatus::not_ready && output == 123);
    CHECK(service.ResolveTarget(old, TargetKind::player, 1, 9000, &resolved) == ServiceStatus::not_ready);
    CHECK(service.Apply(old, source, player(), {}, 10, 9000, &output) == ServiceStatus::not_ready);
    CHECK(service.Update(old, source, effect, {}, 10, 9000) == ServiceStatus::not_ready);
    CHECK(service.Remove(old, source, effect, 9000) == ServiceStatus::not_ready);
    CHECK(service.RevokeOwner(old, source, 9000) == ServiceStatus::not_ready);
    CHECK(service.Pump(old, 9000) == ServiceStatus::not_ready);
    CHECK(service.RegisterTarget(old, player(7, 1, 10, 3), 9000) == ServiceStatus::not_ready);
    CHECK(service.InvalidateTarget(old, player(), 9000) == ServiceStatus::not_ready);
    CHECK(service.ReadSnapshot(old, player(), 9000, &read) == ServiceStatus::not_ready);
    CHECK(read == before && resolved == player() && output == 123);
    CHECK(service.BeginWorld(2, 149) == ServiceStatus::clock_regressed);
    CHECK(service.GetContext(&current) == ServiceStatus::not_ready);
    CHECK(service.BeginWorld(2, 150) == ServiceStatus::ok);
    current = context(service);
    CHECK(current.world == 2 && current.generation == old.generation + 1);
    CHECK(service.EndWorld(old, 9000) == ServiceStatus::stale_context);
    CHECK(service.ResolveTarget(current, TargetKind::player, 1, 150, &resolved) == ServiceStatus::invalid_target);
    const auto nextOwner = owner(service, 150);
    CHECK(nextOwner == effect + 1);
    CHECK(service.Update(current, source, effect, {}, 100, 150) == ServiceStatus::invalid_owner);
    CHECK(service.RegisterTarget(current, player(2), 150) == ServiceStatus::ok);
    CHECK(snapshot(service, player(2), 150).revision == 1);
}

void real_registry_composition_expiry_and_ownership() {
    EffectServiceProvider service;
    setup(service);
    const auto ctx = context(service);
    const auto a = owner(service), b = owner(service);
    const auto slow = apply(service, a, player(), cap(80), 100);
    const auto input = apply(service, b, player(), locks(lock_move | lock_jump), 200);
    const auto zero = apply(service, a, player(), gravity(0), 150);
    auto state = snapshot(service, player(), 150);
    CHECK(state.movement.wishSpeedCap == 80 && state.movement.gravityFactor == 0);
    CHECK(state.movement.inputLocks == (lock_move | lock_jump));
    CHECK(service.Update(ctx, b, slow, cap(40), 100, 150) == ServiceStatus::wrong_owner);
    CHECK(service.Remove(ctx, b, slow, 150) == ServiceStatus::wrong_owner);
    CHECK(snapshot(service, player(), 150) == state);
    CHECK(service.Update(ctx, a, slow, cap(-1), 100, 150) == ServiceStatus::invalid_state);
    CHECK(service.Update(ctx, a, slow, cap(40), 0, 150) == ServiceStatus::invalid_lease);
    CHECK(snapshot(service, player(), 150) == state);
    CHECK(service.Pump(ctx, 149) == ServiceStatus::clock_regressed);
    CHECK(service.Pump(ctx, 200) == ServiceStatus::ok); // Independent host cleanup, no Pawn calls.
    CHECK(service.Update(ctx, a, slow, cap(80), 100, 200) == ServiceStatus::missing_effect);
    state = snapshot(service, player(), 200);
    CHECK(!(state.movement.capability & wish_speed_cap));
    CHECK(state.movement.gravityFactor == 0 && state.movement.inputLocks == (lock_move | lock_jump));
    CHECK(snapshot(service, player(), 250).movement == locks(lock_move | lock_jump)); // Read crosses expiry.
    CHECK(service.Remove(ctx, a, zero, 250) == ServiceStatus::missing_effect);
    CHECK(service.RevokeOwner(ctx, b, 250) == ServiceStatus::ok);
    CHECK(snapshot(service, player(), 250).movement == MovementState{});
    CHECK(service.Update(ctx, b, input, {}, 100, 250) == ServiceStatus::invalid_owner);
    std::uint64_t output = 91;
    CHECK(service.Apply(ctx, a, player(), gravity(std::numeric_limits<float>::quiet_NaN()), 100, 250, &output)
          == ServiceStatus::invalid_state && output == 91);
    CHECK(service.Apply(ctx, a, player(), {}, 60001, 250, &output) == ServiceStatus::invalid_lease);
    const auto first = apply(service, a, player(), gravity(16), 100, 250);
    CHECK(service.Apply(ctx, a, player(), gravity(16), 100, 250, &output) == ServiceStatus::combination_out_of_range);
    CHECK(snapshot(service, player(), 250).movement == gravity(16));
    CHECK(service.Remove(ctx, a, first, 250) == ServiceStatus::ok);
}

void bounded_ledgers_registry_capacity_and_exhaustion() {
    ProviderLimits limits;
    limits.registry = {1, 1, 1, 60000};
    limits.targetKeysPerWorld = 2;
    limits.worldsPerProvider = 2;
    EffectServiceProvider service(limits);
    setup(service);
    auto ctx = context(service);
    const auto source = owner(service);
    std::uint64_t output = 99;
    CHECK(service.CreateOwner(ctx, 100, &output) == ServiceStatus::capacity_exhausted && output == 99);
    const auto effect = apply(service, source, player(), cap(80));
    CHECK(service.Apply(ctx, source, player(), {}, 1000, 100, &output) == ServiceStatus::capacity_exhausted);
    CHECK(service.RegisterTarget(ctx, player(7, 2), 100) == ServiceStatus::capacity_exhausted);
    CHECK(service.InvalidateTarget(ctx, player(), 100) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(ctx, player(7, 2), 100) == ServiceStatus::ok);
    CHECK(service.InvalidateTarget(ctx, player(7, 2), 100) == ServiceStatus::ok);
    CHECK(service.RegisterTarget(ctx, player(7, 3), 100) == ServiceStatus::capacity_exhausted);
    CHECK(service.RegisterTarget(ctx, player(), 100) == ServiceStatus::invalid_target); // Tombstone not evicted.
    CHECK(service.RegisterTarget(ctx, player(7, 1, 10, 3), 100) == ServiceStatus::ok); // Existing key can advance.
    CHECK(service.Update(ctx, source, effect, {}, 1000, 100) == ServiceStatus::missing_effect);
    CHECK(service.BeginWorld(2, 100) == ServiceStatus::ok);
    ctx = context(service);
    CHECK(service.RegisterTarget(ctx, player(2, 3), 100) == ServiceStatus::ok); // Per-world target ledger cleared.
    CHECK(service.BeginWorld(8, 100) == ServiceStatus::capacity_exhausted);
    CHECK(context(service) == ctx);
    CHECK(service.BeginWorld(7, 100) == ServiceStatus::stale_context);
    CHECK(snapshot(service, player(2, 3)).revision == 1);

    EffectServiceProvider tokens({}, UINT64_MAX - 1);
    setup(tokens);
    const auto lastOwner = owner(tokens);
    CHECK(lastOwner == UINT64_MAX - 1);
    CHECK(apply(tokens, lastOwner, player(), cap(0)) == UINT64_MAX);
    CHECK(tokens.CreateOwner(context(tokens), 100, &output) == ServiceStatus::token_exhausted);
    CHECK(tokens.BeginWorld(8, 100) == ServiceStatus::ok);
    CHECK(tokens.CreateOwner(context(tokens), 100, &output) == ServiceStatus::token_exhausted);
    CHECK(output == 99);

    EffectServiceProvider generations({}, 1, UINT64_MAX);
    setup(generations);
    const auto lastContext = context(generations);
    CHECK(lastContext.generation == UINT64_MAX);
    CHECK(generations.BeginWorld(8, 100) == ServiceStatus::token_exhausted);
    CHECK(context(generations) == lastContext);
    CHECK(snapshot(generations, player()).revision == 1);
    EffectServiceProvider noGeneration({}, 1, 0);
    CHECK(noGeneration.BeginWorld(7, 100) == ServiceStatus::token_exhausted);
}

// Setup is excluded from allocation measurement. Fixed arrays keep the oracle
// itself allocation-free, while the real provider holds multiple map/set nodes.
struct Populated {
    EffectServiceProvider service;
    ServiceContext ctx{};
    std::array<std::uint64_t, 8> owners{};
    std::array<Target, 24> targets{};
    std::array<Snapshot, 24> before{};
    std::array<std::uint64_t, 72> handles{};
    std::size_t count;
    std::uint64_t lastToken = 0;
    explicit Populated(std::size_t size) : count(size) {
        CHECK(service.BeginWorld(7, 100) == ServiceStatus::ok);
        ctx = context(service);
        for (auto& token : owners) lastToken = token = owner(service);
        for (std::size_t i = 0; i < count; ++i) {
            targets[i] = player(7, i + 1);
            CHECK(service.RegisterTarget(ctx, targets[i], 100) == ServiceStatus::ok);
            for (std::size_t e = 0; e < 3; ++e)
                lastToken = handles[i * 3 + e] = apply(service, owners[e], targets[i], cap(80 + float(e)));
            before[i] = snapshot(service, targets[i]);
        }
    }
    void unchanged() {
        CHECK(context(service) == ctx);
        for (std::size_t i = 0; i < count; ++i) {
            CHECK(snapshot(service, targets[i]) == before[i]);
            Target current{};
            CHECK(service.ResolveTarget(ctx, TargetKind::player, targets[i].key, 100, &current) == ServiceStatus::ok);
            CHECK(current == targets[i]);
            for (std::size_t e = 0; e < 3; ++e)
                CHECK(service.Update(ctx, owners[e], handles[i * 3 + e], cap(80 + float(e)), 1000, 100)
                      == ServiceStatus::ok);
        }
    }
};

// Discover the actual successful-path allocation count before faulting each
// observed allocation. The final count boundary must succeed without injection.
// This is exhaustive for each fixture's observed C++ path, not all possible
// heaps, custom allocators or native Windows allocations.
template<class Attempt>
void sweep_observed(const char* name, Attempt&& run) {
    const auto total = run(nullptr);
    CHECK(total > 0 && total < 4096);
    if (!total || total >= 4096) return;
    for (std::size_t point = 0; point <= total; ++point) {
        const auto observed = run(&point);
        CHECK(observed == (point < total ? point + 1 : total));
        CHECK(allocations::injected == (point < total));
    }
    std::cout << "allocation sweep " << name << ": " << total
              << " observed nodes, all failures + success boundary verified\n";
}

void allocation_failure_is_atomic() {
    for (const std::size_t size : {std::size_t{1}, std::size_t{24}}) {
        for (const bool replace : {false, true}) {
            sweep_observed(replace ? "replace-target" : "new-target", [&](const std::size_t* point) {
                Populated f(size);
                const Target proposed = replace ? player(7, 1, 10, 3) : player(7, size + 1);
                auto operation = [&] { return f.service.RegisterTarget(f.ctx, proposed, 100); };
                const auto start = allocations::count;
                const auto status = point ? failing_after(*point, operation) : operation();
                const auto observed = allocations::count - start;
                if (point && allocations::injected) {
                    CHECK(status == ServiceStatus::capacity_exhausted);
                    f.unchanged();
                    CHECK(f.service.RegisterTarget(f.ctx, proposed, 100) == ServiceStatus::ok);
                } else CHECK(status == ServiceStatus::ok);
                CHECK(snapshot(f.service, proposed).revision == 1);
                for (std::size_t i = replace ? 1 : 0; i < size; ++i)
                    CHECK(snapshot(f.service, f.targets[i]) == f.before[i]);
                return observed;
            });
        }
    }
    // These operations each allocate too; measure instead of assuming one node.
    for (const int operationKind : {0, 1, 2}) {
        sweep_observed(operationKind == 0 ? "owner" : operationKind == 1 ? "effect" : "world", [&](const std::size_t* point) {
            Populated f(24);
            std::uint64_t output = 0xFFFFFFFFFFFFFFFFULL;
            auto operation = [&] {
                if (operationKind == 0) return f.service.CreateOwner(f.ctx, 100, &output);
                if (operationKind == 1) return f.service.Apply(f.ctx, f.owners[0], f.targets[0], cap(40), 1000, 100, &output);
                return f.service.BeginWorld(8, 100);
            };
            const auto start = allocations::count;
            const auto status = point ? failing_after(*point, operation) : operation();
            const auto observed = allocations::count - start;
            if (point && allocations::injected) {
                CHECK(status == ServiceStatus::capacity_exhausted);
                CHECK(output == UINT64_MAX);
                f.unchanged();
                CHECK(operation() == ServiceStatus::ok);
            } else CHECK(status == ServiceStatus::ok);
            if (operationKind < 2) CHECK(output == f.lastToken + 1);
            else CHECK(context(f.service) == (ServiceContext{8, f.ctx.generation + 1}));
            return observed;
        });
    }
    sweep_observed("first-world", [](const std::size_t* point) {
        EffectServiceProvider service;
        auto operation = [&] { return service.BeginWorld(7, 100); };
        const auto start = allocations::count;
        const auto status = point ? failing_after(*point, operation) : operation();
        const auto observed = allocations::count - start;
        if (point && allocations::injected) {
            CHECK(status == ServiceStatus::capacity_exhausted);
            ServiceContext out{77, 88};
            CHECK(service.GetContext(&out) == ServiceStatus::not_ready && out == (ServiceContext{77, 88}));
            CHECK(operation() == ServiceStatus::ok);
        } else CHECK(status == ServiceStatus::ok);
        CHECK(context(service) == (ServiceContext{7, 1}));
        return observed;
    });
}

void hot_paths_do_not_allocate_and_expire_independently() {
    EffectServiceProvider service;
    setup(service);
    const auto ctx = context(service);
    const auto source = owner(service);
    const auto effect = apply(service, source, player(), cap(80), 100);
    Snapshot out{};
    Target resolved{};
    const auto before = allocations::count;
    CHECK(failing_after(0, [&] { return service.ResolveTarget(ctx, TargetKind::player, 1, 100, &resolved); }) == ServiceStatus::ok);
    CHECK(failing_after(0, [&] { return service.Update(ctx, source, effect, cap(40), 100, 150); }) == ServiceStatus::ok);
    CHECK(failing_after(0, [&] { return service.Pump(ctx, 250); }) == ServiceStatus::ok);
    CHECK(failing_after(0, [&] { return service.ReadSnapshot(ctx, player(), 250, &out); }) == ServiceStatus::ok);
    CHECK(out.movement == MovementState{} && out.revision == 4);
    CHECK(failing_after(0, [&] { return service.InvalidateTarget(ctx, player(), 250); }) == ServiceStatus::ok);
    CHECK(failing_after(0, [&] { return service.RevokeOwner(ctx, source, 250); }) == ServiceStatus::ok);
    CHECK(failing_after(0, [&] { return service.EndWorld(ctx, 250); }) == ServiceStatus::ok);
    CHECK(allocations::count == before);
    CHECK(service.RegisterTarget(ctx, player(), 250) == ServiceStatus::not_ready);
}
// Inserted into the existing real-provider fixture; no replacement registry.
void leased_snapshots_track_real_contribution_validity() {
    EffectServiceProvider service;
    setup(service);
    const auto ctx = context(service);
    const auto who = player();
    const MovementState hold{position_hold, 0, hold_x, 0, 1};
    LeasedSnapshot value{};
    const auto read = [&](std::uint64_t now, std::uint32_t ceiling = 60000) {
        CHECK(service.ReadLeasedSnapshot(ctx, who, now, ceiling, &value) == ServiceStatus::ok);
    };
    read(100, 500);
    CHECK(value.snapshot.movement == MovementState{} && value.sampledAtMs == 100 && value.remainingMs == 500);
    const auto a = owner(service), b = owner(service);
    const auto first = apply(service, a, who, hold, 1000);
    read(100);
    CHECK(value.remainingMs == 1000 && value.snapshot.revision == 2);
    const auto second = apply(service, b, who, hold, 100);
    read(100);
    CHECK(value.remainingMs == 100 && value.snapshot.revision == 2);
    read(199);
    CHECK(value.remainingMs == 1 && value.sampledAtMs == 199);
    CHECK(service.Update(ctx, b, second, hold, 1000, 199) == ServiceStatus::ok);
    read(199);
    CHECK(value.remainingMs == 901 && value.snapshot.revision == 2);

    // An earliest-expiry tie must survive extension of only one tied owner.
    const auto c = owner(service, 199);
    const auto tied = apply(service, c, who, hold, 901, 199);
    CHECK(service.Update(ctx, a, first, hold, 2000, 199) == ServiceStatus::ok);
    read(199);
    CHECK(value.remainingMs == 901 && value.snapshot.revision == 2);
    read(200); // Old nextExpiry=200 was conservative; no contribution expires.
    CHECK(value.remainingMs == 900 && value.snapshot.movement == hold);
    CHECK(service.Remove(ctx, c, tied, 200) == ServiceStatus::ok);
    read(200);
    CHECK(value.remainingMs == 999 && value.snapshot.revision == 2);

    // Even a no-op contribution bounds a grant conservatively.
    const auto d = owner(service, 200);
    apply(service, d, who, MovementState{}, 10, 200);
    read(200);
    CHECK(value.remainingMs == 10 && value.snapshot.revision == 2);
    CHECK(service.RevokeOwner(ctx, d, 205) == ServiceStatus::ok);
    read(205);
    CHECK(value.remainingMs == 994 && value.snapshot.revision == 2);
    const auto other = player(7, 2);
    CHECK(service.RegisterTarget(ctx, other, 205) == ServiceStatus::ok);
    apply(service, c, other, hold, 1, 205);
    read(206);
    CHECK(value.remainingMs == 993 && value.snapshot.revision == 2);

    // State-changing Update refreshes the same deadline metadata as Apply.
    CHECK(service.Update(ctx, b, second, cap(80), 200, 206) == ServiceStatus::ok);
    read(206);
    CHECK(value.remainingMs == 200 && value.snapshot.revision == 3 &&
          value.snapshot.movement.capability == (position_hold | wish_speed_cap));
    CHECK(service.Update(ctx, a, first, hold, 20, 206) == ServiceStatus::ok);
    read(206);
    CHECK(value.remainingMs == 20 && value.snapshot.revision == 3);
    CHECK(service.Update(ctx, b, first, hold, 1, 206) == ServiceStatus::wrong_owner);
    read(206);
    CHECK(value.remainingMs == 20);
    CHECK(service.Remove(ctx, b, second, 206) == ServiceStatus::ok);
    read(206);
    CHECK(value.remainingMs == 20 && value.snapshot.revision == 4 && value.snapshot.movement == hold);
    CHECK(service.Update(ctx, a, first, hold, 2000, 206) == ServiceStatus::ok);
    read(226);
    CHECK(value.remainingMs == 1980 && value.snapshot.revision == 4);

    const auto unchanged = value;
    CHECK(service.ReadLeasedSnapshot(ctx, who, 225, 500, &value) == ServiceStatus::clock_regressed);
    CHECK(value == unchanged);
    CHECK(service.ReadLeasedSnapshot(ctx, who, 999999, 0, &value) == ServiceStatus::invalid_lease);
    CHECK(value == unchanged);
    CHECK(service.ReadLeasedSnapshot(ctx, who, 999999, 60001, &value) == ServiceStatus::invalid_lease);
    CHECK(value == unchanged);
    CHECK(service.ReadLeasedSnapshot(ctx, who, 999999, 500, nullptr) == ServiceStatus::invalid_state);
    CHECK(service.ReadLeasedSnapshot({8, ctx.generation}, who, 999999, 500, &value) == ServiceStatus::stale_context);
    CHECK(value == unchanged);
    read(227, 25);
    CHECK(value.remainingMs == 25 && value.snapshot.movement == hold);

    const auto allocationsBefore = allocations::count;
    CHECK(failing_after(0, [&] { return service.ReadLeasedSnapshot(ctx, who, 227, 500, &value); }) == ServiceStatus::ok);
    CHECK(allocations::count == allocationsBefore && value.remainingMs == 500);
    read(2206, 500); // The leased read itself pumps expiry, without a prior Pump.
    CHECK(value.snapshot.movement == MovementState{} && value.snapshot.revision == 5 && value.remainingMs == 500);
    CHECK(service.InvalidateTarget(ctx, who, 2206) == ServiceStatus::ok);
    const auto retired = value;
    CHECK(service.ReadLeasedSnapshot(ctx, who, 2206, 500, &value) == ServiceStatus::invalid_target);
    CHECK(value == retired);
    CHECK(service.EndWorld(ctx, 2206) == ServiceStatus::ok);
    CHECK(service.ReadLeasedSnapshot(ctx, who, 2206, 500, &value) == ServiceStatus::not_ready);
    CHECK(value == retired);

    // Natural expiry of a redundant contribution changes remaining time even
    // though the movement state and its semantic revision remain identical.
    EffectServiceProvider redundant;
    setup(redundant);
    const auto redundantCtx = context(redundant);
    const auto longOwner = owner(redundant), shortOwner = owner(redundant);
    apply(redundant, longOwner, who, hold, 1000);
    apply(redundant, shortOwner, who, hold, 100);
    CHECK(redundant.ReadLeasedSnapshot(redundantCtx, who, 100, 5000, &value) == ServiceStatus::ok);
    CHECK(value.snapshot.revision == 2 && value.remainingMs == 100);
    CHECK(redundant.ReadLeasedSnapshot(redundantCtx, who, 200, 5000, &value) == ServiceStatus::ok);
    CHECK(value.snapshot.revision == 2 && value.snapshot.movement == hold && value.remainingMs == 900);

    EffectServiceProvider boundary;
    constexpr auto end = std::numeric_limits<std::uint64_t>::max();
    setup(boundary, who, end - 10);
    const auto boundaryCtx = context(boundary);
    const auto boundaryOwner = owner(boundary, end - 10);
    apply(boundary, boundaryOwner, who, hold, 10, end - 10);
    CHECK(boundary.ReadLeasedSnapshot(boundaryCtx, who, end - 1, 500, &value) == ServiceStatus::ok);
    CHECK(value.remainingMs == 1 && value.snapshot.movement == hold);
    CHECK(boundary.ReadLeasedSnapshot(boundaryCtx, who, end, 500, &value) == ServiceStatus::ok);
    CHECK(value.remainingMs == 0 && value.snapshot.movement == MovementState{});
}

void abort_and_clock_failure_are_timestamp_free() {
    EffectServiceProvider service;
    setup(service);
    const auto old = context(service);
    const auto a = owner(service);
    const auto effect = apply(service, a, player(), cap(80));
    service.AbortWorld();
    ServiceContext untouched{99, 88};
    CHECK(service.GetContext(&untouched) == ServiceStatus::not_ready);
    CHECK((untouched == ServiceContext{99, 88}));
    CHECK(service.BeginWorld(7, 100) == ServiceStatus::stale_context);
    CHECK(service.BeginWorld(8, 100) == ServiceStatus::ok);
    const auto next = context(service);
    CHECK(next.generation > old.generation);
    CHECK(service.RegisterTarget(next, player(8), 100) == ServiceStatus::ok);
    const auto b = owner(service);
    CHECK(b > effect);
    (void)apply(service, b, player(8), cap(40));
    const auto initial = snapshot(service, player(8));
    const auto allocationsBefore = allocations::count;
    service.ClockFault();
    service.ClockFault(); // Idempotent and never invokes registry time/pump.
    CHECK(allocations::count == allocationsBefore);
    Snapshot output = initial;
    CHECK(service.ReadSnapshot(next, player(8), 0, &output) == ServiceStatus::clock_failed && output == initial);
    std::uint64_t token = 999;
    CHECK(service.CreateOwner(next, 0, &token) == ServiceStatus::clock_failed && token == 999);
    CHECK(service.Apply(next, b, player(8), cap(20), 10, 0, &token) == ServiceStatus::clock_failed && token == 999);
    CHECK(service.Update(next, b, effect, cap(20), 10, 0) == ServiceStatus::clock_failed);
    CHECK(service.Remove(next, b, effect, 0) == ServiceStatus::clock_failed);
    CHECK(service.RevokeOwner(next, b, 0) == ServiceStatus::clock_failed);
    CHECK(service.Pump(next, 0) == ServiceStatus::clock_failed);
    CHECK(service.EndWorld(next, 0) == ServiceStatus::clock_failed);
    CHECK(service.InvalidateTarget(next, player(8), 0) == ServiceStatus::clock_failed);
    CHECK(service.RegisterTarget(next, player(8, 2), 0) == ServiceStatus::clock_failed);
    Target target = player(99);
    CHECK(service.ResolveTarget(next, TargetKind::player, 1, 0, &target) == ServiceStatus::clock_failed && target == player(99));
    goldcraft::effects::LeasedSnapshot lease{};
    CHECK(service.ReadLeasedSnapshot(next, player(8), 0, 10, &lease) == ServiceStatus::clock_failed);
    CHECK(service.BeginWorld(9, 100) == ServiceStatus::clock_failed);
    CHECK(service.GetContext(&untouched) == ServiceStatus::clock_failed && (untouched == ServiceContext{99, 88}));
}

void qualified_lease_cannot_overgrant() {
    EffectServiceProvider service;
    setup(service);
    const auto ctx = context(service);
    const auto a = owner(service);
    (void)apply(service, a, player(), cap(80), 10);
    goldcraft::effects::LeasedSnapshot native;
    CHECK(service.ReadLeasedSnapshot(ctx, player(), 108, 100, &native) == ServiceStatus::ok);
    goldcraft::effect_clock::Sample sampled;
    CHECK(goldcraft::effect_clock::convert({108000, 1000000}, sampled) == goldcraft::effect_clock::Error::none);
    QualifiedLeasedSnapshot qualified{native, 99, 99};
    CHECK(!qualify_lease(native, sampled, qualified)); // 2ms raw minus 2ms margin = zero.
    CHECK(qualified.safeRemainingMs == 99 && qualified.marginMs == 99); // Failure never publishes a grant.
    native.remainingMs = 3;
    CHECK(qualify_lease(native, sampled, qualified) && qualified.safeRemainingMs == 1 && qualified.native.remainingMs == 3);
    ++sampled.milliseconds;
    CHECK(!qualify_lease(native, sampled, qualified));
    --sampled.milliseconds;
    sampled.frequency = 0;
    CHECK(!qualify_lease(native, sampled, qualified));
}

void provider_observation_survives_inactive_worlds() {
    EffectServiceProvider service;
    CHECK(service.ObserveClock({100000, 1000000}) == ServiceStatus::ok); // Before BeginWorld.
    setup(service);
    CHECK(service.ObserveClock({100010, 1000000}) == ServiceStatus::ok);
    service.AbortWorld();
    CHECK(service.ObserveClock({100005, 1000000}) == ServiceStatus::clock_failed); // Same ms, inactive.
    CHECK(service.ObserveClock({200000, 1000000}) == ServiceStatus::clock_failed);
    CHECK(service.BeginWorld(8, 200) == ServiceStatus::clock_failed);
}

} // namespace

int main() {
    actual_interface_and_uninitialized_state();
    abort_and_clock_failure_are_timestamp_free();
    qualified_lease_cannot_overgrant();
    provider_observation_survives_inactive_worlds();
    random_worlds_stale_context_and_nonreuse();
    authoritative_targets_and_high_water_marks();
    deactivation_gates_every_operation_and_cannot_resume();
    real_registry_composition_expiry_and_ownership();
    bounded_ledgers_registry_capacity_and_exhaustion();
    allocation_failure_is_atomic();
    hot_paths_do_not_allocate_and_expire_independently();
    leased_snapshots_track_real_contribution_validity();
    std::cout << "real effect provider: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
