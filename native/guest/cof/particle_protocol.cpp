// SPDX-License-Identifier: GPL-3.0-or-later
#include "particle_protocol.hpp"
#include "weapon_identity.hpp"

#include <bit>

namespace guest::cof
{
namespace
{
constexpr bool AttachmentOne(unsigned weapon) noexcept
{
    return weapon == unsigned(OriginalWeaponId::Famas) || weapon == unsigned(OriginalWeaponId::G43) || weapon == unsigned(OriginalWeaponId::Shotgun);
}
bool Valid(const ParticleMessageData& value) noexcept
{
    switch (value.operation)
    {
    case ParticleOperation::Add:
        return KnownOriginalWeaponId(value.originalWeapon) && !value.immediate &&
            value.attachment == (AttachmentOne(value.originalWeapon) ? 1 : 0);
    case ParticleOperation::DeleteOwner:
        return !value.originalWeapon && !value.immediate && !value.attachment;
    case ParticleOperation::KillLocal:
        return !value.originalWeapon && !value.owner && !value.attachment;
    }
    return false;
}
}

bool EncodeParticle(const ParticleMessageData& value, ParticlePacket& output) noexcept
{
    if (!Valid(value)) return false;
    const auto owner = std::bit_cast<std::uint16_t>(value.owner);
    const auto attachment=std::bit_cast<std::uint16_t>(value.attachment);
    const auto protocol=AttachmentOne(value.originalWeapon) ? AttachmentParticleProtocol : ParticleProtocol;
    output = {std::byte{protocol}, std::byte(value.operation),
        std::byte(value.originalWeapon), std::byte(value.immediate),
        std::byte(owner & 255U), std::byte(owner >> 8U), std::byte(attachment & 255U), std::byte(attachment >> 8U)};
    return true;
}

bool DecodeParticle(std::span<const std::byte> bytes, ParticleMessageData& output) noexcept
{
    if (bytes.size() != ParticleBytes ||
        bytes[0] != std::byte{AttachmentOne(std::to_integer<unsigned>(bytes[2])) ? AttachmentParticleProtocol : ParticleProtocol} ||
        std::to_integer<unsigned>(bytes[3]) > 1)
        return false;
    const auto owner = std::uint16_t(std::to_integer<unsigned>(bytes[4]) |
        (std::to_integer<unsigned>(bytes[5]) << 8U));
    const auto attachment=std::uint16_t(std::to_integer<unsigned>(bytes[6]) |
        (std::to_integer<unsigned>(bytes[7]) << 8U));
    const ParticleMessageData decoded{ParticleOperation(std::to_integer<std::uint8_t>(bytes[1])),
        std::to_integer<std::uint8_t>(bytes[2]), bytes[3] != std::byte{},
        std::bit_cast<std::int16_t>(owner),std::bit_cast<std::int16_t>(attachment)};
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
