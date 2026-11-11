// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "teddy_spark_particle.hpp"

namespace csol::teddy_sliding
{
inline constexpr std::string_view Sprite = "sprites/ef_bat.spr";
inline constexpr std::size_t MessageBytes = 82;
using teddy_sparks::Vec3;
using teddy_sparks::Particle;
using teddy_sparks::Corners;
struct Emitter
{
    std::uint16_t owner{};
    Vec3 offset{}, velocity{};
};

// Only MP10936D30 ->11493570's flags30 entity emitter. The caller owns
// scheduling and supplies a validated current owner; no engine ABI is inferred.
inline std::optional<Emitter> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != MessageBytes || bytes[Sprite.size()] != 0 ||
        std::string_view(reinterpret_cast<const char*>(bytes.data()), Sprite.size()) != Sprite) return {};
    std::size_t at = Sprite.size() + 1;
    const auto byte = [&]() { return bytes[at++]; };
    const auto word = [&]() { const unsigned low = byte(); return low | unsigned(byte()) << 8; };
    const auto integer = [&]() { const std::uint32_t low = word(); return low | std::uint32_t(word()) << 16; };
    const auto real = [&]() { return std::bit_cast<float>(integer()); };
    Emitter event;
    event.owner = static_cast<std::uint16_t>(word());
    if (!event.owner || event.owner > 32767) return {};
    for (auto* vector : {&event.offset, &event.velocity})
        for (float& coordinate : *vector)
            coordinate = static_cast<float>(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word()))) * .125f;
    if (word() != 3 || real() != .1f || real() != .5f || integer() != 0x30 ||
        byte() != 0 || byte() != 30 || byte() != 5 || byte() != 255) return {};
    for (unsigned i = 0; i != 3; ++i) if (byte() != 255) return {};
    for (float expected : {0.0f, 50.0f, 0.0f, .2f, .2f, 0.0f, 0.0f}) if (real() != expected) return {};
    return at == bytes.size() ? std::optional{event} : std::nullopt;
}

template<class Random>
Particle Spawn(const Emitter& event, const Vec3& ownerOrigin, float now, Random& random)
{
    Particle particle;
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        particle.origin[axis] = ownerOrigin[axis] + event.offset[axis];
        particle.velocity[axis] = random(-1, 1) * 0.0f + event.velocity[axis];
    }
    random(1, 1.5f); // Original factory consumes mass even with zero gravity.
    particle.born = now;
    particle.die = now + .5f;
    particle.size = 50; particle.growth = .2f;
    particle.alpha = 250; // Parser02AA565E..566C clamps wire255 to250.
    particle.rotationVelocity = 0;
    particle.color = {255, 255, 255};
    return particle;
}

// Original fixed flags30: internal0 selects looping01DE10F0. Existing
// particles keep their emitted world origin; this never follows the owner.
inline bool Tick(Particle& particle, float now, float previousTime, float gravity, int fps, int frames)
{
    if (frames < 1 || frames > static_cast<int>(teddy_sparks::FrameCapacity) || fps < 0 ||
        !std::isfinite(now) || !std::isfinite(previousTime) || !std::isfinite(gravity) || now < particle.born) return false;
    const float age = now - particle.born;
    if (particle.alpha > 0) particle.alpha -= age * .1f;
    if (particle.alpha < 0) { particle.alpha = 0; particle.die = now; }
    const float animation = age * static_cast<float>(fps) + 0.0f;
    if (!std::isfinite(animation) || animation >= 2147483648.0f) return false;
    particle.frame = static_cast<int>(animation) % frames;
    particle.size = 50.0f + age * (particle.growth * 30.0f);
    if (particle.size < .0001f) particle.die = now;
    particle.alpha = 250.0f - (.2f * 30.0f) * age;
    if (particle.alpha < 1) particle.die = now;
    const float speed = std::sqrt(particle.rotationVelocity * particle.rotationVelocity);
    particle.roll = 1.0f + (age * (speed * 30.0f)) * particle.rotationVelocity;
    const float delta = now - previousTime;
    const float acceleration = ((-delta) * gravity) * 0.0f;
    for (unsigned axis = 0; axis != 3; ++axis) particle.origin[axis] += particle.velocity[axis] * delta;
    particle.velocity[2] += acceleration;
    if (particle.alpha < 0) particle.alpha = 0;
    if (!std::isfinite(particle.size) || !std::isfinite(particle.alpha) || !std::isfinite(particle.roll)) return false;
    for (unsigned axis = 0; axis != 3; ++axis)
        if (!std::isfinite(particle.origin[axis]) || !std::isfinite(particle.velocity[axis])) return false;
    return particle.die == 0 || now < particle.die;
}
}
