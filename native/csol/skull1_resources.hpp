// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array Skull1Resources{
    "models/v_skull1.mdl","models/p_skull1.mdl","models/w_skull1.mdl","models/pshell.mdl",
    "events/skull1.sc","events/skull1_rapid.sc","sprites/csol/weapon_skull1_v1.txt",
    "sprites/csol/640hud64.spr","sprites/csol/640hud2.spr","sprites/csol/640hud4.spr",
    "sound/weapons/skull1-1.wav","sound/weapons/skull1-2.wav",
    "sound/weapons/skull1_clipout.wav","sound/weapons/skull1_clipin.wav","sound/weapons/skull1_draw.wav",
    "sound/weapons/dryfire_pistol.wav","sprites/pistol_smoke1.spr","sprites/pistol_smoke2.spr",
    "sprites/wall_puff1.spr","sprites/wall_puff2.spr","sprites/wall_puff3.spr","sprites/wall_puff4.spr"
};
inline constexpr std::string_view Skull1HUD=
    "3\nweapon 640 csol/640hud64 0 0 170 45\nweapon_s 640 csol/640hud64 0 45 170 45\n"
    "ammo 640 csol/640hud2 25 231 24 24\n";
inline constexpr std::string_view Skull1DeathHUD="d_skull1 640 csol/640hud4 220 48 32 16\n";
inline std::string_view Skull1ResourceSource(std::string_view path){
    if(path=="sprites/csol/640hud64.spr")return "sprites/640hud64.spr";
    if(path=="sprites/csol/640hud2.spr")return "sprites/640hud2.spr";
    if(path=="sprites/csol/640hud4.spr")return "sprites/640hud4.spr";
    return path;
}
}
