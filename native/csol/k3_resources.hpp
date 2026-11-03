// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
inline constexpr std::array K3Resources{
    "models/v_k3.mdl", "models/p_k3.mdl", "models/w_k3.mdl", "models/rshell.mdl",
    "events/k3.sc", "sprites/csol/weapon_k3_v2.txt", "sprites/csol/640hud85.spr",
    "sprites/csol/640hud7_v2.spr", "sprites/csol/640hud11.spr",
    "sound/weapons/K3-1.wav", "sound/weapons/K3_clipout1.wav", "sound/weapons/K3_clipout2.wav",
    "sound/weapons/K3_clipin1.wav", "sound/weapons/K3_clipin2.wav", "sound/weapons/K3_clipin3.wav"
};
inline constexpr std::string_view K3HUD =
    "3\nweapon 640 csol/640hud85 0 90 170 45\nweapon_s 640 csol/640hud85 0 135 170 45\n"
    "ammo 640 csol/640hud7_v2 0 96 24 24\n";
}
