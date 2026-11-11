// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace guest::cof
{
inline constexpr std::uint8_t SoundProtocol = 1;
inline constexpr std::size_t SoundHeaderBytes = 12;
inline constexpr std::size_t SoundPacketLimit = 192; // Actual ReHLDS user-message limit.
inline constexpr std::size_t SoundSampleLimit = SoundPacketLimit - SoundHeaderBytes - 1;

struct SoundWire
{
    std::string_view sample;
    std::uint8_t mode{}, enabled{}, remote{};
    // Original READ_COORD consumes signed eighths. The caller supplies already
    // quantized words; this codec does not guess original server rounding.
    std::array<std::int16_t, 3> originEighths{};
    std::int16_t recipientIndex{}; // Consumed and unused by original playback.
};

struct SoundMessageData
{
    std::string_view sample;
    std::uint8_t mode{}, enabled{}, remote{};
    std::array<float, 3> origin{};
    std::int16_t recipientIndex{};
};

struct SoundPacket
{
    std::array<std::byte, SoundPacketLimit> bytes{};
    std::uint8_t size{};
    std::span<const std::byte> View() const noexcept { return {bytes.data(), size}; }
};

// Host transport, not the original PlaySFX wire ABI. Failed operations preserve
// their output. Decode borrows the input packet's sample; consume synchronously.
bool EncodeSound(const SoundWire& value, SoundPacket& output) noexcept;
bool DecodeSound(std::span<const std::byte> bytes, SoundMessageData& output) noexcept;
}
