// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "studio_sound.hpp"
#include "famas_resources.hpp"
#include <array>
#include <cstddef>
#include <string_view>

// Original logical keys and authored host paths. Immutable shared input for
// server precache, client cold admission and resource candidate tooling.
namespace guest::cof::resources
{
// Original1006DE40 server precache only. Client sounds, effects and HUD form a
// separate closure; these core resources never grant firearm admission.
enum class ShotgunCoreModel : unsigned { View, World, Player, Null, Shell, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(ShotgunCoreModel::Count)> ShotgunCoreModels{{
    {"models/weapons/shotgun/v_shotgun.mdl", "models/guest/cof/shotgun/v_shotgun.mdl"},
    {"models/weapons/shotgun/w_shotgun.mdl", "models/guest/cof/shotgun/w_shotgun.mdl"},
    {"police/models/weapons/shotgun/p_shotgun.mdl", "models/guest/cof/shotgun/p_shotgun.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shotgunshell.mdl", "models/guest/cof/common/shotgunshell.mdl"}
}};
enum class ShotgunCoreSound : unsigned { ItemGet, Fire, Insert, PumpForward, PumpBack, PumpSequence, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(ShotgunCoreSound::Count)> ShotgunCoreSounds{{
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/shotgun/shoot.wav", "guest/cof/weapons/shotgun/shoot.wav"},
    {"weapons/shotgun/insert.wav", "guest/cof/weapons/shotgun/insert.wav"},
    {"weapons/shotgun/pump_forward.wav", "guest/cof/weapons/shotgun/pump_forward.wav"},
    {"weapons/shotgun/pump_back.wav", "guest/cof/weapons/shotgun/pump_back.wav"},
    {"weapons/shotgun/pump_seq.wav", "guest/cof/weapons/shotgun/pump_seq.wav"}
}};
inline constexpr ResourcePath ShotgunCoreInventory{
    "inventoryitems/weapons/weapon_shotgun.txt", "inventoryitems/guest/cof/weapons/weapon_shotgun.txt"};
inline constexpr ResourcePath ShotgunCoreParticle{"aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
inline constexpr char CustomDecalMessage[] = "CoFDecal";
// Original10059110 shot resource group; width/height6 and overlap.5 are
// consumed as half-extents3 and overlap1 by10057DA0. Host namespace only.
inline constexpr std::array<ResourcePath, 5> ShotDecals{{
    {"gfx/decals/shot1.tga", "gfx/guest/cof/decals/shot1.tga"},
    {"gfx/decals/shot2.tga", "gfx/guest/cof/decals/shot2.tga"},
    {"gfx/decals/shot3.tga", "gfx/guest/cof/decals/shot3.tga"},
    {"gfx/decals/shot4.tga", "gfx/guest/cof/decals/shot4.tga"},
    {"gfx/decals/shot5.tga", "gfx/guest/cof/decals/shot5.tga"}
}};
enum class P345Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(P345Model::Count)> P345Models{{
    {"models/weapons/p345/v_p345.mdl", "models/guest/cof/p345/v_p345.mdl"},
    {"models/weapons/p345/w_p345.mdl", "models/guest/cof/p345/w_p345.mdl"},
    {"police/models/weapons/p345/p_p345.mdl", "models/guest/cof/p345/p_p345.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
enum class P345Sound : unsigned
{
    Click, MagIn, MagOut, MagPlace, RackBack, Slide, Fire, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
// Engine sound paths omit sound/. Logical original keys remain separate from
// namespaced host assets, including the melee CHAN_VOICE samples.
inline constexpr std::array<ResourcePath, static_cast<unsigned>(P345Sound::Count)> P345Sounds{{
    {"weapons/p345/p345_click.wav", "guest/cof/weapons/p345/p345_click.wav"},
    {"weapons/p345/p345_magin.wav", "guest/cof/weapons/p345/p345_magin.wav"},
    {"weapons/p345/p345_magout.wav", "guest/cof/weapons/p345/p345_magout.wav"},
    {"weapons/p345/p345_magplace.wav", "guest/cof/weapons/p345/p345_magplace.wav"},
    {"weapons/p345/p345_rackback.wav", "guest/cof/weapons/p345/p345_rackback.wav"},
    {"weapons/p345/p345_slide.wav", "guest/cof/weapons/p345/p345_slide.wav"},
    {"weapons/p345/p345_fire.wav", "guest/cof/weapons/p345/p345_fire.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    // P345 also uses virtual +190 -> 1011E4A0, not p345_click.wav.
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
// Studio5004 and PlaySFX local rewrites consume weapons_new keys. These seven
// PCM assets are downloaded generically; this does not create client voices.
inline constexpr std::array<ResourcePath, 7> P345LocalWaves{{
    {"weapons_new/p345/p345_click.wav", "sound/guest/cof/weapons_new/p345/p345_click.wav"},
    {"weapons_new/p345/p345_magin.wav", "sound/guest/cof/weapons_new/p345/p345_magin.wav"},
    {"weapons_new/p345/p345_magout.wav", "sound/guest/cof/weapons_new/p345/p345_magout.wav"},
    {"weapons_new/p345/p345_magplace.wav", "sound/guest/cof/weapons_new/p345/p345_magplace.wav"},
    {"weapons_new/p345/p345_rackback.wav", "sound/guest/cof/weapons_new/p345/p345_rackback.wav"},
    {"weapons_new/p345/p345_slide.wav", "sound/guest/cof/weapons_new/p345/p345_slide.wav"},
    {"weapons_new/p345/p345_fire.wav", "sound/guest/cof/weapons_new/p345/p345_fire.wav"}
}};
inline constexpr std::array<ResourcePath, 2> P345Events{{
    {"events/muzzleflash.sc", "events/guest/cof/muzzleflash.sc"},
    {"events/viewpunch.sc", "events/guest/cof/viewpunch.sc"}
}};
inline constexpr ResourcePath P345Inventory{
    "inventoryitems/weapons/weapon_p345.txt", "inventoryitems/guest/cof/weapons/weapon_p345.txt"};
inline constexpr ResourcePath P345Particle{
    "aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
// Companion of the externally textured clip model. ReHLDS automatically adds
// its generic download; it is a dependency, not a ninth model/40th primary.
inline constexpr ResourcePath P345ClipTexture{
    "models/w_9mmclipT.mdl", "models/guest/cof/common/w_9mmclipT.mdl"};

enum class VP70Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(VP70Model::Count)> VP70Models{{
    {"models/weapons/vp70/v_vp70.mdl", "models/guest/cof/vp70/v_vp70.mdl"},
    {"models/weapons/vp70/w_vp70.mdl", "models/guest/cof/vp70/w_vp70.mdl"},
    {"police/models/weapons/vp70/p_vp70.mdl", "models/guest/cof/vp70/p_vp70.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
enum class VP70Sound : unsigned
{
    Cock, MagIn, MagOut, DrawFirst, Fire, ItemGet, WeaponGet, Sleeve1, Sleeve2, Sleeve3,
    Swing1, Swing2, Swing3, Swing4, Swing5, Swing6, PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(VP70Sound::Count)> VP70Sounds{{
    {"weapons/vp70/vp70_cock.wav", "guest/cof/weapons/vp70/vp70_cock.wav"},
    {"weapons/vp70/vp70_magin.wav", "guest/cof/weapons/vp70/vp70_magin.wav"},
    {"weapons/vp70/vp70_magout.wav", "guest/cof/weapons/vp70/vp70_magout.wav"},
    {"weapons/vp70/vp70_draw_first_seq.wav", "guest/cof/weapons/vp70/vp70_draw_first_seq.wav"},
    {"weapons/vp70/vp70_fire.wav", "guest/cof/weapons/vp70/vp70_fire.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 5> VP70LocalWaves{{
    {"weapons_new/vp70/vp70_cock.wav", "sound/guest/cof/weapons_new/vp70/vp70_cock.wav"},
    {"weapons_new/vp70/vp70_magin.wav", "sound/guest/cof/weapons_new/vp70/vp70_magin.wav"},
    {"weapons_new/vp70/vp70_magout.wav", "sound/guest/cof/weapons_new/vp70/vp70_magout.wav"},
    {"weapons_new/vp70/vp70_draw_first_seq.wav", "sound/guest/cof/weapons_new/vp70/vp70_draw_first_seq.wav"},
    {"weapons_new/vp70/vp70_fire.wav", "sound/guest/cof/weapons_new/vp70/vp70_fire.wav"}
}};
inline constexpr std::array<ResourcePath, 2> VP70Events{{
    {"events/muzzleflash.sc", "events/guest/cof/muzzleflash.sc"},
    {"events/viewpunch.sc", "events/guest/cof/viewpunch.sc"}
}};
inline constexpr ResourcePath VP70Inventory{
    "inventoryitems/weapons/weapon_vp70.txt", "inventoryitems/guest/cof/weapons/weapon_vp70.txt"};
inline constexpr ResourcePath VP70Particle{
    "aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
inline constexpr auto VP70ClipTexture = P345ClipTexture;

enum class TMPModel : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(TMPModel::Count)> TMPModels{{
    {"models/weapons/tmp/v_tmp.mdl", "models/guest/cof/tmp/v_tmp.mdl"},
    {"models/weapons/tmp/w_tmp.mdl", "models/guest/cof/tmp/w_tmp.mdl"},
    {"police/models/weapons/tmp/p_tmp.mdl", "models/guest/cof/tmp/p_tmp.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
// Original1007F070 stores loop/end in1A8/1AC; automatic sound owns their lifecycle.
enum class TMPSound : unsigned
{
    ChargerBack, ChargerForward, MagIn, MagOut, Stock, Fire, Tail, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(TMPSound::Count)> TMPSounds{{
    {"weapons/tmp/tmp_charger_back.wav", "guest/cof/weapons/tmp/tmp_charger_back.wav"},
    {"weapons/tmp/tmp_charger_forward.wav", "guest/cof/weapons/tmp/tmp_charger_forward.wav"},
    {"weapons/tmp/tmp_magin.wav", "guest/cof/weapons/tmp/tmp_magin.wav"},
    {"weapons/tmp/tmp_magout.wav", "guest/cof/weapons/tmp/tmp_magout.wav"},
    {"weapons/tmp/tmp_stock.wav", "guest/cof/weapons/tmp/tmp_stock.wav"},
    {"weapons/tmp/tmp_shoot_loop.wav", "guest/cof/weapons/tmp/tmp_shoot_loop.wav"},
    {"weapons/tmp/tmp_shoot_end.wav", "guest/cof/weapons/tmp/tmp_shoot_end.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
// Only the five original Studio5004 samples use the local weapons_new rewrite.
inline constexpr std::array<ResourcePath, 5> TMPLocalWaves{{
    {"weapons_new/tmp/tmp_charger_back.wav", "sound/guest/cof/weapons_new/tmp/tmp_charger_back.wav"},
    {"weapons_new/tmp/tmp_charger_forward.wav", "sound/guest/cof/weapons_new/tmp/tmp_charger_forward.wav"},
    {"weapons_new/tmp/tmp_magin.wav", "sound/guest/cof/weapons_new/tmp/tmp_magin.wav"},
    {"weapons_new/tmp/tmp_magout.wav", "sound/guest/cof/weapons_new/tmp/tmp_magout.wav"},
    {"weapons_new/tmp/tmp_stock.wav", "sound/guest/cof/weapons_new/tmp/tmp_stock.wav"}
}};
inline constexpr std::array<ResourcePath, 2> TMPEvents{{
    {"events/muzzleflash.sc", "events/guest/cof/muzzleflash.sc"},
    {"events/viewpunch.sc", "events/guest/cof/viewpunch.sc"}
}};
inline constexpr ResourcePath TMPInventory{
    "inventoryitems/weapons/weapon_tmp.txt", "inventoryitems/guest/cof/weapons/weapon_tmp.txt"};
inline constexpr ResourcePath TMPParticle{
    "aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
inline constexpr auto TMPClipTexture = P345ClipTexture;

enum class MP5Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(MP5Model::Count)> MP5Models{{
    {"models/weapons/mp5/v_mp5.mdl", "models/guest/cof/mp5/v_mp5.mdl"},
    {"models/weapons/mp5/w_mp5.mdl", "models/guest/cof/mp5/w_mp5.mdl"},
    // Host authoring input, not an original P. The resource author must supply
    // the separately calibrated P generated from this W; copying W is invalid.
    {"models/weapons/mp5/w_mp5.mdl", "models/guest/cof/mp5/p_mp5.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
// Original global precache1011FD90 registers this through10116990, before
// client1004BD77 selects it. A generic download cannot supply its model index.
inline constexpr std::array<ResourcePath, 1> MP5ClientModels{{
    {"models/flash2.mdl", "models/guest/cof/common/flash2.mdl"}
}};

enum class MP5Sound : unsigned
{
    ChargerBack, ChargerForward, MagIn, MagOut, Fire, Tail, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(MP5Sound::Count)> MP5Sounds{{
    {"weapons/mp5/mp5_charger_back.wav", "guest/cof/weapons/mp5/mp5_charger_back.wav"},
    {"weapons/mp5/mp5_charger_forward.wav", "guest/cof/weapons/mp5/mp5_charger_forward.wav"},
    {"weapons/mp5/mp5_magin.wav", "guest/cof/weapons/mp5/mp5_magin.wav"},
    {"weapons/mp5/mp5_magout.wav", "guest/cof/weapons/mp5/mp5_magout.wav"},
    {"weapons/mp5/mp5_shoot_loop.wav", "guest/cof/weapons/mp5/mp5_shoot_loop.wav"},
    {"weapons/mp5/mp5_shoot_end.wav", "guest/cof/weapons/mp5/mp5_shoot_end.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 4> MP5LocalWaves{{
    {"weapons_new/mp5/mp5_charger_back.wav", "sound/guest/cof/weapons_new/mp5/mp5_charger_back.wav"},
    {"weapons_new/mp5/mp5_charger_forward.wav", "sound/guest/cof/weapons_new/mp5/mp5_charger_forward.wav"},
    {"weapons_new/mp5/mp5_magin.wav", "sound/guest/cof/weapons_new/mp5/mp5_magin.wav"},
    {"weapons_new/mp5/mp5_magout.wav", "sound/guest/cof/weapons_new/mp5/mp5_magout.wav"}
}};
inline constexpr std::array<ResourcePath, 2> MP5Events{{
    {"events/muzzleflash.sc", "events/guest/cof/muzzleflash.sc"},
    {"events/viewpunch.sc", "events/guest/cof/viewpunch.sc"}
}};
inline constexpr ResourcePath MP5Inventory{
    "inventoryitems/weapons/weapon_mp5.txt", "inventoryitems/guest/cof/weapons/weapon_mp5.txt"};
inline constexpr ResourcePath MP5Particle{
    "aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
inline constexpr auto MP5ClipTexture = P345ClipTexture;

enum class M16Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(M16Model::Count)> M16Models{{
    {"models/weapons/m16/v_m16.mdl", "models/guest/cof/m16/v_m16.mdl"},
    {"models/weapons/m16/w_m16.mdl", "models/guest/cof/m16/w_m16.mdl"},
    {"police/models/weapons/m16/p_m16.mdl", "models/guest/cof/m16/p_m16.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/556shell.mdl", "models/guest/cof/common/556shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
// Original local id9 and world M16 P consumers both select flash2.
inline constexpr auto M16ClientModels = MP5ClientModels;
enum class M16Sound : unsigned
{
    Cock, MagIn, MagOut, Fire, MagTap, Cock1, Cock2, Switch,
    ItemGet, WeaponGet, Sleeve1, Sleeve2, Sleeve3, PushSwing, PushHit,
    EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(M16Sound::Count)> M16Sounds{{
    {"weapons/m16/m16_cock.wav", "guest/cof/weapons/m16/m16_cock.wav"},
    {"weapons/m16/m16_magin.wav", "guest/cof/weapons/m16/m16_magin.wav"},
    {"weapons/m16/m16_magout.wav", "guest/cof/weapons/m16/m16_magout.wav"},
    {"weapons/m16/m16_fire.wav", "guest/cof/weapons/m16/m16_fire.wav"},
    {"weapons/m16/m16_magtap.wav", "guest/cof/weapons/m16/m16_magtap.wav"},
    {"weapons/m16/m16_cock1.wav", "guest/cof/weapons/m16/m16_cock1.wav"},
    {"weapons/m16/m16_cock2.wav", "guest/cof/weapons/m16/m16_cock2.wav"},
    {"weapons/m16/m16_switch.wav", "guest/cof/weapons/m16/m16_switch.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"weapons/melee_swing.wav", "guest/cof/weapons/melee_swing.wav"},
    {"weapons/melee_hit.wav", "guest/cof/weapons/melee_hit.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 7> M16LocalWaves{{
    {"weapons_new/m16/m16_magin.wav", "sound/guest/cof/weapons_new/m16/m16_magin.wav"},
    {"weapons_new/m16/m16_magout.wav", "sound/guest/cof/weapons_new/m16/m16_magout.wav"},
    {"weapons_new/m16/m16_fire.wav", "sound/guest/cof/weapons_new/m16/m16_fire.wav"},
    {"weapons_new/m16/m16_magtap.wav", "sound/guest/cof/weapons_new/m16/m16_magtap.wav"},
    {"weapons_new/m16/m16_cock1.wav", "sound/guest/cof/weapons_new/m16/m16_cock1.wav"},
    {"weapons_new/m16/m16_cock2.wav", "sound/guest/cof/weapons_new/m16/m16_cock2.wav"},
    {"weapons_new/m16/m16_switch.wav", "sound/guest/cof/weapons_new/m16/m16_switch.wav"}
}};
inline constexpr auto M16Events = MP5Events;
inline constexpr ResourcePath M16Inventory{
    "inventoryitems/weapons/weapon_m16.txt", "inventoryitems/guest/cof/weapons/weapon_m16.txt"};
inline constexpr auto M16Particle = MP5Particle;
inline constexpr auto M16ClipTexture = P345ClipTexture;

enum class G43Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(G43Model::Count)> G43Models{{
    {"models/weapons/g43/v_g43.mdl", "models/guest/cof/g43/v_g43.mdl"},
    {"models/weapons/g43/w_g43.mdl", "models/guest/cof/g43/w_g43.mdl"},
    {"police/models/weapons/g43/p_g43.mdl", "models/guest/cof/g43/p_g43.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
// Original local26 selects flash2/scale6; world G43 P keeps flash1/scale3.
inline constexpr auto G43ClientModels = MP5ClientModels;
enum class G43Sound : unsigned
{
    Fire, Cock, MagIn, MagOut, ItemGet, WeaponGet, Sleeve1, Sleeve2, Sleeve3,
    PushSwing, PushHit, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(G43Sound::Count)> G43Sounds{{
    {"weapons/g43/g43_fire.wav", "guest/cof/weapons/g43/g43_fire.wav"},
    {"weapons/g43/g43_cock.wav", "guest/cof/weapons/g43/g43_cock.wav"},
    {"weapons/g43/g43_magin.wav", "guest/cof/weapons/g43/g43_magin.wav"},
    {"weapons/g43/g43_magout.wav", "guest/cof/weapons/g43/g43_magout.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"weapons/melee_swing.wav", "guest/cof/weapons/melee_swing.wav"},
    {"weapons/melee_hit.wav", "guest/cof/weapons/melee_hit.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 4> G43LocalWaves{{
    {"weapons_new/g43/g43_fire.wav", "sound/guest/cof/weapons_new/g43/g43_fire.wav"},
    {"weapons_new/g43/g43_cock.wav", "sound/guest/cof/weapons_new/g43/g43_cock.wav"},
    {"weapons_new/g43/g43_magin.wav", "sound/guest/cof/weapons_new/g43/g43_magin.wav"},
    {"weapons_new/g43/g43_magout.wav", "sound/guest/cof/weapons_new/g43/g43_magout.wav"}
}};
inline constexpr auto G43Events = MP5Events;
inline constexpr ResourcePath G43Inventory{
    "inventoryitems/weapons/weapon_g43.txt", "inventoryitems/guest/cof/weapons/weapon_g43.txt"};
inline constexpr auto G43Particle = MP5Particle;
inline constexpr auto G43ClipTexture = P345ClipTexture;

// Full ordinary 870 consumers. Its shell and flash differ from the pistols;
// neither the 9mm magazine texture nor flash2 belongs to this closure.
enum class ShotgunModel : unsigned { View, World, Player, Null, Shell, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(ShotgunModel::Count)> ShotgunModels{{
    {"models/weapons/shotgun/v_shotgun.mdl", "models/guest/cof/shotgun/v_shotgun.mdl"},
    {"models/weapons/shotgun/w_shotgun.mdl", "models/guest/cof/shotgun/w_shotgun.mdl"},
    {"police/models/weapons/shotgun/p_shotgun.mdl", "models/guest/cof/shotgun/p_shotgun.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shotgunshell.mdl", "models/guest/cof/common/shotgunshell.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
enum class ShotgunSound : unsigned
{
    Fire, Insert, PumpForward, PumpBack, PumpSequence, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, PushSwing, PushHit, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(ShotgunSound::Count)> ShotgunSounds{{
    {"weapons/shotgun/shoot.wav", "guest/cof/weapons/shotgun/shoot.wav"},
    {"weapons/shotgun/insert.wav", "guest/cof/weapons/shotgun/insert.wav"},
    {"weapons/shotgun/pump_forward.wav", "guest/cof/weapons/shotgun/pump_forward.wav"},
    {"weapons/shotgun/pump_back.wav", "guest/cof/weapons/shotgun/pump_back.wav"},
    {"weapons/shotgun/pump_seq.wav", "guest/cof/weapons/shotgun/pump_seq.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"weapons/melee_swing.wav", "guest/cof/weapons/melee_swing.wav"},
    {"weapons/melee_hit.wav", "guest/cof/weapons/melee_hit.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 5> ShotgunLocalWaves{{
    {"weapons_new/shotgun/shoot.wav", "sound/guest/cof/weapons_new/shotgun/shoot.wav"},
    {"weapons_new/shotgun/insert.wav", "sound/guest/cof/weapons_new/shotgun/insert.wav"},
    {"weapons_new/shotgun/pump_forward.wav", "sound/guest/cof/weapons_new/shotgun/pump_forward.wav"},
    {"weapons_new/shotgun/pump_back.wav", "sound/guest/cof/weapons_new/shotgun/pump_back.wav"},
    {"weapons_new/shotgun/pump_seq.wav", "sound/guest/cof/weapons_new/shotgun/pump_seq.wav"}
}};
// The original draw requests a missing weapons_new/syringe/syringe_sleeve.wav.
// Original10080410 returns on a null play2D result; do not substitute the
// existing legacy WAV. Full Studio ownership/dependency handling is separate.
inline constexpr auto ShotgunEvents = P345Events;
inline constexpr auto ShotgunInventory = ShotgunCoreInventory;
inline constexpr auto ShotgunParticle = ShotgunCoreParticle;

enum class GlockModel : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(GlockModel::Count)> GlockModels{{
    {"models/weapons/glock/v_glock.mdl", "models/guest/cof/glock/v_glock.mdl"},
    {"models/weapons/glock/w_glock.mdl", "models/guest/cof/glock/w_glock.mdl"},
    {"police/models/weapons/glock/p_glock.mdl", "models/guest/cof/glock/p_glock.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
enum class GlockSound : unsigned
{
    Switch, MagIn, MagOut, MagPlace, RackBack, Slide, Fire, RifleInsert, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(GlockSound::Count)> GlockSounds{{
    {"weapons/glock/glock_switch.wav", "guest/cof/weapons/glock/glock_switch.wav"},
    {"weapons/glock/glock_magin.wav", "guest/cof/weapons/glock/glock_magin.wav"},
    {"weapons/glock/glock_magout.wav", "guest/cof/weapons/glock/glock_magout.wav"},
    {"weapons/glock/glock_magplace.wav", "guest/cof/weapons/glock/glock_magplace.wav"},
    {"weapons/glock/glock_rackback.wav", "guest/cof/weapons/glock/glock_rackback.wav"},
    {"weapons/glock/glock_slide.wav", "guest/cof/weapons/glock/glock_slide.wav"},
    {"weapons/glock/glock_fire.wav", "guest/cof/weapons/glock/glock_fire.wav"},
    {"weapons/rifle/rifle_insert.wav", "guest/cof/weapons/rifle/rifle_insert.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    // Original virtual +190 -> 1011E4A0 selects these by weapon+1B4.
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 7> GlockLocalWaves{{
    {"weapons_new/glock/glock_switch.wav", "sound/guest/cof/weapons_new/glock/glock_switch.wav"},
    {"weapons_new/glock/glock_magin.wav", "sound/guest/cof/weapons_new/glock/glock_magin.wav"},
    {"weapons_new/glock/glock_magout.wav", "sound/guest/cof/weapons_new/glock/glock_magout.wav"},
    {"weapons_new/glock/glock_magplace.wav", "sound/guest/cof/weapons_new/glock/glock_magplace.wav"},
    {"weapons_new/glock/glock_rackback.wav", "sound/guest/cof/weapons_new/glock/glock_rackback.wav"},
    {"weapons_new/glock/glock_slide.wav", "sound/guest/cof/weapons_new/glock/glock_slide.wav"},
    {"weapons_new/glock/glock_fire.wav", "sound/guest/cof/weapons_new/glock/glock_fire.wav"}
}};

enum class RevolverModel : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Flash, CheapSmoke, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(RevolverModel::Count)> RevolverModels{{
    {"models/weapons/revolver/v_revolver.mdl", "models/guest/cof/revolver/v_revolver.mdl"},
    {"models/weapons/revolver/w_revolver.mdl", "models/guest/cof/revolver/w_revolver.mdl"},
    {"police/models/weapons/revolver/p_revolver.mdl", "models/guest/cof/revolver/p_revolver.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"},
    {"models/flash1.mdl", "models/guest/cof/common/flash1.mdl"},
    {"sprites/gunsmoke.spr", "sprites/guest/cof/gunsmoke.spr"}
}};
enum class RevolverSound : unsigned
{
    Fire, Shell, CylinderOpen, Insert, CylinderClose, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, EmptyClick, EmptyNoClick, Count
};
inline constexpr std::array<ResourcePath, static_cast<unsigned>(RevolverSound::Count)> RevolverSounds{{
    {"weapons/revolver/revolver_fire.wav", "guest/cof/weapons/revolver/revolver_fire.wav"},
    {"weapons/revolver/revolver_shell.wav", "guest/cof/weapons/revolver/revolver_shell.wav"},
    {"weapons/revolver/revolver_cylinder_open.wav", "guest/cof/weapons/revolver/revolver_cylinder_open.wav"},
    {"weapons/revolver/revolver_insert.wav", "guest/cof/weapons/revolver/revolver_insert.wav"},
    {"weapons/revolver/revolver_cylinder_close.wav", "guest/cof/weapons/revolver/revolver_cylinder_close.wav"},
    {"weapons/item_get.wav", "guest/cof/weapons/item_get.wav"},
    {"weapons/weapon_get.wav", "guest/cof/weapons/weapon_get.wav"},
    {"weapons/sleeve_generic1.wav", "guest/cof/weapons/sleeve_generic1.wav"},
    {"weapons/sleeve_generic2.wav", "guest/cof/weapons/sleeve_generic2.wav"},
    {"weapons/sleeve_generic3.wav", "guest/cof/weapons/sleeve_generic3.wav"},
    {"player/police/Swing/Swing1.wav", "guest/cof/player/police/Swing/Swing1.wav"},
    {"player/police/Swing/Swing2.wav", "guest/cof/player/police/Swing/Swing2.wav"},
    {"player/police/Swing/Swing3.wav", "guest/cof/player/police/Swing/Swing3.wav"},
    {"player/police/Swing/Swing4.wav", "guest/cof/player/police/Swing/Swing4.wav"},
    {"player/police/Swing/Swing5.wav", "guest/cof/player/police/Swing/Swing5.wav"},
    {"player/police/Swing/Swing6.wav", "guest/cof/player/police/Swing/Swing6.wav"},
    {"weapons/mobile/punch_swing.wav", "guest/cof/weapons/mobile/punch_swing.wav"},
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"},
    {"weapons/weapon_fire_empty.wav", "guest/cof/weapons/weapon_fire_empty.wav"},
    {"weapons/weapon_fire_empty_noclick.wav", "guest/cof/weapons/weapon_fire_empty_noclick.wav"}
}};
inline constexpr std::array<ResourcePath, 4> RevolverLocalWaves{{
    {"weapons_new/revolver/revolver_fire.wav", "sound/guest/cof/weapons_new/revolver/fire.wav"},
    {"weapons_new/revolver/revolver_cylinder_open.wav", "sound/guest/cof/weapons_new/revolver/cylinder_open.wav"},
    {"weapons_new/revolver/revolver_insert.wav", "sound/guest/cof/weapons_new/revolver/insert.wav"},
    {"weapons_new/revolver/revolver_cylinder_close.wav", "sound/guest/cof/weapons_new/revolver/cylinder_close.wav"}
}};
inline constexpr std::array<ResourcePath, 3> RevolverEvents{{
    {"events/muzzleflash.sc", "events/guest/cof/muzzleflash.sc"},
    {"events/viewpunch.sc", "events/guest/cof/viewpunch.sc"},
    {"events/revolvershells.sc", "events/guest/cof/revolvershells.sc"}
}};
inline constexpr ResourcePath RevolverInventory{
    "inventoryitems/weapons/weapon_revolver.txt", "inventoryitems/guest/cof/weapons/weapon_revolver.txt"};
inline constexpr ResourcePath RevolverParticle{
    "aurora/gunsmoke.aur", "aurora/guest/cof/gunsmoke.aur"};
inline constexpr auto RevolverClipTexture = P345ClipTexture;

// Original Studio5004 rebuilds these firearm keys as weapons_new/<suffix>.
// Cold dependency discovery must follow the actual local audio consumer rather
// than require a second, unconsumed copy at sound/weapons/<suffix>.
constexpr StudioSoundDependency StudioLocalSoundDependency(std::string_view model, int event,
                                                         std::string_view sample) noexcept
{
    if (event != 5004 || sample.find("weapons/") == sample.npos) return {};
    const auto separator = sample.find_first_of("/\\");
    if (separator == sample.npos) return {};
    const auto suffix = sample.substr(separator + 1);
    // Original client10080410 returns after the absent rewritten draw sample
    // fails to load. Its legacy WAV is not a dependency of that consumer.
    if (model == ShotgunModels[static_cast<unsigned>(ShotgunModel::View)].host &&
        sample == "weapons/syringe/syringe_sleeve.wav")
        return {StudioSoundKind::OriginalMissing};
    const auto find = [suffix](const auto& waves) -> StudioSoundDependency {
        constexpr std::string_view prefix = "weapons_new/";
        for (const auto& wave : waves)
        {
            const std::string_view key = wave.original;
            if (key.starts_with(prefix) && key.substr(prefix.size()) == suffix)
                return {StudioSoundKind::LocalResource, wave.host};
        }
        return {};
    };
    if (model == FamasModels[static_cast<unsigned>(FamasModel::View)].host)
        return suffix == "m16/m16_switch.wav" ? find(M16LocalWaves) : find(FamasLocalWaves);
    if (model == GlockModels[static_cast<unsigned>(GlockModel::View)].host) return find(GlockLocalWaves);
    if (model == P345Models[static_cast<unsigned>(P345Model::View)].host) return find(P345LocalWaves);
    if (model == RevolverModels[static_cast<unsigned>(RevolverModel::View)].host) return find(RevolverLocalWaves);
    if (model == VP70Models[static_cast<unsigned>(VP70Model::View)].host) return find(VP70LocalWaves);
    if (model == TMPModels[static_cast<unsigned>(TMPModel::View)].host) return find(TMPLocalWaves);
    if (model == MP5Models[static_cast<unsigned>(MP5Model::View)].host) return find(MP5LocalWaves);
    if (model == M16Models[static_cast<unsigned>(M16Model::View)].host) return find(M16LocalWaves);
    if (model == G43Models[static_cast<unsigned>(G43Model::View)].host) return find(G43LocalWaves);
    if (model == ShotgunModels[static_cast<unsigned>(ShotgunModel::View)].host) return find(ShotgunLocalWaves);
    return {};
}
constexpr std::string_view StudioLocalSoundResource(std::string_view model, int event,
                                                   std::string_view sample) noexcept
{
    return StudioLocalSoundDependency(model, event, sample).resource;
}
inline constexpr auto GlockEvents = P345Events;
inline constexpr ResourcePath GlockInventory{
    "inventoryitems/weapons/weapon_glock.txt", "inventoryitems/guest/cof/weapons/weapon_glock.txt"};
inline constexpr auto GlockParticle = P345Particle;
inline constexpr auto GlockClipTexture = P345ClipTexture;

struct LocalSoundResource
{
    std::string_view localKey, remoteKey, localFile, remoteAsset;
};

// Resolve the original local weapons_new/ and remote weapons/ pairing at
// compile time. Input and playback never search the server resource catalogue.
template<std::size_t LocalCount, std::size_t NativeCount>
consteval auto AudioResources(const std::array<ResourcePath, LocalCount>& local,
                              const std::array<ResourcePath, NativeCount>& native)
{
    std::array<LocalSoundResource, LocalCount> result{};
    for (std::size_t i = 0; i < local.size(); ++i)
    {
        const std::string_view key = local[i].original;
        const std::string_view file = local[i].host;
        if (!key.starts_with("weapons_new/") || !file.starts_with("sound/guest/cof/"))
            throw "Invalid local CoF sound mapping";
        unsigned matches{};
        for (const auto& entry : native)
        {
            const std::string_view original = entry.original;
            if (original.starts_with("weapons/") && original.substr(8) == key.substr(12))
            {
                result[i] = {key, original, file, entry.host};
                ++matches;
            }
        }
        if (matches != 1) throw "Missing or ambiguous native CoF sound mapping";
    }
    return result;
}
inline constexpr auto P345AudioResources = AudioResources(P345LocalWaves, P345Sounds);
inline constexpr auto GlockAudioResources = AudioResources(GlockLocalWaves, GlockSounds);
inline constexpr auto RevolverAudioResources = AudioResources(RevolverLocalWaves, RevolverSounds);
inline constexpr auto VP70AudioResources = AudioResources(VP70LocalWaves, VP70Sounds);
inline constexpr auto TMPAudioResources = AudioResources(TMPLocalWaves, TMPSounds);
inline constexpr auto MP5AudioResources = AudioResources(MP5LocalWaves, MP5Sounds);
inline constexpr auto M16AudioResources = AudioResources(M16LocalWaves, M16Sounds);
inline constexpr auto FamasAudioResources = AudioResources(FamasLocalWaves, std::array{
    FamasSounds[static_cast<unsigned>(FamasSound::LegacyFire)],
    FamasReloadSounds[0], FamasReloadSounds[1], FamasReloadSounds[2]});
inline constexpr auto G43AudioResources = AudioResources(G43LocalWaves, G43Sounds);
inline constexpr auto ShotgunAudioResources = AudioResources(ShotgunLocalWaves, ShotgunSounds);
// Actual engine resource_t/PF_precache_generic ABI includes the terminating NUL.
template<std::size_t N>
consteval bool ResourceNamesFit(const std::array<ResourcePath,N>& entries)
{
    for(const auto& entry:entries)if(std::string_view(entry.host).size()>=64)return false;
    return true;
}
static_assert(ResourceNamesFit(P345Models)&&ResourceNamesFit(P345Sounds)&&ResourceNamesFit(P345LocalWaves)&&ResourceNamesFit(P345Events));
static_assert(ResourceNamesFit(GlockModels)&&ResourceNamesFit(GlockSounds)&&ResourceNamesFit(GlockLocalWaves)&&ResourceNamesFit(GlockEvents));
static_assert(ResourceNamesFit(RevolverModels)&&ResourceNamesFit(RevolverSounds)&&ResourceNamesFit(RevolverLocalWaves)&&ResourceNamesFit(RevolverEvents));
static_assert(ResourceNamesFit(TMPModels)&&ResourceNamesFit(TMPSounds)&&ResourceNamesFit(TMPLocalWaves)&&ResourceNamesFit(TMPEvents));
static_assert(ResourceNamesFit(G43Models)&&ResourceNamesFit(G43ClientModels)&&ResourceNamesFit(G43Sounds)&&ResourceNamesFit(G43LocalWaves)&&ResourceNamesFit(G43Events));
static_assert(ResourceNamesFit(ShotgunModels)&&ResourceNamesFit(ShotgunSounds)&&ResourceNamesFit(ShotgunLocalWaves)&&ResourceNamesFit(ShotgunEvents));
static_assert(ResourceNamesFit(M16Models)&&ResourceNamesFit(M16ClientModels)&&ResourceNamesFit(M16Sounds)&&ResourceNamesFit(M16LocalWaves)&&ResourceNamesFit(M16Events));
static_assert(ResourceNamesFit(MP5Models)&&ResourceNamesFit(MP5ClientModels)&&ResourceNamesFit(MP5Sounds)&&ResourceNamesFit(MP5LocalWaves)&&ResourceNamesFit(MP5Events));
static_assert(ResourceNamesFit(VP70Models)&&ResourceNamesFit(VP70Sounds)&&ResourceNamesFit(VP70LocalWaves)&&ResourceNamesFit(VP70Events));
}
