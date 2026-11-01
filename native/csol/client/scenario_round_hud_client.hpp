// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
#include <array>
#include <cstdint>
#include <string_view>
namespace csol::client
{
void InitializeScenarioRoundHud();
void ResetScenarioRoundHud();
void ScenarioRoundHudFrame();
void ScenarioRoundHudVideoInit();
void DrawScenarioRoundHud(float time, int intermission);
void ShutdownScenarioRoundHud();
void ScenarioRoundHudDiagnostics(std::ostream&);
bool ShowScenarioRoundHudText(unsigned channel, std::wstring_view text,
    const std::array<std::uint8_t, 3>& color);
}
