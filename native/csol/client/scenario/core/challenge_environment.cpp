// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "challenge_environment.hpp"
#include "../../../scenario/custom/fog_owner.hpp"
#include "../../../scenario/custom/sky_lighting.hpp"
#include <triangleapi.h>
#include <pm_movevars.h>
#include <ostream>

namespace csol::client
{
namespace
{
namespace custom = csol::scenario::custom;
DWORD ownerThread{};
custom::EnvironmentPresentation environment;
custom::FogOwner fog;
custom::FogPair output;
bool outputParameters{}, outputDraw{}, viewKnown{}, applying{}, enabled{};
bool borrowingSky{};
int waterlevel{};
movevars_t* movement{};
custom::PreparedSkyLight preparedSky;
custom::NumericCvarOwner skyFogOwner;
cvar_t* skyFog{};
engine_studio_api_t* studioTable{};
decltype(engine_studio_api_t::StudioDynamicLight) previousStudioLight{};
std::uint64_t skyEpoch{}, scopedSkyCalls{}, skyConflicts{};
triangleapi_t* triangle{};
decltype(triangleapi_t::FogParams) previousParameters{};
decltype(triangleapi_t::Fog) previousFog{};
pfnUserMsgHook previousMessage{}, previousInit{};
std::uint64_t appliedCalls{}, accepted{}, rejected{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }

custom::SkyColor Sky(movevars_t& value) noexcept
{ return {value.skycolor_r, value.skycolor_g, value.skycolor_b}; }
void SkyFog()
{
    if (!skyFog) return;
    const auto* value = environment.View();
    if (enabled && viewKnown && value && value->view.active && value->view.fog)
        skyFogOwner.Force(skyFog->value, skyFog->string, value->profile.sky ? 1.0f : 0.0f);
    else skyFogOwner.Release(skyFog->value, skyFog->string);
}
void StudioLight(cl_entity_t* entity, alight_t* light)
{
    if (!previousStudioLight) return;
    if (!Owner()) { previousStudioLight(entity, light); return; }
    const auto* value = environment.View();
    if (!enabled || borrowingSky || !movement || !viewKnown || !value ||
        !value->view.active || !preparedSky.Output())
    { previousStudioLight(entity, light); return; }
    const auto source = Sky(*movement);
    if (source != preparedSky.Source())
    { ++skyConflicts; previousStudioLight(entity, light); return; }
    const auto target = *preparedSky.Output();
    auto* vars = movement;
    const auto epoch = skyEpoch;
    vars->skycolor_r = target[0]; vars->skycolor_g = target[1]; vars->skycolor_b = target[2];
    borrowingSky = true; ++scopedSkyCalls;
    struct RestoreSky
    {
        movevars_t* vars;
        custom::SkyColor source, target;
        std::uint64_t epoch;
        ~RestoreSky()
        {
            if (skyEpoch == epoch)
            {
                if (vars->skycolor_r == target[0]) vars->skycolor_r = source[0];
                if (vars->skycolor_g == target[1]) vars->skycolor_g = source[1];
                if (vars->skycolor_b == target[2]) vars->skycolor_b = source[2];
            }
            borrowingSky = false;
        }
    } restore{vars, source, target, epoch};
    // Preserve the native sky trace, map samples, fullbright and subsequent
    // dynamic/entity lights. Only the original sky-color input is adapted.
    previousStudioLight(entity, light);
}

void Apply()
{
    if (Owner()) SkyFog();
    if (!Owner() || !enabled || applying || !previousParameters || !previousFog) return;
    const auto value = fog.Desired(environment.View(), waterlevel, viewKnown);
    if (!value) return;
    applying = true;
    if (!outputParameters || output.parameters != value->parameters)
    {
        previousParameters(value->parameters.density, value->parameters.sky);
        output.parameters = value->parameters; outputParameters = true; ++appliedCalls;
    }
    if (!outputDraw || output.draw != value->draw)
    {
        auto color = value->draw.color;
        previousFog(color.data(), value->draw.start, value->draw.end, value->draw.on);
        output.draw = value->draw; outputDraw = true; ++appliedCalls;
    }
    applying = false;
}
void Parameters(float density, qboolean sky)
{
    if (!Owner() || !enabled || applying)
    { if (previousParameters) previousParameters(density, sky); return; }
    fog.Observe(custom::FogParameters{density, sky});
    const auto desired = fog.Desired(environment.View(), waterlevel, viewKnown);
    const auto value = desired ? desired->parameters : custom::FogParameters{density, sky};
    if (previousParameters) previousParameters(value.density, value.sky);
    output.parameters = value; outputParameters = true;
}
void Draw(float* color, float start, float end, int on)
{
    if (!Owner() || !enabled || applying || !color)
    { if (previousFog) previousFog(color, start, end, on); return; }
    fog.Observe(custom::FogDraw{{color[0], color[1], color[2]}, start, end, on});
    const auto desired = fog.Desired(environment.View(), waterlevel, viewKnown);
    const auto value = desired ? desired->draw : fog.Baseline().draw;
    auto rgb = value.color;
    if (previousFog) previousFog(rgb.data(), value.start, value.end, value.on);
    output.draw = value; outputDraw = true;
}
int Receive(const char* name, int size, void* bytes)
{
    if (Owner() && enabled && bytes && size == int(custom::EnvironmentMessageSize))
    {
        const auto packet = std::span(static_cast<const std::byte*>(bytes), std::size_t(size));
        if (custom::DecodeEnvironment(packet))
        {
            SynchronizeSession();
            if (environment.Receive(packet)) { ++accepted; Apply(); }
            else ++rejected;
        }
        else ++rejected;
    }
    return previousMessage ? previousMessage(name, size, bytes) : 1;
}
int Init(const char* name, int size, void* bytes)
{
    ResetChallengeEnvironment();
    return previousInit ? previousInit(name, size, bytes) : 1;
}
}
void InitializeChallengeEnvironment()
{
    if (ownerThread && !Owner()) return;
    ownerThread = GetCurrentThreadId();
    enabled = true;
    InstallChainedUserMessage(custom::EnvironmentMessage, Receive, previousMessage);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    if (!skyFog && engine.pfnGetCvarPointer) skyFog = engine.pfnGetCvarPointer("r_wsurf_sky_fog");
    if (triangle) return;
    auto* current = engine.pTriAPI;
    if (!current || !current->FogParams || !current->Fog) return;
    // Renderer already wraps this shared standard table. Install once; another
    // plugin may wrap us later, so never rewrap that chain and create recursion.
    previousParameters = current->FogParams; previousFog = current->Fog;
    triangle = current; triangle->FogParams = Parameters; triangle->Fog = Draw;
}
void ChallengeEnvironmentView(int level, movevars_t* vars)
{
    if (!Owner() || !enabled) return;
    waterlevel = level; viewKnown = level >= 0 && level <= 3;
    movement = vars;
    const auto* value = environment.View();
    if (movement && value && value->view.active)
        preparedSky.Prepare(Sky(*movement), value->view.level);
    else preparedSky.Reset();
    SkyFog();
}
void InstallChallengeEnvironmentStudio(engine_studio_api_t* functions)
{
    if (!functions || !functions->StudioDynamicLight) return;
    if (ownerThread && !Owner()) return;
    if (!ownerThread) ownerThread = GetCurrentThreadId();
    if (functions->StudioDynamicLight != StudioLight)
    { previousStudioLight = functions->StudioDynamicLight; functions->StudioDynamicLight = StudioLight; }
    studioTable = functions;
}
void DrawChallengeEnvironment() { Apply(); }
void ResetChallengeEnvironment()
{
    if (!Owner()) return;
    if (skyFog) skyFogOwner.Release(skyFog->value, skyFog->string);
    ++skyEpoch; movement = nullptr; preparedSky.Reset();
    // Map/connection boundary: the host resets its fog. Never send an old
    // baseline into the next scene.
    environment.Reset(); fog.Reset(); output = {};
    outputParameters = outputDraw = viewKnown = false;
}
void ShutdownChallengeEnvironment()
{
    if (!Owner()) return;
    environment.Reset(); Apply();
    enabled = false;
    if (triangle)
    {
        if (triangle->FogParams == Parameters) triangle->FogParams = previousParameters;
        if (triangle->Fog == Draw) triangle->Fog = previousFog;
        // Preserve a still-wrapped chain's downstream targets until process
        // exit; its opaque outer wrapper may continue calling our callbacks.
        if (triangle->FogParams == previousParameters && triangle->Fog == previousFog) triangle = nullptr;
    }
    ResetChallengeEnvironment();
    if (studioTable && studioTable->StudioDynamicLight == StudioLight)
        studioTable->StudioDynamicLight = previousStudioLight;
}
void ChallengeEnvironmentDiagnostics(std::ostream& out)
{
    out << "challengeEnvironment baseline=" << fog.Known() << " view=" << viewKnown
        << " water=" << waterlevel << " accepted=" << accepted << " rejected=" << rejected
        << " apiUpdates=" << appliedCalls;
    out << " skyScopes=" << scopedSkyCalls << " skyConflicts=" << skyConflicts
        << " rendererSkyFog=" << (skyFog ? skyFog->value : -1.0f);
    if (const auto* value = environment.View())
        out << " owner=" << value->owner << " revision=" << value->revision
            << " active=" << value->view.active << " level=" << int(value->view.level)
            << " fog=" << value->view.fog << " density=" << value->profile.density
            << " sky=" << value->profile.sky;
    out << '\n';
}
}
