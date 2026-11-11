// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "teddy_spark_particle.hpp"

namespace csol::teddy_blackhole_smoke
{
using teddy_sparks::Vec3;
inline constexpr std::string_view Sprite = "sprites/smoke_ia.spr";
inline constexpr std::size_t MessageBytes = 99, RingCount = 36, EmitterCapacity = 128;
struct Ring
{
    Vec3 center{};
};
struct Emitter
{
    Vec3 origin{}, velocity{};
    float spread{}, size{}, growth{}, next{};
    int remaining{};
};
// Narrow MP114931C0 contract; coordinate mode is the explicitly supported
// signed-short eighth host adapter. Other MPToCL messages remain caller-owned.
inline std::optional<Ring> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != MessageBytes || bytes[0] != 88 || bytes[Sprite.size() + 1] != 0 ||
        std::string_view(reinterpret_cast<const char *>(bytes.data() + 1), Sprite.size()) != Sprite)
        return {};
    std::size_t at = Sprite.size() + 2;
    const auto byte = [&]() { return bytes[at++]; };
    const auto word = [&]()
    {
        const unsigned low = byte();
        return low | unsigned(byte()) << 8;
    };
    const auto integer = [&]()
    {
        const std::uint32_t low = word();
        return low | std::uint32_t(word()) << 16;
    };
    // Exact original serialized float bits, including angle increment.
    constexpr std::uint32_t fields[]{
        0x42c80000, 0x3e32b8c0, 0x42200000, 5,  0x3db851ec, 0x3f800000, 50, 70,        0x3ca3d70a,
        0x3f000000, 2,          5,          10, 1,          0x34,       36, 0x42480000};
    for (auto expected : fields)
        if (integer() != expected)
            return {};
    for (unsigned i = 0; i != 3; ++i)
        if (byte() != 150)
            return {};
    Ring ring;
    for (float &v : ring.center)
        v = static_cast<float>(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word()))) *
            .125f;
    return ring;
}
// Caller supplies the verified original CRT numeric boundary. Cold per-message
// preparation; original repeated float angle addition must not become i*step.
template <class Trigonometry, class Random, class Add>
void Prepare(const Ring &ring, float now, Trigonometry &trig, Random &random, Add &add)
{
    constexpr float step = std::bit_cast<float>(0x3e32b8c0u);
    float angle = step;
    for (std::size_t i = 0; i != RingCount; ++i)
    {
        const float cosine = static_cast<float>(trig.cosine(static_cast<double>(angle)));
        const float sine = static_cast<float>(trig.sine(static_cast<double>(angle)));
        Emitter e;
        e.origin = {100.f * cosine + ring.center[0], 100.f * sine + ring.center[1],
                    100.f * 0.f + ring.center[2]};
        e.velocity = {50.f * cosine, 50.f * sine, 50.f * 0.f};
        e.spread = random(-32.f, 32.f);
        e.size = random(50.f, 70.f);
        e.growth = random(2.f, 5.f);
        e.next = now > 0.f ? now : 0.f;
        e.remaining = 5;
        add(e);
        angle = angle + step;
    }
}
struct Particle
{
    Vec3 origin{}, velocity{}, lastPosition{}, angles{}, rotationVelocity{0, 0, 1};
    float born{}, die{}, size{}, initialSize{}, growth{}, alpha{40}, lastUpdate{}, mass{};
    int frame{}, internalFlags{0x20};
    static constexpr int Mode = 4, Frames = 12, Fps = 1, DrawFlags = 0x1c;
};
// Allocation/gate failure is represented by the caller before Spawn; the four
// original random samples are consumed only after successful factory creation.
template <class Random> Particle Spawn(const Emitter &emitter, float now, Random &random)
{
    Particle p;
    p.origin = emitter.origin;
    p.lastPosition = p.origin;
    for (unsigned i = 0; i != 3; ++i)
        p.velocity[i] = random(-1.f, 1.f) * emitter.spread + emitter.velocity[i];
    p.mass = random(1.f, 1.5f);
    p.born = now;
    p.die = now + 1.f;
    p.size = p.initialSize = emitter.size;
    p.growth = emitter.growth;
    p.angles = {0, 0, 1};
    return p;
}
struct Emitters
{
    std::array<Emitter, EmitterCapacity> slots{};
    bool Add(const Emitter &emitter)
    {
        for (auto &slot : slots)
            if (slot.remaining == 0)
            {
                slot = emitter;
                return true;
            }
        return false;
    }
    template <class Factory> void Frame(float now, Factory &factory)
    {
        for (auto &e : slots)
        {
            if (e.remaining <= 0 || !(now > e.next) || e.next == 0.f)
                continue;
            --e.remaining;
            e.next = now + .09f;
            if (!factory(e, now))
                return;
        }
    }
};
struct Trace
{
    float fraction;
    Vec3 endPosition, normal;
    int entity;
};
inline Vec3 VectorMA(const Vec3 &a, float scale, const Vec3 &b)
{
    return {scale * b[0] + a[0], scale * b[1] + a[1], scale * b[2] + a[2]};
}
// Exact actual internal20 branch of01DE2210. Typed callback receives the
// original hull2/start/end/flags10/ignore-1 contract, never a guessed engine ABI.
template <class TraceWorld>
void Collide(Particle &p, float now, float previousTime, float gravity, TraceWorld &world)
{
    if (p.internalFlags == 0)
        return;
    p.lastUpdate = now;
    const Trace trace = world(2, p.lastPosition, p.origin, 10, -1);
    if (trace.fraction != 1.f)
    {
        for (float &v : p.velocity)
            v = .6f * v;
        const float speed = static_cast<float>(std::sqrt(
            static_cast<double>((p.velocity[0] * p.velocity[0] + p.velocity[1] * p.velocity[1]) +
                                p.velocity[2] * p.velocity[2])));
        if (speed < 10.f)
        {
            p.internalFlags = 0;
            p.velocity = {};
            p.rotationVelocity = {};
            p.origin = trace.endPosition;
        }
        const float delta = now - previousTime;
        p.origin = VectorMA(p.lastPosition, delta * trace.fraction, p.velocity);
        float damping = 1.f * .5f;
        const float acceleration = ((-delta) * gravity) * .02f;
        if (trace.normal[2] > .9f && !(0.f < p.velocity[2]) &&
            !(p.velocity[2] < acceleration * 3.f))
        {
            p.velocity = {};
            p.rotationVelocity = {};
            p.internalFlags = 0;
            damping = 0.f;
        }
        if (damping != 0.f)
        {
            const float dot = (trace.normal[0] * p.velocity[0] + trace.normal[1] * p.velocity[1]) +
                              trace.normal[2] * p.velocity[2];
            const float reflection = static_cast<float>(static_cast<double>(dot) * -2.0);
            p.velocity = VectorMA(p.velocity, reflection, trace.normal);
            p.angles[1] = -p.angles[1];
        }
        if (damping != 1.f)
        {
            for (float &v : p.velocity)
                v = damping * v;
            for (float &a : p.angles)
                a = a * .9f;
        }
        // Actual extraFlags0 makes virtual collision callback02AD42F0 return
        // without secondary effects; no unsupported flag states are exposed.
    }
    p.lastPosition = p.origin;
}
inline bool Expired(const Particle &p, float now) { return p.die != 0.f && now >= p.die; }
template <class TraceWorld, class Camera, class AngleVectors>
bool Tick(Particle &p, float now, float previousTime, float gravity, TraceWorld &world,
          Camera &camera, AngleVectors &angleVectors)
{
    const float age = now - p.born;
    // Derived alpha adjustment precedes shared fade. Actual alpha40 never takes
    // the >=255 branch; retain its death sideeffect for carried alpha below0.
    if (p.die - now <= .5f && p.alpha > 0.f)
        p.alpha -= age * .1f;
    if (p.alpha < 0.f)
    {
        p.alpha = 0.f;
        p.die = now;
    }
    p.frame = static_cast<int>(age * 1.f + 0.f) % 12;
    p.size = p.initialSize + age * (p.growth * 30.f);
    if (p.size < .0001f)
        p.die = now;
    p.alpha = 40.f - (.5f * 30.f) * age;
    if (p.alpha < 1.f)
        p.die = now;
    if (p.rotationVelocity[0] != 0.f || p.rotationVelocity[1] != 0.f ||
        p.rotationVelocity[2] != 0.f)
    {
        const float speed = static_cast<float>(
            std::sqrt(static_cast<double>((p.rotationVelocity[0] * p.rotationVelocity[0] +
                                           p.rotationVelocity[1] * p.rotationVelocity[1]) +
                                          p.rotationVelocity[2] * p.rotationVelocity[2])));
        p.angles[2] = 1.f + (age * (speed * 30.f)) * p.rotationVelocity[2];
    }
    const float delta = now - previousTime;
    const float acceleration = ((-delta) * gravity) * .02f;
    for (unsigned i = 0; i != 3; ++i)
        p.origin[i] = p.velocity[i] * delta + p.origin[i];
    p.velocity[2] = p.velocity[2] + acceleration;
    Collide(p, now, previousTime, gravity, world);
    if (p.alpha < 0.f)
        p.alpha = 0.f;
    // Original invokes the getter three times, not a cached single result.
    const Vec3 view{camera()[0], camera()[1], camera()[2]};
    angleVectors(view);
    p.angles[0] = view[0];
    p.angles[1] = view[1];
    return !Expired(p, now);
}
inline auto Corners(const Particle &particle, const Vec3 &right, const Vec3 &up)
{
    teddy_sparks::Particle geometry;
    geometry.origin = particle.origin;
    geometry.size = particle.size;
    return teddy_sparks::Corners(geometry, right, up);
}
struct DrawData
{
    teddy_sparks::Quad corners;
    std::array<float, 4> color;
    int frame;
    static constexpr int Mode = 4;
};
// Original actual drawFlags1C: direct RGB150, aspect1, mode4. Resource bind
// and AngleVectors remain explicit host boundaries; no engine ABI or GL calls.
inline std::optional<DrawData> PrepareDraw(const Particle &particle, float now, const Vec3 &right,
                                           const Vec3 &up)
{
    if (now >= particle.die)
        return {};
    return DrawData{Corners(particle, right, up),
                    {150.f / 255.f, 150.f / 255.f, 150.f / 255.f, particle.alpha / 255.f},
                    particle.frame};
}
} // namespace csol::teddy_blackhole_smoke
