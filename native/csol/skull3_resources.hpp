// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array Skull3Resources{
    "models/v_skull3.mdl", "models/p_skull3.mdl", "models/p_skull3dual.mdl", "models/w_skull3.mdl",
    "models/pshell.mdl", "models/rshell.mdl",
    "events/skull3.sc", "events/skull3d_left.sc", "events/skull3d_right.sc",
    "sprites/csol/weapon_skull3_v1.txt", "sprites/csol/weapon_skull3d_v1.txt",
    "sprites/csol/640hud68.spr", "sprites/csol/640hud2.spr", "sprites/csol/640hud6.spr",
    "sound/weapons/skull3-1.wav", "sound/weapons/skull3_clipout.wav", "sound/weapons/skull3_clipin.wav",
    "sound/weapons/skull3_boltpull.wav", "sound/weapons/skull3_idle.wav",
    "sound/weapons/dryfire_rifle.wav",
    "sprites/pistol_smoke1.spr", "sprites/pistol_smoke2.spr",
    "sprites/rifle_smoke1.spr", "sprites/rifle_smoke2.spr", "sprites/rifle_smoke3.spr",
    "sprites/wall_puff1.spr", "sprites/wall_puff2.spr", "sprites/wall_puff3.spr", "sprites/wall_puff4.spr"
};
inline constexpr std::string_view Skull3HUD =
    "3\nweapon 640 csol/640hud68 0 0 170 45\nweapon_s 640 csol/640hud68 0 45 170 45\n"
    "ammo 640 csol/640hud2 49 231 24 24\n";
inline constexpr std::string_view Skull3DualHUD =
    "3\nweapon 640 csol/640hud68 0 90 170 45\nweapon_s 640 csol/640hud68 0 135 170 45\n"
    "ammo 640 csol/640hud2 49 231 24 24\n";
inline constexpr std::string_view Skull3DeathHUD =
    "d_skull3 640 csol/640hud6 172 128 48 16\nd_skull3d 640 csol/640hud6 172 144 48 16\n";
}
