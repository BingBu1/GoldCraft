// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "challenge_environment.hpp"
#include "../../../scenario/custom/fog_owner.hpp"
#include <triangleapi.h>
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
int waterlevel{};
triangleapi_t* triangle{};
decltype(triangleapi_t::FogParams) previousParameters{};
decltype(triangleapi_t::Fog) previousFog{};
pfnUserMsgHook previousMessage{}, previousInit{};
std::uint64_t appliedCalls{}, accepted{}, rejected{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }

void Apply()
{
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
    if (triangle) return;
    auto* current = engine.pTriAPI;
    if (!current || !current->FogParams || !current->Fog) return;
    // Renderer already wraps this shared standard table. Install once; another
    // plugin may wrap us later, so never rewrap that chain and create recursion.
    previousParameters = current->FogParams; previousFog = current->Fog;
    triangle = current; triangle->FogParams = Parameters; triangle->Fog = Draw;
}
void ChallengeEnvironmentView(int level)
{
    if (!Owner() || !enabled) return;
    waterlevel = level; viewKnown = level >= 0 && level <= 3;
}
void DrawChallengeEnvironment() { Apply(); }
void ResetChallengeEnvironment()
{
    if (!Owner()) return;
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
}
void ChallengeEnvironmentDiagnostics(std::ostream& out)
{
    out << "challengeEnvironment baseline=" << fog.Known() << " view=" << viewKnown
        << " water=" << waterlevel << " accepted=" << accepted << " rejected=" << rejected
        << " apiUpdates=" << appliedCalls;
    if (const auto* value = environment.View())
        out << " owner=" << value->owner << " revision=" << value->revision
            << " active=" << value->view.active << " level=" << int(value->view.level)
            << " fog=" << value->view.fog << " density=" << value->profile.density
            << " sky=" << value->profile.sky;
    out << '\n';
}
}
