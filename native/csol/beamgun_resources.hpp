// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol
{
// Explicit event dependencies complement the model JSON/animation dependencies.
// The two skins share sounds but have distinct world-effect textures/models.
inline constexpr std::array BeamgunSounds{
    "sound/weapons/beamgun-1.wav", "sound/weapons/beamgun-1_exp1.wav",
    "sound/weapons/beamgun-1_exp2.wav", "sound/weapons/beamgun-2.wav",
    "sound/weapons/beamgun-3.wav", "sound/weapons/beamgun-4.wav",
    "sound/weapons/beamgun-4_exp.wav", "sound/weapons/beamgun-5.wav",
    "sound/weapons/beamgun_jump.wav", "sound/weapons/beamgun_jump_end.wav"
};
inline constexpr std::array BeamgunResources{
    "models/v_beamgun.mdl", "models/p_beamgun.mdl", "models/w_beamgun.mdl",
    "models/ef_beamgun_shootB.mdl", "models/ef_beamgun_shootB2_landx.mdl",
    "models/ef_beamgun_shootB2_exp.mdl", "models/ef_beamgun_shootB2_land.mdl",
    "models/ef_beamgun_jump.mdl", "models/ef_beamgun_jump_land.mdl",
    "models/ef_beamgun_wingman.mdl", "sprites/ef_beamgun_wingman.spr",
    "sprites/ef_beamgun_shootA_hit.spr", "sprites/ef_beamgun_charge_shootA.spr",
    "sprites/ef_beamgun_chargeshootAhit.spr", "sprites/ef_beamgun_shootB2_exp2.spr",
    "sprites/weapon_beamgun.txt", "sprites/csol/640hud266.spr", "sprites/csol/640hud224.spr", "sprites/csol/640hud7.spr",
    "sprites/huntbow_aim_default1.tga", "sprites/huntbow_aim_default2.tga", "sprites/huntbow_aim_default3.tga",
    "sprites/beamgun_gauge_bg.tga", "sprites/beamgun_gauge.tga", "sprites/beamgun_gauge_fx.webm"
};
inline constexpr std::array BeamgunLEResources{
    "models/v_beamgunle.mdl", "models/p_beamgunle.mdl", "models/w_beamgunle.mdl",
    "models/ef_beamgunle_shootB.mdl", "models/ef_beamgunle_shootB2_landx.mdl",
    "models/ef_beamgunle_shootB2_exp.mdl", "models/ef_beamgunle_shootB2_land.mdl",
    "models/ef_beamgunle_jump.mdl", "models/ef_beamgunle_jump_land.mdl",
    "models/ef_beamgunle_wingman.mdl", "sprites/ef_beamgunle_wingman.spr",
    "sprites/ef_beamgunle_shootA_hit.spr", "sprites/ef_beamgunle_charge_shootA.spr",
    "sprites/ef_beamgunle_chargeshootAhit.spr", "sprites/ef_beamgunle_shootB2_exp2.spr",
    "sprites/weapon_beamgunle.txt", "sprites/csol/640hud266.spr", "sprites/csol/640hud224.spr", "sprites/csol/640hud7.spr",
    "sprites/huntbow_aim_default1.tga", "sprites/huntbow_aim_default2.tga", "sprites/huntbow_aim_default3.tga",
    "sprites/beamgunle_gauge_bg.tga", "sprites/beamgunle_gauge.tga", "sprites/beamgunle_gauge_fx.webm"
};
inline constexpr std::string_view BeamgunHUD =
    "5\nweapon 640 csol/640hud266 0 0 170 45\nweapon_s 640 csol/640hud266 0 45 170 45\n"
    "ammo 640 csol/640hud266 1 231 24 24\nammo2 640 csol/640hud224 49 231 24 24\nkill 640 csol/640hud266 172 0 48 16\n";
inline constexpr std::string_view BeamgunLEHUD =
    "5\nweapon 640 csol/640hud266 0 90 170 45\nweapon_s 640 csol/640hud266 0 135 170 45\n"
    "ammo 640 csol/640hud266 1 231 24 24\nammo2 640 csol/640hud224 49 231 24 24\nkill 640 csol/640hud266 172 16 48 16\n";
}
