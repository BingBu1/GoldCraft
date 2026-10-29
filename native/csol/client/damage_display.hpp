// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "damage_styles.hpp"

namespace csol::client
{
void InstallDamageOptions(); // LoadEngine, before GameUI initializes.
void InitializeDamageDisplay();
void DamageDisplayFrame();
void DamageDisplayVideoInit();
void ResetDamageDisplay();
void ShutdownDamageDisplay();
std::span<const damage::Skin> DamageSkins();
const damage::Skin* DamageSkin(int id);
int DamageSelectedSkin();
bool DamageEnabled();
void ApplyDamageSettings(bool enabled, int id);
std::span<const damage::Number> DamageEntries();
void PrepareDamageGraphics(int id);
void ResetDamageGraphics();
void ShutdownDamageOptions();
}
