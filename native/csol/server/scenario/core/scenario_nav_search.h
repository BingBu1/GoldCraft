// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "game_shared/bot/nav_area.h"
#include <span>

enum class CSOLNavSearchResult { Reached, Partial, Failed };

// The map navigation owner supplies the actual ladders whose map targetname
// is "climber". This metadata is separate from the stock SDK ladder layout.
// Search uses the SDK's real area lists, parents and open-list ordering. It is
// synchronous: no engine/plugin callbacks may release the NAV graph here.
CSOLNavSearchResult CSOL_FindScenarioRoute(CNavArea* start, CNavArea* goal,
    const Vector& destination, bool climber,
    std::span<const CNavLadder* const> climberLadders, CNavArea*& closest);
