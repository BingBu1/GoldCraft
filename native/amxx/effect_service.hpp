#pragma once

#include <cstdint>
#include "goldcraft/effect_registry.hpp"
#include "effect_clock.hpp"

// Include the matched HLSDK interface.h first, as for server_api.hpp. This is a
// separate in-process x86 contract, not a modification of ServerControl001 and
// not an IPC packet. Both sides must build from this exact declaration.
namespace goldcraft::amxx_effects {

inline constexpr const char* service_version = "GoldCraftEffects002";
inline constexpr std::uint32_t maximum_lease_ms = 60000;

struct ServiceContext {
    std::uint64_t world = 0;
    std::uint64_t generation = 0;
    bool operator==(const ServiceContext&) const = default;
};

enum class ServiceStatus : std::uint32_t {
    ok = 0,
    not_ready = 1,
    stale_context = 2,
    invalid_owner = 3,
    invalid_target = 4,
    missing_effect = 5,
    wrong_owner = 6,
    invalid_state = 7,
    invalid_lease = 8,
    capacity_exhausted = 9,
    token_exhausted = 10,
    clock_regressed = 11,
    internal_error = 12,
    revision_exhausted = 13,
    combination_out_of_range = 14,
    clock_failed = 15,
};

// Host-local raw validity stays distinguishable from duration after the QPC
// conversion margin. No remote request/nonce, rate bound, anchor or grant here.
struct QualifiedLeasedSnapshot {
    effects::LeasedSnapshot native;
    std::uint32_t safeRemainingMs = 0;
    std::uint32_t marginMs = 0;
};
inline bool qualify_lease(const effects::LeasedSnapshot& native, const effect_clock::Sample& sample,
                          QualifiedLeasedSnapshot& output) noexcept {
    if (native.sampledAtMs != sample.milliseconds || !sample.frequency || !sample.marginMs) return false;
    const auto safe = effect_clock::safe_remaining(native.remainingMs, sample);
    if (!safe) return false; // A zero safe duration never qualifies for remote execution.
    output = {native, safe, sample.marginMs};
    return true;
}

// REQUIRED PROVIDER SEMANTICS (an analysis-only implementation lives under
// candidate/native/server; it is not wired to the production bridge):
// * Single host thread, no callbacks/reentrancy into Pawn; all methods noexcept.
// * GetContext returns nonzero current world and a never-reused service/map
//   generation. Every other method rejects stale context before any mutation.
// * CreateOwner mints a registry owner; it accepts no owner supplied by Pawn.
// * ResolveTarget consults actual native/MC-object authority and returns its
//   current serial/life. It must not invent objects or activate stale identities.
//   Every operation after GetContext in one adapter call uses the same supplied
//   monotonic timestamp; queries must not secretly sample a later clock and make
//   the next mutation reject that call's original timestamp as regressed.
// * Apply/Update/Remove/RevokeOwner delegate ownership and strong-target checks
//   to the authoritative EffectRegistry, using the supplied host monotonic ms.
// * Apply/Update success means accepted state registration, NOT physical effect
//   execution, committed native mutation, or a remote ACK. A newly registered
//   deferred motion intent must not be treated as already applied in native CS.
//   Typed provenance, actual physical commit and sync/ACK barriers belong to
//   separate game integration and are not supplied by this adapter contract.
// * Host/game and AMXX adapter use the same absolute QPC-to-ms epoch.
//   Host/game code independently samples that clock and pumps the registry before
//   snapshots/pose authorization and every host frame. Expiry must NOT depend on
//   AMXX callbacks: a paused/unloaded module cannot be the only cleanup clock.
// * No API stores borrowed pointers. Failure never leaves a partly added effect;
//   old valid state survives invalid payloads, apart from independently due expiry.
// * All outputs are valid only on ok. On reset the provider revokes old owners,
//   effects and targets before publishing its new generation.
class IEffectService : public IBaseInterface {
public:
    virtual ServiceStatus GetContext(ServiceContext* output) noexcept = 0;
    // 002 only: trusted in-process adapter reports its clock failure without a
    // fabricated timestamp or stale context. Closes all effect eligibility for
    // this provider lifetime, preserving nonreuse history. Not a Pawn native.
    virtual void ClockFault() noexcept = 0;
    // Record the trusted adapter's raw QPC sample in the common host high-water
    // mark BEFORE calls with its derived milliseconds. Host samples cross the
    // same boundary; per-DLL Clock checks alone cannot detect every regression.
    virtual ServiceStatus ObserveClock(const effect_clock::RawSample& sample) noexcept = 0;
    virtual ServiceStatus CreateOwner(const ServiceContext& context, std::uint64_t nowMs,
                                      std::uint64_t* owner) noexcept = 0;
    virtual ServiceStatus ResolveTarget(const ServiceContext& context, effects::TargetKind kind,
                                        std::uint64_t key, std::uint64_t nowMs,
                                        effects::Target* target) noexcept = 0;
    virtual ServiceStatus Apply(const ServiceContext& context, std::uint64_t owner,
                                const effects::Target& target, const effects::MovementState& state,
                                std::uint32_t leaseMs, std::uint64_t nowMs,
                                std::uint64_t* effect) noexcept = 0;
    virtual ServiceStatus Update(const ServiceContext& context, std::uint64_t owner,
                                 std::uint64_t effect, const effects::MovementState& state,
                                 std::uint32_t leaseMs, std::uint64_t nowMs) noexcept = 0;
    virtual ServiceStatus Remove(const ServiceContext& context, std::uint64_t owner,
                                 std::uint64_t effect, std::uint64_t nowMs) noexcept = 0;
    virtual ServiceStatus RevokeOwner(const ServiceContext& context, std::uint64_t owner,
                                      std::uint64_t nowMs) noexcept = 0;
};

} // namespace goldcraft::amxx_effects
