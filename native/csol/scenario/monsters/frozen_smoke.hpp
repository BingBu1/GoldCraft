// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::frozen_smoke
{
inline constexpr char SmokeSprite[] = "sprites/smoke_ia.spr";
inline constexpr char SmokeMessage[] = "CsoFrSmoke";
inline constexpr std::size_t PayloadSize = 37;
using Vec3 = std::array<float, 3>;

struct Event
{
    Vec3 center{}, absmin{}, absmax{};
};

inline bool Valid(const Event& event)
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(event.center[axis]) || !std::isfinite(event.absmin[axis]) ||
            !std::isfinite(event.absmax[axis]) || event.absmin[axis] > event.absmax[axis] ||
            !std::isfinite(event.absmax[axis] - event.absmin[axis])) return false;
    return true;
}

// A private, versioned host-space transport, not CSO's MPToCL89 wire format.
// The original coordinate-width negotiation is unavailable in stock GoldSrc;
// raw floats also preserve positions outside the stock signed-short range.
// All non-position parameters are the fixed FrozenDeath89 effect, not user
// supplied particle flags: alpha110, count1, interval0, life4, size220..250,
// gravity.05, fade0, growth3..5, rotation1, fps0, flags0x30, emitters3,
// RGB180/180/225, velocity0/0/50 and render mode4.
inline std::optional<std::array<std::uint8_t, PayloadSize>> Write(const Event& event)
{
    if (!Valid(event)) return std::nullopt;
    std::array<std::uint8_t, PayloadSize> bytes{1};
    const Vec3* vectors[]{&event.center, &event.absmin, &event.absmax};
    for (unsigned i = 0; i < 9; ++i)
    {
        const auto bits = std::bit_cast<std::uint32_t>((*vectors[i / 3])[i % 3]);
        for (unsigned j = 0; j < 4; ++j) bytes[1 + i * 4 + j] = static_cast<std::uint8_t>(bits >> (8 * j));
    }
    return bytes;
}

inline std::optional<Event> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != PayloadSize || bytes[0] != 1) return std::nullopt;
    Event event;
    Vec3* vectors[]{&event.center, &event.absmin, &event.absmax};
    for (unsigned i = 0; i < 9; ++i)
    {
        std::uint32_t bits{};
        for (unsigned j = 0; j < 4; ++j) bits |= std::uint32_t(bytes[1 + i * 4 + j]) << (8 * j);
        (*vectors[i / 3])[i % 3] = std::bit_cast<float>(bits);
    }
    return Valid(event) ? std::optional(event) : std::nullopt;
}
}
