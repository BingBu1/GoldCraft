// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_damage_mode.h"
#include "game_rules_gate.h"

CSOLWeaponDamageMode CSOL_CurrentWeaponDamageMode(CSOLWeaponDamageMode legacyMode)
{
    // Use the installed rules, not a requested mode cvar or a player's role.
    // Query per attack so map changes cannot retain the previous damage mode.
    return CSOL_UsesScenarioRules() ? CSOLWeaponDamageMode::Scenario : legacyMode;
}
