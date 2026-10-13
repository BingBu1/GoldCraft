#pragma once

// Include the matched ReGameDLL public/interface.h before this header.
#include "../amxx/effect_service.hpp"

#include <cstddef>
#include <map>
#include <set>

namespace goldcraft::amxx_effects {

struct ProviderLimits {
    effects::Limits registry{};
    // Includes inactive tombstones; no eviction is safe within a world.
    std::size_t targetKeysPerWorld = 8192;
    // World epochs are random, not ordered. Never evict used-world identities.
    std::size_t worldsPerProvider = 256;
};

// Analysis-only provider backed by the actual EffectRegistry. No engine hook,
// export registration, AMXX VM, physics, typed motion receipt or network ACK is
// implemented here. Only the host holds this concrete class; Pawn sees the
// narrower IEffectService through the adapter and cannot register targets.
//
// One provider lives for the host service lifetime. It is deliberately neither
// copyable nor movable. BeginWorld clears owners/effects/targets but never its
// owner/effect allocator, generation allocator, or used-world ledger. Rebuilding
// this object/DLL with old tokens still reachable is NOT a supported reset.
// Constructors may reject invalid configuration; all operational APIs noexcept.
// All calls occur on one host thread, without callbacks or reentrancy.
// ResolveTarget reads this registered authority view, not engine memory. Actual
// kill/disconnect/object-stream reset must invalidate before any subsequent API
// or pose authorization; the eventual bridge must verify alive state at its
// real lifecycle/read boundaries. This candidate cannot repair a missed hook.
class EffectServiceProvider final : public IEffectService {
public:
    explicit EffectServiceProvider(ProviderLimits limits = {},
                                   std::uint64_t firstToken = 1,
                                   std::uint64_t firstGeneration = 1);
    EffectServiceProvider(const EffectServiceProvider&) = delete;
    EffectServiceProvider& operator=(const EffectServiceProvider&) = delete;
    EffectServiceProvider(EffectServiceProvider&&) = delete;
    EffectServiceProvider& operator=(EffectServiceProvider&&) = delete;

    // HOST ONLY. Each nonzero random world may be used once in this provider's
    // lifetime. A rejected transition preserves the old context and registry.
    // generation is minted here, never supplied by Pawn or reset by BeginWorld.
    ServiceStatus BeginWorld(std::uint64_t world, std::uint64_t nowMs) noexcept;

    // HOST ONLY. Deactivation first pumps a non-regressing host time, then gates
    // every query/mutation with not_ready, including GetContext. Repeat EndWorld
    // is not_ready. The bounded old registry allocation stays private until the
    // next BeginWorld resets it; nothing may read/apply it through this provider.
    // Old world cannot resume. All token/generation/used-world history survives.
    ServiceStatus EndWorld(const ServiceContext& context, std::uint64_t nowMs) noexcept;

    // HOST ONLY. Already-live exact identity is idempotent, preserving revision.
    // A dead identity cannot reactivate. For the same kind/key, serial must grow,
    // or life must grow with unchanged serial; neither counter may wrap. A new
    // serial may restart life. Replacing an active identity atomically revokes
    // its effects. Failed admission does not consume the new high-water mark.
    // serial/life MUST originate in the real host lifecycle: player binding
    // incarnation/life, or an independently issued object incarnation/life.
    // Pairing is not required. Do not synthesize object serial/life as constant 1.
    ServiceStatus RegisterTarget(const ServiceContext& context,
                                 const effects::Target& target, std::uint64_t nowMs) noexcept;
    ServiceStatus InvalidateTarget(const ServiceContext& context,
                                   const effects::Target& target, std::uint64_t nowMs) noexcept;

    // HOST ONLY. Call every host frame and before pose authorization regardless
    // of AMXX/Pawn pause state. ReadSnapshot also crosses this expiry barrier.
    ServiceStatus Pump(const ServiceContext& context, std::uint64_t nowMs) noexcept;
    ServiceStatus ReadSnapshot(const ServiceContext& context, const effects::Target& target,
                              std::uint64_t nowMs, effects::Snapshot* output) noexcept;

    // HOST ONLY; same atomic pump/read boundary as ReadSnapshot. The earliest
    // contribution expiry is conservative even when removal leaves state equal.
    // No promise survives an explicit later mutation/revoke; native admission
    // must re-read current state before applying a returned position.
    ServiceStatus ReadLeasedSnapshot(const ServiceContext& context, const effects::Target& target,
                                    std::uint64_t nowMs, std::uint32_t freshnessMs,
                                    effects::LeasedSnapshot* output) noexcept;

    // Emergency host teardown needs no time. The old registry stays inaccessible
    // and token/generation/world ledgers survive; a new unused world may begin.
    void AbortWorld() noexcept;
    // Clock failure latches across BeginWorld and ordinary map/plugin reloads.
    void ClockFault() noexcept override;
    ServiceStatus ObserveClock(const effect_clock::RawSample& sample) noexcept override;
    ServiceStatus GetContext(ServiceContext* output) noexcept override;
    ServiceStatus CreateOwner(const ServiceContext& context, std::uint64_t nowMs,
                              std::uint64_t* owner) noexcept override;
    ServiceStatus ResolveTarget(const ServiceContext& context, effects::TargetKind kind,
                                std::uint64_t key, std::uint64_t nowMs,
                                effects::Target* target) noexcept override;
    ServiceStatus Apply(const ServiceContext& context, std::uint64_t owner,
                        const effects::Target& target, const effects::MovementState& state,
                        std::uint32_t leaseMs, std::uint64_t nowMs,
                        std::uint64_t* effect) noexcept override;
    ServiceStatus Update(const ServiceContext& context, std::uint64_t owner,
                         std::uint64_t effect, const effects::MovementState& state,
                         std::uint32_t leaseMs, std::uint64_t nowMs) noexcept override;
    ServiceStatus Remove(const ServiceContext& context, std::uint64_t owner,
                         std::uint64_t effect, std::uint64_t nowMs) noexcept override;
    ServiceStatus RevokeOwner(const ServiceContext& context, std::uint64_t owner,
                              std::uint64_t nowMs) noexcept override;

private:
    struct Key {
        effects::TargetKind kind;
        std::uint64_t key;
        auto operator<=>(const Key&) const = default;
    };
    struct Identity { effects::Target highWater; bool live; };

    ServiceStatus CheckContext(const ServiceContext& context) const noexcept;
    static ServiceStatus Translate(effects::Status status) noexcept;
    static bool Newer(const effects::Target& candidate, const effects::Target& previous) noexcept;

    ProviderLimits limits_;
    effects::EffectRegistry registry_;
    ServiceContext context_{};
    bool active_ = false;
    bool clockFailed_ = false;
    effect_clock::Clock observedClock_{nullptr};
    std::uint64_t nextGeneration_;
    std::set<std::uint64_t> usedWorlds_;
    std::map<Key, Identity> identities_;
};

// Small real dispatch helper for the eventual existing bridge CreateInterface
// path. It does NOT register or export a new factory by itself and does not
// replace GoldCraftServerControl001. The host retains provider lifetime.
IBaseInterface* QueryEffectServiceInterface(EffectServiceProvider& provider,
                                          const char* name, int* result) noexcept;

} // namespace goldcraft::amxx_effects
