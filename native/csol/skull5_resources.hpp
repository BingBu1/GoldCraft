// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array Skull5Resources{
    "models/v_skull5.mdl","models/p_skull5.mdl","models/w_skull5.mdl","models/rshell.mdl",
    "events/skull5.sc","sprites/csol/weapon_skull5_v1.txt",
    "sprites/csol/640hud57.spr","sprites/csol/640hud4.spr","sprites/csol/640hud7_v2.spr",
    "sprites/csol/crosshairs.spr","sprites/csol/sniper_scope.spr","sprites/scope_arc.tga",
    "sound/weapons/skull5-1.wav","sound/weapons/zoom.wav","sound/weapons/dryfire_rifle.wav",
    "sprites/rifle_smoke1.spr","sprites/rifle_smoke2.spr","sprites/rifle_smoke3.spr",
    "sprites/wall_puff1.spr","sprites/wall_puff2.spr","sprites/wall_puff3.spr","sprites/wall_puff4.spr"
};
inline constexpr std::string_view Skull5HUD=
    "7\nweapon 640 csol/640hud57 0 0 170 45\nweapon_s 640 csol/640hud57 0 45 170 45\n"
    "ammo 640 csol/640hud7_v2 230 96 24 24\ncrosshair 640 csol/crosshairs 24 0 24 24\n"
    "autoaim 640 csol/crosshairs 0 72 24 24\nzoom 640 csol/sniper_scope 0 0 256 256\n"
    "zoom_autoaim 640 csol/sniper_scope 0 0 256 256\n";
inline constexpr std::string_view Skull5DeathHUD="d_skull5 640 csol/640hud4 172 144 48 16\n";
inline std::string_view Skull5ResourceSource(std::string_view path)
{
    if(path=="sprites/csol/640hud57.spr")return "sprites/640hud57.spr";
    if(path=="sprites/csol/640hud4.spr")return "sprites/640hud4.spr";
    if(path=="sprites/csol/640hud7_v2.spr")return "sprites/640hud7.spr";
    if(path=="sprites/csol/crosshairs.spr")return "sprites/crosshairs.spr";
    if(path=="sprites/csol/sniper_scope.spr")return "sprites/sniper_scope.spr";
    return path;
}
}
