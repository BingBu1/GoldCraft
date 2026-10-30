// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeScenarioSmoke();
void ResetScenarioSmoke();
void ShutdownScenarioSmoke();
// DrawWorldEffects owns the existing graphics state guard.
void DrawScenarioSmoke();
void ScenarioSmokeDiagnostics(std::ostream& out);
}
