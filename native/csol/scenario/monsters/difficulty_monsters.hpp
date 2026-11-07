// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../core/difficulty.hpp"
#include "monster_types.hpp"

namespace csol::scenario
{
// Apply the original priority-specific HP/speed multipliers to the effective
// type records. The map owner supplies the once-per-config-load guard. This
// does not scale attackDamage; its multiplier belongs in damage resolution.
// Validate all results first, then commit scalars in place so active type
// pointers, source rows and duplicate-ID history are not relocated.
bool ApplyMonsterDifficulty(MonsterTypeTable& types, const DifficultyResources& difficulty,
    ConfigError& error);

// Rules10826C80 selects this from the attacker's original type record, before
// resistance effects. The caller multiplies only after those effects; neither
// the type's attackDamage nor the inflictor's type is a substitute.
bool MonsterIncomingDamageFactor(const MonsterType& attacker,
    const DifficultyResources& difficulty, float& factor, ConfigError& error);
}
