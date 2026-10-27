// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace csol {
inline constexpr std::array CannonResources{
    "models/v_cannon.mdl", "models/p_cannon.mdl", "models/w_cannon.mdl",
    "sprites/flame_puff01.spr", "sprites/black_smoke4.spr",
    "sprites/pistol_smoke1.spr", "sprites/pistol_smoke2.spr", "sprites/muzzleflash12.spr",
    "sprites/weapon_cannon.txt", "sprites/csol/640hud69.spr", "sprites/csol/640hud2.spr",
    "sprites/csol/640hud6.spr", "sprites/csol/640hud7.spr",
    "sound/weapons/cannon-1.wav", "sound/weapons/cannon_draw.wav"
};
inline constexpr std::string_view CannonHUD=
    "3\nweapon 640 csol/640hud69 0 0 170 45\nweapon_s 640 csol/640hud69 0 45 170 45\nammo 640 csol/640hud2 73 231 24 24\n";
}
