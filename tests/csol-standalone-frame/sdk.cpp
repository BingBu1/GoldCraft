// SPDX-License-Identifier: GPL-3.0-or-later
// Real registered Session/player and stamina owner. Engine calls and a ReAPI
// callback are explicit test boundaries; no local PM or client is simulated.
#define CSOL_PHASE_SDK_LIBRARY
#include "../csol-challenge-phase/sdk.cpp"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include <functional>

namespace standalone_frame_test
{
using namespace phase_sdk;
std::function<void()> speedCallback;
unsigned speedCalls{};
std::array<const char*, 3> commandArguments{};
int commandCount{};
void Command(Case& c, const char* name, const std::string& life, int count = 2)
{
    commandArguments = {name, life.c_str(), "extra"}; commandCount = count;
    g_engfuncs.pfnCmd_Argc = [] { return commandCount; };
    g_engfuncs.pfnCmd_Argv = [](int index) -> const char*
    { return index >= 0 && index < commandCount && index < 3 ? commandArguments[unsigned(index)] : ""; };
    ClientCommand_(c.player.edict());
    commandArguments = {}; commandCount = 0;
}
void ReadyCommand(Case& c, const std::string& life, const std::string& revision, int count = 3)
{
    commandArguments = {custom::StandaloneInputReadyCommand, life.c_str(), revision.c_str()}; commandCount = count;
    g_engfuncs.pfnCmd_Argc = [] { return commandCount; };
    g_engfuncs.pfnCmd_Argv = [](int index) -> const char*
    { return index >= 0 && index < commandCount && index < 3 ? commandArguments[unsigned(index)] : ""; };
    ClientCommand_(c.player.edict());
    commandArguments = {}; commandCount = 0;
}
void Speed(IReGameHook_CBasePlayer_ResetMaxSpeed* chain, CBasePlayer* player)
{
    ++speedCalls;
    const auto callback = speedCallback;
    if (callback) callback();
    chain->callNext(player);
}
void Settings(bool enabled = true, bool timeCycle = false)
{
    if (timeCycle) environment_test::LoadProvider();
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Set(custom::Feature::CofSprint, enabled, error) &&
        draft.Set(custom::Feature::TimeCycle, timeCycle, error) &&
        draft.Set(custom::Setting::MiddaySeconds, 2, error) &&
        draft.Set(custom::Setting::DuskSeconds, 3, error) && draft.Finish(snapshot, error),
        "typed StandAlone policy accepts omitted native defaults and distance units");
    const auto path = std::filesystem::current_path() / "cstrike/addons/goldcraft/config/gameplay.json";
    Require(!std::filesystem::exists(path), "fresh standalone SDK configuration");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << custom::Serialize(*snapshot);
    Require(bool(file), "persist configured policy before actual GameDLL initialization");
}
float Amount(Case& c)
{
    const auto value = CSOL_ActionStaminaView(c.f.runtime, c.player);
    Require(bool(value), "actual current stamina owner is readable");
    return value->amount;
}
custom::StandaloneInputGrant Grant(Case& c)
{
    const auto found = c.f.engine.messages.find(custom::StandaloneInputMessage);
    Require(found != c.f.engine.messages.end(), "cold path registered actual input grant message");
    for (auto packet = c.packets.rbegin(); packet != c.packets.rend(); ++packet)
        if (packet->id == found->second)
        {
            Require(packet->destination == MSG_ONE && packet->receiver == c.player.entindex(),
                "actual input packet belongs to the current player");
            if (const auto value = custom::DecodeStandaloneInput(packet->bytes)) return *value;
            Require(bool(custom::DecodeStandaloneInputProbe(packet->bytes)),
                "only a valid readiness probe can follow an input grant or revocation");
        }
    throw std::runtime_error("Actual owner emitted no input grant");
}
custom::StandaloneInputProbe Probe(Case& c)
{
    const auto id = c.f.engine.messages.at(custom::StandaloneInputMessage);
    for (auto packet = c.packets.rbegin(); packet != c.packets.rend(); ++packet)
        if (packet->id == id)
            if (const auto value = custom::DecodeStandaloneInputProbe(packet->bytes))
            {
                Require(packet->destination == MSG_ONE && packet->receiver == c.player.entindex(),
                    "actual readiness probe belongs to this player only");
                return *value;
            }
    throw std::runtime_error("Actual owner emitted no readiness probe");
}
bool Frame(Case& c, float now)
{
    c.f.engine.globals.time = now; c.f.engine.globals.frametime = .01f;
    c.error = {};
    return CSOL_TickStandaloneMovementFrame(c.f.runtime, c.error);
}
void Admit(Case& c, float now)
{
    // Explicit boundary: native UpdateClientData has completed InitHUD.
    // The hud-order group separately drives that real native method.
    c.player.m_fGameHUDInitialized = TRUE;
    Require(Frame(c, std::nextafter(now, -std::numeric_limits<float>::infinity())),
        "cold current life emits a readiness probe without enabling movement");
    const auto challenge = Probe(c);
    ReadyCommand(c, std::to_string(challenge.life), std::to_string(challenge.revision));
}
void SetNight(Case& c)
{
    Require(CSOL_SetChallengeStaminaPhase(c.f.runtime, custom::Phase::Night, true),
        "direct movement fixture starts with an authoritative unlocked phase");
}
void Run(std::string_view group)
{
    Settings(group != "disabled", group == "rules-frame");
    Case c(MusicResources);
    c.player.m_iJoiningState = JOINED;
    c.player.m_bJustConnected = false;
    static_cast<CHalfLifeMultiplay*>(g_pGameRules)->m_bFreezePeriod = FALSE;
    c.f.engine.Set("sv_maxspeed", "900");
    g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.registerHook(Speed, 200);
    struct Cleanup
    {
        ~Cleanup()
        { speedCallback = {}; g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.unregisterHook(Speed); }
    } cleanup;
    if (group == "disabled")
    {
        const auto id = c.f.engine.messages.at(custom::StandaloneInputMessage);
        const auto grants = [&]
        { return std::count_if(c.packets.begin(), c.packets.end(), [&](const auto& packet) { return packet.id == id; }); };
        const auto before = grants();
        Require(Frame(c, 100), "disabled configuration does not prepare movement owner");
        c.f.engine.Set("csol_custom_cof_sprint", "1");
        NativeActor(c); Restart(c); Tick(c, 120);
        Expect(grants() == before && !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
            "a CVAR change after map preparation cannot enable sprint on the current map");
        Command(c, "sprintON", "1");
        Expect(grants() == before && !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
            "manual native command cannot manufacture a granted movement life");
        Expect(!CSOL_ActionStaminaInputReady(c.f.runtime, c.player, "1", "1"),
            "unprobed acknowledgement cannot manufacture disabled or unsupported movement");
        return;
    }
    if (group == "rules-frame")
    {
        Expect(CSOL_CustomFeatureEnabled(custom::Feature::CofSprint),
            "persistent sprint configuration initializes the production CVAR");
        NativeActor(c);
        Restart(c);
        c.player.m_fGameHUDInitialized = TRUE;
        CSOL_RetireActionStamina(c.player);
        c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));
        ++c.player.m_iNumSpawns;
        Require(CSOL_TickActionStamina(c.f.runtime, c.player, 120.011f, c.error, true),
            "new life can publish its HUD in a client command ahead of the first world frame");
        Tick(c, 120);
        c.player.ResetMaxSpeed();
        const float normalSpeed = c.player.pev->maxspeed;
        Require(normalSpeed > 0 && normalSpeed != 300, "native scenario movement has a distinct baseline");
        const auto probe = Probe(c);
        Require(CSOL_TickActionStamina(c.f.runtime, c.player, 120.021f, c.error),
            "client post-think can run ahead of the next world frame while awaiting readiness");
        ReadyCommand(c, std::to_string(probe.life), std::to_string(probe.revision));
        c.f.engine.Set("csol_custom_cof_sprint", "0");
        const int id = c.f.engine.messages.at(custom::StandaloneInputMessage);
        const auto inputPackets = [&]
        {
            return std::count_if(c.packets.begin(), c.packets.end(),
                [id](const auto& packet) { return packet.id == id; });
        };
        const auto noonPackets = inputPackets();
        Tick(c, 120.01f);
        const auto life = std::to_string(probe.life);
        Command(c, "sprintON", life);
        Tick(c, 121);
        Expect(Read(c).time.phase == custom::Phase::Noon &&
            CSOL_ActionStaminaView(c.f.runtime, c.player)->infinite && inputPackets() == noonPackets &&
            !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) && c.player.pev->maxspeed == normalSpeed,
            "noon keeps native movement and unlimited stamina even after a sprint command and client acknowledgement");

        Tick(c, 122);
        const auto granted = Grant(c);
        Expect(Read(c).time.phase == custom::Phase::Dusk && granted.enabled && granted.life == probe.life &&
            c.player.pev->maxspeed == c.weapon->GetMaxSpeed() * .5f &&
            CSOL_ActionStaminaView(c.f.runtime, c.player)->maximum == 150,
            "dusk opens the existing movement policy with enhanced finite stamina and a fresh request");
        Command(c, "sprintON", life);
        Tick(c, 122.02f);
        Expect(c.player.pev->maxspeed == 300, "dusk sprint uses the fixed standing speed");
        const auto duskPackets = inputPackets();
        Tick(c, 125);
        Expect(Read(c).time.phase == custom::Phase::Night && c.player.pev->maxspeed == 300 &&
            CSOL_ActionStaminaView(c.f.runtime, c.player)->maximum == 100 && inputPackets() == duskPackets,
            "eternal night keeps the live sprint and token without publishing another input packet");

        c.f.engine.Set("csol_custom_time_cycle", "0");
        Tick(c, 125);
        const auto revoked = Grant(c);
        const auto nextProbe = Probe(c);
        Expect(!revoked.enabled && revoked.life == probe.life && revoked.revision > granted.revision &&
            nextProbe.life > revoked.life && c.player.pev->maxspeed == normalSpeed &&
            !CSOL_ActionStaminaView(c.f.runtime, c.player)->sprinting,
            "disabling time clears sprint and restores native speed even at the last frame timestamp");
        ReadyCommand(c, std::to_string(nextProbe.life), std::to_string(nextProbe.revision));
        Command(c, "sprintON", life);
        c.f.engine.Set("csol_custom_time_cycle", "1");
        Tick(c, 127);
        Expect(Read(c).time.phase == custom::Phase::Noon && c.player.pev->maxspeed == normalSpeed &&
            CSOL_ActionStaminaView(c.f.runtime, c.player)->infinite && !Grant(c).enabled,
            "re-enabling the clock begins a new noon with the sprint still locked");
        Tick(c, 129);
        const auto nextGrant = Grant(c);
        Expect(nextGrant.enabled && nextGrant.life == nextProbe.life && nextGrant.life > revoked.life &&
            c.player.pev->maxspeed == c.weapon->GetMaxSpeed() * .5f,
            "the next dusk grants a new token without inheriting a sprint request");
        Command(c, "sprintON", life);
        Tick(c, 129.01f);
        Expect(c.player.pev->maxspeed == c.weapon->GetMaxSpeed() * .5f,
            "queued sprint from the previous period cannot activate the new one");
        Command(c, "sprintON", std::to_string(nextGrant.life));
        Tick(c, 129.02f);
        Expect(c.player.pev->maxspeed == 300, "the new token enables a fresh sprint");

        auto* replay = static_cast<csol::IScenarioReplayService*>(
            CreateInterface(csol::ScenarioReplayServiceVersion, nullptr));
        const auto challenge = Read(c).challenge;
        Require(replay && replay->RequestReplay(98), "real same-map replay accepts current map");
        c.player.m_iMenu = Menu_ChooseAppearance;
        Tick(c, 130);
        Expect(Read(c).challenge > challenge && Read(c).time.phase == custom::Phase::Noon &&
            !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) &&
            !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, std::to_string(nextGrant.life)),
            "same-map replay resets to noon and cannot inherit the previous sprint token");
        return;
    }
    SetNight(c);
    if (group == "hud-order")
    {
        c.player.m_fGameHUDInitialized = FALSE;
        const auto before = c.packets.size(), calls = speedCalls;
        Require(Frame(c, 100), "pre-InitHUD native frame remains usable");
        Expect(c.packets.size() == before && speedCalls == calls &&
            !CSOL_ActionStaminaInputReady(c.f.runtime, c.player, "1", "1") &&
            !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
            "no movement owner or one-shot probe precedes native InitHUD completion");
        c.player.m_fInitHUD = TRUE;
        // Actual ReHLDS COM_FreeFile accepts nullptr. InitHUD may request an
        // absent optional MOTD; nonnull buffers retain the fixture's exact
        // one-release check instead of accepting unknown allocations.
        g_engfuncs.pfnFreeFile = [](void* data) { if (data) Engine::Free(data); };
        c.player.UpdateClientData();
        Require(c.player.m_fGameHUDInitialized != FALSE && !CSOL_RulesRejected(),
            "real native UpdateClientData completes HUD initialization");
        const auto initializedPackets = c.packets.size();
        Require(Frame(c, 100.01f), "first initialized frame may publish its readiness probe");
        const auto challenge = Probe(c);
        Expect(c.packets.size() > initializedPackets && challenge.life != 0,
            "readiness probe follows native HUD messages in the actual transport sequence");
        ReadyCommand(c, std::to_string(challenge.life), std::to_string(challenge.revision));
        Require(Frame(c, 100.02f), "queued post-InitHUD acknowledgement admits movement");
        Expect(Grant(c).enabled, "HUD initialization cannot erase the first movement grant");
        return;
    }
    c.player.m_fGameHUDInitialized = TRUE;
    if (group == "probe-reset" || group == "probe-close" || group == "probe-ack-reentry")
    {
        bool fired{}, nestedAcknowledged{};
        c.callback = [&](std::string_view event)
        {
            if (fired || event != "message" || c.packets.empty()) return;
            const auto challenge = custom::DecodeStandaloneInputProbe(c.packets.back().bytes);
            if (!challenge) return;
            fired = true;
            if (group == "probe-reset") CSOL_ResetActionStamina();
            else if (group == "probe-close") c.f.runtime->RequestClose();
            else nestedAcknowledged = CSOL_ActionStaminaInputReady(c.f.runtime, c.player,
                std::to_string(challenge->life), std::to_string(challenge->revision));
        };
        const bool complete = Frame(c, 100);
        c.callback = {};
        Require(fired, "actual prepared probe enters the native transport callback boundary");
        if (group != "probe-ack-reentry")
            Expect(!complete && !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
                "reset or close during probe publication cannot retain an active movement owner");
        else
        {
            Expect(complete && !nestedAcknowledged && !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
                "synchronous transport callback cannot acknowledge a partially executing owner");
            const auto challenge = Probe(c);
            ReadyCommand(c, std::to_string(challenge.life), std::to_string(challenge.revision));
            Require(Frame(c, 100.01f), "later queued native acknowledgement admits the same prepared life");
            Expect(Grant(c).enabled, "only queued acknowledgement after callback completion activates input");
        }
        return;
    }
    if (group == "readiness" || group == "old-client" || group == "ready-retired" || group == "fake-client")
    {
        const auto calls = speedCalls;
        c.player.pev->maxspeed = 321;
        if (group == "fake-client") c.player.pev->flags |= FL_FAKECLIENT;
        Require(Frame(c, 100), "unacknowledged frame keeps the map usable");
        Expect(speedCalls == calls && c.player.pev->maxspeed == 321 &&
            !CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250),
            "readiness never applies speed before an installed client acknowledges");
        if (group == "fake-client")
        {
            const auto id = c.f.engine.messages.at(custom::StandaloneInputMessage);
            Expect(std::none_of(c.packets.begin(), c.packets.end(), [id](const auto& packet) { return packet.id == id; }),
                "fake clients receive no input probe and retain their native movement");
            return;
        }
        const auto challenge = Probe(c);
        const auto life = std::to_string(challenge.life), revision = std::to_string(challenge.revision);
        const auto packets = c.packets.size();
        Command(c, "sprintON", life);
        c.player.pev->origin.x += 800;
        Require(Frame(c, 100.01f), "waiting for readiness advances no movement owner");
        Expect(c.packets.size() == packets && speedCalls == calls && Amount(c) == 100,
            "old consumer neither receives repeated probes nor gets distance debit or speed changes");
        if (group == "old-client") return;
        if (group == "ready-retired")
        {
            CSOL_RetireActionStamina(c.player);
            Expect(!CSOL_ActionStaminaInputReady(c.f.runtime, c.player, life, revision),
                "queued acknowledgement cannot revive a retired native life");
            CSOL_ScenarioPlayerSpawnStarted(c.player); ++c.player.m_iNumSpawns;
            Admit(c, 100.02f);
            Require(Frame(c, 100.02f), "independent native spawn admits its own acknowledgement");
            Expect(Grant(c).life > challenge.life && Amount(c) == 100 &&
                !CSOL_ActionStaminaInputReady(c.f.runtime, c.player, life, revision),
                "new owner cannot inherit the preceding readiness token");
            return;
        }
        for (const auto* bad : {"", "-1", "0", "1x", "4294967296"})
            Expect(!CSOL_ActionStaminaInputReady(c.f.runtime, c.player, life, bad),
                "malformed or oversized acknowledgement revision is rejected");
        Expect(!CSOL_ActionStaminaInputReady(c.f.runtime, c.player, std::to_string(challenge.life + 1), revision),
            "foreign life cannot acknowledge this current input owner");
        ReadyCommand(c, life, revision, 4);
        Require(Frame(c, 100.02f), "extra native acknowledgement argument leaves readiness pending");
        Expect(speedCalls == calls, "native ClientCommand cannot accept a partial acknowledgement");
        ReadyCommand(c, life, revision);
        Expect(!CSOL_ActionStaminaInputReady(c.f.runtime, c.player, life, revision),
            "same acknowledgement cannot activate twice");
        Require(Frame(c, 100.03f), "next actual frame applies acknowledged movement");
        const auto grant = Grant(c);
        Expect(grant.enabled && grant.life == challenge.life && grant.revision > challenge.revision &&
            CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 125 && Amount(c) == 100,
            "exact current acknowledgement precedes first grant and cannot charge pre-admission distance");
        return;
    }
    Admit(c, 100);
    Require(Frame(c, 100), "actual frame owner prepares joined native player");
    const auto grant = Grant(c);
    const auto life = std::to_string(grant.life);
    Expect(grant.enabled && Amount(c) == 100, "initial frame grants current finite life without debit");
    if (group == "init")
    {
        const auto packets = c.packets.size(); const auto calls = speedCalls;
        Require(Frame(c, 100), "completed same-time frame permits idempotent observation");
        Expect(c.packets.size() == packets && speedCalls == calls && Amount(c) == 100,
            "same frame neither grants twice nor calls native speed reset twice");
        return;
    }
    if (group == "command")
    {
        const auto weaponSpeed = c.weapon->GetMaxSpeed();
        Command(c, "sprintON", life, 3);
        Require(Frame(c, 100.01f), "malformed native command keeps movement frame valid");
        Expect(CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 125,
            "native ClientCommand rejects an extra argument instead of accepting partial command");
        Command(c, "sprintON", std::to_string(grant.life + 1));
        Require(Frame(c, 100.02f), "stale native command keeps movement owner");
        Expect(CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 125,
            "native route rejects stale life token");
        const auto calls = speedCalls;
        Command(c, "sprintON", life);
        Expect(speedCalls == calls && Amount(c) == 100,
            "original command edge changes request only, without resetting speed or debiting");
        Require(Frame(c, 100.03f), "next real frame applies valid native run request");
        Expect(CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 300 && c.player.pev->maxspeed == 300,
            "native ResetMaxSpeed applies the fixed standing sprint speed");
        Command(c, "sprintOFF", life);
        Require(Frame(c, 100.04f), "next real frame applies native release request");
        Expect(CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 125 && c.player.pev->maxspeed == weaponSpeed * .5f,
            "actual ResetMaxSpeed reaches the native weapon speed then original half-walk multiplier");
        return;
    }
    Expect(!CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, "") &&
        !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, "-1") &&
        !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, life + "x") &&
        !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, std::to_string(grant.life + 1)),
        "malformed and stale input cannot change actual run request");
    Require(CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, life), "current grant accepts run request");
    Require(Frame(c, 100.01f), "first running frame captures original displacement origin");
    Expect(Amount(c) == 100 && CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250) == 300,
        "request edge starts native run without charging the prior position");
    if (group == "speed-layers")
    {
        const auto weaponSpeed = c.weapon->GetMaxSpeed();
        c.player.ResetMaxSpeed();
        Expect(c.player.pev->maxspeed == 300 &&
            CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 210) == 300 &&
            CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 400) == 300,
            "standing sprint reaches 300 with weapon speeds on either side of 300");
        auto* rules = static_cast<CHalfLifeMultiplay*>(g_pGameRules);
        rules->m_bFreezePeriod = TRUE; c.player.ResetMaxSpeed();
        Expect(c.player.pev->maxspeed == 1, "native freeze retains priority over running");
        rules->m_bFreezePeriod = FALSE;
        c.player.pev->iuser1 = OBS_ROAMING; c.player.ResetMaxSpeed();
        Expect(c.player.pev->maxspeed == 900, "native observer movement is unchanged");
        c.player.pev->iuser1 = OBS_NONE; c.player.pev->flags |= FL_DUCKING;
        c.player.ResetMaxSpeed();
        Expect(c.player.pev->maxspeed == weaponSpeed * .8f, "actual crouch reset uses original weapon-derived0.8 multiplier");
        c.player.pev->flags &= ~FL_DUCKING;
        c.f.engine.Set("sv_maxspeed", "100"); c.player.ResetMaxSpeed();
        Expect(c.player.pev->maxspeed == 100, "actual engine maximum caps the final native run speed");
        return;
    }
    if (group == "callback-close" || group == "callback-reset" || group == "callback-partial")
    {
        bool fired{};
        speedCallback = [&]
        {
            if (fired) return; fired = true;
            if (group == "callback-close") c.f.runtime->RequestClose();
            else if (group == "callback-reset") CSOL_ResetActionStamina();
            else CSOL_RetireActionStamina(c.player);
        };
        c.player.pev->origin.x += 100;
        Expect(!Frame(c, 100.02f), "callback invalidation cancels the actual in-progress frame");
        speedCallback = {};
        if (group == "callback-partial")
        {
            const auto packets = c.packets.size();
            Expect(!Frame(c, 100.02f) && !c.error.message.empty() && c.packets.size() == packets,
                "partially completed frame cannot be mistaken for successful same-frame duplicate");
        }
        return;
    }
    c.player.pev->origin.x += 100;
    if (group == "crouch") c.player.pev->flags |= FL_DUCKING;
    if (group == "release")
        Require(CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, false, life), "release accepts current owner");
    if (group == "depleted") c.player.pev->origin.x += 1000;
    Require(Frame(c, 100.02f), "next native frame consumes horizontal displacement");
    const float expected = group == "crouch" || group == "release" ? 100 : group == "depleted" ? 0 : 90;
    Expect(Amount(c) == expected, "actual frame uses original horizontal-distance cost once");
    const auto speed = CSOL_ActionStaminaRunSpeed(c.f.runtime, c.player, 250);
    Expect(speed == (group == "crouch" ? 200 : group == "release" || group == "depleted" ? 125 : 300),
        "current owner applies fixed sprint speed and weapon-derived walk and crouch speeds");
    if (group == "postthink")
    {
        const auto packets = c.packets.size(); const auto calls = speedCalls;
        Require(CSOL_TickActionStamina(c.f.runtime, c.player, 100.02f, c.error), "HUD/kick consumer reads frame-owned state");
        Require(CSOL_TickActionStamina(c.f.runtime, c.player, 100.02f, c.error), "repeated late consumer remains read-only");
        Expect(Amount(c) == expected && c.packets.size() == packets && speedCalls == calls,
            "late kick/HUD cannot consume another displacement or recovery step");
    }
    else if (group == "clock")
    {
        Expect(!Frame(c, 99) && !c.error.message.empty() && Amount(c) == expected,
            "backwards frame clock rejects before another debit");
    }
    else if (group == "retire" || group == "new-life")
    {
        CSOL_RetireActionStamina(c.player);
        Expect(!Grant(c).enabled && !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, life),
            "retirement revokes grant and rejects queued old-life input");
        if (group == "new-life")
        {
            CSOL_ScenarioPlayerSpawnStarted(c.player); ++c.player.m_iNumSpawns;
            Admit(c, 100.03f);
            Require(Frame(c, 100.03f), "native SpawnStarted creates independent stamina and input life");
            const auto next = Grant(c);
            Expect(next.enabled && next.life > grant.life && Amount(c) == 100 &&
                !CSOL_ActionStaminaRunCommand(c.f.runtime, c.player, true, life),
                "new spawn is full and cannot inherit queued sprint from its predecessor");
        }
    }
}
}
#ifndef CSOL_STANDALONE_FRAME_LIBRARY
int main(int argc, char** argv)
{
    try { sdk_test::Require(argc == 2, "one standalone frame group"); standalone_frame_test::Run(argv[1]); }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
#endif
