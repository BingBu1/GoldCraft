// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "elite_visuals.hpp"
#include "../elite_visual_protocol.hpp"
#include <triangleapi.h>
#include <algorithm>
#include <cstdlib>
#include <ostream>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
pfnUserMsgHook previousTrail{}, previousEmitter{}, previousInit{}, previousReset{};
struct Emission
{
    elitefx::Emitter parameters;
    model_t* ownerModel{};
    int ownerModelIndex{};
    float next{};
    bool active{};
};
struct Particle
{
    elitefx::Emitter parameters;
    std::array<float, 3> origin{}, velocity{};
    float born{}, die{}, size{}, alpha{};
    int sprite{}, frame{};
    bool active{};
};
std::array<Emission, 128> emitters;
std::array<Particle, 1200> particles;
std::array<model_t*, 3> sprites;
float lastTime{};
bool haveTime{};
std::uint64_t received{}, emitted{}, exhausted{}, retired{}, draws{};
bool Models()
{
    if (!engine.CL_LoadModel) return false;
    constexpr const char* paths[]{"sprites/burn5.spr", "sprites/burn6.spr", "sprites/burn7.spr"};
    for (unsigned i = 0; i < sprites.size(); ++i)
    {
        if (!sprites[i]) { int index{}; sprites[i] = engine.CL_LoadModel(paths[i], &index); }
        if (!sprites[i] || sprites[i]->type != mod_sprite || sprites[i]->numframes <= 0)
        { sprites[i] = nullptr; return false; }
    }
    return true;
}
void Add(const elitefx::Emitter& parameters)
{
    if (!engine.GetClientTime || !engine.GetEntityByIndex) return;
    const float now = static_cast<float>(engine.GetClientTime());
    if (!std::isfinite(now)) return;
    // Messages observe packet time, normally behind the last interpolated
    // frame. Do not treat that as a rewind: it cleared every live trail on
    // each new emitter (same defect measured live for Balrog11 TempEmit).
    if (!Models()) return;
    const auto* owner = engine.GetEntityByIndex(parameters.owner);
    if (!owner || !owner->model) return;
    const auto slot = std::find_if(emitters.begin(), emitters.end(), [](const Emission& e) { return !e.active; });
    if (slot == emitters.end()) { ++exhausted; return; }
    *slot = {parameters, owner->model, owner->curstate.modelindex, now, true};
    ++received;
}
float CRTFloat(float low, float high)
{
    // Original01DED700 uses CRT rand (15 bits), not engine RANDOM_FLOAT.
    //02FB3370 is float bits38000100, exactly the original1/32767 constant.
    return (static_cast<float>(std::rand()) * 3.0518509447574615e-05f) * (high - low) + low;
}
int Trail(const char* name, int size, void* data)
{
    SynchronizeSession();
    int owner{};
    if (data && size == 6 && elitefx::DecodeFireball(
        {static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)}, owner))
    {
        elitefx::Emitter emitter;
        emitter.owner = owner; emitter.interval = emitter.life = .2f;
        emitter.fireball = true; emitter.alpha = 250;
        emitter.spread = CRTFloat(12, 32);
        emitter.size = CRTFloat(27, 32);
        emitter.growth = CRTFloat(2, 5);
        Add(emitter); return 1;
    }
    return previousTrail ? previousTrail(name, size, data) : 1;
}
int TempEmit(const char* name, int size, void* data)
{
    SynchronizeSession();
    elitefx::Emitter emitter;
    if (data && size > 0 && size <= 192 && elitefx::DecodeFirewall(
        {static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)}, emitter))
    { Add(emitter); return 1; }
    return previousEmitter ? previousEmitter(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{ ResetEliteVisuals(); return previousInit ? previousInit(name, size, data) : 1; }
int Reset(const char* name, int size, void* data)
{ ResetEliteVisuals(); return previousReset ? previousReset(name, size, data) : 1; }
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    InstallChainedUserMessage(name, hook, previous);
}
bool Origin(const Emission& emission, const cl_entity_t& owner, std::array<float, 3>& position)
{
    const auto& parameters = emission.parameters;
    const float* base = owner.origin;
    if (!parameters.fireball && owner.model->type == mod_studio && studio.Mod_Extradata)
    {
        const auto* header = static_cast<const studiohdr_t*>(studio.Mod_Extradata(owner.model));
        // Original0x800 follows attachment0. Stock studio leaves attachments
        // untouched for null.mdl (zero attachments); use its entity origin.
        if (header && header->numattachments > 0) base = owner.attachment[0];
    }
    for (unsigned axis = 0; axis < position.size(); ++axis)
    {
        position[axis] = base[axis] + parameters.offset[axis];
        if (!std::isfinite(position[axis])) return false;
    }
    return true;
}
bool Emit(const Emission& emission, const std::array<float, 3>& position, float now)
{
    const auto slot = std::find_if(particles.begin(), particles.end(), [](const Particle& p) { return !p.active; });
    if (slot == particles.end()) { ++exhausted; return false; }
    Particle particle;
    particle.parameters = emission.parameters; particle.origin = position;
    particle.sprite = emission.parameters.sprite;
    if (particle.parameters.fireball)
    {
        //02AA6ACE: size, sprite, Z/Y/X offsets, then the three velocities.
        particle.parameters.size += engine.pfnRandomFloat(0, 3);
        particle.sprite = engine.pfnRandomLong(0, 2);
        particle.origin[2] += engine.pfnRandomFloat(5, 10);
        particle.origin[1] += engine.pfnRandomFloat(-10, 10);
        particle.origin[0] += engine.pfnRandomFloat(-10, 10);
    }
    for (unsigned axis = 0; axis < particle.velocity.size(); ++axis)
        particle.velocity[axis] = particle.parameters.velocity[axis] +
            engine.pfnRandomFloat(-1, 1) * particle.parameters.spread;
    // Original mass sample is still consumed for the noncolliding particles.
    engine.pfnRandomFloat(1, 1.5f);
    particle.born = now; particle.die = now + particle.parameters.life;
    //01DE3490 divides by die-birth, including float addition rounding.
    particle.parameters.life = particle.die - now;
    particle.size = particle.parameters.size;
    particle.alpha = particle.parameters.alpha; particle.active = true;
    *slot = particle; ++emitted;
    return true;
}
}

void InitializeEliteVisuals()
{
    Install("ZombieBoss", Trail, previousTrail);
    Install("TempEmit", TempEmit, previousEmitter);
    Install("InitHUD", Init, previousInit);
    Install("ResetHUD", Reset, previousReset);
}
void ResetEliteVisuals()
{
    emitters = {}; particles = {}; sprites = {}; lastTime = 0; haveTime = false;
}
void ShutdownEliteVisuals() { ResetEliteVisuals(); }

void EliteVisualsFrame()
{
    if (!engine.GetClientTime || !engine.GetEntityByIndex || !engine.GetLocalPlayer ||
        !engine.pfnRandomFloat || !engine.pfnRandomLong) return;
    const float now = static_cast<float>(engine.GetClientTime());
    if (!std::isfinite(now)) { ResetEliteVisuals(); return; }
    // Preserve the last integrated time until the corrected clock catches up;
    // otherwise particles can acquire negative frames or move twice over the
    // same interval. InitHUD/ResetHUD and session changes own clearing.
    if (haveTime && now <= lastTime) return;
    const float delta = haveTime ? now - lastTime : 0;
    lastTime = now; haveTime = true;
    const auto* local = engine.GetLocalPlayer();
    if (!local) { ResetEliteVisuals(); return; }
    for (auto& particle : particles)
    {
        if (!particle.active) continue;
        if (now > particle.die) { particle.active = false; continue; }
        const float elapsed = now - particle.born;
        particle.size = elitefx::Size(particle.parameters, elapsed);
        particle.alpha = elitefx::Alpha(particle.parameters, elapsed);
        if (!std::isfinite(particle.size) || !std::isfinite(particle.alpha))
        { particle.active = false; continue; }
        if (particle.alpha < 1 || particle.size < .0001f) particle.die = now;
        for (unsigned axis = 0; axis < particle.origin.size(); ++axis)
            particle.origin[axis] += particle.velocity[axis] * delta;
        const auto* sprite = sprites[particle.sprite];
        if (!sprite) { particle.active = false; continue; }
        particle.frame = static_cast<int>(elapsed * 15) % sprite->numframes;
    }
    for (auto& emission : emitters)
    {
        if (!emission.active || emission.next == 0 || now <= emission.next) continue;
        // Exactly one emission per frame, no catch-up loop after a stall.
        emission.next = now + emission.parameters.interval;
        const auto* owner = engine.GetEntityByIndex(emission.parameters.owner);
        if (!owner || !owner->model || owner->model != emission.ownerModel ||
            owner->curstate.modelindex != emission.ownerModelIndex ||
            owner->curstate.messagenum != local->curstate.messagenum)
        { emission.active = false; ++retired; continue; }
        std::array<float, 3> position;
        if (!Origin(emission, *owner, position)) { emission.active = false; ++retired; continue; }
        if (!Emit(emission, position, now)) break; // original pool exhaustion ends this pass.
    }
}

void DrawEliteVisuals()
{
    auto* tri = engine.pTriAPI;
    if (!tri || !engine.pfnAngleVectors || !tri->SpriteTexture || !tri->RenderMode ||
        !tri->CullFace || !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f) return;
    // Original particle flags0x1c contain neither0x80 nor0x100 billboard.
    // Preserve its fixed0,0,1 plane instead of letting sprite type2 face the camera.
    float angles[]{0, 0, 1}, forward[3], right[3], up[3];
    engine.pfnAngleVectors(angles, forward, right, up);
    tri->RenderMode(kRenderTransAdd); tri->CullFace(TRI_NONE);
    constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    for (const auto& particle : particles)
    {
        if (!particle.active || !sprites[particle.sprite] ||
            !tri->SpriteTexture(sprites[particle.sprite], particle.frame)) continue;
        float corners[4][3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float horizontal = right[axis] * particle.size, vertical = up[axis] * particle.size;
            const float bottom = (particle.origin[axis] - horizontal * .5f) - vertical * .5f;
            corners[0][axis] = bottom + vertical; corners[1][axis] = bottom;
            corners[2][axis] = bottom + horizontal; corners[3][axis] = (bottom + horizontal) + vertical;
        }
        tri->Begin(TRI_QUADS); tri->Color4f(1, 1, 1, particle.alpha / 255.0f);
        for (unsigned corner = 0; corner < 4; ++corner)
        { tri->TexCoord2f(uv[corner][0], uv[corner][1]); tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]); }
        tri->End(); ++draws;
    }
    tri->Color4f(1, 1, 1, 1); tri->RenderMode(kRenderNormal); tri->CullFace(TRI_FRONT);
}
void EliteVisualsDiagnostics(std::ostream& out)
{
    out << "eliteVisuals received=" << received << " emitted=" << emitted << " exhausted=" << exhausted
        << " retired=" << retired << " draws=" << draws << " emitters="
        << std::count_if(emitters.begin(), emitters.end(), [](const Emission& e) { return e.active; })
        << " particles=" << std::count_if(particles.begin(), particles.end(), [](const Particle& p) { return p.active; }) << '\n';
}
}
