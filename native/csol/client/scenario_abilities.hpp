// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeScenarioAbilities();
void ResetScenarioAbilities();
void ShutdownScenarioAbilities();
void ScenarioAbilitiesFrame();
void ScenarioAbilitiesVideoInit();
void DrawScenarioAbilities(float time,int intermission);
void ScenarioAbilitiesDiagnostics(std::ostream&);
}
