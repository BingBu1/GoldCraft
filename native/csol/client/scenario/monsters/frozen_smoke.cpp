// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "frozen_smoke.hpp"
#include "../../../scenario/monsters/frozen_smoke_particle.hpp"
#include <pm_movevars.h>
#include <triangleapi.h>
#include <algorithm>
#include <ostream>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::client
{
namespace
{
struct Slot
{
    frozen_smoke::Particle particle;
    std::uint64_t generation{};
    float nextPvsCheck{}, distance{};
    bool active{}, inPvs{true};
};
std::array<frozen_smoke::Emission, 128> emitters;
std::array<Slot, 1200> particles;
model_t* sprite{};
float previousTime{};
bool haveTime{};
std::uint64_t received{}, emitted{}, exhausted{}, invalid{}, draws{};
std::uint64_t epoch{};
bool busy{};

struct Operation
{
    Operation() { busy = true; }
    ~Operation() { busy = false; }
};
struct Random
{
    std::uint64_t generation;
    float operator()(float low, float high) const
    {
        // A reset inside the first sample must not call back into the engine
        // for the remaining fields of that retired Prepare/Spawn operation.
        return generation == epoch ? engine.pfnRandomFloat(low, high) : 0;
    }
};

bool LoadSprite(std::uint64_t generation)
{
    if (!sprite && engine.CL_LoadModel)
    {
        int index{};
        auto* loaded = engine.CL_LoadModel(frozen_smoke::SmokeSprite, &index);
        if (generation != epoch) return false;
        if (loaded && loaded->type == mod_sprite && loaded->numframes >= 1) sprite = loaded;
    }
    return sprite != nullptr;
}

bool SampleVisibility(Slot& slot, float now, std::uint64_t generation)
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
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    if (generation != epoch) return false;
    const float* origin = local ? local->origin : view.vieworg;
    slot.distance = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const float delta = origin[axis] - slot.particle.origin[axis];
        slot.distance += delta * delta;
    }
    return true;
}
void Advance()
{
    if (!engine.GetClientTime || !engine.pfnRandomFloat) return;
    const auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch) return;
    if (!std::isfinite(now) || (haveTime && now < previousTime)) { ResetFrozenSmoke(); return; }
    if (haveTime && now == previousTime) return;
    const float before = haveTime ? previousTime : now;
    previousTime = now;
    haveTime = true;
    // Movevars is the host's replicated world gravity. The original particle
    // manager receives gravity from the engine, not from each dead entity.
    const float gravity = view.movevars ? view.movevars->gravity : 800.0f;
    if (!std::isfinite(gravity)) { ResetFrozenSmoke(); return; }
    Random random{generation};
    for (auto& emission : emitters)
    {
        if (!emission.active || emission.next == 0 || now <= emission.next) continue;
        // Remaining count1 is consumed before allocation. Pool exhaustion
        // ends this emission pass; the other pending records wait a frame.
        emission.active = false;
        const auto free = std::find_if(particles.begin(), particles.end(), [](const Slot& slot) { return !slot.active; });
        if (free == particles.end()) { ++exhausted; break; }
        const auto particle = frozen_smoke::Spawn(emission, now, random);
        if (generation != epoch) return;
        *free = {particle, ++emitted, now, 0, true, true};
    }
    for (auto& slot : particles)
    {
        if (!slot.active) continue;
        // Original visibility and transparent-sort distance precede Think.
        if (!SampleVisibility(slot, now, generation)) return;
        slot.active = frozen_smoke::Tick(slot.particle, now, before, gravity);
    }
}
}

bool PlayFrozenSmoke(const frozen_smoke::Event& event)
{
    if (busy) return false;
    Operation operation;
    SynchronizeSession();
    if (!frozen_smoke::Valid(event) || !engine.GetClientTime || !engine.pfnRandomFloat)
    { ++invalid; return false; }
    auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch) return false;
    if (!std::isfinite(now)) { ++invalid; return false; }
    if (haveTime && now < previousTime) { ResetFrozenSmoke(); generation = epoch; }
    if (!LoadSprite(generation)) { ++invalid; return false; }
    if (!haveTime) { haveTime = true; previousTime = now; }
    Random random{generation};
    for (unsigned i = 0; i < 3; ++i)
    {
        auto emission = frozen_smoke::Prepare(event, now, random);
        if (generation != epoch) return false;
        const auto free = std::find_if(emitters.begin(), emitters.end(), [](const auto& value) { return !value.active; });
        if (free == emitters.end()) { ++exhausted; continue; }
        *free = emission;
    }
    ++received;
    return true;
}

bool ConsumeFrozenSmoke(int size, const void* data)
{
    if (!data || size != static_cast<int>(frozen_smoke::PayloadSize)) return false;
    const auto event = frozen_smoke::Read({static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)});
    if (!event) return false;
    PlayFrozenSmoke(*event);
    return true;
}

void ResetFrozenSmoke()
{
    ++epoch;
    for (auto& emission : emitters) emission.active = false;
    for (auto& particle : particles) particle.active = false;
    sprite = nullptr;
    previousTime = 0;
    haveTime = false;
}

void FrozenSmokeFrame()
{
    if (busy) return;
    Operation operation;
    Advance();
}

void DrawFrozenSmoke()
{
    if (busy) return;
    Operation operation;
    const auto generation = epoch;
    auto* tri = engine.pTriAPI;
    if (!tri || !engine.pfnAngleVectors || !tri->SpriteTexture || !tri->RenderMode ||
        !tri->CullFace || !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f) return;
    Advance();
    if (generation != epoch || !sprite) return;
    std::array<std::size_t, particles.size()> ordered;
    std::size_t count{};
    for (std::size_t index = 0; index < particles.size(); ++index)
        if (particles[index].active && particles[index].inPvs) ordered[count++] = index;
    if (!count) return;
    std::sort(ordered.begin(), ordered.begin() + count, [](std::size_t a, std::size_t b) {
        return particles[a].distance > particles[b].distance;
    });
    struct Restore
    {
        decltype(tri) api;
        bool begun{};
        ~Restore()
        {
            if (begun) api->End();
            api->Color4f(1, 1, 1, 1);
            api->RenderMode(kRenderNormal);
            api->CullFace(TRI_FRONT);
        }
    } restore{tri};
    tri->RenderMode(kRenderTransAlpha);
    if (generation != epoch) return;
    tri->CullFace(TRI_NONE);
    if (generation != epoch) return;
    constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& particle = particles[ordered[i]].particle;
        // The wire enables frame handling with fps0. Do not invent an
        // animation rate from the number of frames stored in the SPR.
        const bool bound = tri->SpriteTexture(sprite, 0) != 0;
        if (generation != epoch) return;
        if (!bound) continue;
        float angles[]{view.viewangles[0], view.viewangles[1], particle.roll};
        float forward[3], right[3], up[3], corners[4][3];
        engine.pfnAngleVectors(angles, forward, right, up);
        if (generation != epoch) return;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float horizontal = right[axis] * particle.size, vertical = up[axis] * particle.size;
            const float bottom = (particle.origin[axis] - horizontal * .5f) - vertical * .5f;
            corners[0][axis] = bottom + vertical;
            corners[1][axis] = bottom;
            corners[2][axis] = bottom + horizontal;
            corners[3][axis] = (bottom + horizontal) + vertical;
        }
        restore.begun = true;
        tri->Begin(TRI_QUADS);
        if (generation != epoch) return;
        tri->Color4f(180.0f / 255.0f, 180.0f / 255.0f, 225.0f / 255.0f, particle.alpha / 255.0f);
        if (generation != epoch) return;
        for (unsigned corner = 0; corner < 4; ++corner)
        {
            tri->TexCoord2f(uv[corner][0], uv[corner][1]);
            if (generation != epoch) return;
            tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]);
            if (generation != epoch) return;
        }
        restore.begun = false;
        tri->End();
        if (generation != epoch) return;
        ++draws;
    }
}

void FrozenSmokeDiagnostics(std::ostream& out)
{
    const auto active = std::count_if(particles.begin(), particles.end(), [](const Slot& slot) { return slot.active; });
    const auto pending = std::count_if(emitters.begin(), emitters.end(), [](const auto& value) { return value.active; });
    out << "frozenSmoke received=" << received << " emitted=" << emitted << " active=" << active
        << " pending=" << pending << " exhausted=" << exhausted << " invalid=" << invalid << " draws=" << draws << '\n';
}
}
