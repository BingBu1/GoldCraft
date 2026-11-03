// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace csol::part_effects
{
// Original HW382FF860 entries 79,81,82,90; client199 effects43,46,47,53.
inline constexpr std::array Sprites{
    "sprites/weaponparts_critical.spr", "sprites/weaponparts_healban.spr",
    "sprites/weaponparts_slow.spr", "sprites/weaponparts_zbombdown.spr"};
constexpr int Index(int effect)
{
    switch (effect)
    {
    case 43: return 0;
    case 46: return 1;
    case 47: return 2;
    case 53: return 3;
    default: return -1;
    }
}
}
