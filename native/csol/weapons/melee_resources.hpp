// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "melee_attacks.hpp"
#include <array>

namespace csol::melee {

struct Resources {
    const char* classname;
    const char* viewModel;
    const char* playerModel;
    const char* worldModel;
    const char* animationExtension;
    const char* originalHud;
    const char* deathIcon;
    const char* damageConfig;
    float deployDelay;
    float deployIdle;
};

inline constexpr Resources DragonSwordResources{
    "weapon_dragonsword", "models/v_dragonsword.mdl", "models/p_dragonsword.mdl",
    "models/w_dragonsword.mdl", "dragonsword", "sprites/knife_dragonsword.txt",
    "d_dragonsword", "csol/weapons/dragonsword_damage.json", .75f, 1.86f};
inline constexpr Resources SkullAxeResources{
    "weapon_skullaxe", "models/v_skullaxe.mdl", "models/p_skullaxe.mdl",
    "models/w_skullaxe.mdl", "skullaxe", "sprites/knife_skullaxe.txt",
    "d_skullaxe", "csol/weapons/skullaxe_damage.json", .75f, 1.5f};

inline constexpr std::array DragonSwordSounds{
    "weapons/dragonsword_draw.wav", "weapons/dragonsword_slash1.wav",
    "weapons/dragonsword_slash2.wav", "weapons/dragonsword_stab_hit.wav",
    "weapons/dragonsword_hit1.wav", "weapons/dragonsword_hit2.wav",
    "weapons/dragonsword_idle.wav", "weapons/skullaxe_wall.wav"};
inline constexpr std::array SkullAxeSounds{
    "weapons/skullaxe_draw.wav", "weapons/skullaxe_slash1.wav",
    "weapons/skullaxe_slash2.wav", "weapons/skullaxe_miss.wav",
    "weapons/skullaxe_wall.wav", "weapons/skullaxe_hit.wav"};

constexpr const Resources& Resource(Variant variant)
{
    return variant == Variant::DragonSword ? DragonSwordResources : SkullAxeResources;
}

} // namespace csol::melee
