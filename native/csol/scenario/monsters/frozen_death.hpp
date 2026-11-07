// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::frozen_death
{
inline constexpr char IceModel[] = "models/ice_gibs.mdl";
inline constexpr char IceMessage[] = "CsoIce";
inline constexpr std::size_t IcePayloadSize = 25;
using Vec3 = std::array<float, 3>;

struct IceEvent
{
    Vec3 center{}, size{};
};

inline bool Valid(const IceEvent& event)
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(event.center[axis]) || !std::isfinite(event.size[axis]) ||
            event.size[axis] < 0) return false;
    return true;
}

// Stock GoldSrc cannot consume CSO TE147. This dedicated user message carries
// the six host-space values as raw floats, preserving large-map positions.
// The only supported effect is the original frozen-death TE147 parameter set:
// zero velocity, random100, count0, life2.5, flags64, studio bodies8..15.
inline std::optional<std::array<std::uint8_t, IcePayloadSize>> WriteIce(const IceEvent& event)
{
    if (!Valid(event)) return std::nullopt;
    std::array<std::uint8_t, IcePayloadSize> bytes{1};
    for (unsigned i = 0; i < 6; ++i)
    {
        const auto bits = std::bit_cast<std::uint32_t>(i < 3 ? event.center[i] : event.size[i - 3]);
        for (unsigned j = 0; j < 4; ++j) bytes[1 + i * 4 + j] = static_cast<std::uint8_t>(bits >> (8 * j));
    }
    return bytes;
}
inline std::optional<IceEvent> ReadIce(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != IcePayloadSize || bytes[0] != 1) return std::nullopt;
    IceEvent event;
    for (unsigned i = 0; i < 6; ++i)
    {
        std::uint32_t bits{};
        for (unsigned j = 0; j < 4; ++j) bits |= std::uint32_t(bytes[1 + i * 4 + j]) << (8 * j);
        (i < 3 ? event.center[i] : event.size[i - 3]) = std::bit_cast<float>(bits);
    }
    return Valid(event) ? std::optional(event) : std::nullopt;
}
inline int FragmentCount(const Vec3& size)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    // HW37857B8D: three float products/additions, then double division by432.
    const float xy = size[0] * size[1], yz = size[1] * size[2], xz = size[0] * size[2];
    const double count = static_cast<double>((xy + yz) + xz) / 432.0;
    // CVTTSD2SI produces INT_MIN for NaN/overflow, then the original returns.
    if (!(count > 0) || !(count < 2147483648.0)) return 0;
    return count >= 100 ? 100 : static_cast<int>(count);
}
}
