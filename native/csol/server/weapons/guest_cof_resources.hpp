// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "native/guest/cof/magazine_model.hpp"
#include "vector.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace csol::server::cof
{
struct ResourcePath { const char* original; const char* host; };
enum class P345Model : unsigned { View, World, Player, Null, Shell, Clip, Bubble, Fog, Count };
inline constexpr std::array<ResourcePath, static_cast<unsigned>(P345Model::Count)> P345Models{{
    {"models/weapons/p345/v_p345.mdl", "models/guest/cof/p345/v_p345.mdl"},
    {"models/weapons/p345/w_p345.mdl", "models/guest/cof/p345/w_p345.mdl"},
    {"police/models/weapons/p345/p_p345.mdl", "models/guest/cof/p345/p_p345.mdl"},
    {"models/null.mdl", "models/guest/cof/common/null.mdl"},
    {"models/shell.mdl", "models/guest/cof/common/shell.mdl"},
    {"models/w_9mmclip.mdl", "models/guest/cof/common/w_9mmclip.mdl"},
    {"sprites/bubble.spr", "sprites/guest/cof/bubble.spr"},
    {"sprites/fog2.spr", "sprites/guest/cof/fog2.spr"}
}};
enum class P345Sound : unsigned
{
    Click, MagIn, MagOut, MagPlace, RackBack, Slide, Fire, ItemGet, WeaponGet,
    Sleeve1, Sleeve2, Sleeve3, Swing1, Swing2, Swing3, Swing4, Swing5, Swing6,
    PunchSwing, PunchSmack, Count
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
    {"weapons/mobile/punch_smack.wav", "guest/cof/weapons/mobile/punch_smack.wav"}
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

struct ResourceAuthority
{
    void* context{};
    bool (*current)(void*) noexcept {};
    std::uint64_t generation{};
};
struct P345Resources
{
    std::uint64_t generation{};
    guest::cof::MagazineSequenceTimes sequences{};
    Vector aimOffset{}; // Original forward, right, up fields; not world axes.
    int aimZoom{};
    std::array<int, P345Models.size()> models{};
    std::array<int, P345Sounds.size()> sounds{};
    std::array<int, P345LocalWaves.size()> localWaveDownloads{};
    std::array<unsigned short, P345Events.size()> events{};
    int viewModelString{}, playerModelString{}, worldModelString{};
    int inventoryItemString{}, particleScriptString{};
    int inventoryDownload{}, particleDownload{};
};
enum class ResourceStatus : unsigned char
{ Prepared, InvalidAuthority, MissingEngine, MissingFile, InvalidMetadata, InvalidModel, EngineRejected, Cancelled };
struct ResourceResult
{
    ResourceStatus status;
    const char* path{};
    explicit operator bool() const noexcept { return status == ResourceStatus::Prepared; }
};

// Cold authoritative-thread operation. Uses the normal engine VFS/precache and
// its existing dependency/download closure, never a private filesystem cache.
// The caller authenticates map/content generation and caches this value before
// input starts. Failure leaves output unchanged; precache side effects remain.
// This is resource preparation, not P-model grip or effect/backend admission.
ResourceResult PrepareP345Resources(const ResourceAuthority&, P345Resources&) noexcept;
}
