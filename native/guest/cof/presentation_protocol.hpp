// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "authority.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace guest::cof
{
inline constexpr char StateMessage[] = "CoFWpnState";
inline constexpr char SoundMessage[] = "CoFSFX";
inline constexpr std::uint8_t StateProtocol = 1;
inline constexpr std::size_t StateBytes = 24;

struct PresentationState
{
    AuthorityKey key;
    std::uint32_t revision{};
    // None (zero) explicitly releases the active presentation. Other values
    // are original CoF IDs, independent of the host inventory IDs.
    std::uint8_t originalWeapon{};
    bool aimEnabled{};
    std::uint16_t fovMode{};
    // Original CameraZoom uses signed short coordinates in eighths.
    std::int16_t zoomSpeedEighths{};
    bool operator==(const PresentationState&) const noexcept = default;
};

using StatePacket = std::array<std::byte, StateBytes>;
bool EncodeState(const PresentationState& state, StatePacket& output) noexcept;
bool DecodeState(std::span<const std::byte> bytes, PresentationState& output) noexcept;

enum class StateAcceptance { Accepted, Invalid, PreviousSession, PreviousLife, PreviousInstance, Replayed };

// Reliable packets still need explicit life/model-instance invalidation.
// Scope may only advance within one authenticated server connection. Reset
// when that connection ends, never in response to an untrusted state packet.
class PresentationReceiver
{
public:
    void Reset() noexcept;
    StateAcceptance Receive(std::span<const std::byte> bytes, PresentationState& output) noexcept;
    const PresentationState* Current() const noexcept { return initialized_ ? &current_ : nullptr; }

private:
    PresentationState current_{};
    bool initialized_{};
};
}
