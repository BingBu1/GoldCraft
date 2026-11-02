// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hud_style.hpp"
#include <iosfwd>

namespace csol::client
{
void InitializeHudStyle();
void HudStyleFrame();
void HudStyleVideoInit();
void ResetHudStyle();
void ShutdownHudStyle();
// Prepare visibility before entering the stock HUD, without changing its
// HideWeapon mask, selection ownership or prediction state.
void BeginHudStyle(int hidden, int intermission);
void ConstrainHudStyle(bool nativeVitalsReady, bool nativeWeaponReady);
bool HudStyleVitalsVisible();
hud_style::Vitals HudStyleVitalsState();
bool HudStyleWeaponVisible();
bool HudStyleLevelVisible();
float HudStyleScale();
void DrawHudStyle();
void HudStyleDiagnostics(std::ostream&);
hud_style::WeaponState CurrentWeaponHudState();
}
