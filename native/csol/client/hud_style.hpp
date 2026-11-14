// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hud_style.hpp"
#include <iosfwd>

namespace csol::client
{
void InitializeHudStyle();
void HudStyleFrame();
// Read-only readiness of the card warmed by HUD_Frame; never loads resources.
bool HudStyleWeaponResourcesReady(int id, unsigned ammoFrames = 1) noexcept;
void HudStyleVideoInit();
void ResetHudStyle();
void ShutdownHudStyle();
// Prepare visibility before entering the stock HUD, without changing its
// HideWeapon mask, selection ownership or prediction state.
void BeginHudStyle(int hidden, int intermission);
void ConstrainHudStyle(bool nativeVitalsReady, bool nativeWeaponReady, bool nativeMoneyReady);
bool HudStyleVitalsVisible();
hud_style::Vitals HudStyleVitalsState();
bool HudStyleWeaponVisible();
bool HudStyleMoneyVisible();
hud_style::Money HudStyleMoneyState();
float HudStyleScale();
bool HudProportionalValue(int normalized,int& scaled);
void DrawHudStyle();
void HudStyleDiagnostics(std::ostream&);
hud_style::WeaponState CurrentWeaponHudState();
}
