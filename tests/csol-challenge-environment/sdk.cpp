// SPDX-License-Identifier: GPL-3.0-or-later
#define CSOL_PHASE_SDK_LIBRARY
#include "../csol-challenge-phase/sdk.cpp"
#include "../../native/csol/server/custom/challenge_environment.h"
#include "goldcraft/lightstyle_api.hpp"
#include <cstdlib>

namespace environment_sdk
{
using namespace phase_sdk;
using MapFn = void (*)(const char*);
using StatusFn = bool (*)(goldcraft::MapLightStyleStatus*);
MapFn map{}, external{};
StatusFn status{};
void Provider()
{
    const auto* path = std::getenv("GOLDCRAFT_TEST_LIGHT_PROVIDER");
    Require(path && *path, "explicit controlled engine provider path supplied");
    const auto module = LoadLibraryA(path);
    Require(module != nullptr, "controlled x86 engine provider loads");
    map = reinterpret_cast<MapFn>(GetProcAddress(module, "EnvironmentTestMap"));
    external = reinterpret_cast<MapFn>(GetProcAddress(module, "EnvironmentTestExternal"));
    status = reinterpret_cast<StatusFn>(GetProcAddress(module, "EnvironmentTestState"));
    Require(map && external && status, "exact test boundary control exports exist");
    map("m");
    // The provider remains loaded until this test process exits: actual runtime
    // owners can still finalize after the local gameplay fixture unwinds.
}
goldcraft::MapLightStyleStatus State()
{
    goldcraft::MapLightStyleStatus value;
    Require(status && status(&value), "actual engine lightstyle state is available");
    return value;
}
custom::EnvironmentSnapshot Last(Case& c)
{
    const auto message = c.f.engine.messages.at(custom::EnvironmentMessage);
    const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(),
        [&](const auto& p) { return p.id == message; });
    Require(packet != c.packets.rend(), "actual environment transport exists");
    const auto value = custom::DecodeEnvironment(packet->bytes);
    Require(bool(value), "actual writer emits a valid fixed32-byte payload");
    return *value;
}
unsigned Count(Case& c)
{
    const auto id = c.f.engine.messages.at(custom::EnvironmentMessage);
    return unsigned(std::count_if(c.packets.begin(), c.packets.end(), [&](const auto& p) { return p.id == id; }));
}
void Start(Case& c)
{
    NativeActor(c); Restart(c); Tick(c, 120); Tick(c, 122); Tick(c, 125);
}
void Dark(Case& c)
{
    for (const float now : {128.f, 131.f, 134.f, 137.f, 140.f, 143.f, 146.f}) Tick(c, now);
    Expect(State().applied && State().level == 'b' && Last(c).view.fog,
        "actual rules reach requested darker target with original fog authority");
}
void Flow(bool join, bool replay)
{
    Settings(true); Case c(MusicResources); Start(c);
    const auto owner = Last(c).owner;
    Expect(State().applied && State().level == 'i' && !Last(c).view.fog,
        "actual dusk/night entry owns the light but preserves map fog until dark");
    const auto first = Count(c);
    Tick(c, 127.5f);
    Expect(Count(c) == first && State().level == 'i', "real Think respects strict2.5s equality without network traffic");
    Dark(c);
    const auto token = State().owner;
    external("klm");
    Expect(State().applied && State().level == 'b' && std::string_view(State().baseline) == "klm",
        "external writes update the actual engine baseline while retaining night");
    const auto count = Count(c);
    for (unsigned i = 0; i != 50; ++i) Tick(c, 146.001f + float(i) * .001f);
    Expect(Count(c) == count, "50 unchanged actual Think frames never flood environment messages");
    Restart(c);
    Expect(State().owner == token && Last(c).owner == owner && State().level == 'b',
        "ordinary intermediate-stage restart keeps current light lease and environment owner");
    if (join)
    {
        c.f.engine.files["env-motd.txt"] = "Environment join boundary\n";
        CVAR_SET_STRING("motdfile", "env-motd.txt");
        const auto before = Count(c);
        g_pGameRules->InitHUD(&c.player);
        const auto id = c.f.engine.messages.at(custom::EnvironmentMessage);
        const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(), [&](const auto& p) { return p.id == id; });
        Expect(Count(c) == before + 1 && Last(c).owner == owner && Last(c).view.level == 'b' && Last(c).view.fog &&
            packet->destination == MSG_ONE && packet->receiver == c.player.entindex(),
            "actual InitHUD replays the final night profile only to the late joining player");
    }
    Tick(c, 170);
    c.player.pev->health = 0; c.player.pev->deadflag = DEAD_DEAD;
    Tick(c, 171);
    Expect(!State().applied && !State().owner && std::string_view(State().baseline) == "klm" && !Last(c).view.active,
        "actual all-dead failure restores latest map light and retires client fog");
    if (!replay) return;
    auto* service = static_cast<csol::IScenarioReplayService*>(CreateInterface(csol::ScenarioReplayServiceVersion, nullptr));
    Require(service && service->RequestReplay(98), "actual administration accepts one replay");
    Expect(Last(c).owner == owner && !Last(c).view.active, "queued replay leaves the old result intact");
    c.player.m_iMenu = Menu_ChooseAppearance;
    Tick(c, 172);
    Expect(Last(c).owner > owner && !Last(c).view.active && !State().applied,
        "committed replay creates a new inactive noon environment without erasing map baseline");
}
void Cancel(bool nextMap)
{
    Settings(true); Case c(MusicResources); NativeActor(c); Restart(c); Tick(c, 120);
    external("klm");
    const auto id = c.f.engine.messages.at(custom::EnvironmentMessage);
    c.callback = [&](std::string_view edge)
    {
        if (edge == "message" && c.packets.back().id == id)
        { if (nextMap) map("o"); c.f.runtime->RequestClose(); }
    };
    c.f.engine.globals.time = 122;
    const auto consumed = CSOL_ScenarioThink(*static_cast<CHalfLifeMultiplay*>(g_pGameRules));
    Expect(consumed && c.f.runtime->Closing(), "scenario wrapper remains handled while real environment callback retires its runtime");
    const auto environment = c.f.runtime->environment;
    Require(environment != nullptr, "real runtime owns environment service");
    Expect(!environment->Replay(c.f.runtime, c.player), "revoked runtime cannot publish a late effect");
    environment->Restore();
    Expect(!State().applied && std::string_view(State().baseline) == (nextMap ? "o" : "klm"),
        nextMap ? "old map finalization cannot restore its pattern over next map" : "safe retired-map cleanup restores exact external pattern");
}
void Missing()
{
    Settings(false); Case c(MusicResources);
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Finish(snapshot, error), "actual default profile exists");
    CCSOLChallengeEnvironment environment;
    Expect(!environment.Precache(*snapshot) && !c.f.engine.messages.contains(custom::EnvironmentMessage),
        "missing actual engine service does not fabricate successful environment capability");
}
}
int main(int argc, char** argv)
{
    try
    {
        sdk_test::Require(argc == 2, "one environment SDK group");
        const std::string_view group = argv[1];
        if (group != "environment-missing") environment_sdk::Provider();
        if (group == "environment-flow" || group == "environment-join" || group == "environment-replay")
            environment_sdk::Flow(group == "environment-join", group == "environment-replay");
        else if (group == "environment-cancel" || group == "environment-map") environment_sdk::Cancel(group == "environment-map");
        else if (group == "environment-missing") environment_sdk::Missing();
        else phase_sdk::Run(group);
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
