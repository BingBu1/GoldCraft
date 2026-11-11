// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace guest::cof
{
// Host-owned, nonzero generations. Advance mapEpoch on map/session reset and
// modelGeneration whenever this actor's model/role or model content changes.
struct PlayerModelGeneration
{
    std::uint64_t mapEpoch{}, modelGeneration{};
    bool operator==(const PlayerModelGeneration&) const = default;
};

struct PlayerActionLabels
{
    std::string_view standing, crouching;
};

struct PlayerActionSequence
{
    std::int32_t sequence{-1}, frames{}, flags{};
    float fps{}, sdkDuration{}, normalizedFrameRate{};
};

// Supply the actual host Q_stricmp implementation, including its configured
// optimized/CRT branch. Invoked only while cold binding, never by Select.
using PlayerActionLabelCompare = int (*)(const char*, const char*);

class PlayerActionBindings
{
public:
    static constexpr std::size_t Capacity = 32;

    // Authoritative game thread only. The caller first runs its full model
    // validation; Bind rechecks the header/descriptor data that it consumes.
    // Explicit full ref_/crouch_ labels only; no weapon-family inference.
    // Any failed Bind invalidates the previous cache. No source pointer or
    // string_view survives this operation. error allocation is cold only.
    bool Bind(std::span<const std::byte> validatedModel,
              PlayerModelGeneration generation,
              std::span<const PlayerActionLabels> labels,
              PlayerActionLabelCompare compare, std::string& error);

    // O(1), no allocations, I/O or source-model access. Missing action, stale
    // generation or unbound state returns nullopt, never sequence zero.
    // Apply immediately under the same host owner/life/map/model validation;
    // changing generations cannot revoke a value the caller already copied.
    std::optional<PlayerActionSequence> Select(PlayerModelGeneration current,
                                              std::size_t action, bool crouching) const noexcept;
    void Reset() noexcept;
    std::size_t Size() const noexcept { return size_; }

private:
    struct Pair { PlayerActionSequence standing, crouching; };
    std::array<Pair, Capacity> values_{};
    PlayerModelGeneration generation_{};
    std::size_t size_{};
};
}
