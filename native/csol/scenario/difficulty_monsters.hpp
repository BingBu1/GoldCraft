// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "difficulty.hpp"
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
}
