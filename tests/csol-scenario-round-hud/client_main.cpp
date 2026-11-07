// SPDX-License-Identifier: GPL-3.0-or-later
// Include the entire production consumer without rewriting its methods. The
// controlled engine/resource exits permit observing private lifecycle state;
// ISurface stays absent, so this does not assert a rendered VGUI/GL frame.
#include "native/csol/client/scenario/core/scenario_round_hud_client.cpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sstream>

namespace fixture
{
unsigned checks{}, failures{}, forwards{}, soundCount{}, syncCount{}, reads{};
float clientTime = 4000;
std::filesystem::path common;
std::map<std::string, pfnUserMsgHook> hooks;
std::vector<std::string> played;
bool newSession{};
void Check(bool condition, const char* label, std::source_location location = std::source_location::current())
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << label << " line=" << location.line() << '\n'; }
}
int Previous(const char*, int, void*) { ++forwards; return 73; }
void Log(const char*, ...) {}
std::vector<std::byte> Bytes(std::initializer_list<unsigned> values)
{
    std::vector<std::byte> result;
    for (unsigned value : values) result.push_back(std::byte(value));
    return result;
}
int Send(const char* name, std::vector<std::byte> payload)
{ return hooks.at(name)(name, int(payload.size()), payload.data()); }
std::vector<std::byte> Round(float elapsed)
{
    auto payload = Bytes({27, 3, 2, 2});
    const auto bits = std::bit_cast<std::uint32_t>(elapsed);
    for (unsigned shift : {0u, 8u, 16u, 24u}) payload.push_back(std::byte(bits >> shift));
    return payload;
}
std::string Diagnostics()
{ std::ostringstream out; csol::client::ScenarioRoundHudDiagnostics(out); return out.str(); }
}

CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE) { return nullptr; }
namespace csol
{
Image DecodeTGA(std::span<const std::byte>)
{ throw std::runtime_error("unreachable graphics decoder in no-surface fixture"); }
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
void SynchronizeSession()
{
    ++fixture::syncCount;
    if (fixture::newSession) { fixture::newSession = false; ResetScenarioRoundHud(); }
}
std::vector<std::byte> ReadResource(const char* path)
{
    ++fixture::reads;
    std::ifstream file(fixture::common / path, std::ios::binary);
    if (!file) throw std::runtime_error("actual common resource missing");
    std::vector<std::byte> result;
    for (char value; file.get(value);) result.push_back(std::byte(static_cast<unsigned char>(value)));
    return result;
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        using namespace fixture;
        using namespace csol::client;
        if (argc != 2) throw std::runtime_error("expected unpacked common directory");
        common = argv[1];
        metahook_api_t storage{}; api = &storage;
        storage.GetEngineFactory = []() -> CreateInterfaceFn { return nullptr; };
        storage.GetPluginModuleHandleByBaseFileName = [](const char*) -> HINTERFACEMODULE { return nullptr; };
        storage.HookUserMsg = [](const char* name, pfnUserMsgHook hook) -> pfnUserMsgHook
        {
            const auto previous = hooks.contains(name) ? hooks.at(name) : Previous;
            hooks[name] = hook; return previous;
        };
        engine.GetClientTime = [] { return clientTime; };
        engine.pfnGetCvarFloat = [](const char*) { return 1.0f; };
        engine.Con_DPrintf = Log;
        engine.pfnPlaySoundByNameAtPitch = [](const char* path, float volume, int pitch)
        { Check(volume == 1 && pitch == 100, "dialogue sound preserves original pitch and volume"); ++soundCount; played.emplace_back(path); };
        InitializeScenarioRoundHud();
        Check(hooks.size() == 5, "all production consumer hooks registered");
        InitializeScenarioRoundHud();
        Check(Send("MPToCL", Round(3600.75f)) == 73 && forwards == 1, "reinstall does not form recursive previous chain");
        Check(state.Active() && state.Layout(1024, 1, clientTime).seconds == 3600, "real receiver initializes state from raw float32");
        Check(Send("MPToCL", Bytes({27,25,0xff,0xff})) == 73 && state.Layout(1024,1,clientTime).seconds == 3599,
            "real receiver applies original signed-short carry");
        Check(Send("MPToCL", Bytes({27,25,0,0})) == 73, "carry can be authoritatively cleared");
        const auto forwardedAfterClock = forwards;
        const auto before = accepted;
        Check(Send("MPToCL", Bytes({27, 3, 2})) == 1 && accepted == before && forwards == forwardedAfterClock, "malformed known packet is not forwarded");
        Check(Send("MPToCL", Bytes({27, 27, 0})) == 73 && forwards == forwardedAfterClock + 1, "unrelated boss opcode forwarded");
        Check(Send("ValueSync", Bytes({6, 0, 0x34, 0x12})) == 73, "original team counter handler forwards chain");
        Check(Send("MPToCL", Bytes({27, 2, 32, 0xff, 0xff})) == 73, "slot32 personal frags accepted");

        Check(Send("MPToCL", Bytes({27, 7, 193, 0})) == 73, "actual deadend event handled through production hook");
        Check(catalogLoaded && messages.rows.size() == 4409 && reads == 2, "catalog and Chinese localization loaded once from actual common");
        Check(lines.at(193) == std::vector<std::wstring>{L"指挥本部向所有生还士兵发出通告！"}, "actual dialogue is Chinese without global localization replacement");
        Check(state.Notices()[1].active && state.Notices()[1].id == 193 && soundCount == 1 && played.back() == "events/tutor_msg.wav",
            "event schedules text and exactly one original sound");
        Check(Send("MPToCL", Bytes({27, 7, 43, 0})) == 73 && lines.at(43) == std::vector<std::wstring>{L" "} && soundCount == 1,
            "original blank-message control keeps leading space and does not replay sound");
        for (unsigned id : {13u, 179u, 181u, 867u})
            Check(Send("MPToCL", Bytes({27, 7, id & 255, id >> 8})) == 73, "actual image event passes production consumer");
        Check(state.ImageNotices()[0].id == 13 && state.ImageNotices()[1].id == 179 &&
            state.ImageNotices()[2].id == 181 && state.ImageNotices()[3].id == 867, "all four image channels coexist");
        Check(reads == 2 && soundCount == 5, "cached table avoids redundant reads and each event plays once");
        const auto priorMissing = missingMessages;
        Check(Send("MPToCL", Bytes({27, 7, 0xff, 0xff})) == 73 && missingMessages == priorMissing + 1 && soundCount == 5,
            "unknown table ID has no invented message or sound");

        DrawScenarioRoundHud(clientTime, 0);
        Check(requested, "HUD redraw schedules presentation");
        Check(Send("HideWeapon", Bytes({4})) == 73 && hidden == 4, "hide HUD mask preserved");
        Check(Send("ResetHUD", Bytes({0})) == 73 && hidden == 0 && !requested && state.Active(), "respawn reset clears presentation without losing clock");
        Check(state.Layout(1024, 32, clientTime).seconds == 3600 && catalogLoaded && reads == 2 && soundCount == 5,
            "respawn does not reload catalog or replay event audio");
        Check(std::none_of(state.Notices().begin(), state.Notices().end(), [](const auto& n) { return n.active; }) &&
            std::none_of(state.ImageNotices().begin(), state.ImageNotices().end(), [](const auto& n) { return n.active; }), "respawn clears old dialogue in every channel");
        ScenarioRoundHudVideoInit();
        Check(state.Active() && catalogLoaded, "video init retains authoritative counters and loaded strings");
        ScenarioRoundHudFrame();
        Check(reads == 2 && soundCount == 5, "frame with absent surface neither loads graphics nor replays sound");
        DrawScenarioRoundHud(clientTime, 1); Check(!requested, "intermission suppresses requested overlay");

        Check(Send("InitHUD", Bytes({0})) == 73 && !state.Active() && !catalogLoaded && messages.rows.empty() && lines.empty(),
            "same-map reconnect InitHUD clears all previous catalog and session state");
        clientTime = 4500;
        Check(Send("MPToCL", Round(4100.75f)) == 73 && state.Layout(1024, 32, clientTime).seconds == 4100,
            "reconnect time replay initializes new epoch from server state");
        Check(Send("MPToCL", Bytes({27, 7, 193, 0})) == 73 && reads == 4 && soundCount == 6,
            "new session reloads catalog exactly once");
        newSession = true;
        Check(Send("ValueSync", Bytes({6, 0, 1, 0})) == 73 && !state.Active() && !catalogLoaded, "map synchronization resets before consuming new counter");
        ShutdownScenarioRoundHud();
        Check(!state.Active() && !surface && !guiSystem && !extension && !privateFont && !requested, "shutdown retires presentation handles");
        std::cout << "scenario_round_client checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
