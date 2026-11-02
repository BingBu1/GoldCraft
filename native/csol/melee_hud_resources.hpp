// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace csol {
// CSO knife_*.txt has one card and a death-notice symbol, with no ammo row.
// Adapt that same card to both CS selection roles; do not invent another crop.
inline constexpr std::string_view DragonSwordHUD =
    "2\nweapon 640 csol/640hud74 0 180 170 45\nweapon_s 640 csol/640hud74 0 180 170 45\n";
inline constexpr std::string_view SkullAxeHUD =
    "2\nweapon 640 csol/640hud54 0 180 170 45\nweapon_s 640 csol/640hud54 0 180 170 45\n";
inline constexpr std::array DragonSwordHudResources{
    "sprites/knife_dragonsword.txt", "sprites/weapon_dragonsword.txt",
    "sprites/csol/640hud74.spr", "sprites/csol/640hud10.spr"};
inline constexpr std::array SkullAxeHudResources{
    "sprites/knife_skullaxe.txt", "sprites/weapon_skullaxe.txt",
    "sprites/csol/640hud54.spr", "sprites/csol/640hud4.spr"};
inline std::string_view MeleeResourceSource(std::string_view path)
{
    if(path=="sprites/weapon_dragonsword.txt")return "sprites/knife_dragonsword.txt";
    if(path=="sprites/weapon_skullaxe.txt")return "sprites/knife_skullaxe.txt";
    if(path=="sprites/csol/640hud74.spr")return "sprites/640hud74.spr";
    if(path=="sprites/csol/640hud10.spr")return "sprites/640hud10.spr";
    if(path=="sprites/csol/640hud54.spr")return "sprites/640hud54.spr";
    if(path=="sprites/csol/640hud4.spr")return "sprites/640hud4.spr";
    return path;
}
inline std::string_view GeneratedMeleeResource(std::string_view path)
{
    if(path=="sprites/weapon_dragonsword.txt")return DragonSwordHUD;
    if(path=="sprites/weapon_skullaxe.txt")return SkullAxeHUD;
    return {};
}
} // namespace csol
