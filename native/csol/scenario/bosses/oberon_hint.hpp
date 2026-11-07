// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::oberon_hint
{
inline constexpr char Sprite[] = "sprites/sc_pass.spr";
inline constexpr std::uint8_t Message = 0x5a, Resource = 22;
inline constexpr std::size_t PayloadSize = 20;
inline constexpr float Hold = .5f, FadeSpeed = .5f, DefaultScale = 2.0f;
struct Event
{
    std::int16_t owner{};
    std::array<float, 3> offset{};
    float duration{Hold}, scale{DefaultScale};
};

// This is the verified rules354/effect22 subset. Other 0x5A resources and
// secondary CSO TEMPENTITY flags are left to their own consumers.
inline bool Recognizes(std::span<const std::uint8_t> bytes)
{ return bytes.size() >= 5 && bytes[0] == Message && bytes[4] == Resource; }
inline std::optional<Event> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != PayloadSize || !Recognizes(bytes) || bytes[1] != 0 || bytes[15] != 1)
        return std::nullopt;
    const auto word = [&](std::size_t at)
    { return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[at] | (unsigned(bytes[at + 1]) << 8))); };
    const auto number = [&](std::size_t at)
    {
        std::uint32_t bits{};
        for (unsigned i = 0; i < 4; ++i) bits |= std::uint32_t(bytes[at + i]) << (8 * i);
        return std::bit_cast<float>(bits);
    };
    Event event;
    event.owner = word(2);
    for (unsigned i = 0; i < 3; ++i) event.offset[i] = float(word(5 + 2 * i)) * .125f;
    event.duration = number(11);
    event.scale = number(16);
    if (event.owner <= 0 || !std::isfinite(event.duration) || event.duration < 0 ||
        !std::isfinite(event.scale)) return std::nullopt;
    return event;
}
inline std::optional<std::array<std::uint8_t, PayloadSize>> Write(const Event& event)
{
    if (event.owner <= 0 || !std::isfinite(event.duration) || event.duration < 0 ||
        !std::isfinite(event.scale)) return std::nullopt;
    std::array<std::uint8_t, PayloadSize> bytes{};
    bytes[0] = Message;
    bytes[4] = Resource;
    bytes[15] = 1;
    const auto word = [&](std::size_t at, std::int16_t value)
    {
        const auto bits = std::bit_cast<std::uint16_t>(value);
        bytes[at] = static_cast<std::uint8_t>(bits);
        bytes[at + 1] = static_cast<std::uint8_t>(bits >> 8);
    };
    const auto number = [&](std::size_t at, float value)
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(bits >> (8 * i));
    };
    word(2, event.owner);
    for (unsigned i = 0; i < 3; ++i)
    {
        const float value = event.offset[i] * 8.0f;
        if (!std::isfinite(value) || value < -32768.0f || value >= 32768.0f) return std::nullopt;
        word(5 + 2 * i, static_cast<std::int16_t>(value));
    }
    number(11, event.duration);
    number(16, event.scale);
    return bytes;
}
}
