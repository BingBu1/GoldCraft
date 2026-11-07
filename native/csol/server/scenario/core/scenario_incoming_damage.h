// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
struct entvars_s;

// Original mode15 v1B0: source category -> equipment resistances -> difficulty.
// Call before v2C, protection, dodge, character buffs, shields and armor.
bool CSOL_ScenarioApplyMonsterIncomingDamage(CBasePlayer& player,
    entvars_s* attacker, float& damage, int damageType, entvars_s* inflictor = nullptr);
