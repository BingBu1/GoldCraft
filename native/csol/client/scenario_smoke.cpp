// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_smoke.hpp"
#include "../scenario_smoke.hpp"

#include <event_api.h>
#include <event_args.h>
#include <pmtrace.h>
#include <triangleapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::client
{
namespace
{
// The source particle manager uses 1200 entries in its ordinary path. Scenario
// smoke has four server quotas; storage stays stable while delayed fill emits.
constexpr std::size_t Capacity = 1200;
struct Slot
{
    smoke::Particle particle;
    std::uint64_t generation{};
    float nextPvsCheck{};
    bool active{}, inPvs{true};
};
std::array<Slot, Capacity> particles;
model_t* sprite{};
float previousTime{};
bool haveTime{};
std::uint64_t events{}, emitted{}, exhausted{}, invalidEvents{}, traceCalls{}, draws{};

class EngineServices final : public smoke::Services
{
public:
    float RandomFloat(float low, float high) override { return engine.pfnRandomFloat(low, high); }
    int RandomInt(int low, int high) override { return engine.pfnRandomLong(low, high); }
    void AngleVectors(float yaw, smoke::Vec3& forward, smoke::Vec3& up) override
    {
        float angles[]{0, yaw, 0}, right[3]{};
        engine.pfnAngleVectors(angles, forward.data(), right, up.data());
    }
    smoke::Trace TraceWorld(const smoke::Vec3& from, const smoke::Vec3& to) override
    {
        float start[]{from[0], from[1], from[2]}, end[]{to[0], to[1], to[2]};
        pmtrace_t trace{};
        engine.pEventAPI->EV_SetTraceHull(2);
        engine.pEventAPI->EV_PlayerTrace(start, end, 0xA, -1, &trace);
        ++traceCalls;
        return {trace.fraction, {trace.endpos[0], trace.endpos[1], trace.endpos[2]},
                {trace.plane.normal[0], trace.plane.normal[1], trace.plane.normal[2]}};
    }
    void Emit(const smoke::Particle& particle) override
    {
        const auto free = std::find_if(particles.begin(), particles.end(), [](const Slot& slot) { return !slot.active; });
        if (free == particles.end()) { ++exhausted; return; }
        free->particle = particle;
        free->generation = emitted + 1;
        free->nextPvsCheck = particle.birth;
        free->inPvs = true;
        free->active = true;
        ++emitted;
    }
} services;

bool Ready()
{
    const auto* event = engine.pEventAPI;
    return engine.GetClientTime && engine.pfnRandomFloat && engine.pfnRandomLong &&
        engine.pfnAngleVectors && event && event->EV_PlayerTrace && event->EV_SetTraceHull &&
        event->EV_SetUpPlayerPrediction && event->EV_PushPMStates && event->EV_PopPMStates;
}

void CreateSmoke(event_args_t* args)
{
    SynchronizeSession();
    if (!args || args->iparam2 != 1 || !Ready()) { ++invalidEvents; return; }
    const double time = engine.GetClientTime();
    if (!std::isfinite(time) || !std::all_of(std::begin(args->origin), std::end(args->origin),
        [](float value) { return std::isfinite(value); })) { ++invalidEvents; return; }
    if (haveTime && static_cast<float>(time) < previousTime) ResetScenarioSmoke();
    if (!sprite && engine.CL_LoadModel)
    {
        int index{};
        sprite = engine.CL_LoadModel(smoke::Sprite, &index);
        if (sprite && (sprite->type != mod_sprite || sprite->numframes < 2)) sprite = nullptr;
    }
    if (!sprite) { ++invalidEvents; return; }
    if (!haveTime) { previousTime = static_cast<float>(time); haveTime = true; }
    smoke::CreateCloud({args->origin[0], args->origin[1], args->origin[2]}, time, services);
    ++events;
}
}

void InitializeScenarioSmoke()
{
    if (engine.pfnHookEvent) engine.pfnHookEvent("events/createsmoke.sc", CreateSmoke);
}

void ResetScenarioSmoke()
{
    for (auto& slot : particles) slot.active = false;
    sprite = nullptr;
    haveTime = false;
    previousTime = 0;
}

void ShutdownScenarioSmoke() { ResetScenarioSmoke(); }

void DrawScenarioSmoke()
{
    auto* tri = engine.pTriAPI;
    if (!tri || !Ready()) return;
    const double clientTime = engine.GetClientTime();
    if (!std::isfinite(clientTime)) return;
    const float now = static_cast<float>(clientTime);
    if (haveTime && now < previousTime) ResetScenarioSmoke();
    if (!haveTime) { previousTime = now; haveTime = true; }

    struct Draw { std::size_t index; float distance; std::uint64_t generation; bool visible; };
    std::array<Draw, Capacity> ordered;
    std::size_t count{};
    const auto* player = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    const float* origin = player ? player->origin : view.vieworg;
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        auto& slot = particles[index];
        if (!slot.active) continue;
        if (now >= slot.nextPvsCheck && tri->BoxInPVS)
        {
            const float extent = slot.particle.size / 5.0f;
            float mins[3], maxs[3];
            for (int axis = 0; axis < 3; ++axis)
            {
                mins[axis] = slot.particle.origin[axis] - extent;
                maxs[axis] = slot.particle.origin[axis] + extent;
            }
            slot.inPvs = tri->BoxInPVS(mins, maxs) != 0;
            slot.nextPvsCheck = static_cast<float>(clientTime + static_cast<double>(0.1f));
        }
        float distance{};
        for (int axis = 0; axis < 3; ++axis)
        {
            const float delta = origin[axis] - particles[index].particle.origin[axis];
            distance += delta * delta;
        }
        ordered[count++] = {index, distance, slot.generation, slot.inPvs};
    }
    if (count && now != previousTime)
    {
        auto* event = engine.pEventAPI;
        event->EV_SetUpPlayerPrediction(0, 1);
        event->EV_PushPMStates();
        std::sort(ordered.begin(), ordered.begin() + count,
            [](const Draw& left, const Draw& right) { return left.generation > right.generation; });
        // Snapshot indices before Tick: its delayed particles start on the
        // next manager pass, matching the original head-inserted list.
        for (std::size_t i = 0; i < count; ++i)
        {
            auto& slot = particles[ordered[i].index];
            if (slot.active && slot.generation == ordered[i].generation)
                slot.active = smoke::Tick(slot.particle, clientTime, previousTime, services);
        }
        event->EV_PopPMStates();
    }
    previousTime = now;
    if (!sprite || !count) return;

    std::sort(ordered.begin(), ordered.begin() + count,
        [](const Draw& left, const Draw& right) { return left.distance > right.distance; });
    tri->RenderMode(kRenderTransAlpha);
    tri->CullFace(TRI_NONE);
    constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& slot = particles[ordered[i].index];
        if (!slot.active || slot.generation != ordered[i].generation || !ordered[i].visible) continue;
        const auto& particle = slot.particle;
        if (!tri->SpriteTexture(sprite, particle.frame)) continue;
        float angles[]{view.viewangles[0], view.viewangles[1], particle.angles[2]};
        float forward[3], right[3], up[3], bottomLeft[3], bottomRight[3], topLeft[3], topRight[3];
        engine.pfnAngleVectors(angles, forward, right, up);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float horizontal = right[axis] * particle.size;
            const float vertical = up[axis] * particle.size;
            bottomLeft[axis] = (particle.origin[axis] - horizontal * 0.5f) - vertical * 0.5f;
            bottomRight[axis] = bottomLeft[axis] + horizontal;
            topLeft[axis] = bottomLeft[axis] + vertical;
            topRight[axis] = bottomRight[axis] + vertical;
        }
        const float* corners[]{topLeft, bottomLeft, bottomRight, topRight};
        tri->Begin(TRI_QUADS);
        tri->Color4f(smoke::Color, smoke::Color, smoke::Color, particle.brightness / 255.0f);
        for (int corner = 0; corner < 4; ++corner)
        {
            tri->TexCoord2f(uv[corner][0], uv[corner][1]);
            tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]);
        }
        tri->End();
        ++draws;
    }
    tri->Color4f(1, 1, 1, 1);
    tri->RenderMode(kRenderNormal);
    tri->CullFace(TRI_FRONT);
}

void ScenarioSmokeDiagnostics(std::ostream& out)
{
    const auto active = std::count_if(particles.begin(), particles.end(), [](const Slot& slot) { return slot.active; });
    out << "scenarioSmoke events=" << events << " emitted=" << emitted << " active=" << active
        << " exhausted=" << exhausted << " invalid=" << invalidEvents << " traces=" << traceCalls
        << " draws=" << draws << '\n';
}
}
