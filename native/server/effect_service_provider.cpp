#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "effect_service_provider.hpp"

#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace goldcraft::amxx_effects {
namespace {
template<class Operation>
ServiceStatus guarded(Operation&& operation) noexcept {
    try {
        return operation();
    } catch (const std::bad_alloc&) {
        return ServiceStatus::capacity_exhausted;
    } catch (...) {
        return ServiceStatus::internal_error;
    }
}
} // namespace

EffectServiceProvider::EffectServiceProvider(ProviderLimits limits, std::uint64_t firstToken,
                                             std::uint64_t firstGeneration)
    : limits_(limits), registry_(limits.registry, firstToken), nextGeneration_(firstGeneration) {
    if (!limits.targetKeysPerWorld || limits.targetKeysPerWorld > 65536 ||
        limits.targetKeysPerWorld < limits.registry.targets ||
        !limits.worldsPerProvider || limits.worldsPerProvider > 65536)
        throw std::invalid_argument("effect provider ledger limits outside supported bounds");
}

ServiceStatus EffectServiceProvider::Translate(effects::Status status) noexcept {
    using S = effects::Status;
    switch (status) {
    case S::ok: return ServiceStatus::ok;
    case S::not_initialized: return ServiceStatus::not_ready;
    case S::invalid_epoch: return ServiceStatus::stale_context;
    case S::clock_regressed: return ServiceStatus::clock_regressed;
    case S::invalid_target:
    case S::target_missing:
    case S::target_conflict: return ServiceStatus::invalid_target;
    case S::invalid_owner: return ServiceStatus::invalid_owner;
    case S::handle_missing: return ServiceStatus::missing_effect;
    case S::wrong_owner: return ServiceStatus::wrong_owner;
    case S::invalid_state: return ServiceStatus::invalid_state;
    case S::invalid_lease: return ServiceStatus::invalid_lease;
    case S::capacity_exhausted: return ServiceStatus::capacity_exhausted;
    case S::token_exhausted: return ServiceStatus::token_exhausted;
    case S::revision_exhausted: return ServiceStatus::revision_exhausted;
    case S::combination_out_of_range: return ServiceStatus::combination_out_of_range;
    }
    return ServiceStatus::internal_error;
}

ServiceStatus EffectServiceProvider::CheckContext(const ServiceContext& context) const noexcept {
    if (clockFailed_) return ServiceStatus::clock_failed;
    if (!active_) return ServiceStatus::not_ready;
    return context == context_ ? ServiceStatus::ok : ServiceStatus::stale_context;
}

bool EffectServiceProvider::Newer(const effects::Target& candidate,
                                  const effects::Target& previous) noexcept {
    return candidate.serial > previous.serial ||
        (candidate.serial == previous.serial && candidate.life > previous.life);
}

ServiceStatus EffectServiceProvider::BeginWorld(std::uint64_t world, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (clockFailed_) return ServiceStatus::clock_failed;
        if (!world || usedWorlds_.contains(world)) return ServiceStatus::stale_context;
        if (!nextGeneration_) return ServiceStatus::token_exhausted;
        if (usedWorlds_.size() >= limits_.worldsPerProvider) return ServiceStatus::capacity_exhausted;
        // Allocate the ledger node BEFORE registry reset. reset clears nodes but
        // allocates none, so no failed allocation can leave a half-reset world.
        const auto inserted = usedWorlds_.insert(world);
        const auto status = registry_.reset(world, nowMs);
        if (status != effects::Status::ok) {
            usedWorlds_.erase(inserted.first);
            return Translate(status);
        }
        identities_.clear();
        context_ = {world, nextGeneration_};
        active_ = true;
        nextGeneration_ = nextGeneration_ == std::numeric_limits<std::uint64_t>::max()
            ? 0 : nextGeneration_ + 1;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::EndWorld(const ServiceContext& context, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        active_ = false;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::RegisterTarget(const ServiceContext& context,
    const effects::Target& target, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        if (!effects::valid_target(target) || target.world != context_.world) return ServiceStatus::invalid_target;
        const Key key{target.kind, target.key};
        auto previous = identities_.find(key);
        if (previous != identities_.end()) {
            if (previous->second.live && previous->second.highWater == target) return ServiceStatus::ok;
            if (!Newer(target, previous->second.highWater)) return ServiceStatus::invalid_target;
        } else if (identities_.size() >= limits_.targetKeysPerWorld) {
            return ServiceStatus::capacity_exhausted;
        }

        // Replacement has to allocate a new registry node after removing the old
        // one. Prepare it in an actual registry copy: allocation/admission failure
        // leaves the old target, effects and revision intact (apart from expiry).
        // Only host lifecycle registration pays this bounded copy cost.
        effects::EffectRegistry prepared(registry_);
        if (previous != identities_.end() && previous->second.live) {
            const auto status = prepared.invalidate_target(previous->second.highWater, nowMs);
            if (status != effects::Status::ok) return Translate(status);
        }
        const auto status = prepared.activate_target(target, nowMs);
        if (status != effects::Status::ok) return Translate(status);

        // Prepare the high-water node before committing either structure. Existing
        // nodes need no allocation. No throwing operation follows this insertion.
        if (previous == identities_.end())
            previous = identities_.emplace(key, Identity{target, true}).first;
        static_assert(std::is_nothrow_move_assignable_v<effects::EffectRegistry>);
        registry_ = std::move(prepared);
        previous->second = {target, true};
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::InvalidateTarget(const ServiceContext& context,
    const effects::Target& target, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        if (!effects::valid_target(target) || target.world != context_.world) return ServiceStatus::invalid_target;
        const auto found = identities_.find({target.kind, target.key});
        if (found == identities_.end() || !found->second.live || found->second.highWater != target)
            return ServiceStatus::invalid_target;
        const auto status = registry_.invalidate_target(target, nowMs);
        if (status != effects::Status::ok) return Translate(status);
        found->second.live = false; // Keep the exact tombstone and high-water mark.
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::Pump(const ServiceContext& context, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        return Translate(registry_.pump(nowMs));
    });
}

ServiceStatus EffectServiceProvider::ReadSnapshot(const ServiceContext& context,
    const effects::Target& target, std::uint64_t nowMs, effects::Snapshot* output) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (!output) return ServiceStatus::invalid_state;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        const auto snapshot = registry_.query(target);
        if (!snapshot) return ServiceStatus::invalid_target;
        *output = *snapshot;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::ReadLeasedSnapshot(const ServiceContext& context,
    const effects::Target& target, std::uint64_t nowMs, std::uint32_t freshnessMs,
    effects::LeasedSnapshot* output) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (!output) return ServiceStatus::invalid_state;
        if (!freshnessMs || freshnessMs > 60000) return ServiceStatus::invalid_lease;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        const auto value = registry_.query_leased(target, freshnessMs);
        if (!value) return ServiceStatus::invalid_target;
        *output = *value;
        return ServiceStatus::ok;
    });
}

void EffectServiceProvider::AbortWorld() noexcept {
    active_ = false;
}

void EffectServiceProvider::ClockFault() noexcept {
    clockFailed_ = true;
    AbortWorld();
}

ServiceStatus EffectServiceProvider::ObserveClock(const effect_clock::RawSample& sample) noexcept {
    if (clockFailed_) return ServiceStatus::clock_failed;
    effect_clock::Sample qualified;
    if (observedClock_.observe(sample, qualified) == effect_clock::Error::none) return ServiceStatus::ok;
    ClockFault();
    return ServiceStatus::clock_failed;
}

ServiceStatus EffectServiceProvider::GetContext(ServiceContext* output) noexcept {
    if (!output) return ServiceStatus::invalid_state;
    if (clockFailed_) return ServiceStatus::clock_failed;
    if (!active_) return ServiceStatus::not_ready;
    *output = context_;
    return ServiceStatus::ok;
}

ServiceStatus EffectServiceProvider::CreateOwner(const ServiceContext& context,
    std::uint64_t nowMs, std::uint64_t* owner) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (!owner) return ServiceStatus::invalid_state;
        const auto result = registry_.create_owner(nowMs);
        if (!result) return Translate(result.status);
        *owner = result.token;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::ResolveTarget(const ServiceContext& context,
    effects::TargetKind kind, std::uint64_t key, std::uint64_t nowMs, effects::Target* target) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (!target) return ServiceStatus::invalid_state;
        if (const auto status = registry_.pump(nowMs); status != effects::Status::ok) return Translate(status);
        const auto found = identities_.find({kind, key});
        if (found == identities_.end() || !found->second.live) return ServiceStatus::invalid_target;
        *target = found->second.highWater;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::Apply(const ServiceContext& context, std::uint64_t owner,
    const effects::Target& target, const effects::MovementState& state, std::uint32_t leaseMs,
    std::uint64_t nowMs, std::uint64_t* effect) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        if (!effect) return ServiceStatus::invalid_state;
        const auto result = registry_.apply(owner, target, state, leaseMs, nowMs);
        if (!result) return Translate(result.status);
        *effect = result.token;
        return ServiceStatus::ok;
    });
}

ServiceStatus EffectServiceProvider::Update(const ServiceContext& context, std::uint64_t owner,
    std::uint64_t effect, const effects::MovementState& state, std::uint32_t leaseMs,
    std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        return Translate(registry_.update(owner, effect, state, leaseMs, nowMs));
    });
}

ServiceStatus EffectServiceProvider::Remove(const ServiceContext& context, std::uint64_t owner,
    std::uint64_t effect, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        return Translate(registry_.remove(owner, effect, nowMs));
    });
}

ServiceStatus EffectServiceProvider::RevokeOwner(const ServiceContext& context,
    std::uint64_t owner, std::uint64_t nowMs) noexcept {
    return guarded([&] {
        if (const auto status = CheckContext(context); status != ServiceStatus::ok) return status;
        return Translate(registry_.revoke_owner(owner, nowMs));
    });
}

IBaseInterface* QueryEffectServiceInterface(EffectServiceProvider& provider,
    const char* name, int* result) noexcept {
    const bool matched = name && std::strcmp(name, service_version) == 0;
    if (result) *result = matched ? IFACE_OK : IFACE_FAILED;
    return matched ? static_cast<IEffectService*>(&provider) : nullptr;
}

} // namespace goldcraft::amxx_effects
