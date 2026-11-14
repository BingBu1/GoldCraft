// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_damage.hpp"

#include <array>
#include <string_view>

namespace guest::cof
{
// Authored CS inventory placement. CoF's original HUD slot is not a CS slot.
inline constexpr int GlockHudSlot = 1;
inline constexpr int GlockHudPosition = 0;
inline constexpr int P345HudSlot = 1;
inline constexpr int P345HudPosition = 1;

// Host-only IDs, classnames, prices and shop placement. Original CoF IDs
// collide with stock CS; never publish an original ID as its host identity.
struct PistolDefinition
{
    Pistol identity;
    int hostId;
    int originalId;
    int clip;
    int reserve;
    int price;
    int ammunitionPrice;
    int ammunitionCount;
    std::string_view classname;
    std::string_view ammunition;
    std::string_view displayName;
    std::string_view viewModel;
    std::string_view playerModel;
    std::string_view worldModel;
};

inline constexpr std::array Pistols{
    PistolDefinition{Pistol::Glock, 896, 3, 15, 150, 650, 30, 15,
        "weapon_cof_glock", "cof_glock", "客串武器·Glock 19",
        "models/guest/cof/glock/v_glock.mdl", "models/guest/cof/glock/p_glock.mdl",
        "models/guest/cof/glock/w_glock.mdl"},
    PistolDefinition{Pistol::P345, 897, 7, 8, 80, 800, 30, 8,
        "weapon_cof_p345", "cof_p345", "客串武器·P345",
        "models/guest/cof/p345/v_p345.mdl", "models/guest/cof/p345/p_p345.mdl",
        "models/guest/cof/p345/w_p345.mdl"},
    PistolDefinition{Pistol::Revolver, 898, 21, 5, 50, 1300, 45, 5,
        "weapon_cof_revolver", "cof_revolver", "客串武器·Taurus .357",
        "models/guest/cof/revolver/v_revolver.mdl", "models/guest/cof/revolver/p_revolver.mdl",
        "models/guest/cof/revolver/w_revolver.mdl"},
    PistolDefinition{Pistol::VP70, 899, 19, 18, 180, 1100, 45, 18,
        "weapon_cof_vp70", "cof_vp70", "客串武器·VP70",
        "models/guest/cof/vp70/v_vp70.mdl", "models/guest/cof/vp70/p_vp70.mdl",
        "models/guest/cof/vp70/w_vp70.mdl"}
};

constexpr const PistolDefinition* FindPistol(int hostId) noexcept
{
    const unsigned index = static_cast<unsigned>(hostId) - Pistols.front().hostId;
    return index < Pistols.size() ? &Pistols[index] : nullptr;
}

constexpr const PistolDefinition* FindPistol(Pistol identity) noexcept
{
    const auto index = static_cast<unsigned>(identity);
    return index < Pistols.size() ? &Pistols[index] : nullptr;
}

constexpr bool OriginalPistolId(int id) noexcept
{
    return id == 3 || id == 7 || id == 19 || id == 21;
}

constexpr const PistolDefinition* FindOriginalPistol(int id) noexcept
{
    switch(id)
    {
    case 3: return &Pistols[static_cast<unsigned>(Pistol::Glock)];
    case 7: return &Pistols[static_cast<unsigned>(Pistol::P345)];
    case 21: return &Pistols[static_cast<unsigned>(Pistol::Revolver)];
    case 19: return &Pistols[static_cast<unsigned>(Pistol::VP70)];
    default: return nullptr;
    }
}
}
