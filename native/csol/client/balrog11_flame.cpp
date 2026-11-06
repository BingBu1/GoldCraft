// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "balrog11_flame.hpp"
#include "../balrog11_flame.hpp"
#include <triangleapi.h>
#include <algorithm>
#include <cstring>
#include <ostream>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
namespace effect = balrog11::flame;
struct Emission
{
    effect::Emitter parameters;
    model_t* model{};
    int modelIndex{};
    float next{}, bindUntil{};
    bool active{};
};
struct Slot { effect::Particle particle; bool active{}; };
std::array<Emission, 128> emitters;
std::array<Slot, 1200> particles;
model_t* sprite{};
pfnUserMsgHook previous{};
float lastTime{};
bool haveTime{}, busy{};
std::uint64_t epoch{}, received{}, emitted{}, retired{}, invalid{}, exhausted{}, draws{};
struct Operation { Operation() { busy = true; } ~Operation() { busy = false; } };
int Receive(const char* name, int size, void* data)
{
    const auto bytes = data && size > 0 && size <= 192 ?
        std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size) : std::span<const std::uint8_t>{};
    if (!effect::IsFlameMessage(bytes)) return previous ? previous(name, size, data) : 1;
    const auto event = effect::Read(bytes);
    if (!event || busy || !engine.GetClientTime) { ++invalid; return 1; }
    Operation guard;
    SynchronizeSession();
    auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch || !std::isfinite(now)) { ++invalid; return 1; }
    // User messages observe packet time, which normally lags the previous
    // frame's interpolated cl.time. That is not a clock rewind: clearing here
    // wiped every live trail on each new TempEmit (live B 2026-11-05:
    // 25 received, 105 emitted, 0 retired). Level changes reset via session.
    const auto free = std::find_if(emitters.begin(), emitters.end(), [](const auto& e) { return !e.active; });
    if (free == emitters.end()) { ++exhausted; return 1; }
    // TempEmit precedes the new entity's first snapshot. Resolve its model
    // when the entity becomes visible, with a bounded startup lifetime.
    *free = {*event, nullptr, 0, now, now + 1, true};
    if (!haveTime) { haveTime = true; lastTime = now; }
    ++received;
    return 1;
}
void Advance()
{
    if (!engine.GetClientTime || !engine.GetEntityByIndex || !engine.GetLocalPlayer || !engine.pfnRandomFloat) return;
    const auto generation = epoch;
    const float now = static_cast<float>(engine.GetClientTime());
    if (generation != epoch) return;
    if (!std::isfinite(now)) { ResetBalrog11Flame(); return; }
    // Keep the last integrated time across clock corrections. Lowering it
    // would integrate the same interval twice and can make age/frame negative
    // for particles born before the correction. Session reset owns clearing.
    if (haveTime && now <= lastTime) return;
    const float before = haveTime ? lastTime : now;
    lastTime = now; haveTime = true;
    if (!sprite && engine.CL_LoadModel)
    {
        int index{};
        auto* loaded = engine.CL_LoadModel(effect::Sprite, &index);
        if (generation != epoch) return;
        if (loaded && loaded->type == mod_sprite && loaded->numframes > 0 && loaded->numframes <= 256) sprite = loaded;
    }
    if (!sprite) return;
    const auto* local = engine.GetLocalPlayer();
    if (generation != epoch || !local) return;
    const int message = local->curstate.messagenum;
    for (auto& emission : emitters)
    {
        if (!emission.active || now <= emission.next) continue;
        const auto* owner = engine.GetEntityByIndex(emission.parameters.owner);
        if (generation != epoch) return;
        const bool valid = owner && owner->model && owner->model->type == mod_sprite &&
            !std::strcmp(owner->model->name, effect::Sprite) && owner->curstate.messagenum == message;
        if (!emission.model)
        {
            if (!valid)
            {
                if (now >= emission.bindUntil) { emission.active = false; ++retired; }
                continue;
            }
            emission.model = owner->model;
            emission.modelIndex = owner->curstate.modelindex;
        }
        if (!valid || emission.model != owner->model || emission.modelIndex != owner->curstate.modelindex)
        { emission.active = false; ++retired; continue; }
        emission.next = now + effect::Interval; // Original emits once per frame, without catch-up.
        const auto free = std::find_if(particles.begin(), particles.end(), [](const auto& p) { return !p.active; });
        if (free == particles.end()) { ++exhausted; break; }
        effect::Particle next;
        std::copy_n(owner->origin, 3, next.origin.begin());
        if (std::any_of(next.origin.begin(), next.origin.end(), [](float n) { return !std::isfinite(n); }))
        { emission.active = false; ++invalid; continue; }
        for (float& velocity : next.velocity)
        {
            velocity = engine.pfnRandomFloat(-1, 1) * effect::Spread;
            if (generation != epoch) return;
        }
        engine.pfnRandomFloat(1, 1.5f); // Preserve original mass RNG even with no collision.
        if (generation != epoch) return;
        next.born = now; next.die = now + effect::ParticleLife;
        next.rotation = emission.parameters.rotation * .1f;
        *free = {next, true}; ++emitted;
    }
    for (auto& slot : particles)
        if (slot.active) slot.active = effect::Tick(slot.particle, now, before, sprite->numframes);
}
}
void InitializeBalrog11Flame() { InstallChainedUserMessage(balrog11::flame::Message, Receive, previous); }
void ResetBalrog11Flame()
{
    ++epoch;
    emitters = {}; particles = {}; sprite = nullptr;
    haveTime = false; lastTime = 0;
}
void Balrog11FlameFrame()
{
    if (busy) return;
    Operation guard; Advance();
}
void DrawBalrog11Flame()
{
    if (busy) return;
    Operation guard;
    const auto generation = epoch;
    auto* tri = engine.pTriAPI;
    if (!tri || !engine.pfnAngleVectors || !tri->SpriteTexture || !tri->RenderMode || !tri->CullFace ||
        !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f) return;
    Advance();
    if (generation != epoch || !sprite) return;
    // Additive particles can be batched by sprite frame without depth sort.
    std::array<int, 256> heads; heads.fill(-1);
    std::array<int, particles.size()> links;
    for (unsigned i = 0; i < particles.size(); ++i)
        if (particles[i].active)
        { const int frame = particles[i].particle.frame; links[i] = heads[frame]; heads[frame] = int(i); }
    struct Restore
    {
        decltype(tri) api; bool begun{};
        ~Restore() { if (begun) api->End(); api->Color4f(1, 1, 1, 1); api->RenderMode(kRenderNormal); api->CullFace(TRI_FRONT); }
    } restore{tri};
    tri->RenderMode(kRenderTransAdd);
    if (generation != epoch) return;
    tri->CullFace(TRI_NONE);
    if (generation != epoch) return;
    constexpr float uv[4][2]{{0,0},{0,1},{1,1},{1,0}};
    for (int frame = 0; frame < sprite->numframes; ++frame)
    {
        if (heads[frame] < 0) continue;
        const bool bound = tri->SpriteTexture(sprite, frame) != 0;
        if (generation != epoch) return;
        if (!bound) continue;
        restore.begun = true; tri->Begin(TRI_QUADS);
        if (generation != epoch) return;
        for (int i = heads[frame]; i >= 0; i = links[i])
        {
            const auto particle = particles[i].particle;
            float angles[]{view.viewangles[0], view.viewangles[1], particle.roll}, forward[3], right[3], up[3];
            engine.pfnAngleVectors(angles, forward, right, up);
            if (generation != epoch) return;
            tri->Color4f(1, 1, 1, particle.alpha / 255.f);
            if (generation != epoch) return;
            for (unsigned corner = 0; corner < 4; ++corner)
            {
                float position[3];
                for (unsigned axis = 0; axis < 3; ++axis)
                    position[axis] = particle.origin[axis] + effect::Size *
                        (right[axis] * (corner < 2 ? -.5f : .5f) + up[axis] * (corner == 0 || corner == 3 ? .5f : -.5f));
                tri->TexCoord2f(uv[corner][0], uv[corner][1]);
                if (generation != epoch) return;
                tri->Vertex3f(position[0], position[1], position[2]);
                if (generation != epoch) return;
            }
            ++draws;
        }
        restore.begun = false; tri->End();
        if (generation != epoch) return;
    }
}
void Balrog11FlameDiagnostics(std::ostream& out)
{
    out << ",\"balrog11Flame\":{\"received\":" << received << ",\"emitted\":" << emitted
        << ",\"retired\":" << retired << ",\"invalid\":" << invalid << ",\"exhausted\":" << exhausted << ",\"draws\":" << draws << '}';
}
}
