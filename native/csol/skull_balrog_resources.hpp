// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array M249EXResources{
    "models/v_m249ex.mdl", "models/p_m249ex.mdl", "models/w_m249ex.mdl", "models/rshell.mdl",
    "events/m249ex.sc", "sprites/csol/weapon_m249ex_v2.txt", "sprites/csol/640hud51.spr",
    "sprites/csol/640hud3.spr", "sprites/csol/640hud7_v2.spr", "sprites/csol/crosshairs.spr",
    "sound/weapons/m249ex-1.wav", "sound/weapons/zoom.wav", "sound/weapons/dryfire_rifle.wav",
    "sound/weapons/m249ex_clipout1.wav", "sound/weapons/m249ex_clipout2.wav",
    "sound/weapons/m249ex_clipin1.wav", "sound/weapons/m249ex_clipin2.wav", "sound/weapons/m249ex_clipin3.wav"
};
inline constexpr std::array Balrog11Resources{
    "models/v_balrog11.mdl", "models/p_balrog11.mdl", "models/w_balrog11.mdl",
    "models/shotgunshell.mdl", "models/shell_bcs.mdl", "sprites/flame_puff01.spr",
    "events/balrog11.sc", "sprites/csol/weapon_balrog11_v2.txt", "sprites/csol/640hud89.spr",
    "sprites/csol/640hud3.spr", "sprites/csol/640hud7_v2.spr", "sprites/csol/640hud11.spr",
    "sound/weapons/balrog11-1.wav", "sound/weapons/balrog11-2.wav", "sound/weapons/balrog11_insert.wav",
    "sound/weapons/balrog11_draw.wav", "sound/weapons/balrog9_charge_finish1.wav", "sound/weapons/dryfire_rifle.wav"
};
inline constexpr std::string_view M249EXHUD =
    "4\nweapon 640 csol/640hud51 0 90 170 45\nweapon_s 640 csol/640hud51 0 135 170 45\n"
    "ammo 640 csol/640hud7_v2 230 96 24 24\nautoaim 640 csol/crosshairs 0 72 24 24\n";
inline constexpr std::string_view Balrog11HUD =
    "4\nweapon 640 csol/640hud89 0 0 170 45\nweapon_s 640 csol/640hud89 0 45 170 45\n"
    "ammo 640 csol/640hud3 222 112 24 24\nammo2 640 csol/640hud7_v2 192 124 24 24\n";
inline std::string_view SkullBalrogResourceSource(std::string_view path) {
    if (path == "sprites/csol/640hud51.spr") return "sprites/640hud51.spr";
    if (path == "sprites/csol/640hud89.spr") return "sprites/640hud89.spr";
    if (path == "sprites/csol/640hud3.spr") return "sprites/640hud3.spr";
    if (path == "sprites/csol/640hud7_v2.spr") return "sprites/640hud7.spr";
    if (path == "sprites/csol/640hud11.spr") return "sprites/640hud11.spr";
    if (path == "sprites/csol/crosshairs.spr") return "sprites/crosshairs.spr";
    return path;
}
inline std::string_view GeneratedSkullBalrogResource(std::string_view path) {
    if (path == "sprites/csol/weapon_m249ex_v2.txt") return M249EXHUD;
    if (path == "sprites/csol/weapon_balrog11_v2.txt") return Balrog11HUD;
    return {};
}
}
