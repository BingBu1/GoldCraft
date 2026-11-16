// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace guest::cof
{
inline constexpr char ParticleMessage[] = "CoFParticle";
inline constexpr char ViewEntityMessage[] = "CoFViewEntity";
inline constexpr std::uint8_t ParticleProtocol = 1;
inline constexpr std::uint8_t G43ParticleProtocol = 2;
inline constexpr std::size_t ParticleBytes = 8;

enum class ParticleOperation : std::uint8_t { Add = 1, DeleteOwner, KillLocal };

// Host transport for the verified firearm gunsmoke messages. A local
// Add still replaces owner with the actual viewmodel index at consumption;
// zero is an original value, not a synonym for the local player entity.
struct ParticleMessageData
{
    ParticleOperation operation{ParticleOperation::Add};
    std::uint8_t originalWeapon{};
    bool immediate{};
    std::int16_t owner{};
    std::int16_t attachment{};
    bool operator==(const ParticleMessageData&) const noexcept = default;
};

using ParticlePacket = std::array<std::byte, ParticleBytes>;
// This is a new namespaced wire ABI, not CoF's original variable-string ABI.
// Add represents local=1 and aurora/gunsmoke.aur. The v1 wire is unchanged;
// G43 uses v2 with its proven attachment1 in the last signed short. Its original
// model is selected by identity. Other script/attachment forms are unsupported.
// Failed encode/decode leaves output unchanged.
bool EncodeParticle(const ParticleMessageData&, ParticlePacket&) noexcept;
bool DecodeParticle(std::span<const std::byte>, ParticleMessageData&) noexcept;

using ViewEntityPacket = std::array<std::byte, 4>;
ViewEntityPacket EncodeViewEntity(std::int16_t) noexcept;
bool DecodeViewEntity(std::span<const std::byte>, std::int16_t&) noexcept;
}
