// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
#include "beamgun_resources.hpp"
#include "cannon_resources.hpp"

namespace csol
{
inline constexpr std::array TKnifeEx2Resources{
    "models/v_tknifeex2.mdl", "models/p_tknifeex2.mdl", "models/w_tknifeex2.mdl", "models/tknifeb.mdl",
    "sprites/smoke.spr",
    "sprites/weapon_tknifeex2.txt", "sprites/csol/640hud97.spr", "sprites/csol/640hud12.spr", "sprites/csol/640hud7.spr",
    "sound/weapons/tknife_shoot1.wav", "sound/weapons/tknifeex2_draw.wav", "sound/weapons/tknifeex2_shoot1.wav",
    "sound/weapons/tknife_stone1.wav", "sound/weapons/tknife_stone2.wav",
    "sound/weapons/tknife_wood1.wav", "sound/weapons/tknife_wood2.wav",
    "sound/weapons/tknife_metal1.wav", "sound/weapons/tknife_metal2.wav", "sound/weapons/tknife_metal3.wav",
    "sound/weapons/combatknife_hit_1.wav", "sound/weapons/axe_hit1.wav", "sound/weapons/axe_hit2.wav"
};
// Keep CSO's ammo and number atlases out of the stock CS namespace.
inline std::string_view WeaponResourceSource(std::string_view path)
{
    if(path=="sprites/csol/640hud69.spr")return "sprites/640hud69.spr";
    if(path=="sprites/csol/640hud2.spr")return "sprites/640hud2.spr";
    if(path=="sprites/csol/640hud6.spr")return "sprites/640hud6.spr";
    if(path=="sprites/csol/640hud12.spr")return "sprites/640hud12.spr";
    if(path=="sprites/csol/640hud7.spr")return "sprites/640hud7.spr";
    if(path=="sprites/csol/640hud97.spr")return "sprites/640hud97.spr";
    if(path=="sprites/csol/640hud266.spr")return "sprites/640hud266.spr";
    if(path=="sprites/csol/640hud224.spr")return "sprites/640hud224.spr";
    return path;
}
inline constexpr std::string_view TKnifeEx2HUD =
    "3\nweapon 640 csol/640hud97 0 0 170 45\nweapon_s 640 csol/640hud97 0 45 170 45\nammo 640 csol/640hud12 49 231 24 24\n";
inline std::string_view GeneratedWeaponResource(std::string_view path)
{
    if(path=="sprites/weapon_tknifeex2.txt")return TKnifeEx2HUD;
    if(path=="sprites/weapon_beamgun.txt")return BeamgunHUD;
    if(path=="sprites/weapon_beamgunle.txt")return BeamgunLEHUD;
    if(path=="sprites/weapon_cannon.txt")return CannonHUD;
    return {};
}
}
