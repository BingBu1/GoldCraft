#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace goldcraft::effects {

enum class TargetKind : std::uint32_t { player = 1, object = 2 };

struct Target {
    std::uint64_t world = 0;
    TargetKind kind = TargetKind::player;
    std::uint64_t key = 0;
    std::uint32_t serial = 0;
    std::uint32_t life = 0;
    auto operator<=>(const Target&) const = default;
};

enum Capability : std::uint32_t {
    input_lock = 1u << 0,
    position_hold = 1u << 1,
    wish_speed_cap = 1u << 2,
    gravity_factor = 1u << 3,
};
enum InputLock : std::uint32_t {
    lock_move = 1u << 0,
    lock_jump = 1u << 1,
    lock_attack = 1u << 2,
    lock_use = 1u << 3,
};
enum HoldAxis : std::uint32_t { hold_x = 1u << 0, hold_y = 1u << 1, hold_z = 1u << 2 };

inline constexpr std::uint32_t all_capabilities = input_lock | position_hold | wish_speed_cap | gravity_factor;
inline constexpr std::uint32_t all_input_locks = lock_move | lock_jump | lock_attack | lock_use;
inline constexpr std::uint32_t all_hold_axes = hold_x | hold_y | hold_z;
inline constexpr float maximum_wish_speed_cap = 16384.0f;
inline constexpr float maximum_gravity_factor = 16.0f;
inline constexpr float maximum_combined_gravity = 64.0f;

struct MovementState {
    std::uint32_t capability = 0;
    std::uint32_t inputLocks = 0;
    std::uint32_t holdAxes = 0;
    // A missing cap is distinct from an explicit zero cap. This caps wish input,
    // never total velocity. Units are defined by the adapter, not this registry.
    float wishSpeedCap = 0.0f;
    // Normalized multiplier: zero means zero gravity. Native entvars gravity=0
    // meaning default gravity must be translated before calling this interface.
    float gravityFactor = 1.0f;
    bool operator==(const MovementState&) const = default;
};

struct Snapshot {
    Target target;
    std::uint64_t revision = 0;
    MovementState movement;
    bool operator==(const Snapshot&) const = default;
};

// Host-local validity, never a cross-process clock deadline. The response
// consumer must charge queue/roundtrip time from its own request timestamp.
struct LeasedSnapshot {
    Snapshot snapshot;
    std::uint64_t sampledAtMs = 0;
    std::uint32_t remainingMs = 0;
    bool operator==(const LeasedSnapshot&) const = default;
};

inline bool valid_target(const Target& target) noexcept {
    if (!target.world || !target.key || !target.serial || !target.life) return false;
    if (target.kind == TargetKind::player) return target.key <= 64;
    return target.kind == TargetKind::object;
}

// Strict wire-friendly shape validation; inactive fields must have canonical
// defaults. Aggregate gravity has a larger bound than a single contribution.
inline bool valid_movement(const MovementState& state) noexcept {
    if (state.capability & ~all_capabilities) return false;
    if ((state.inputLocks & ~all_input_locks) || (state.holdAxes & ~all_hold_axes)) return false;
    if (!std::isfinite(state.wishSpeedCap) || state.wishSpeedCap < 0.0f ||
        state.wishSpeedCap > maximum_wish_speed_cap) return false;
    if (!std::isfinite(state.gravityFactor) || state.gravityFactor < 0.0f ||
        state.gravityFactor > maximum_combined_gravity) return false;
    if (!(state.capability & input_lock) && state.inputLocks) return false;
    if (!(state.capability & position_hold) && state.holdAxes) return false;
    if (!(state.capability & wish_speed_cap) && state.wishSpeedCap != 0.0f) return false;
    if (!(state.capability & gravity_factor) && state.gravityFactor != 1.0f) return false;
    return true;
}

enum class Status {
    ok,
    not_initialized,
    invalid_epoch,
    clock_regressed,
    invalid_target,
    target_missing,
    target_conflict,
    invalid_owner,
    handle_missing,
    wrong_owner,
    invalid_state,
    invalid_lease,
    capacity_exhausted,
    token_exhausted,
    revision_exhausted,
    combination_out_of_range,
};

struct TokenResult {
    Status status = Status::ok;
    std::uint64_t token = 0;
    explicit operator bool() const noexcept { return status == Status::ok; }
};

struct Limits {
    std::size_t owners = 64;
    std::size_t targets = 128;
    std::size_t effects = 512;
    std::uint64_t leaseMs = 60000;
};

// This is an authority-side state container, not caller authentication. Only the
// AMXX adapter may mint owners/activate targets/reset worlds. It must derive
// callerOwner from the current AMX registration, never from Pawn arguments.
// Single host thread; no AMX pointers, transport, callbacks, or physics here.
class EffectRegistry {
public:
    // firstToken permits restoring an externally persisted high-water mark and
    // testing exhaustion. Never construct a replacement registry at token 1
    // while old handles can still exist. Zero denotes an exhausted allocator.
    explicit EffectRegistry(Limits limits = {}, std::uint64_t firstToken = 1)
        : limits_(limits), nextToken_(firstToken) {
        if (!limits.owners || limits.owners > 4096 || !limits.targets || limits.targets > 4096 ||
            !limits.effects || limits.effects > 16384 || !limits.leaseMs || limits.leaseMs > 60000)
            throw std::invalid_argument("effect registry limits outside supported bounds");
    }

    // The authority supplies a fresh, never-reused epoch. Consecutive reuse is
    // rejected; global epoch uniqueness across authorities belongs to pairing.
    Status reset(std::uint64_t world, std::uint64_t nowMs) {
        if (!world || world == world_) return Status::invalid_epoch;
        if (initialized_ && nowMs < nowMs_) return Status::clock_regressed;
        owners_.clear();
        effects_.clear();
        targets_.clear();
        world_ = world;
        nowMs_ = nowMs;
        initialized_ = true;
        nextExpiry_.reset();
        // Deliberately do not reset nextToken_.
        return Status::ok;
    }

    Status activate_target(const Target& target, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return status;
        if (!valid_target(target) || target.world != world_) return Status::invalid_target;
        if (targets_.contains(target)) return Status::ok;
        for (const auto& [identity, record] : targets_) {
            (void)record;
            if (identity.kind == target.kind && identity.key == target.key) return Status::target_conflict;
        }
        if (targets_.size() == limits_.targets) return Status::capacity_exhausted;
        targets_.emplace(target, TargetRecord{Snapshot{target, 1, {}}});
        return Status::ok;
    }

    TokenResult create_owner(std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return {status, 0};
        if (owners_.size() == limits_.owners) return {Status::capacity_exhausted, 0};
        if (!nextToken_) return {Status::token_exhausted, 0};
        const auto token = nextToken_;
        owners_.insert(token);
        consume_token();
        return {Status::ok, token};
    }

    TokenResult apply(std::uint64_t callerOwner, const Target& target, const MovementState& state,
                      std::uint64_t leaseMs, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return {status, 0};
        if (!owners_.contains(callerOwner)) return {Status::invalid_owner, 0};
        if (!valid_target(target) || target.world != world_) return {Status::invalid_target, 0};
        const auto entry = targets_.find(target);
        if (entry == targets_.end()) return {Status::target_missing, 0};
        if (!valid_contribution(state)) return {Status::invalid_state, 0};
        if (!valid_lease(leaseMs)) return {Status::invalid_lease, 0};
        if (effects_.size() == limits_.effects) return {Status::capacity_exhausted, 0};
        if (!nextToken_) return {Status::token_exhausted, 0};

        const auto token = nextToken_;
        const Effect proposed{callerOwner, target, state, nowMs_ + leaseMs};
        Aggregate aggregate;
        if (!combine(target, token, &proposed, true, aggregate)) return {Status::combination_out_of_range, 0};
        if (!revision_room(entry->second, aggregate)) return {Status::revision_exhausted, 0};
        effects_.emplace(token, proposed);
        consume_token();
        publish(entry->second, aggregate);
        note_expiry(proposed.expires);
        return {Status::ok, token};
    }

    Status update(std::uint64_t callerOwner, std::uint64_t handle, const MovementState& state,
                  std::uint64_t leaseMs, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return status;
        if (!owners_.contains(callerOwner)) return Status::invalid_owner;
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return Status::handle_missing;
        if (found->second.owner != callerOwner) return Status::wrong_owner;
        if (!valid_contribution(state)) return Status::invalid_state;
        if (!valid_lease(leaseMs)) return Status::invalid_lease;
        if (found->second.state == state) {
            const auto previousExpiry = found->second.expires;
            found->second.expires = nowMs_ + leaseMs;
            auto& record = targets_.find(found->second.target)->second;
            if (found->second.expires <= record.earliestExpiry) {
                record.earliestExpiry = found->second.expires;
            } else if (previousExpiry == record.earliestExpiry) {
                // Only extending a former minimum needs a bounded rescan.
                // Lease lookup after the expiry barrier is O(log targets).
                record.earliestExpiry = std::numeric_limits<std::uint64_t>::max();
                for (const auto& [token, effect] : effects_) {
                    (void)token;
                    if (effect.target == found->second.target)
                        record.earliestExpiry = std::min(record.earliestExpiry, effect.expires);
                }
            }
            note_expiry(found->second.expires);
            return Status::ok;
        }
        const Effect proposed{callerOwner, found->second.target, state, nowMs_ + leaseMs};
        Aggregate aggregate;
        if (!combine(proposed.target, handle, &proposed, false, aggregate)) return Status::combination_out_of_range;
        auto& target = targets_.find(proposed.target)->second;
        if (!revision_room(target, aggregate)) return Status::revision_exhausted;
        found->second = proposed;
        publish(target, aggregate);
        note_expiry(proposed.expires);
        return Status::ok;
    }

    Status remove(std::uint64_t callerOwner, std::uint64_t handle, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return status;
        if (!owners_.contains(callerOwner)) return Status::invalid_owner;
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return Status::handle_missing;
        if (found->second.owner != callerOwner) return Status::wrong_owner;
        targets_.find(found->second.target)->second.dirty = true;
        effects_.erase(found);
        refresh_dirty();
        return Status::ok;
    }

    Status revoke_owner(std::uint64_t callerOwner, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return status;
        if (!owners_.erase(callerOwner)) return Status::invalid_owner;
        for (auto effect = effects_.begin(); effect != effects_.end();) {
            if (effect->second.owner == callerOwner) {
                targets_.find(effect->second.target)->second.dirty = true;
                effect = effects_.erase(effect);
            } else ++effect;
        }
        refresh_dirty();
        return Status::ok;
    }

    // Only the authority calls this on death/respawn/disconnect/entity removal.
    // Reactivation of an old identity is not a Pawn operation. The adapter must
    // use the current authoritative serial/life when activating its replacement.
    Status invalidate_target(const Target& target, std::uint64_t nowMs) {
        if (const auto status = pump(nowMs); status != Status::ok) return status;
        if (!valid_target(target) || target.world != world_) return Status::invalid_target;
        if (!targets_.erase(target)) return Status::target_missing;
        for (auto effect = effects_.begin(); effect != effects_.end();) {
            if (effect->second.target == target) effect = effects_.erase(effect);
            else ++effect;
        }
        return Status::ok;
    }

    // Every mutation advances through this expiration barrier first. Rejected
    // payloads do not alter contributions; independent expiry at nowMs still
    // applies. A regressing clock rejects the entire call without any change.
    Status pump(std::uint64_t nowMs) {
        if (!initialized_) return Status::not_initialized;
        if (nowMs < nowMs_) return Status::clock_regressed;
        nowMs_ = nowMs;
        if (!nextExpiry_ || nowMs < *nextExpiry_) return Status::ok;
        nextExpiry_.reset();
        for (auto effect = effects_.begin(); effect != effects_.end();) {
            if (effect->second.expires <= nowMs) {
                targets_.find(effect->second.target)->second.dirty = true;
                effect = effects_.erase(effect);
            } else {
                note_expiry(effect->second.expires);
                ++effect;
            }
        }
        refresh_dirty();
        return Status::ok;
    }

    // No allocation or clock access. The integration must pump(hostNow) before
    // querying/authorizing a pose. Snapshots are copies, never borrowed handles.
    std::optional<Snapshot> query(const Target& target) const {
        const auto found = targets_.find(target);
        if (found == targets_.end()) return std::nullopt;
        return found->second.snapshot;
    }

    // Caller must pump its current host time first. A redundant/no-op owner
    // contribution also bounds validity: revision alone cannot prove freshness.
    std::optional<LeasedSnapshot> query_leased(const Target& target, std::uint32_t freshnessMs) const noexcept {
        if (!freshnessMs || freshnessMs > 60000) return std::nullopt;
        const auto found = targets_.find(target);
        if (found == targets_.end()) return std::nullopt;
        const auto deadline = found->second.earliestExpiry;
        // UINT64_MAX also represents no contribution. At clock exhaustion no
        // positive validity can be granted; subtraction never wraps.
        const auto remaining = deadline > nowMs_ ? std::min<std::uint64_t>(freshnessMs, deadline - nowMs_) : 0;
        return LeasedSnapshot{found->second.snapshot, nowMs_, static_cast<std::uint32_t>(remaining)};
    }

private:
    struct Effect {
        std::uint64_t owner;
        Target target;
        MovementState state;
        std::uint64_t expires;
    };
    struct TargetRecord {
        Snapshot snapshot;
        bool dirty = false;
        std::uint64_t earliestExpiry = std::numeric_limits<std::uint64_t>::max();
    };
    struct Aggregate {
        MovementState state;
        std::size_t count = 0;
        std::uint64_t earliestExpiry = std::numeric_limits<std::uint64_t>::max();
    };

    static bool valid_contribution(const MovementState& state) noexcept {
        return valid_movement(state) && state.gravityFactor <= maximum_gravity_factor;
    }

    bool valid_lease(std::uint64_t leaseMs) const noexcept {
        return leaseMs && leaseMs <= limits_.leaseMs &&
               leaseMs <= std::numeric_limits<std::uint64_t>::max() - nowMs_;
    }

    void consume_token() noexcept {
        nextToken_ = nextToken_ == std::numeric_limits<std::uint64_t>::max() ? 0 : nextToken_ + 1;
    }

    void note_expiry(std::uint64_t expires) noexcept {
        if (!nextExpiry_ || expires < *nextExpiry_) nextExpiry_ = expires;
    }

    // Counterfactual add/update is computed before mutation, in increasing handle
    // order. Envelopes guarantee every subset stays bounded after removal/expiry:
    // a zero or reducing multiplier cannot hide an excessive positive product.
    bool combine(const Target& target, std::uint64_t replacementHandle, const Effect* replacement,
                 bool append, Aggregate& result) const noexcept {
        result = {};
        double increasing = 1.0;
        double reducing = 1.0;
        double gravity = 1.0;
        const auto add = [&](const Effect& contribution) -> bool {
            const auto& state = contribution.state;
            result.earliestExpiry = std::min(result.earliestExpiry, contribution.expires);
            ++result.count;
            result.state.inputLocks |= state.inputLocks;
            result.state.holdAxes |= state.holdAxes;
            if (state.capability & wish_speed_cap) {
                if (!(result.state.capability & wish_speed_cap)) result.state.wishSpeedCap = state.wishSpeedCap;
                else result.state.wishSpeedCap = std::min(result.state.wishSpeedCap, state.wishSpeedCap);
                result.state.capability |= wish_speed_cap;
            }
            if (state.capability & gravity_factor) {
                const double factor = state.gravityFactor;
                if (factor > 1.0) {
                    increasing *= factor;
                    if (increasing > maximum_combined_gravity) return false;
                } else if (factor > 0.0 && factor < 1.0) {
                    reducing *= factor;
                    if (reducing < std::numeric_limits<float>::min()) return false;
                }
                const double product = gravity * factor;
                if (!std::isfinite(product) || product > maximum_combined_gravity ||
                    (product > 0.0 && product < std::numeric_limits<float>::min())) return false;
                gravity = product;
            }
            return true;
        };
        for (const auto& [handle, effect] : effects_) {
            if (effect.target != target) continue;
            if (handle == replacementHandle) {
                if (replacement && !add(*replacement)) return false;
            } else if (!add(effect)) return false;
        }
        if (append && replacement && !add(*replacement)) return false;
        if (result.state.inputLocks) result.state.capability |= input_lock;
        if (result.state.holdAxes) result.state.capability |= position_hold;
        const float finalGravity = static_cast<float>(gravity);
        if (finalGravity != 1.0f) {
            result.state.capability |= gravity_factor;
            result.state.gravityFactor = finalGravity;
        }
        return true;
    }

    static bool revision_room(const TargetRecord& target, const Aggregate& result) noexcept {
        const std::uint64_t change = target.snapshot.movement != result.state ? 1 : 0;
        // Reserve enough revisions for all future deletions. Admission can fail;
        // expiration/owner cleanup must never fail or wrap the revision counter.
        const auto reserve = static_cast<std::uint64_t>(result.count) + change;
        return target.snapshot.revision <= std::numeric_limits<std::uint64_t>::max() - reserve;
    }

    static void publish(TargetRecord& target, const Aggregate& aggregate) noexcept {
        if (target.snapshot.movement != aggregate.state) {
            target.snapshot.movement = aggregate.state;
            ++target.snapshot.revision;
        }
        target.earliestExpiry = aggregate.earliestExpiry;
        target.dirty = false;
    }

    void refresh_dirty() noexcept {
        for (auto& [identity, target] : targets_) {
            if (!target.dirty) continue;
            Aggregate aggregate;
            // Existing subsets satisfy combine's bounds by admission invariant.
            // An unexpected violation must not publish partially combined state.
            if (!combine(identity, 0, nullptr, false, aggregate)) std::terminate();
            publish(target, aggregate);
        }
    }

    Limits limits_;
    std::uint64_t nextToken_;
    std::uint64_t world_ = 0;
    std::uint64_t nowMs_ = 0;
    bool initialized_ = false;
    std::optional<std::uint64_t> nextExpiry_;
    std::set<std::uint64_t> owners_;
    std::map<Target, TargetRecord> targets_;
    std::map<std::uint64_t, Effect> effects_;
};

} // namespace goldcraft::effects
