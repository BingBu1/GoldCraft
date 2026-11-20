// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "resource_path.hpp"
#include <array>

namespace guest::cof::resources
{
enum class FamasModel : unsigned { View, World, Player, Null, Shell, Clip, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(FamasModel::Count)> FamasModels{{
    {"models/weapons/famas/v_famas.mdl", "models/guest/cof/famas/v_famas.mdl"},
    {"models/weapons/famas/w_famas.mdl", "models/guest/cof/famas/w_famas.mdl"},
    // The host supplies the calibrated model built from the original world mesh.
    {nullptr, "models/guest/cof/famas/p_famas.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"}
}};
enum class FamasSound : unsigned { LegacyFire, FirstDraw, ItemGet, Fire, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(FamasSound::Count)> FamasSounds{{
    {"weapons/famas/famas_shoot.wav", "guest/cof/weapons/famas/famas_shoot.wav"},
    {"subtitles/french4.wav", "guest/cof/subtitles/french4.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons_new/famas/famas_shoot.wav", "guest/cof/weapons_new/famas/famas_shoot.wav"}
}};
inline constexpr std::array<ResourcePath, 3> FamasReloadSounds{{
    {"weapons/famas/famas_cock.wav", "guest/cof/weapons/famas/famas_cock.wav"},
    {"weapons/famas/famas_magin.wav", "guest/cof/weapons/famas/famas_magin.wav"},
    {"weapons/famas/famas_magout.wav", "guest/cof/weapons/famas/famas_magout.wav"}
}};
inline constexpr std::array<ResourcePath, 4> FamasLocalWaves{{
    {"weapons_new/famas/famas_shoot.wav", "sound/guest/cof/weapons_new/famas/famas_shoot.wav"},
    {"weapons_new/famas/famas_cock.wav", "sound/guest/cof/weapons_new/famas/famas_cock.wav"},
    {"weapons_new/famas/famas_magin.wav", "sound/guest/cof/weapons_new/famas/famas_magin.wav"},
    {"weapons_new/famas/famas_magout.wav", "sound/guest/cof/weapons_new/famas/famas_magout.wav"}
}};
inline constexpr ResourcePath FamasInventory{
    "inventoryitems/weapons/weapon_famas.txt", "inventoryitems/guest/cof/weapons/weapon_famas.txt"};
}
