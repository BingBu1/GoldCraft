// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "frozen_smoke.hpp"

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::frozen_smoke
{
struct Emission
{
    Vec3 origin{};
    float spread{}, size{}, growth{}, next{};
    bool active{};
};

struct Particle
{
    Vec3 origin{}, velocity{};
    float initialSize{}, growth{}, born{}, die{}, size{}, alpha{110}, roll{1};
};

// RandomFloat calls retain the original receiver's order, including samples
// taken before a full emitter pool rejects a record.
template<class Random>
Emission Prepare(const Event& event, float now, Random& random)
{
    Emission emission;
    emission.origin[0] = random(event.absmin[0], event.absmax[0]);
    emission.origin[1] = random(event.absmin[1], event.absmax[1]);
    emission.origin[2] = event.center[2];
    emission.spread = random(-5, 5);
    emission.size = random(220, 250);
    emission.growth = random(3, 5);
    emission.next = now > 0 ? now : 0;
    emission.active = true;
    return emission;
}

template<class Random>
Particle Spawn(const Emission& emission, float now, Random& random)
{
    Particle particle;
    particle.origin = emission.origin;
    for (unsigned axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = random(-1, 1) * emission.spread + (axis == 2 ? 50.0f : 0.0f);
    // The original still samples mass for a noncolliding sprite.
    random(1, 1.5f);
    particle.initialSize = particle.size = emission.size;
    particle.growth = emission.growth;
    particle.born = now;
    particle.die = now + 4.0f;
    return particle;
}

// Fixed FrozenDeath instance of original virtual Update02AD3F10, including
// its base size/fade/rotation/motion order. Visibility never pauses ageing.
inline bool Tick(Particle& particle, float now, float previousTime, float gravity)
{
    const float age = now - particle.born;
    if (particle.alpha > 0) particle.alpha -= age * .1f;
    if (particle.alpha < 0) { particle.alpha = 0; particle.die = now; }
    particle.size = particle.initialSize + age * (particle.growth * 30.0f);
    if (particle.size < .0001f) particle.die = now;
    particle.alpha = 110.0f * (1.0f - age / (particle.die - particle.born));
    if (particle.alpha < 1.0f) particle.die = now;
    // Original Length(0,0,.1) rounds back to float before multiplying by30.
    const float angularSpeed = std::sqrt(.1f * .1f);
    particle.roll = 1.0f + (age * (angularSpeed * 30.0f)) * .1f;
    const float delta = now - previousTime;
    const float acceleration = ((-delta) * gravity) * .05f;
    for (unsigned axis = 0; axis < 3; ++axis)
        particle.origin[axis] += particle.velocity[axis] * delta;
    particle.velocity[2] += acceleration;
    if (particle.alpha < 0) particle.alpha = 0;
    if (!std::isfinite(particle.size) || !std::isfinite(particle.alpha) || !std::isfinite(particle.roll)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(particle.origin[axis]) || !std::isfinite(particle.velocity[axis])) return false;
    return particle.die == 0 || now < particle.die;
}
}
