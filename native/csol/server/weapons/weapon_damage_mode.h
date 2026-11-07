// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

enum class CSOLWeaponDamageMode { Normal, Zombie, Scenario, ZombieEscape };

// The installed server rules select Scenario automatically. Outside scenario,
// preserve the weapon's legacy mode/cvar semantics, including zombie escape.
// This selects a base-damage profile only: hitgroup, armor, character upgrades
// and map multipliers remain in their existing authoritative damage stages.
CSOLWeaponDamageMode CSOL_CurrentWeaponDamageMode(CSOLWeaponDamageMode legacyMode);
