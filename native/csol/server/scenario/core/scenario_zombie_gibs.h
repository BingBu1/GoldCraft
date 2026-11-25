// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

struct edict_s;

// ReHLDS calls Touch on the brush for an impact from a SOLID_NOT fragment.
bool CSOL_ZombieGibImpact(edict_s* surface, edict_s* moving);
std::array<const char*, 3> CSOL_ScenarioGibModelNames(
    const std::array<int, 3>& indices, int pieces);
