// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeStaminaHud();
void StaminaHudFrame();
void DrawStaminaHud(int intermission);
void ResetStaminaHud();
void InvalidateStaminaHudResources();
void ShutdownStaminaHud();
void StaminaHudDiagnostics(std::ostream&);
}
