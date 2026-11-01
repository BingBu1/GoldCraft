// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeScenarioRoundHud();
void ResetScenarioRoundHud();
void ScenarioRoundHudFrame();
void ScenarioRoundHudVideoInit();
void DrawScenarioRoundHud(float time, int intermission);
void ShutdownScenarioRoundHud();
void ScenarioRoundHudDiagnostics(std::ostream&);
}
