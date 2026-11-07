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
// Uses the installed authoritative skill snapshot; the server repeats all
// readiness/automatic-trigger gates. True means the installed server skill
// owns slot7, even when unavailable; false preserves ordinary CS commands.
bool TryActivateScenarioAbility();
}
