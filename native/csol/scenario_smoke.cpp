// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_smoke.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::smoke
{
namespace
{
float Length(const Vec3& vector)
{
    const float square = (vector[0] * vector[0] + vector[1] * vector[1]) + vector[2] * vector[2];
    return static_cast<float>(std::sqrt(static_cast<double>(square)));
}

void Normalize(Vec3& vector)
{
    const float length = Length(vector);
    if (length != 0)
    {
        const float inverse = 1.0f / length;
        for (float& component : vector)
            component *= inverse;
    }
}

void Emit(const Vec3& origin, const Vec3& velocity, bool stationary, bool fill,
          float brightness, double time, Services& services)
{
    Particle particle;
    particle.origin = particle.previousOrigin = particle.sourceOrigin = origin;
    particle.velocity = velocity;
    particle.size = particle.initialSize = services.RandomFloat(175, 225);
    particle.brightness = particle.sourceBrightness = brightness;
    particle.birth = static_cast<float>(time);
    particle.frame = services.RandomInt(0, 1);
    particle.angularVelocity[2] = stationary ? services.RandomFloat(-0.8f, 0.8f)
                                            : services.RandomFloat(-1, 1);
    particle.deadline = stationary ? 0.0f : static_cast<float>(time + 0.001);
    particle.die = static_cast<float>(time + (stationary ? 30.0 : 20.0));
    particle.expansion = stationary ? 0.05f : 0.1f;
    particle.stationary = particle.started = stationary;
    particle.emitFill = fill;
    particle.mass = services.RandomFloat(1, 1.5f);
    services.Emit(particle);
}

Vec3 Direction(float yaw, float forwardScale, float upScale, Services& services)
{
    Vec3 forward{}, up{}, result{};
    services.AngleVectors(yaw, forward, up);
    Normalize(forward);
    for (std::size_t axis = 0; axis < result.size(); ++axis)
        result[axis] = forward[axis] * forwardScale + up[axis] * upScale;
    return result;
}

void Fill(const Vec3& origin, float brightness, double time, Services& services)
{
    for (int ring = 0; ring < 2; ++ring)
    {
        for (int yaw = 0; yaw < 360; yaw += 45)
        {
            // AngleVectors/normalization precede the radius RNG in the client.
            Vec3 forward{}, up{}, position{};
            services.AngleVectors(static_cast<float>(yaw), forward, up);
            Normalize(forward);
            const float radius = ring == 0 ? services.RandomFloat(90, 110) : 90.0f;
            const float height = ring == 0 ? 45.0f : 60.0f;
            for (std::size_t axis = 0; axis < position.size(); ++axis)
                position[axis] = (origin[axis] + forward[axis] * radius) + up[axis] * height;
            Emit(position, {}, true, false, brightness, time, services);
        }
    }
    for (const float yaw : {45.0f, 270.0f})
    {
        Vec3 forward{}, up{}, position{};
        services.AngleVectors(yaw, forward, up);
        Normalize(forward);
        for (std::size_t axis = 0; axis < position.size(); ++axis)
            position[axis] = (origin[axis] + forward[axis] * 45) + up[axis] * 55;
        Emit(position, {}, true, false, brightness, time, services);
    }
    Emit(origin, {}, true, false, brightness, time, services);
}

void Move(Particle& particle, float delta, Services& services)
{
    for (std::size_t axis = 0; axis < particle.origin.size(); ++axis)
        particle.origin[axis] += particle.velocity[axis] * delta;
    if (!particle.collide)
        return;

    const Trace trace = services.TraceWorld(particle.previousOrigin, particle.origin);
    if (trace.fraction != 1)
    {
        for (float& component : particle.velocity)
            component *= 0.6f;
        if (Length(particle.velocity) < 10)
        {
            particle.collide = false;
            particle.velocity = particle.angularVelocity = {};
            particle.origin = trace.end;
        }
        const float partialDelta = delta * trace.fraction;
        for (std::size_t axis = 0; axis < particle.origin.size(); ++axis)
            particle.origin[axis] = particle.previousOrigin[axis] + particle.velocity[axis] * partialDelta;

        float bounce = 0.5f;
        if (trace.normal[2] > 0.9f && particle.velocity[2] == 0)
        {
            particle.velocity = particle.angularVelocity = {};
            particle.collide = false;
            bounce = 0;
        }
        if (bounce != 0)
        {
            const float dot = (trace.normal[0] * particle.velocity[0] +
                               trace.normal[1] * particle.velocity[1]) +
                               trace.normal[2] * particle.velocity[2];
            const float reflection = static_cast<float>(static_cast<double>(dot) * -2.0);
            for (std::size_t axis = 0; axis < particle.velocity.size(); ++axis)
                particle.velocity[axis] += trace.normal[axis] * reflection;
            particle.angles[1] = -particle.angles[1];
        }
        for (float& component : particle.velocity)
            component *= bounce;
        for (float& angle : particle.angles)
            angle *= 0.9f;
    }
    particle.previousOrigin = particle.origin;
}
}

void CreateCloud(const Vec3& origin, double clientTime, Services& services)
{
    for (const float height : {165.0f, 265.0f})
        for (int yaw = 0; yaw < 360; yaw += 45)
            Emit(origin, Direction(static_cast<float>(yaw), 375, height, services),
                 false, false, 190, clientTime, services);
    Emit(origin, Direction(45, 375, 120, services), false, false, 190, clientTime, services);
    Emit(origin, Direction(270, 375, 120, services), false, true, 190, clientTime, services);
}

bool Tick(Particle& particle, double clientTime, float previousTime, Services& services)
{
    const float now = static_cast<float>(clientTime);
    if (now == previousTime)
        return particle.die == 0 || now < particle.die;

    if (particle.brightness >= 255 && particle.die - now < 3)
    {
        particle.expansion = 0;
        particle.angularVelocity = {};
        particle.birth = now;
    }
    // Constructor fixes fadeSpeed=-1, so the early-life brightness increase
    // (which requires fadeSpeed==0) and the generic Fade are both inactive.
    if (particle.die - now > 5)
    {
        if (particle.brightness > 255)
            particle.brightness = 255;
    }
    else
    {
        if (particle.brightness > 0)
            particle.brightness -= (now - particle.birth) * 0.1f;
        if (particle.brightness < 0)
        {
            particle.brightness = 0;
            particle.die = now;
        }
    }
    const float age = now - particle.birth;
    if (particle.expansion != 0)
    {
        particle.size = particle.initialSize + age * (particle.expansion * 30);
        if (particle.size < 0.0001f)
            particle.die = now;
    }
    if (particle.angularVelocity != Vec3{})
    {
        // The original normalizes a temporary copy to obtain its length and
        // multiplies the original angular velocity, not that normalized copy.
        const float rotation = age * (Length(particle.angularVelocity) * 30);
        particle.angles = {rotation * particle.angularVelocity[0],
                           rotation * particle.angularVelocity[1],
                           1 + rotation * particle.angularVelocity[2]};
    }
    Move(particle, now - previousTime, services);
    if (particle.deadline != 0 && now >= particle.deadline && !particle.started)
    {
        particle.deadline = static_cast<float>(clientTime + 0.2);
        particle.started = true;
        if (particle.emitFill)
            Fill(particle.sourceOrigin, particle.sourceBrightness, clientTime, services);
    }
    if (particle.deadline != 0 && now >= particle.deadline && particle.started)
    {
        particle.deadline = 0;
        for (float& component : particle.velocity)
            component *= 0.035f;
    }
    return particle.die == 0 || now < particle.die;
}
}
