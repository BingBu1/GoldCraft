// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/night_player_lights.cpp"
#include "../csol-hud-style/allocation_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

namespace test
{
namespace custom = csol::scenario::custom;
unsigned checks{}, updates{}, clears{}, queries{};
bool failUpdate{}, sessionReset{};
pfnUserMsgHook message{};
std::array<cl_entity_t, 32> entities;
std::array<MetaRendererMovingLight, 32> seen;
unsigned count{};
model_t model{};
model_t heldModel{};
cl_entity_t heldView{};
bool handheldModels{};
unsigned modelQueries{}, flashlightCommands{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
class Scene final : public IMetaRendererScene
{
public:
    void RegisterSceneCallbacks(IMetaRendererSceneCallbacks*) override {}
    void UnregisterSceneCallbacks(IMetaRendererSceneCallbacks*) override {}
    void SetDynamicLightShadowSize(int, unsigned) override {}
} scene;
class Lights final : public IMetaRendererMovingLights
{
public:
    bool PrepareLights(IMetaRendererSceneCallbacks*, uint32_t capacity) noexcept override { return capacity == 32; }
    bool UpdateLights(IMetaRendererSceneCallbacks*, const MetaRendererMovingLight* lights, uint32_t n) noexcept override
    {
        ++updates;
        if (failUpdate) return false;
        std::copy_n(lights, n, seen.begin()); count = n; return true;
    }
    void ClearLights(IMetaRendererSceneCallbacks*) noexcept override { ++clears; count = 0; }
} lights;
void Send(const custom::NightLightingSnapshot& snapshot)
{
    const auto bytes = custom::EncodeNightLighting(snapshot);
    Check(bytes && message, "valid native fixed-size control packet and registered consumer");
    message(custom::NightLightingMessage, int(bytes->size()), const_cast<std::byte*>(bytes->data()));
}
void Entities()
{
    for (unsigned i = 0; i < entities.size(); ++i)
    {
        auto& entity = entities[i]; entity = {};
        entity.index = int(i + 1); entity.player = true; entity.model = &model;
        entity.curstate.messagenum = 17; entity.curstate.solid = SOLID_SLIDEBOX;
        entity.curstate.movetype = MOVETYPE_WALK; entity.curstate.health = 0;
        entity.origin[0] = float(i * 10); entity.origin[2] = 36;
    }
}
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
void SynchronizeSession()
{ if (test::sessionReset) { ResetNightPlayerLights(); test::sessionReset = false; } }
}
int main()
{
    using namespace csol::client;
    using test::Check;
    namespace custom = csol::scenario::custom;
    custom::Draft draft; custom::Error error; custom::SnapshotPtr settings;
    Check(draft.Finish(settings, error), "one-file defaults remain valid");
    const auto config = custom::ReadNightLighting(*settings);
    Check(config && config->enabled && config->radius == 128 && config->brightness == 48 && config->color == 0xffffff,
        "documented custom defaults are read from the common settings snapshot");
    const auto json = custom::Serialize(*settings);
    custom::SnapshotPtr roundtrip;
    Check(custom::Parse(json, roundtrip, error) && custom::ReadNightLighting(*roundtrip) == config,
        "one JSON grouped persistence roundtrips the actual control configuration");
    metahook_api_t meta{};
    meta.HookUserMsg = [](const char* name, pfnUserMsgHook callback) -> pfnUserMsgHook
    {
        Check(!std::strcmp(name, custom::NightLightingMessage), "only own control hook installed");
        const auto old = test::message; test::message = callback; return old;
    };
    meta.FindUserMsgHook = [](const char*) -> usermsg_t* { return nullptr; };
    api = &meta;
    engine.pfnHookUserMsg = [](const char* name, pfnUserMsgHook callback)
    { api->HookUserMsg(name, callback); return 1; };
    engine.GetLocalPlayer = []() -> cl_entity_t* { return &test::entities[0]; };
    engine.GetEntityByIndex = [](int index) -> cl_entity_t*
    { ++test::queries; return index >= 1 && index <= 32 ? &test::entities[unsigned(index - 1)] : nullptr; };
    event_api_t events{};
    events.EV_FindModelIndex = [](const char* path) {
        ++test::modelQueries;
        if (!std::strcmp(path, custom::NightFlashlightView)) return 100;
        Check(!std::strcmp(path, custom::NightFlashlightPlayer), "query only the authored equipment models");
        return test::handheldModels ? 101 : 0;
    };
    engine.pEventAPI = &events;
    engine.GetViewModel = [] { return &test::heldView; };
    engine.pfnServerCmd = [](const char* command) {
        Check(!std::strcmp(command, custom::NightFlashlightSelect), "send the night equipment command");
        ++test::flashlightCommands;
        return 1;
    };
    InitializeNightPlayerLights(); InitializeNightPlayerLights();
    // Renderer lookup/allocation is a controlled boundary here. Its real
    // factory/pool/visibility/shadow dispatch are exercised by Renderer tests.
    nightRenderer = &test::scene; nightLights = &test::lights; nightPrepared = true;
    test::Entities();
    ref_params_t params{}; params.health = 100; params.maxclients = 32;
    params.simorg[0] = 400; params.simorg[1] = 500; params.simorg[2] = 36;
    NightPlayerLightsView(params);
    Check(!test::updates && !test::queries, "ordinary server does no light/entity work");
    custom::NightLightingSnapshot snapshot{*config, custom::Phase::Noon, 1, 1};
    test::Send(snapshot); NightPlayerLightsView(params);
    Check(!test::updates && !test::queries, "noon uses no light/entity work");
    Check(!TrySelectNightFlashlight() && !test::modelQueries, "noon never looks up or selects the equipment");
    snapshot.phase = custom::Phase::Dusk; ++snapshot.revision; test::Send(snapshot); NightPlayerLightsView(params);
    Check(!test::updates, "dusk does not enable night light");
    snapshot.phase = custom::Phase::Night; ++snapshot.revision; test::Send(snapshot); NightPlayerLightsView(params);
    Check(test::modelQueries == 2 && !TrySelectNightFlashlight(),
        "the first-person model alone cannot expose slot 5");
    Check(test::count == 32 && nightActive == 32, "all 32 current alive players create client-managed lights");
    Check(test::seen[0].light.origin[0] == 400 && test::seen[0].light.origin[1] == 500 && test::seen[1].light.origin[0] == 10,
        "own light uses prediction, other lights use already-interpolated network origins");
    Check(test::seen[31].slot == 31 && test::seen[31].sourceEntityIndex == 32 && test::seen[1].light.color == 0x303030,
        "stable slot/source identity and 48/255 neutral brightness reach Renderer");
    test::entities[1].curstate.movetype = MOVETYPE_FLY; NightPlayerLightsView(params);
    Check(test::count == 32, "living ladder players retain their light through native FLY movement");
    test::entities[1].curstate.movetype = MOVETYPE_WALK;
    allocation_probe::calls = 0; allocation_probe::active = true;
    for (unsigned i = 0; i < 2000; ++i)
    { params.simorg[0] += .25f; test::entities[31].origin[0] += .5f; NightPlayerLightsView(params); }
    allocation_probe::active = false;
    Check(!allocation_probe::calls && test::count == 32 && test::seen[0].light.origin[0] == params.simorg[0] &&
        test::seen[31].light.origin[0] == test::entities[31].origin[0], "2000 real moving 32-player updates perform zero allocations");
    test::entities[1].curstate.solid = SOLID_NOT; NightPlayerLightsView(params);
    Check(test::count == 31, "remote death removes its lamp without an unsent health field");
    test::entities[1].curstate.solid = SOLID_SLIDEBOX; test::entities[2].curstate.messagenum = 16;
    test::entities[3].curstate.effects = EF_NODRAW; test::entities[4].curstate.spectator = 1;
    test::entities[5].curstate.movetype = MOVETYPE_TOSS; test::entities[6].model = nullptr;
    test::entities[7].origin[0] = std::numeric_limits<float>::quiet_NaN();
    NightPlayerLightsView(params);
    Check(test::count == 26, "stale PVS, hidden, spectator, dead body, disconnected and nonfinite entities are rejected");
    params.health = 0; NightPlayerLightsView(params); Check(test::count == 25, "own death removes its predicted lamp");
    test::Entities(); params.health = 100; NightPlayerLightsView(params); Check(test::count == 32, "respawn reuses all identities");
    ++snapshot.revision; snapshot.config.color = 0xff8000; snapshot.config.brightness = 255;
    test::Send(snapshot); NightPlayerLightsView(params);
    Check(test::seen[0].light.color == 0x0080ff, "configured RRGGBB converts to Renderer AABBGGRR");
    snapshot.config.enabled = false; ++snapshot.revision; test::Send(snapshot);
    Check(!test::count && !nightActive, "persistent toggle off clears immediately on receipt");
    snapshot.config.enabled = true; ++snapshot.revision; test::Send(snapshot); NightPlayerLightsView(params);
    Check(test::count == 32, "toggle on during the same night is valid");
    const auto accepted = nightAccepted; const auto wire = *custom::EncodeNightLighting(snapshot);
    test::message(custom::NightLightingMessage, int(wire.size()), const_cast<std::byte*>(wire.data()));
    Check(nightAccepted == accepted, "duplicate reliable packet does not reset presentation");
    for (std::size_t n = 0; n < wire.size(); ++n)
        Check(!custom::DecodeNightLighting(std::span(wire.data(), n)), "all truncations rejected");
    for (const auto offset : {0u, 1u, 2u, 11u})
    { auto bad = wire; bad[offset] = std::byte{255}; Check(!custom::DecodeNightLighting(bad), "malformed version/flags/phase/color rejected"); }
    const auto updates = test::updates;
    std::thread foreign([&] { NightPlayerLightsView(params); ResetNightPlayerLights(); }); foreign.join();
    Check(test::updates == updates && test::count == 32, "foreign thread cannot mutate render-owned lights");
    snapshot.phase = custom::Phase::Ended; ++snapshot.revision; test::Send(snapshot);
    Check(!test::count, "round end clears all lights");
    snapshot.phase = custom::Phase::Night; ++snapshot.revision; test::Send(snapshot); NightPlayerLightsView(params);
    Check(!test::count, "retired challenge cannot reactivate");
    snapshot.owner = 2; snapshot.phase = custom::Phase::Noon; ++snapshot.revision; test::Send(snapshot);
    test::handheldModels = true;
    snapshot.phase = custom::Phase::Night; ++snapshot.revision; test::Send(snapshot); NightPlayerLightsView(params);
    Check(test::count == 32, "new challenge can start a new night");
    Check(NightFlashlightSlotAvailable() && TrySelectNightFlashlight() && test::flashlightCommands == 1,
        "both precached models allow slot 5 to send one command");
    std::strcpy(test::heldModel.name, custom::NightFlashlightView);
    test::heldView.model = &test::heldModel;
    Check(NightFlashlightHeld(), "the actual held model owns the flashlight control");
    Check(TrySelectNightFlashlight() && test::flashlightCommands == 1 && test::modelQueries == 4,
        "an already-held flashlight consumes slot 5 without a new command or model lookup");
    ResetNightPlayerLights(); NightPlayerLightsView(params); Check(!test::count, "map/connection reset clears state");
    Check(!NightFlashlightHeld(), "a lingering view model after a reset owns no input");
    Check(!TrySelectNightFlashlight(), "map reset retires slot 5 before the next authoritative snapshot");
    snapshot.owner = 1; snapshot.revision = 1; test::Send(snapshot); NightPlayerLightsView(params);
    Check(test::count == 32, "fresh connection uses its own numbering");
    test::failUpdate = true; NightPlayerLightsView(params); Check(!test::count, "renderer rejection clears previous lamps");
    test::failUpdate = false; NightPlayerLightsView(params); ShutdownNightPlayerLights();
    Check(!nightPrepared && !test::count, "shutdown unregisters and clears");
    test::Send(snapshot); NightPlayerLightsView(params); Check(!test::count, "stopped hooks cannot restart lights");
    std::printf("night client/control: %u checks; 2000 x 32 moving frames zero allocations; real SDK, controlled Renderer sink; no gameplay/FPS claim\n", test::checks);
}
