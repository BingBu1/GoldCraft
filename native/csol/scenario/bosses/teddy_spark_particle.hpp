// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "teddy_sparks.hpp"

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::teddy_sparks
{
struct Particle
{
    Vec3 origin{}, velocity{};
    float born{}, die{}, size{180}, growth{2}, alpha{240}, roll{1}, rotationVelocity{};
    std::array<std::uint8_t, 3> color{};
    std::uint8_t sprite{};
    int frame{};
};

template<class Random>
Particle Spawn(const Emitter& event, float now, Random& random)
{
    Particle particle;
    particle.origin = event.origin;
    for (unsigned axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = random(-1, 1) * 30.0f + (axis == 2 ? 200.0f : 0.0f);
    random(1, 1.5f); // Original consumes mass even for this noncolliding sprite.
    particle.born = now;
    particle.die = now + 1.8f;
    particle.rotationVelocity = static_cast<float>(event.rotation) * .1f;
    particle.color = event.color;
    particle.sprite = event.sprite;
    return particle;
}

// Fixed flags0x50 instance: original02AD3F10 ->01DE5260 ->01DE1140.
// Keep finite animation's strict > check, including frame==frames.
inline bool Tick(Particle& particle, float now, float previousTime, float gravity, int fps, int frames)
{
    if (frames < 1 || frames > static_cast<int>(FrameCapacity) || !std::isfinite(now) ||
        !std::isfinite(previousTime) || !std::isfinite(gravity) || now < particle.born) return false;
    const float age = now - particle.born;
    if (particle.alpha > 0) particle.alpha -= age * .1f;
    if (particle.alpha < 0) { particle.alpha = 0; particle.die = now; }
    const float animation = age * static_cast<float>(fps) + 0.0f;
    if (animation > static_cast<float>(frames)) particle.die = now;
    else particle.frame = static_cast<int>(animation);
    if (particle.frame > frames) particle.frame = frames - 1;
    particle.size = 180.0f + age * (particle.growth * 30.0f);
    if (particle.size < .0001f) particle.die = now;
    particle.alpha = 240.0f - (.1f * 30.0f) * age;
    if (particle.alpha < 1) particle.die = now;
    const float speed = std::sqrt(particle.rotationVelocity * particle.rotationVelocity);
    particle.roll = 1.0f + (age * (speed * 30.0f)) * particle.rotationVelocity;
    const float delta = now - previousTime;
    const float acceleration = ((-delta) * gravity) * .1f;
    for (unsigned axis = 0; axis < 3; ++axis)
        particle.origin[axis] += particle.velocity[axis] * delta;
    particle.velocity[2] += acceleration;
    if (particle.alpha < 0) particle.alpha = 0;
    if (!std::isfinite(particle.size) || !std::isfinite(particle.alpha) || !std::isfinite(particle.roll)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(particle.origin[axis]) || !std::isfinite(particle.velocity[axis])) return false;
    return particle.frame >= 0 && (particle.die == 0 || now < particle.die);
}

using Quad = std::array<Vec3, 4>;
inline Quad Corners(const Particle& particle, const Vec3& right, const Vec3& up)
{
    Quad corners;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const float horizontal = right[axis] * particle.size;
        const float vertical = up[axis] * particle.size;
        const float bottom = (particle.origin[axis] - horizontal * .5f) - vertical * .5f;
        corners[0][axis] = bottom + vertical;
        corners[1][axis] = bottom;
        corners[2][axis] = bottom + horizontal;
        corners[3][axis] = (bottom + horizontal) + vertical;
    }
    return corners;
}
}
