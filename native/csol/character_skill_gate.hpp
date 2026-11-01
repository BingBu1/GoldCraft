// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::skillgate
{
inline constexpr char MessageName[] = "CsoSkGate";
inline constexpr std::uint8_t Version = 1;
inline constexpr std::size_t MessageBytes = 12;
enum Flag : std::uint8_t
{
    Manual = 1, Ready = 2, Preparing = 4, Active = 8, Blocked = 16
};
inline constexpr std::uint8_t KnownFlags = Manual | Ready | Preparing | Active | Blocked;
struct Snapshot
{
    std::uint16_t subtype{};
    std::uint8_t flags{};
    float prepareRemaining{}, activeRemaining{};
};
inline bool Valid(const Snapshot& value)
{
    if (value.subtype > 55 || (value.flags & ~KnownFlags) ||
        !std::isfinite(value.prepareRemaining) || value.prepareRemaining < 0 ||
        !std::isfinite(value.activeRemaining) || value.activeRemaining < 0) return false;
    if (!value.subtype) return !value.flags && value.prepareRemaining == 0 && value.activeRemaining == 0;
    // An effect can outlive its cooldown; Active + Ready is intentional.
    return !(value.flags & Ready) || !(value.flags & (Preparing | Blocked));
}
inline bool CanRequest(const Snapshot& value)
{
    return Valid(value) && value.subtype && (value.flags & (Manual | Ready)) == (Manual | Ready) &&
        !(value.flags & (Preparing | Blocked));
}
inline bool Encode(const Snapshot& value, std::array<std::uint8_t, MessageBytes>& output)
{
    if (!Valid(value)) return false;
    std::array<std::uint8_t, MessageBytes> bytes{Version, std::uint8_t(value.subtype),
        std::uint8_t(value.subtype >> 8), value.flags};
    const auto real = [&](unsigned at, float number)
    {
        const auto bits = std::bit_cast<std::uint32_t>(number);
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::uint8_t(bits >> (8 * i));
    };
    real(4, value.prepareRemaining); real(8, value.activeRemaining);
    output = bytes; return true;
}
inline bool Decode(std::span<const std::uint8_t> bytes, Snapshot& output)
{
    if (bytes.size() != MessageBytes || bytes[0] != Version) return false;
    const auto real = [&](unsigned at)
    {
        std::uint32_t bits{};
        for (unsigned i = 0; i < 4; ++i) bits |= std::uint32_t(bytes[at + i]) << (8 * i);
        return std::bit_cast<float>(bits);
    };
    const Snapshot value{std::uint16_t(unsigned(bytes[1]) | unsigned(bytes[2]) << 8),
        bytes[3], real(4), real(8)};
    if (!Valid(value)) return false;
    output = value; return true;
}
}
