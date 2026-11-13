// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "interface.h"
#include "game_mode.h"
#include "custom/gameplay_settings.h"
#include "scenario/core/scenario_performance.h"
#include "../../include/goldcraft/csol_game_mode_api.hpp"

#include <charconv>
#include <string_view>

namespace
{
// Preserve the existing local ZP configuration by default. Ordinary rules
// still require their explicit plugin policy; this is not a weapon damage CVAR.
cvar_t requestedMode{"csol_game_mode", "1", FCVAR_SERVER};
cvar_t requestedDifficulty{"csol_scenario_difficulty", "1", FCVAR_SERVER};
cvar_t shortFinalDelay{"csol_scenario_short_final_delay", "0", FCVAR_SERVER};
cvar_t finishAction{"csol_scenario_finish_action", "3", FCVAR_SERVER};
cvar_t nextScenarioMap{"csol_scenario_nextmap", "zs_deadend", FCVAR_SERVER};
// Original rooms select a maplist row by numeric ID. 0 keeps the unique-name
// lookup; a positive ID is required when maplist names a file twice (e.g.
// zs_decoy 113 and event 197) and must name the loading map.
cvar_t scenarioMapId{"csol_scenario_map_id", "0", FCVAR_SERVER};
bool registered{};

void InitialScenarioMapId()
{
    if (!g_engfuncs.pfnEngCheckParm) return;
    char* initial{};
    // Original CSO rooms pass a numeric map identity directly. ReHLDS loads
    // the first world before command-buffer +cvars are a reliable contract.
    // Prefer the explicit host option and retain existing deployment +syntax.
    if (!ENG_CHECK_PARM("-csol_scenario_map_id", &initial) &&
        !ENG_CHECK_PARM("+csol_scenario_map_id", &initial)) return;
    const std::string_view text = initial ? initial : "";
    unsigned value{};
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),value);
    const bool valid = !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && value <= 65535;
    CVAR_SET_STRING("csol_scenario_map_id",valid ? initial : "invalid");
}

class GameModeService final : public csol::IGameModeService
{
public:
    int BeginMap() override
    {
        m_mode = -1;
        m_isolated = false;
        if (!registered || !requestedMode.string) return m_mode;
        // Accept only documented mode IDs. Reject partial strings, fractions,
        // overflow and NaN instead of float->int truncation selecting a mode.
        const char* text = requestedMode.string;
        if (text[0] >= '0' && text[0] <= '2' && text[1] == '\0')
            m_mode = text[0] - '0';
        return m_mode;
    }
    int LockedMode() const override { return m_mode; }
    bool ConfirmPluginIsolation(int mode) override
    {
        if (m_mode < 0 || mode != m_mode) return false;
        m_isolated = true;
        return true;
    }
    bool PluginIsolationReady() const override { return m_mode >= 0 && m_isolated; }
    void EndMap() override { m_mode = -1; m_isolated = false; }
private:
    int m_mode{-1};
    bool m_isolated{};
};

GameModeService modes;
using csol::IGameModeService;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(GameModeService, IGameModeService, csol::GameModeServiceVersion, modes);
}

void CSOL_InitGameModes()
{
    if (registered) return;
    CSOL_InitScenarioPerformance();
    CVAR_REGISTER(&requestedMode);
    CVAR_REGISTER(&requestedDifficulty);
    CVAR_REGISTER(&shortFinalDelay);
    CVAR_REGISTER(&finishAction);
    CVAR_REGISTER(&nextScenarioMap);
    CVAR_REGISTER(&scenarioMapId);
    CSOL_InitGameplaySettings();
    // Command-buffer +cvar ordering is not a first-map contract. The launcher
    // supplies this explicit option, consumed once after real registration and
    // before AMXX's OnPluginsLoaded locks the initial map's mode.
    char* initialMode{};
    if (g_engfuncs.pfnEngCheckParm && ENG_CHECK_PARM("-csol_game_mode", &initialMode))
    {
        const bool valid = initialMode && initialMode[0] >= '0' && initialMode[0] <= '2' && !initialMode[1];
        CVAR_SET_STRING("csol_game_mode", valid ? initialMode : "invalid");
    }
    InitialScenarioMapId();
    registered = true;
}

int CSOL_LockedGameMode() { return modes.LockedMode(); }
bool CSOL_GameModeIsolationReady() { return modes.PluginIsolationReady(); }

bool CSOL_RequestedScenarioDifficulty(std::uint16_t& output)
{
    if (!registered || !requestedDifficulty.string) return false;
    const std::string_view text(requestedDifficulty.string);
    unsigned int value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0 || value > 65535) return false;
    output = static_cast<std::uint16_t>(value);
    return true;
}

bool CSOL_RequestedScenarioShortFinalDelay(bool& output)
{
    if (!registered || !shortFinalDelay.string) return false;
    const std::string_view text(shortFinalDelay.string);
    if (text != "0" && text != "1") return false;
    output = text == "1";
    return true;
}

bool CSOL_RequestedScenarioMapId(std::optional<int>& output)
{
    if (!registered || !scenarioMapId.string) return false;
    const std::string_view text(scenarioMapId.string);
    unsigned int value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > 65535) return false;
    output = value ? std::optional<int>(static_cast<int>(value)) : std::nullopt;
    return true;
}

bool CSOL_RequestedScenarioShortFinalDelay(bool& output)
{
    if (!registered || !shortFinalDelay.string) return false;
    const std::string_view text(shortFinalDelay.string);
    if (text != "0" && text != "1") return false;
    output = text == "1";
    return true;
}

bool CSOL_WriteScenarioDifficultyRequest(std::uint16_t difficulty)
{
    if (!registered || !difficulty || difficulty > 7 || !g_engfuncs.pfnCVarSetString) return false;
    char text[2]{static_cast<char>('0' + difficulty), '\0'};
    CVAR_SET_STRING("csol_scenario_difficulty", text);
    std::uint16_t actual{};
    return CSOL_RequestedScenarioDifficulty(actual) && actual == difficulty;
}

bool CSOL_RequestedScenarioFinishPolicy(CSOLScenarioFinishPolicy& output)
{
    if (!registered || !finishAction.string) return false;
    const std::string_view action(finishAction.string);
    CSOLScenarioFinishPolicy candidate;
    if (action == "0") { output = candidate; return true; }
    if (action == "3")
    {
        if (!requestedMode.string || std::string_view(requestedMode.string) != "2") return false;
        candidate.action = CSOLScenarioFinishAction::Vote;
        output = candidate;
        return true;
    }
    if (action == "2")
    {
        candidate.action = CSOLScenarioFinishAction::Replay;
        output = candidate;
        return true;
    }
    if (action != "1" || !nextScenarioMap.string || !requestedMode.string ||
        std::string_view(requestedMode.string) != "2") return false;
    const std::string_view map(nextScenarioMap.string);
    if (map.empty() || map.size() >= candidate.nextMap.size()) return false;
    for (std::size_t i = 0; i < map.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(map[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        candidate.nextMap[i] = static_cast<char>(c);
    }
    candidate.action = CSOLScenarioFinishAction::ChangeMap;
    output = candidate;
    return true;
}
