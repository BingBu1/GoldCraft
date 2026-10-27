// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "interface.h"
#include "game_mode.h"
#include "../../include/goldcraft/csol_game_mode_api.hpp"

#include <charconv>
#include <string_view>

namespace
{
// Preserve the existing local ZP configuration by default. Ordinary rules
// still require their explicit plugin policy; this is not a weapon damage CVAR.
cvar_t requestedMode{"csol_game_mode", "1", FCVAR_SERVER};
cvar_t requestedDifficulty{"csol_scenario_difficulty", "1", FCVAR_SERVER};
bool registered{};

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
    CVAR_REGISTER(&requestedMode);
    CVAR_REGISTER(&requestedDifficulty);
    // Command-buffer +cvar ordering is not a first-map contract. The launcher
    // supplies this explicit option, consumed once after real registration and
    // before AMXX's OnPluginsLoaded locks the initial map's mode.
    char* initialMode{};
    if (g_engfuncs.pfnEngCheckParm && ENG_CHECK_PARM("-csol_game_mode", &initialMode))
    {
        const bool valid = initialMode && initialMode[0] >= '0' && initialMode[0] <= '2' && !initialMode[1];
        CVAR_SET_STRING("csol_game_mode", valid ? initialMode : "invalid");
    }
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
