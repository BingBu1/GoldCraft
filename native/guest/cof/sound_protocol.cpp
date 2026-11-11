// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound_protocol.hpp"

#include <bit>
#include <cstring>

namespace guest::cof
{
namespace
{
void Store(std::array<std::byte, SoundPacketLimit>& bytes, std::size_t at, std::int16_t value) noexcept
{
    const auto word = std::bit_cast<std::uint16_t>(value);
    bytes[at] = std::byte(word & 255U);
    bytes[at + 1] = std::byte(word >> 8U);
}

std::int16_t Load(std::span<const std::byte> bytes, std::size_t at) noexcept
{
    const auto word = std::uint16_t(std::to_integer<unsigned>(bytes[at]) |
        (std::to_integer<unsigned>(bytes[at + 1]) << 8U));
    return std::bit_cast<std::int16_t>(word);
}
}

bool EncodeSound(const SoundWire& value, SoundPacket& output) noexcept
{
    if (value.sample.empty() || value.sample.size() > SoundSampleLimit ||
        value.sample.find('\0') != value.sample.npos) return false;
    SoundPacket encoded{};
    encoded.bytes[0] = std::byte(SoundProtocol);
    encoded.bytes[1] = std::byte(value.mode);
    encoded.bytes[2] = std::byte(value.enabled);
    encoded.bytes[3] = std::byte(value.remote);
    for (std::size_t i = 0; i < 3; ++i) Store(encoded.bytes, 4 + 2 * i, value.originEighths[i]);
    Store(encoded.bytes, 10, value.recipientIndex);
    std::memcpy(encoded.bytes.data() + SoundHeaderBytes, value.sample.data(), value.sample.size());
    encoded.size = std::uint8_t(SoundHeaderBytes + value.sample.size() + 1);
    output = encoded;
    return true;
}

bool DecodeSound(std::span<const std::byte> bytes, SoundMessageData& output) noexcept
{
    if (bytes.size() < SoundHeaderBytes + 2 || bytes.size() > SoundPacketLimit ||
        bytes[0] != std::byte(SoundProtocol) || bytes.back() != std::byte{}) return false;
    const std::string_view sample{reinterpret_cast<const char*>(bytes.data() + SoundHeaderBytes),
        bytes.size() - SoundHeaderBytes - 1};
    if (sample.find('\0') != sample.npos) return false;
    SoundMessageData decoded{sample, std::to_integer<std::uint8_t>(bytes[1]),
        std::to_integer<std::uint8_t>(bytes[2]), std::to_integer<std::uint8_t>(bytes[3]), {}, Load(bytes, 10)};
    for (std::size_t i = 0; i < 3; ++i)
        decoded.origin[i] = float(Load(bytes, 4 + 2 * i)) * 0.125f;
    output = decoded;
    return true;
}
}
