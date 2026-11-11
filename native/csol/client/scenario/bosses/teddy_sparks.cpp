// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "teddy_sparks.hpp"
#include "../../../scenario/bosses/teddy_spark_particle.hpp"
#include <pm_movevars.h>
#include <triangleapi.h>
#include <algorithm>
#include <ostream>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
namespace spark = teddy_sparks;
struct Emission { spark::Emitter parameters; float next{}; unsigned remaining{}; };
struct Slot { spark::Particle particle; float nextPvsCheck{}; bool active{}, inPvs{true}; };
std::array<Emission, spark::EmitterCapacity> emitters;
std::array<Slot, spark::ParticleCapacity> particles;
std::array<model_t*, spark::Sprites.size()> sprites{};
std::size_t emitterEnd{}, particleEnd{};
pfnUserMsgHook previous{};
float lastTime{};
bool haveTime{}, busy{};
std::uint64_t epoch{}, received{}, emitted{}, invalid{}, exhausted{}, draws{};
struct Operation { Operation() { busy = true; } ~Operation() { busy = false; } };
struct Random
{
    std::uint64_t generation;
    float operator()(float low, float high) const
    { return generation == epoch ? engine.pfnRandomFloat(low, high) : 0.0f; }
};

int Receive(const char* name, int size, void* data)
{
    const auto bytes = data && size > 0 && size <= 192 ?
        std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)) :
        std::span<const std::uint8_t>{};
    if (!spark::Identify(bytes)) return previous ? previous(name, size, data) : 1;
    const auto event = spark::Read(bytes);
    if (!event || busy || !engine.GetClientTime || !engine.CL_LoadModel) { ++invalid; return 1; }
    Operation operation;
    SynchronizeSession();
    const auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch || !std::isfinite(now)) { ++invalid; return 1; }
    std::size_t index{};
    while (index < emitterEnd && emitters[index].remaining != 0) ++index;
    if (index == emitters.size()) { ++exhausted; return 1; }
    // Original Add loads only after finding a free record. Cache this cold
    // lookup per session; frame and draw never perform resource I/O.
    if (!sprites[event->sprite])
    {
        int modelIndex{};
        auto* model = engine.CL_LoadModel(spark::Sprites[event->sprite].data(), &modelIndex);
        if (generation != epoch) return 1;
        if (!model || model->type != mod_sprite || model->numframes < 1 ||
            model->numframes > static_cast<int>(spark::FrameCapacity)) { ++invalid; return 1; }
        sprites[event->sprite] = model;
    }
    emitters[index] = {*event, std::max(0.0f, now), 3};
    emitterEnd = std::max(emitterEnd, index + 1);
    // Message packet time can lag the last interpolated frame. Never clear
    // existing particles or lower integrated time on an arriving message.
    if (!haveTime) { haveTime = true; lastTime = now; }
    ++received;
    return 1;
}

bool Visibility(Slot& slot, float now, std::uint64_t generation)
{
    auto* tri = engine.pTriAPI;
    if (now >= slot.nextPvsCheck && tri && tri->BoxInPVS)
    {
        const float extent = slot.particle.size / 5.0f;
        float mins[3], maxs[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            mins[axis] = slot.particle.origin[axis] - extent;
            maxs[axis] = slot.particle.origin[axis] + extent;
        }
        const bool visible = tri->BoxInPVS(mins, maxs) != 0;
        if (generation != epoch) return false;
        slot.inPvs = visible;
        slot.nextPvsCheck = static_cast<float>(static_cast<double>(now) + static_cast<double>(.1f));
    }
    return true;
}

void Advance()
{
    if (!engine.GetClientTime || !engine.pfnRandomFloat || !view.movevars) return;
    const auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch || !std::isfinite(now) || (haveTime && now <= lastTime)) return;
    const float gravity = view.movevars->gravity;
    if (!std::isfinite(gravity)) return;
    const float before = haveTime ? lastTime : now;
    lastTime = now; haveTime = true;
    // The original previous-time clock advances even with an empty manager.
    // Otherwise a later spawn would integrate the entire idle interval.
    if (!emitterEnd && !particleEnd) return;
    Random random{generation};
    for (std::size_t i = 0; i < emitterEnd; ++i)
    {
        auto& emission = emitters[i];
        if (!emission.remaining || emission.next == 0 || now <= emission.next) continue;
        --emission.remaining;
        emission.next = now + emission.parameters.interval;
        std::size_t index{};
        while (index < particleEnd && particles[index].active) ++index;
        // A failed allocation ends the original emission pass after consuming
        // this count; other emitters remain pending for the next frame.
        if (index == particles.size()) { ++exhausted; break; }
        const auto particle = spark::Spawn(emission.parameters, now, random);
        if (generation != epoch) return;
        particles[index] = {particle, now, true, true};
        particleEnd = std::max(particleEnd, index + 1);
        ++emitted;
    }
    while (emitterEnd && !emitters[emitterEnd - 1].remaining) --emitterEnd;
    for (std::size_t i = 0; i < particleEnd; ++i)
    {
        auto& slot = particles[i];
        if (!slot.active) continue;
        if (!Visibility(slot, now, generation)) return;
        slot.active = spark::Tick(slot.particle, now, before, gravity,
            slot.particle.sprite ? 17 : 16, sprites[slot.particle.sprite]->numframes);
    }
    while (particleEnd && !particles[particleEnd - 1].active) --particleEnd;
}
}

void InitializeTeddySparks() { InstallChainedUserMessage(spark::Message, Receive, previous); }
void ResetTeddySparks()
{
    ++epoch;
    emitterEnd = particleEnd = 0;
    sprites = {};
    haveTime = false; lastTime = 0;
    // Retired slots are overwritten before reuse; no full-pool clear is needed.
}
void TeddySparksFrame()
{
    if (busy) return;
    Operation operation;
    Advance();
}
void DrawTeddySparks()
{
    if (busy || (!emitterEnd && !particleEnd)) return;
    Operation operation;
    const auto generation = epoch;
    auto* tri = engine.pTriAPI;
    if (!tri || !engine.pfnAngleVectors || !tri->SpriteTexture || !tri->RenderMode || !tri->CullFace ||
        !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f) return;
    Advance();
    if (generation != epoch || !particleEnd) return;
    constexpr std::size_t frames = spark::FrameCapacity + 1; // Preserve original frame==numframes.
    std::array<int, spark::Sprites.size() * frames> heads;
    std::array<int, spark::ParticleCapacity> links;
    heads.fill(-1);
    bool visible{};
    for (std::size_t i = 0; i < particleEnd; ++i)
        if (particles[i].active && particles[i].inPvs)
        {
            const auto& particle = particles[i].particle;
            const auto bucket = particle.sprite * frames + static_cast<std::size_t>(particle.frame);
            links[i] = heads[bucket]; heads[bucket] = static_cast<int>(i); visible = true;
        }
    if (!visible) return;
    struct Restore
    {
        decltype(tri) api; bool begun{};
        ~Restore()
        {
            if (begun) api->End();
            api->Color4f(1, 1, 1, 1); api->RenderMode(kRenderNormal); api->CullFace(TRI_FRONT);
        }
    } restore{tri};
    tri->RenderMode(kRenderTransAdd);
    if (generation != epoch) return;
    tri->CullFace(TRI_NONE);
    if (generation != epoch) return;
    constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    for (std::size_t sprite = 0; sprite < sprites.size(); ++sprite)
    {
        auto* model = sprites[sprite];
        if (!model) continue;
        for (int frame = 0; frame <= model->numframes; ++frame)
        {
            const auto head = heads[sprite * frames + static_cast<std::size_t>(frame)];
            if (head < 0) continue;
            const bool bound = tri->SpriteTexture(model, frame) != 0;
            if (generation != epoch) return;
            if (!bound) continue;
            restore.begun = true; tri->Begin(TRI_QUADS);
            if (generation != epoch) return;
            for (int i = head; i >= 0; i = links[i])
            {
                const auto particle = particles[static_cast<std::size_t>(i)].particle;
                float angles[]{view.viewangles[0], view.viewangles[1], particle.roll};
                spark::Vec3 forward, right, up;
                engine.pfnAngleVectors(angles, forward.data(), right.data(), up.data());
                if (generation != epoch) return;
                const auto corners = spark::Corners(particle, right, up);
                tri->Color4f(particle.color[0] / 255.0f, particle.color[1] / 255.0f,
                    particle.color[2] / 255.0f, particle.alpha / 255.0f);
                if (generation != epoch) return;
                for (unsigned corner = 0; corner < 4; ++corner)
                {
                    tri->TexCoord2f(uv[corner][0], uv[corner][1]);
                    if (generation != epoch) return;
                    tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]);
                    if (generation != epoch) return;
                }
                ++draws;
            }
            restore.begun = false; tri->End();
            if (generation != epoch) return;
        }
    }
}
void TeddySparksDiagnostics(std::ostream& out)
{
    const auto active = std::count_if(particles.begin(), particles.begin() + particleEnd,
        [](const Slot& slot) { return slot.active; });
    const auto pending = std::count_if(emitters.begin(), emitters.begin() + emitterEnd,
        [](const Emission& emitter) { return emitter.remaining != 0; });
    out << "teddySparks received=" << received << " emitted=" << emitted << " active=" << active
        << " pending=" << pending << " invalid=" << invalid << " exhausted=" << exhausted << " draws=" << draws << '\n';
}
}
