// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
#include "beamgun_resources.hpp"
#include "k3_resources.hpp"
#include "cannon_resources.hpp"
#include "melee_hud_resources.hpp"
#include "skull_balrog_resources.hpp"
#include "skull1_resources.hpp"
#include "skull2_resources.hpp"
#include "skull5_resources.hpp"

namespace csol
{
inline constexpr std::array TKnifeEx2Resources{
    "models/v_tknifeex2.mdl", "models/p_tknifeex2.mdl", "models/w_tknifeex2.mdl", "models/tknifeb.mdl",
    "sprites/smoke.spr",
    "sprites/weapon_tknifeex2.txt", "sprites/csol/640hud97.spr", "sprites/csol/640hud12.spr", "sprites/csol/640hud7_v2.spr",
    "sound/weapons/tknife_shoot1.wav", "sound/weapons/tknifeex2_draw.wav", "sound/weapons/tknifeex2_shoot1.wav",
    "sound/weapons/tknife_stone1.wav", "sound/weapons/tknife_stone2.wav",
    "sound/weapons/tknife_wood1.wav", "sound/weapons/tknife_wood2.wav",
    "sound/weapons/tknife_metal1.wav", "sound/weapons/tknife_metal2.wav", "sound/weapons/tknife_metal3.wav",
    "sound/weapons/combatknife_hit_1.wav", "sound/weapons/axe_hit1.wav", "sound/weapons/axe_hit2.wav"
};
// Keep CSO's ammo and number atlases out of the stock CS namespace.
inline std::string_view WeaponResourceSource(std::string_view path)
{
    if(const auto source=MeleeResourceSource(path);source!=path)return source;
    if(const auto source=Skull1ResourceSource(path);source!=path)return source;
    if(const auto source=Skull2ResourceSource(path);source!=path)return source;
    if(const auto source=Skull5ResourceSource(path);source!=path)return source;
    if(const auto source=SkullBalrogResourceSource(path);source!=path)return source;
    if(path=="sprites/csol/640hud85.spr")return "sprites/640hud85.spr";
    if(path=="sprites/csol/640hud69.spr")return "sprites/640hud69.spr";
    if(path=="sprites/csol/640hud2.spr")return "sprites/640hud2.spr";
    if(path=="sprites/csol/640hud6.spr")return "sprites/640hud6.spr";
    if(path=="sprites/csol/640hud12.spr")return "sprites/640hud12.spr";
    if(path=="sprites/csol/640hud7_v2.spr")return "sprites/640hud7.spr";
    if(path=="sprites/csol/640hud97.spr")return "sprites/640hud97.spr";
    if(path=="sprites/csol/640hud266.spr")return "sprites/640hud266.spr";
    if(path=="sprites/csol/640hud224.spr")return "sprites/640hud224.spr";
    return path;
}
inline constexpr std::string_view TKnifeEx2HUD =
    "3\nweapon 640 csol/640hud97 0 0 170 45\nweapon_s 640 csol/640hud97 0 45 170 45\nammo 640 csol/640hud12 49 231 24 24\n";
inline std::string_view GeneratedWeaponResource(std::string_view path)
{
    if(const auto content=GeneratedMeleeResource(path);!content.empty())return content;
    if(const auto content=GeneratedSkullBalrogResource(path);!content.empty())return content;
    if(path=="sprites/csol/weapon_k3_v2.txt")return K3HUD;
    if(path=="sprites/csol/weapon_skull1_v1.txt")return Skull1HUD;
    if(path=="sprites/csol/weapon_skull2_v1.txt")return Skull2HUD;
    // GoldSrc requires an event-script resource; CSO registers these three
    // events by bare name. Their behavior lives in the compiled handlers.
    if(path=="events/skull2_a.sc"||path=="events/skull2_left.sc"||path=="events/skull2_right.sc")
        return "// SKULL-2 native event handler.\n";
    if(path=="sprites/csol/weapon_skull5_v1.txt")return Skull5HUD;
    if(path=="sprites/weapon_tknifeex2.txt")return TKnifeEx2HUD;
    if(path=="sprites/weapon_beamgun.txt")return BeamgunHUD;
    if(path=="sprites/weapon_beamgunle.txt")return BeamgunLEHUD;
    if(path=="sprites/weapon_cannon.txt")return CannonHUD;
    return {};
}
}
