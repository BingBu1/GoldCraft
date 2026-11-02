// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeScenarioUpgrades();
void ResetScenarioUpgrades();
void ShutdownScenarioUpgrades();
void ScenarioUpgradesFrame();
void ScenarioUpgradesVideoInit();
void DrawScenarioUpgrades(float time,int intermission);
void ScenarioUpgradesDiagnostics(std::ostream&);
int ScenarioHudLevel(); // -1 on servers without scenario upgrade state.
}
