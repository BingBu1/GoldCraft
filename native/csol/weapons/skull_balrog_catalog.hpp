// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace csol::skull_balrog
{
// Original resource/item.csv InGameID and item ID, matched to the Chinese
// localization and MP39f962f3 entity exports. These are catalog identities;
// displayed damage/ammo statistics are not weapon simulation parameters.
struct Identity
{
    int weaponId;
    int itemId;
    std::string_view resourceName;
    std::string_view displayName;
    std::string_view classname;
    std::string_view viewModel;
    std::string_view playerModel;
    std::string_view worldModel;
    std::string_view hudList;
};

inline constexpr std::array Identities{
    Identity{130, 407, "skull1", "噬魂SKULL-1", "weapon_skull1",
        "models/v_skull1.mdl", "models/p_skull1.mdl", "models/w_skull1.mdl", "sprites/weapon_skull1.txt"},
    Identity{312, 828, "skull2", "噬血狂袭SKULL-2", "weapon_skull2",
        "models/v_skull2.mdl", "models/p_skull2.mdl", "models/w_skull2.mdl", "sprites/weapon_skull2.txt"},
    Identity{142, 427, "skull3", "喋血SKULL-3", "weapon_skull3",
        "models/v_skull3.mdl", "models/p_skull3.mdl", "models/w_skull3.mdl", "sprites/weapon_skull3.txt"},
    Identity{201, 538, "skull4", "死亡咆哮SKULL-4", "weapon_skull4",
        "models/v_skull4.mdl", "models/p_skull4.mdl", "models/w_skull4.mdl", "sprites/weapon_skull4.txt"},
    Identity{112, 367, "skull5", "死亡骑士SKULL-5", "weapon_skull5",
        "models/v_skull5.mdl", "models/p_skull5.mdl", "models/w_skull5.mdl", "sprites/weapon_skull5.txt"},
    Identity{216, 577, "skull6", "碎魂骑士SKULL-6", "weapon_skull6",
        "models/v_skull6.mdl", "models/p_skull6.mdl", "models/w_skull6.mdl", "sprites/weapon_skull6.txt"},
    Identity{96, 327, "m249ex", "碎魂者SKULL-7", "weapon_m249ex",
        "models/v_m249ex.mdl", "models/p_m249ex.mdl", "models/w_m249ex.mdl", "sprites/weapon_m249ex.txt"},
    Identity{205, 562, "skull8", "碎魂利刃SKULL-8", "weapon_skull8",
        "models/v_skull8.mdl", "models/p_skull8.mdl", "models/w_skull8.mdl", "sprites/weapon_skull8.txt"},
    Identity{152, 437, "skull11", "雷霆SKULL-11", "weapon_skull11",
        "models/v_skull11.mdl", "models/p_skull11.mdl", "models/w_skull11.mdl", "sprites/weapon_skull11.txt"},
    Identity{203, 554, "balrog11", "龙炎Balrog-Ⅺ", "weapon_balrog11",
        "models/v_balrog11.mdl", "models/p_balrog11.mdl", "models/w_balrog11.mdl", "sprites/weapon_balrog11.txt"}
};

constexpr const Identity* Find(int weaponId)
{
    for (const auto& entry : Identities)
        if (entry.weaponId == weaponId) return &entry;
    return nullptr;
}

constexpr const Identity* Find(std::string_view classname)
{
    for (const auto& entry : Identities)
        if (entry.classname == classname) return &entry;
    return nullptr;
}
}
