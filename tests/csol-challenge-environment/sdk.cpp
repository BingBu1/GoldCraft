// SPDX-License-Identifier: GPL-3.0-or-later
#define CSOL_PHASE_SDK_LIBRARY
#include "../csol-challenge-phase/sdk.cpp"
#include "../../native/csol/server/custom/challenge_environment.h"
#include "goldcraft/lightstyle_api.hpp"
#include "../../native/csol/scenario/custom/night_flashlight.hpp"
#include <cstdlib>
#include <limits>

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
custom::NightLightingSnapshot LastNight(Case& c)
{
    const auto id = c.f.engine.messages.at(custom::NightLightingMessage);
    const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(), [&](const auto& p) { return p.id == id; });
    Require(packet != c.packets.rend(), "actual low-frequency lighting control exists");
    const auto value = custom::DecodeNightLighting(packet->bytes);
    Require(bool(value), "actual control packet is exactly24 bytes and has no coordinate/player list");
    return *value;
}
unsigned NightCount(Case& c)
{
    const auto id = c.f.engine.messages.at(custom::NightLightingMessage);
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
    Expect(LastNight(c).phase == custom::Phase::Night && LastNight(c).config.enabled &&
        LastNight(c).owner == owner, "actual clock sends one shared night policy without creating player lights");
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
    const auto nightCount = NightCount(c);
    for (unsigned i = 0; i != 50; ++i) Tick(c, 146.001f + float(i) * .001f);
    Expect(Count(c) == count, "50 unchanged actual Think frames never flood environment messages");
    Expect(NightCount(c) == nightCount, "50 actual frames publish no lighting packets or per-player coordinates");
    Restart(c);
    Expect(State().owner == token && Last(c).owner == owner && State().level == 'b',
        "ordinary intermediate-stage restart keeps current light lease and environment owner");
    if (join)
    {
        c.f.engine.files["env-motd.txt"] = "Environment join boundary\n";
        CVAR_SET_STRING("motdfile", "env-motd.txt");
        const auto before = Count(c);
        const auto beforeNight = NightCount(c);
        g_pGameRules->InitHUD(&c.player);
        const auto id = c.f.engine.messages.at(custom::EnvironmentMessage);
        const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(), [&](const auto& p) { return p.id == id; });
        Expect(Count(c) == before + 1 && Last(c).owner == owner && Last(c).view.level == 'b' && Last(c).view.fog &&
            packet->destination == MSG_ONE && packet->receiver == c.player.entindex(),
            "actual InitHUD replays the final night profile only to the late joining player");
        Expect(NightCount(c) == beforeNight + 1 && LastNight(c).phase == custom::Phase::Night,
            "late join replays one policy so its own client can render every current player");
    }
    Tick(c, 170);
    c.player.pev->health = 0; c.player.pev->deadflag = DEAD_DEAD;
    Tick(c, 171);
    Expect(!State().applied && !State().owner && std::string_view(State().baseline) == "klm" && !Last(c).view.active,
        "actual all-dead failure restores latest map light and retires client fog");
    Expect(LastNight(c).phase == custom::Phase::Ended, "actual result disables client lamps");
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
void FlashlightResources(std::map<std::string,std::string>& files)
{
    MusicResources(files);
    for (const auto* path : {custom::NightFlashlightView, custom::NightFlashlightPlayer})
        files[path] = "controlled model availability";
}
void FlashlightMissing()
{
    Settings(true); Case c(MusicResources); Start(c);
    CSOL_ScenarioSelectNightFlashlight(c.player);
    Expect(LastNight(c).phase == custom::Phase::Night && !CSOL_RulesRejected() &&
        c.player.m_pActiveItem == c.weapon && !CSOL_ScenarioHoldsNightFlashlight(c.player),
        "a map with no optional equipment models remains playable at night");
}
void Flashlight()
{
    Settings(true); Case c(FlashlightResources);
    NativeActor(c); Restart(c);
    CVAR_SET_FLOAT("mp_flashlight", 0);
    auto& player = c.player;
    player.m_iJoiningState = JOINED;
    player.pev->weapons |= (1 << WEAPON_SUIT);
    player.m_iFlashBattery = 99;
    player.m_iAccount = 1000;
    g_engfuncs.pfnCmd_Argv = [](int index) -> const char* {
        return index == 0 ? custom::NightFlashlightSelect : "";
    };
    g_engfuncs.pfnCmd_Argc = [] { return 1; };
    const auto select = [&] { c.f.engine.api.pfnClientCommand(player.edict()); };
    const auto key = [&] {
        player.pev->impulse = 100;
        player.ImpulseCommands();
    };
    const auto lit = [&] { return player.FlashlightIsOn() != FALSE; };
    const auto held = [&] { return CSOL_ScenarioHoldsNightFlashlight(player); };
    select(); key();
    Expect(player.m_pActiveItem == c.weapon && !lit() && !held(),
        "the real equipment command is ignored during noon");
    Tick(c, 120); Tick(c, 122); Tick(c, 125);
    Require(LastNight(c).phase == custom::Phase::Night, "the real clock reaches the night stage");
    auto* const active = player.m_pActiveItem;
    Require(active && active == c.weapon, "actual primary weapon is equipped before pressing 5");
    const auto inventory = std::to_array(player.m_rgpPlayerItems);
    const auto ammo = std::to_array(player.m_rgAmmo);
    const auto weapons = player.pev->weapons;
    const int clip = c.weapon->m_iClip;
    const int entities = c.f.engine.nextEntity;
    key();
    Expect(!lit(), "native F follows mp_flashlight while holding the rifle");
    select();
    Expect(lit() && held() && !player.m_pActiveItem && player.m_pLastItem == active,
        "5 holsters the rifle and automatically switches on the held flashlight");
    Expect(std::string_view(STRING(player.pev->viewmodel)) == custom::NightFlashlightView &&
        std::string_view(STRING(player.pev->weaponmodel)) == custom::NightFlashlightPlayer,
        "server publishes both authored held model paths");
    Expect(c.f.engine.nextEntity == entities && player.pev->weapons == weapons &&
        std::to_array(player.m_rgpPlayerItems) == inventory && std::to_array(player.m_rgAmmo) == ammo &&
        player.m_iAccount == 1000, "equipment preserves funds, edicts, real weapon slots and ammunition");
    player.pev->button = IN_ATTACK | IN_RELOAD;
    player.ItemPostFrame();
    player.pev->button = 0;
    Expect(held() && c.weapon->m_iClip == clip && !c.weapon->m_fInReload,
        "attack and reload inputs cannot run weapon frames while holding equipment");
    key(); select();
    Expect(!lit() && held(), "F toggles the held flashlight and a repeated selection cannot toggle it back on");
    player.m_iFlashBattery = 0;
    key();
    Expect(!lit() && held(), "an exhausted battery leaves the held viewmodel selected");
    player.m_iFlashBattery = 1;
    key();
    Expect(lit(), "F works after the native battery has charge");
    player.SelectItem("weapon_ak47");
    Require(CSOL_ScenarioPlayerPostThink(player), "real scenario player frame after selecting the rifle");
    Expect(!lit() && !held() && player.m_pActiveItem == active && c.weapon->m_iClip == clip &&
        std::string_view(STRING(player.pev->viewmodel)) == "models/v_ak47.mdl",
        "choosing slot 1 redeploys the same rifle instance and retires the light");
    const auto lampPacket = std::find_if(c.packets.rbegin(), c.packets.rend(),
        [&](const auto& packet) { return packet.id == gmsgFlashlight; });
    Expect(lampPacket != c.packets.rend() && lampPacket->bytes.size() == 2 && lampPacket->bytes[0] == std::byte{0},
        "holstering updates the native client's flashlight indicator");
    select(); player.SelectLastItem();
    Require(CSOL_ScenarioPlayerPostThink(player), "real scenario player frame after lastinv");
    Expect(!lit() && !held() && player.m_pActiveItem == active,
        "lastinv also redeploys the original inventory item");

    select();
    CVAR_SET_FLOAT("csol_custom_time_cycle", 0);
    Tick(c, 126);
    Expect(!lit() && !held() && player.m_pActiveItem == active,
        "disabling the clock holsters the equipment and restores the rifle in the same map");
    key(); select();
    Expect(!lit() && player.m_pActiveItem == active, "the night entitlement ends with its phase");
    CVAR_SET_FLOAT("mp_flashlight", 1);
    key();
    Expect(lit() && !held(), "the original flashlight cvar still permits stock lighting");
}
void FlashlightDeath()
{
    Settings(true); Case c(FlashlightResources); Start(c);
    auto& player = c.player;
    player.m_iJoiningState = JOINED;
    player.pev->weapons |= (1 << WEAPON_SUIT);
    player.m_iFlashBattery = 99;
    CSOL_ScenarioSelectNightFlashlight(player);
    Require(CSOL_ScenarioHoldsNightFlashlight(player) && player.FlashlightIsOn(),
        "the player is holding the lit equipment before the death callback");
    // The game owns the death state. The camera sink does not alter equipment.
    g_engfuncs.pfnSetView = [](const edict_t*, const edict_t*) {};
    player.pev->health = 0;
    g_pGameRules->PlayerKilled(&player, player.pev, player.pev);
    Expect(!player.pev->viewmodel && !player.pev->weaponmodel && !player.FlashlightIsOn() &&
        !player.m_pActiveItem && c.weapon->m_pPlayer == &player,
        "the actual death callback clears the handheld models and beam without redeploying a gun");
    CSOL_ScenarioSelectNightFlashlight(player);
    Expect(!player.pev->viewmodel && !player.FlashlightIsOn(), "dead players cannot retake the light");
}
void ConsoleControls()
{
    Settings(true); Case c(MusicResources); Start(c); Dark(c);
    const auto diskBytes = []
    {
        std::ifstream input("cstrike/addons/goldcraft/config/gameplay.json", std::ios::binary);
        Require(bool(input), "single persistent settings file is readable");
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto originalJson = diskBytes();
    const auto owner = LastNight(c).owner;
    const auto generation = c.f.runtime->Generation();
    const auto round = c.f.runtime->session->Map().RoundGeneration();
    const auto* rules = g_pGameRules;
    const auto environmentPackets = Count(c);
    auto packets = NightCount(c);
    float now = 146.01f;
    const auto tick = [&] { now += .01f; Tick(c, now); };
    CVAR_SET_FLOAT("csol_custom_night_player_lights", 0);
    tick();
    Expect(!LastNight(c).config.enabled && NightCount(c) == ++packets,
        "console disable publishes one current-map policy without saving JSON");
    CVAR_SET_FLOAT("csol_custom_night_player_light_radius", 96.5f);
    CVAR_SET_FLOAT("csol_custom_night_player_light_brightness", 64);
    CVAR_SET_FLOAT("csol_custom_night_player_light_color", 0x123456);
    CVAR_SET_FLOAT("csol_custom_night_player_lights", 1);
    tick();
    const custom::NightLightingConfig expected{true, 96.5f, 0x123456, 64};
    Expect(LastNight(c).config == expected && NightCount(c) == ++packets,
        "console numeric changes and re-enable apply together on the next actual rule frame");
    for (unsigned i = 0; i != 50; ++i) tick();
    Expect(NightCount(c) == packets, "unchanged console controls produce no per-frame lighting packets");
    const std::array invalid{
        std::pair{"csol_custom_night_player_light_radius", 15.f},
        std::pair{"csol_custom_night_player_light_radius", 513.f},
        std::pair{"csol_custom_night_player_light_radius", std::numeric_limits<float>::quiet_NaN()},
        std::pair{"csol_custom_night_player_light_radius", std::numeric_limits<float>::infinity()},
        std::pair{"csol_custom_night_player_light_brightness", -1.f},
        std::pair{"csol_custom_night_player_light_brightness", 256.f},
        std::pair{"csol_custom_night_player_light_brightness", 1.5f},
        std::pair{"csol_custom_night_player_light_color", -1.f},
        std::pair{"csol_custom_night_player_light_color", 16777216.f},
        std::pair{"csol_custom_night_player_light_color", .5f},
        std::pair{"csol_custom_night_player_lights", 2.f},
        std::pair{"csol_custom_night_player_lights", std::numeric_limits<float>::quiet_NaN()}
    };
    for (const auto& [name, value] : invalid)
    {
        CVAR_SET_FLOAT("csol_custom_night_player_light_radius", expected.radius);
        CVAR_SET_FLOAT("csol_custom_night_player_light_brightness", expected.brightness);
        CVAR_SET_FLOAT("csol_custom_night_player_light_color", float(expected.color));
        CVAR_SET_FLOAT("csol_custom_night_player_lights", 1);
        CVAR_SET_FLOAT(name, value);
        tick();
        Expect(LastNight(c).config == expected && NightCount(c) == packets,
            "invalid console field preserves the previous valid policy and emits no bad packet");
    }
    CVAR_SET_FLOAT("csol_custom_night_player_light_radius", std::numeric_limits<float>::quiet_NaN());
    CVAR_SET_FLOAT("csol_custom_night_player_lights", 0);
    tick();
    Expect(!LastNight(c).config.enabled && LastNight(c).config.radius == expected.radius &&
        NightCount(c) == ++packets, "disable remains usable while an unrelated console number is invalid");
    CVAR_SET_FLOAT("csol_custom_night_player_light_radius", 512);
    CVAR_SET_FLOAT("csol_custom_night_player_light_brightness", 255);
    CVAR_SET_FLOAT("csol_custom_night_player_light_color", 0xffffff);
    CVAR_SET_FLOAT("csol_custom_night_player_lights", 1);
    tick();
    Expect(LastNight(c).config == custom::NightLightingConfig{true, 512, 0xffffff, 255} &&
        NightCount(c) == ++packets, "correcting console values resumes without map reload or owner reset");
    CVAR_SET_FLOAT("csol_custom_night_player_light_radius", 16);
    CVAR_SET_FLOAT("csol_custom_night_player_light_brightness", 0);
    CVAR_SET_FLOAT("csol_custom_night_player_light_color", 0);
    tick();
    Expect(LastNight(c).config == custom::NightLightingConfig{true, 16, 0, 0} &&
        NightCount(c) == ++packets, "inclusive numeric bounds retain zero-intensity policy semantics");
    Expect(diskBytes() == originalJson, "temporary console light overrides never rewrite the persistent JSON");
    g_engfuncs.pfnGetPlayerUserId = [](edict_t* player)
    { return player == Case::active->player.edict() ? 71 : 0; };
    auto* settings = static_cast<csol::IGameplaySettingsService*>(
        CreateInterface(csol::GameplaySettingsServiceVersion, nullptr));
    Require(settings != nullptr, "actual menu settings service is available after temporary console changes");
    csol::GameplaySettingsStatus before, after;
    const int radius = int(custom::FeatureCount) + int(custom::Setting::NightPlayerLightRadius);
    Require(settings->Status(before) && settings->Edit(1, 71, before.revision, radius, 200) ==
        csol::GameplayEditResult::Queued, "real menu edit queues one authorized persistent light change");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do
    {
        Require(settings->Status(after), "menu status polls its actual atomic settings writer");
        if (!after.busy && after.result != csol::GameplayEditResult::Queued) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    Require(!after.busy && after.result == csol::GameplayEditResult::Saved &&
        after.revision == before.revision + 1, "persistent light settings publish only after the actual file save");
    tick();
    const auto persisted = custom::ReadNightLighting(*CSOL_CustomSettings());
    Expect(persisted && LastNight(c).config == *persisted && persisted->radius == 200 &&
        NightCount(c) == ++packets, "menu save supersedes temporary controls on the next current-map rule frame");
    custom::SnapshotPtr disk; custom::Error error;
    Expect(custom::Parse(diskBytes(), disk, error) &&
        custom::ReadNightLighting(*disk) == persisted, "single saved JSON matches the policy actually sent to clients");
    Expect(LastNight(c).owner == owner && LastNight(c).phase == custom::Phase::Night &&
        c.f.runtime->Generation() == generation && c.f.runtime->session->Map().RoundGeneration() == round &&
        g_pGameRules == rules && Count(c) == environmentPackets && State().level == 'b',
        "console light controls preserve current challenge, runtime, round and map environment");
}
}
int main(int argc, char** argv)
{
    try
    {
        sdk_test::Require(argc == 2, "one environment SDK group");
        const std::string_view group = argv[1];
        if (group != "environment-missing" && group != "live-missing") environment_sdk::Provider();
        if (group == "environment-flow" || group == "environment-join" || group == "environment-replay")
            environment_sdk::Flow(group == "environment-join", group == "environment-replay");
        else if (group == "environment-cancel" || group == "environment-map") environment_sdk::Cancel(group == "environment-map");
        else if (group == "environment-missing") environment_sdk::Missing();
        else if (group == "environment-console") environment_sdk::ConsoleControls();
        else if (group == "night-flashlight") environment_sdk::Flashlight();
        else if (group == "night-flashlight-missing") environment_sdk::FlashlightMissing();
        else if (group == "night-flashlight-death") environment_sdk::FlashlightDeath();
        else phase_sdk::Run(group);
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
