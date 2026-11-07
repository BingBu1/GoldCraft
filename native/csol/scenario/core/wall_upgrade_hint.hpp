// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wall_upgrade.hpp"
#include <bit>
#include <cmath>
#include <optional>
#include <span>

namespace csol::wall_upgrade_hint
{
inline constexpr std::uint8_t Message = 90, Resource = 51, RemoveMessage = 96;
inline constexpr std::size_t PayloadSize = 20;
struct Event
{
    std::int16_t owner{};
    std::array<float, 3> offset{};
    float duration{}, scale{};
};
inline bool Recognizes(std::span<const std::uint8_t> bytes)
{ return bytes.size() >= 5 && bytes[0] == Message && bytes[4] == Resource; }
inline std::optional<Event> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != PayloadSize || !Recognizes(bytes) || bytes[1] != 0 || bytes[15] != 1)
        return std::nullopt;
    const auto word = [&](std::size_t at)
    { return std::bit_cast<std::int16_t>(std::uint16_t(bytes[at] | (unsigned(bytes[at+1]) << 8))); };
    const auto number = [&](std::size_t at)
    {
        std::uint32_t bits{};
        for (unsigned i = 0; i < 4; ++i) bits |= std::uint32_t(bytes[at+i]) << (8*i);
        return std::bit_cast<float>(bits);
    };
    Event result;
    result.owner = word(2);
    for (unsigned i = 0; i < 3; ++i) result.offset[i] = float(word(5+2*i)) * .125f;
    result.duration = number(11); result.scale = number(16);
    if (result.owner <= 0 || !std::isfinite(result.duration) || result.duration < 0 ||
        !std::isfinite(result.scale)) return std::nullopt;
    return result;
}
}
