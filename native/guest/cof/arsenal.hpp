// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace guest::cof::arsenal
{
enum Id { Browning=906, Rifle, Axe, Branch, Nightstick, Pickaxe, Switchblade };
struct Definition
{
    Id id;
    const char *key, *classname, *title;
    int category, price, hudSlot, position;
    const char *ammo;
    int clip, reserve, ammoPrice;
    const char *view, *player, *world, *card, *art, *ammoArt;
    int ammoPurchaseCount{};
    constexpr bool Melee() const { return hudSlot == 2; }
};

// The host owns shop prices, slots and model paths. Clip and reserve come
// from each original GetItemInfo. The base pickaxe uses sledgehammer assets.
inline constexpr std::array Items{
    Definition{Browning,"browning","weapon_cof_browning","客串武器·Browning HP 9mm",1,750,1,4,"9mm",13,130,30,
        "models/guest/cof/browning/v_browning.mdl", "models/guest/cof/browning/p_browning.mdl",
        "models/guest/cof/browning/w_browning.mdl", "gfx/csol/buy/cof_browning.tga",
        "resource/csol/hud/v1/cof_browning_weapon.tga", "resource/csol/hud/v1/cof_browning_ammo.tga",13},
    Definition{Rifle,"rifle","weapon_cof_rifle","客串武器·Hunting Rifle",4,3500,0,14,"762Nato",10,50,50,
        "models/guest/cof/rifle/v_rifle.mdl", "models/guest/cof/rifle/p_rifle.mdl",
        "models/guest/cof/rifle/w_rifle.mdl", "gfx/csol/buy/cof_rifle.tga",
        "resource/csol/hud/v1/cof_rifle_weapon.tga", "resource/csol/hud/v1/cof_rifle_ammo.tga",5},
    Definition{Axe,"axe","weapon_cof_axe","客串武器·斧头",9,0,2,3,nullptr,-1,-1,0,
        "models/guest/cof/axe/v_axe.mdl", "models/guest/cof/axe/p_axe.mdl",
        "models/guest/cof/axe/w_axe.mdl", "gfx/csol/buy/cof_axe.tga",
        "resource/csol/hud/v1/cof_axe_weapon.tga", ""},
    Definition{Branch,"branch","weapon_cof_branch","客串武器·树枝",9,0,2,4,nullptr,-1,-1,0,
        "models/guest/cof/branch/v_branch.mdl", "models/guest/cof/branch/p_branch.mdl",
        "models/guest/cof/branch/w_branch.mdl", "gfx/csol/buy/cof_branch.tga",
        "resource/csol/hud/v1/cof_branch_weapon.tga", ""},
    Definition{Nightstick,"nightstick","weapon_cof_nightstick","客串武器·警棍",9,0,2,5,nullptr,-1,-1,0,
        "models/guest/cof/nightstick/v_nightstick.mdl", "models/guest/cof/nightstick/p_nightstick.mdl",
        "models/guest/cof/nightstick/w_nightstick.mdl", "gfx/csol/buy/cof_nightstick.tga",
        "resource/csol/hud/v1/cof_nightstick_weapon.tga", ""},
    Definition{Pickaxe,"sledgehammer","weapon_cof_sledgehammer","客串武器·十字镐",9,0,2,6,nullptr,-1,-1,0,
        "models/guest/cof/sledgehammer/v_sledgehammer.mdl", "models/guest/cof/sledgehammer/p_sledgehammer.mdl",
        "models/guest/cof/sledgehammer/w_sledgehammer.mdl", "gfx/csol/buy/cof_sledgehammer.tga",
        "resource/csol/hud/v1/cof_sledgehammer_weapon.tga", ""},
    Definition{Switchblade,"switchblade","weapon_cof_switchblade","客串武器·弹簧刀",9,0,2,7,nullptr,-1,-1,0,
        "models/guest/cof/switchblade/v_switchblade.mdl", "models/guest/cof/switchblade/p_switchblade.mdl",
        "models/guest/cof/switchblade/w_switchblade.mdl", "gfx/csol/buy/cof_switchblade.tga",
        "resource/csol/hud/v1/cof_switchblade_weapon.tga", ""}
};
constexpr const Definition* Find(int id)
{
    const auto index = static_cast<unsigned>(id) - Browning;
    return index < Items.size() ? &Items[index] : nullptr;
}
inline constexpr char ScopeModel[] = "models/guest/cof/rifle/v_rifle_scope.mdl";
inline constexpr char DamageFile[] = "configs/guest/cof/arsenal_damage.json";
}
