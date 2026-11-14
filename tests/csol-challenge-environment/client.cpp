// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/challenge_environment.cpp"
#include "../csol-hud-style/allocation_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace test
{
namespace custom = csol::scenario::custom;
unsigned checks{}, parameterCalls{}, drawCalls{};
custom::FogPair seen;
pfnUserMsgHook envMessage{}, initMessage{};
bool resetSession{};
decltype(triangleapi_t::FogParams) wrappedParameters{};
decltype(triangleapi_t::Fog) wrappedFog{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
void Parameters(float density, qboolean sky) { ++parameterCalls; seen.parameters = {density, sky}; }
void Draw(float* color, float start, float end, int on) { ++drawCalls; seen.draw = {{color[0], color[1], color[2]}, start, end, on}; }
void OuterParameters(float density, qboolean sky) { wrappedParameters(density, sky); }
void OuterDraw(float* color, float start, float end, int on) { wrappedFog(color, start, end, on); }
pfnUserMsgHook Hook(const char* name, pfnUserMsgHook callback)
{
    auto& value = std::strcmp(name, custom::EnvironmentMessage) ? initMessage : envMessage;
    auto old = value; value = callback; return old;
}
void Send(custom::EnvironmentSnapshot snapshot)
{
    const auto packet = custom::EncodeEnvironment(snapshot);
    Check(bool(packet) && envMessage, "real registered environment message exists");
    envMessage(custom::EnvironmentMessage, int(packet->size()), const_cast<std::byte*>(packet->data()));
}
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
void SynchronizeSession()
{ if (test::resetSession) { ResetChallengeEnvironment(); test::resetSession = false; } }
}
int main()
{
    using test::Check;
    namespace custom = csol::scenario::custom;
    using namespace csol::client;
    triangleapi_t table{}; table.FogParams = test::Parameters; table.Fog = test::Draw;
    metahook_api_t meta{}; meta.HookUserMsg = test::Hook;
    meta.FindUserMsgHook = [](const char*) -> usermsg_t* { return nullptr; };
    api = &meta; engine.pTriAPI = &table;
    engine.pfnHookUserMsg = [](const char* name, pfnUserMsgHook callback) { test::Hook(name, callback); return 1; };
    InitializeChallengeEnvironment(); InitializeChallengeEnvironment();
    Check(table.Fog != test::Draw && table.FogParams != test::Parameters && test::envMessage, "actual initialization installs standard hooks once");
    std::array<float, 3> color{40, 50, 60};
    table.FogParams(.002f, 0); table.Fog(color.data(), 20, 800, 1);
    const auto baseline = test::seen;
    Check(baseline.parameters.density == .002f && baseline.draw.color == color, "ordinary fog passes unchanged");
    custom::EnvironmentSnapshot snapshot{{true, true, 'c'}, {}, 1, 1};
    test::Send(snapshot);
    Check(test::seen == baseline, "unknown view cannot trigger fog override");
    auto bad = *custom::EncodeEnvironment(snapshot); bad[0] = std::byte{2};
    test::envMessage(custom::EnvironmentMessage, int(bad.size()), bad.data());
    Check(test::seen == baseline, "malformed update cannot change real API state");
    ChallengeEnvironmentView(0); DrawChallengeEnvironment();
    Check(test::seen.parameters.sky == 1 && std::bit_cast<std::uint32_t>(test::seen.parameters.density) == 0x3a378034 &&
        test::seen.draw.color == std::array<float, 3>{1, 1, 1} && test::seen.draw.start == 100 && test::seen.draw.end == 2000,
        "actual consumer applies exact original profile through downstream APIs");
    ChallengeEnvironmentView(2); DrawChallengeEnvironment();
    Check(test::seen.draw.on == 1, "waterlevel2 uses original CSOL rule");
    ChallengeEnvironmentView(3); DrawChallengeEnvironment();
    Check(test::seen.draw.on == 0, "submerged view disables original night fog");
    ChallengeEnvironmentView(0);
    color = {80, 90, 100};
    table.FogParams(.001f, 0); table.Fog(color.data(), 30, 1500, 1); DrawChallengeEnvironment();
    Check(test::seen.parameters.sky == 1 && test::seen.draw.color[0] == 1, "host writes remain overridden while current effect owns them");
    const auto calls = test::parameterCalls + test::drawCalls;
    allocation_probe::calls = 0; allocation_probe::active = true;
    for (unsigned i = 0; i != 1000; ++i) { ChallengeEnvironmentView(0); DrawChallengeEnvironment(); }
    allocation_probe::active = false;
    Check(!allocation_probe::calls && test::parameterCalls + test::drawCalls == calls, "1000 unchanged actual frames allocate nothing and issue no repeated API writes");
    snapshot.revision = 2; snapshot.view = {};
    test::Send(snapshot);
    Check(test::seen.parameters.density == .001f && !test::seen.parameters.sky && test::seen.draw.color == color && test::seen.draw.end == 1500,
        "end restores the latest external baseline");
    snapshot.revision = 3; snapshot.view = {true, true, 'c'};
    test::Send(snapshot);
    Check(test::seen.draw.color == color, "same retired owner cannot restart its effect");
    snapshot.owner = 2; test::Send(snapshot);
    Check(test::seen.draw.color[0] == 1, "new authoritative owner can apply fog");
    const auto beforeMap = test::parameterCalls + test::drawCalls;
    test::initMessage("InitHUD", 0, nullptr);
    Check(test::parameterCalls + test::drawCalls == beforeMap, "new-map reset never writes old baseline");
    color = {10, 20, 30}; table.FogParams(.003f, 1); table.Fog(color.data(), 100, 900, 1);
    Check(test::seen.draw.color == color, "new map retains its own native fog");
    snapshot.owner = 1; snapshot.revision = 1; test::Send(snapshot); ChallengeEnvironmentView(0); DrawChallengeEnvironment();
    Check(test::seen.draw.color[0] == 1, "new connection admits its own numbering");
    ShutdownChallengeEnvironment();
    Check(table.Fog == test::Draw && table.FogParams == test::Parameters && test::seen.draw.color == color && test::seen.parameters.density == .003f,
        "shutdown restores new map baseline and original API pointers");
    const auto stopped = test::parameterCalls + test::drawCalls;
    snapshot.owner = 3; snapshot.revision = 4; test::Send(snapshot); DrawChallengeEnvironment();
    Check(test::parameterCalls + test::drawCalls == stopped, "queued messages cannot reactivate a shut-down consumer");
    InitializeChallengeEnvironment();
    test::wrappedParameters = table.FogParams; test::wrappedFog = table.Fog;
    table.FogParams = test::OuterParameters; table.Fog = test::OuterDraw;
    InitializeChallengeEnvironment();
    Check(table.FogParams == test::OuterParameters && table.Fog == test::OuterDraw, "reinitialization never steals a later plugin wrapper");
    table.FogParams(.003f, 1); table.Fog(color.data(), 100, 900, 1);
    snapshot.owner = 1; snapshot.revision = 1; test::Send(snapshot); ChallengeEnvironmentView(0); DrawChallengeEnvironment();
    Check(test::seen.draw.color[0] == 1, "foreign outer chain remains functional without recursive rewrapping");
    ShutdownChallengeEnvironment();
    table.FogParams(.004f, 0); table.Fog(color.data(), 10, 300, 1);
    Check(table.Fog == test::OuterDraw && test::seen.draw.color == color && test::seen.parameters.density == .004f,
        "shutdown preserves foreign wrapper and keeps our remaining callback inert");
    std::printf("environment actual client: %u checks passed\n", test::checks);
}
