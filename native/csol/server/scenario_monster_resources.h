// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

// Every pooled monster starts with this model before receiving its type's
// appearance. The map resource gate must check it even if no type uses it.
inline constexpr char CSOLScenarioInitialMonsterModel[] =
    "models/player/zombi_origin/zombi_origin.mdl";
inline constexpr char CSOLOberonBlackHoleModel[] = "models/ef_hole.mdl";
inline constexpr char CSOLOberonKnifeEffectModel[] = "models/ef_knife.mdl";
inline constexpr int CSOLOberonMonsterTypeId = 30200;
// Validated against WeaponDependencies. The engine owns generic download
// registration; this list only makes missing assets a recoverable map error
// before the engine begins loading the model.
inline constexpr std::array CSOLOberonBlackHoleDependencies{
    "models/$0a_hole01.tga", "models/$0a_top.tga"};
