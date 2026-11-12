// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace csol::teddy_sparks
{
inline constexpr char Message[] = "TempEmit";
inline constexpr std::array<std::string_view, 2> Sprites{"sprites/spark2.spr", "sprites/spark3.spr"};
inline constexpr std::size_t MessageBytes = 82;
inline constexpr std::size_t EmitterCapacity = 128, ParticleCapacity = 1200, FrameCapacity = 256;
using Vec3 = std::array<float, 3>;
struct Emitter
{
    Vec3 origin{};
    Vec3 velocity{0, 0, 200};
    float interval{};
    float gravity{.1f};
    std::array<std::uint8_t, 3> color{};
    std::uint8_t sprite{}, rotation{};
};

inline std::optional<std::uint8_t> Identify(std::span<const std::uint8_t> bytes)
{
    for (std::uint8_t i = 0; i < Sprites.size(); ++i)
    {
        const auto path = Sprites[i];
        if (bytes.size() > path.size() && bytes[path.size()] == 0 &&
            std::string_view(reinterpret_cast<const char*>(bytes.data()), path.size()) == path) return i;
    }
    return {};
}

// Original Teddy SpawnEvent and GiftBox firework forms. Other consumers keep their
// own message-chain entries; this does not infer a general particle protocol.
inline std::optional<Emitter> Read(std::span<const std::uint8_t> bytes)
{
    const auto sprite = Identify(bytes);
    if (!sprite || bytes.size() != MessageBytes) return {};
    std::size_t at = Sprites[*sprite].size() + 1;
    const auto byte = [&]() { return bytes[at++]; };
    const auto word = [&]() { const unsigned low = byte(); return low | unsigned(byte()) << 8; };
    const auto integer = [&]() { const std::uint32_t low = word(); return low | std::uint32_t(word()) << 16; };
    const auto real = [&]() { return std::bit_cast<float>(integer()); };
    if (word() != 0) return {}; // World emission, no owner/attachment.
    Emitter event;
    event.sprite = *sprite;
    for (float& coordinate : event.origin)
        coordinate = static_cast<float>(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word()))) * .125f;
    for (float& coordinate : event.velocity)
        coordinate = static_cast<float>(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word()))) * .125f;
    if (event.velocity[0] != 0 || event.velocity[1] != 0 ||
        (event.velocity[2] != 0 && event.velocity[2] != 200)) return {};
    if (word() != 3) return {};
    event.interval = real();
    if (!std::isfinite(event.interval) || event.interval < .1f || event.interval > .3f ||
        real() != 1.8f || integer() != 0x50) return {};
    event.rotation = byte();
    if (event.rotation < 1 || event.rotation > 5 || byte() != (*sprite ? 17 : 16) || byte() != 5 || byte() != 240) return {};
    for (auto& channel : event.color)
    {
        channel = byte();
        if (channel < 70 || channel > 250) return {};
    }
    if (real() != 30.0f || real() != 180.0f) return {};
    event.gravity = real();
    if (event.gravity != (event.velocity[2] == 0 ? .01f : .1f)) return {};
    for (float expected : {.1f, 2.0f, 0.0f, 0.0f})
        if (real() != expected) return {};
    return at == bytes.size() ? std::optional{event} : std::nullopt;
}
}
