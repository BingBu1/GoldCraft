// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// Original logical keys and authored host paths. Immutable shared input for
// server precache, client cold admission and resource candidate tooling.
namespace guest::cof::resources
{
struct ResourcePath { const char* original; const char* host; };
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

// Original Studio5004 rebuilds these pistol keys as weapons_new/<suffix>.
// Cold dependency discovery must follow the actual local audio consumer rather
// than require a second, unconsumed copy at sound/weapons/<suffix>.
constexpr std::string_view StudioLocalSoundResource(std::string_view model, int event,
                                                   std::string_view sample) noexcept
{
    if (event != 5004 || sample.find("weapons/") == sample.npos) return {};
    const auto separator = sample.find_first_of("/\\");
    if (separator == sample.npos) return {};
    const auto suffix = sample.substr(separator + 1);
    const auto find = [suffix](const auto& waves) -> std::string_view {
        constexpr std::string_view prefix = "weapons_new/";
        for (const auto& wave : waves)
        {
            const std::string_view key = wave.original;
            if (key.starts_with(prefix) && key.substr(prefix.size()) == suffix) return wave.host;
        }
        return {};
    };
    if (model == GlockModels[static_cast<unsigned>(GlockModel::View)].host) return find(GlockLocalWaves);
    if (model == P345Models[static_cast<unsigned>(P345Model::View)].host) return find(P345LocalWaves);
    if (model == RevolverModels[static_cast<unsigned>(RevolverModel::View)].host) return find(RevolverLocalWaves);
    return {};
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
}
