// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace csol::balrog11::flame
{
inline constexpr char Sprite[] = "sprites/flame_puff01.spr";
inline constexpr char Message[] = "TempEmit";
inline constexpr std::size_t MessageBytes = 88;
inline constexpr float Interval = .01f, ParticleLife = .3f, Size = 40, Alpha = 40, Spread = 16;
struct Emitter { int owner{}, rotation{}; };
inline std::optional<std::array<std::uint8_t, MessageBytes>> Write(Emitter value)
{
    if (value.owner < 1 || value.owner > 32767 || value.rotation < 1 || value.rotation > 180) return {};
    std::array<std::uint8_t, MessageBytes> bytes{};
    std::size_t at = 0;
    const auto byte = [&](unsigned n) { bytes[at++] = static_cast<std::uint8_t>(n); };
    const auto word = [&](unsigned n) { byte(n); byte(n >> 8); };
    const auto integer = [&](unsigned n) { word(n); word(n >> 16); };
    const auto real = [&](float n) { integer(std::bit_cast<std::uint32_t>(n)); };
    for (char c : Sprite) byte(static_cast<unsigned char>(c));
    word(value.owner);
    for (unsigned axis = 0; axis < 6; ++axis) word(0);
    word(0xffff); real(Interval); real(ParticleLife); integer(0x30);
    byte(value.rotation); byte(100); byte(5); byte(40);
    byte(255); byte(255); byte(255);
    real(Spread); real(Size);
    for (unsigned i = 0; i < 5; ++i) real(0);
    return bytes;
}
inline bool IsFlameMessage(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < sizeof(Sprite)) return false;
    for (unsigned i = 0; i < sizeof(Sprite); ++i)
        if (bytes[i] != static_cast<std::uint8_t>(Sprite[i])) return false;
    return true;
}
inline std::optional<Emitter> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != MessageBytes || !IsFlameMessage(bytes)) return {};
    constexpr auto ownerAt = sizeof(Sprite), rotationAt = ownerAt + 28;
    const Emitter value{int(bytes[ownerAt] | unsigned(bytes[ownerAt + 1]) << 8), bytes[rotationAt]};
    const auto expected = Write(value);
    if (!expected) return {};
    for (unsigned i = 0; i < bytes.size(); ++i) if (bytes[i] != (*expected)[i]) return {};
    return value;
}

using Vec3 = std::array<float, 3>;
struct Particle
{
    Vec3 origin{}, velocity{};
    float born{}, die{}, alpha{Alpha}, roll{1}, rotation{};
    int frame{};
};
// Fixed flags0x30 instance of original sprite Update02AD3F10. The emitter
// follows the projectile; emitted particles keep their own world positions.
inline bool Tick(Particle& particle, float now, float previousTime, int frames)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const float age = now - particle.born;
    if (particle.alpha > 0) particle.alpha -= age * .1f;
    if (particle.alpha < 0) { particle.alpha = 0; particle.die = now; }
    particle.alpha = Alpha * (1 - age / (particle.die - particle.born));
    if (particle.alpha < 1) particle.die = now;
    if (particle.alpha < 0) particle.alpha = 0;
    const float speed = std::sqrt(particle.rotation * particle.rotation);
    particle.roll = 1 + (age * (speed * 30.f)) * particle.rotation;
    const float delta = now - previousTime;
    for (unsigned axis = 0; axis < 3; ++axis) particle.origin[axis] += particle.velocity[axis] * delta;
    particle.frame = frames > 0 ? static_cast<int>(age * 100.f) % frames : 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(particle.origin[axis]) || !std::isfinite(particle.velocity[axis])) return false;
    return std::isfinite(particle.alpha) && std::isfinite(particle.roll) && now < particle.die;
}
}
