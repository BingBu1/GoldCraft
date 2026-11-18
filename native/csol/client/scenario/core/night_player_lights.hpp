// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct ref_params_s;
namespace csol::client
{
void InitializeNightPlayerLights();
void NightPlayerLightsView(const ref_params_s& params);
bool NightFlashlightSlotAvailable() noexcept;
bool NightFlashlightHeld() noexcept;
bool TrySelectNightFlashlight();
void ResetNightPlayerLights();
void ShutdownNightPlayerLights();
void NightPlayerLightsDiagnostics(std::ostream& output);
}
