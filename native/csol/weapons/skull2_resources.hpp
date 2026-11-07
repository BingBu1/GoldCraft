// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array Skull2Resources{
    "models/v_skull2.mdl","models/p_skull2.mdl","models/p_skull2dual.mdl","models/w_skull2.mdl",
    "models/pshell.mdl","events/skull2_a.sc","events/skull2_left.sc","events/skull2_right.sc",
    "sprites/csol/weapon_skull2_v1.txt","sprites/csol/640hud136.spr","sprites/csol/640hud2.spr",
    "sprites/csol/640hud38.spr","sound/weapons/skull2-1.wav","sound/weapons/dryfire_pistol.wav",
    "sprites/pistol_smoke1.spr","sprites/pistol_smoke2.spr","sprites/wall_puff1.spr",
    "sprites/wall_puff2.spr","sprites/wall_puff3.spr","sprites/wall_puff4.spr"
};
inline constexpr std::string_view Skull2HUD=
    "3\nweapon 640 csol/640hud136 0 90 170 45\nweapon_s 640 csol/640hud136 0 135 170 45\n"
    "ammo 640 csol/640hud2 25 231 24 24\n";
inline constexpr std::string_view Skull2DeathHUD="d_skull2 640 csol/640hud38 172 0 48 16\n";
inline std::string_view Skull2ResourceSource(std::string_view path){
    if(path=="sprites/csol/640hud136.spr")return "sprites/640hud136.spr";
    if(path=="sprites/csol/640hud2.spr")return "sprites/640hud2.spr";
    if(path=="sprites/csol/640hud38.spr")return "sprites/640hud38.spr";
    return path;
}
}
