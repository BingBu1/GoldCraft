// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace csol {
// GoldSrc keeps previously downloaded files. Revision the list as well as
// its atlas so an existing client cannot retain the old ammo sprite path.
inline std::string WeaponHudList(std::string_view classname)
{
    if (classname == "weapon_skull1") return "sprites/csol/weapon_skull1_v1.txt";
    if (classname == "weapon_skull2") return "sprites/csol/weapon_skull2_v1.txt";
    if (classname == "weapon_skull3" || classname == "weapon_skull3d")
        return "sprites/csol/" + std::string(classname) + "_v1.txt";
    if (classname == "weapon_skull5") return "sprites/csol/weapon_skull5_v1.txt";
    if (classname == "weapon_balrog11") return "sprites/csol/weapon_balrog11_v3.txt";
    if (classname == "weapon_beamgun" || classname == "weapon_beamgunle")
        return "sprites/csol/" + std::string(classname) + "_v2.txt";
    if (classname == "weapon_k3" || classname == "weapon_m249ex")
        return "sprites/csol/" + std::string(classname) + "_v2.txt";
    return "sprites/" + std::string(classname) + ".txt";
}
}
