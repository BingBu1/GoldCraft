// SPDX-License-Identifier: GPL-3.0-or-later
#include "particle_protocol.hpp"
#include "catalog.hpp"

#include <bit>

namespace guest::cof
{
namespace
{
bool Valid(const ParticleMessageData& value) noexcept
{
    switch (value.operation)
    {
    case ParticleOperation::Add:
        return OriginalPistolId(value.originalWeapon) && !value.immediate;
    case ParticleOperation::DeleteOwner:
        return !value.originalWeapon && !value.immediate;
    case ParticleOperation::KillLocal:
        return !value.originalWeapon && !value.owner;
    }
    return false;
}
}

bool EncodeParticle(const ParticleMessageData& value, ParticlePacket& output) noexcept
{
    if (!Valid(value)) return false;
    const auto owner = std::bit_cast<std::uint16_t>(value.owner);
    output = {std::byte{ParticleProtocol}, std::byte(value.operation),
        std::byte(value.originalWeapon), std::byte(value.immediate),
        std::byte(owner & 255U), std::byte(owner >> 8U), std::byte{}, std::byte{}};
    return true;
}

bool DecodeParticle(std::span<const std::byte> bytes, ParticleMessageData& output) noexcept
{
    if (bytes.size() != ParticleBytes || bytes[0] != std::byte{ParticleProtocol} ||
        std::to_integer<unsigned>(bytes[3]) > 1 || bytes[6] != std::byte{} || bytes[7] != std::byte{})
        return false;
    const auto owner = std::uint16_t(std::to_integer<unsigned>(bytes[4]) |
        (std::to_integer<unsigned>(bytes[5]) << 8U));
    const ParticleMessageData decoded{ParticleOperation(std::to_integer<std::uint8_t>(bytes[1])),
        std::to_integer<std::uint8_t>(bytes[2]), bytes[3] != std::byte{},
        std::bit_cast<std::int16_t>(owner)};
    if (!Valid(decoded)) return false;
    output = decoded;
    return true;
}

ViewEntityPacket EncodeViewEntity(std::int16_t value) noexcept
{
    const auto word = std::bit_cast<std::uint16_t>(value);
    return {std::byte{ParticleProtocol}, std::byte{}, std::byte(word & 255U), std::byte(word >> 8U)};
}

bool DecodeViewEntity(std::span<const std::byte> bytes, std::int16_t& output) noexcept
{
    if (bytes.size() != ViewEntityPacket{}.size() || bytes[0] != std::byte{ParticleProtocol} ||
        bytes[1] != std::byte{}) return false;
    const auto word = std::uint16_t(std::to_integer<unsigned>(bytes[2]) |
        (std::to_integer<unsigned>(bytes[3]) << 8U));
    output = std::bit_cast<std::int16_t>(word);
    return true;
}
}
