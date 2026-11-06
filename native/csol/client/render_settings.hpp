// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ostream>

namespace csol::client
{
void InitializeRenderSettings();
// Re-apply the server-forced value each frame for the current session.
void RenderSettingsFrame();
// Session end: drop the forced value and restore the player's saved value.
void ResetRenderSettings();
void RenderSettingsDiagnostics(std::ostream& out);
}
