// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "native/guest/cof/magazine_model.hpp"
#include "native/guest/cof/resource_catalog.hpp"
#include "vector.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace csol::server::cof
{
using guest::cof::resources::ResourcePath;
using guest::cof::resources::P345Model;
using guest::cof::resources::P345Models;
using guest::cof::resources::P345Sound;
using guest::cof::resources::P345Sounds;
using guest::cof::resources::P345LocalWaves;
using guest::cof::resources::P345Events;
using guest::cof::resources::P345Inventory;
using guest::cof::resources::P345Particle;
using guest::cof::resources::P345ClipTexture;
using guest::cof::resources::GlockModel;
using guest::cof::resources::GlockModels;
using guest::cof::resources::GlockSound;
using guest::cof::resources::GlockSounds;
using guest::cof::resources::GlockLocalWaves;
using guest::cof::resources::GlockEvents;
using guest::cof::resources::GlockInventory;
using guest::cof::resources::GlockParticle;
using guest::cof::resources::GlockClipTexture;
using guest::cof::resources::RevolverModel;
using guest::cof::resources::RevolverModels;
using guest::cof::resources::RevolverSound;
using guest::cof::resources::RevolverSounds;
using guest::cof::resources::RevolverLocalWaves;
using guest::cof::resources::RevolverEvents;
using guest::cof::resources::RevolverInventory;
using guest::cof::resources::RevolverParticle;
using guest::cof::resources::RevolverClipTexture;
using guest::cof::resources::ShotDecals;
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
struct RevolverResources
{
    std::uint64_t generation{};
    guest::cof::MagazineSequenceTimes sequences{};
    Vector aimOffset{}; // Original forward, right, up fields; not world axes.
    int aimZoom{};
    std::array<int, RevolverModels.size()> models{};
    std::array<int, RevolverSounds.size()> sounds{};
    std::array<int, RevolverLocalWaves.size()> localWaveDownloads{};
    std::array<unsigned short, RevolverEvents.size()> events{};
    int viewModelString{}, playerModelString{}, worldModelString{};
    int inventoryItemString{}, particleScriptString{};
    int inventoryDownload{}, particleDownload{};
};
struct GlockResources
{
    std::uint64_t generation{};
    guest::cof::MagazineSequenceTimes sequences{};
    Vector aimOffset{};
    int aimZoom{};
    std::array<int, GlockModels.size()> models{};
    std::array<int, GlockSounds.size()> sounds{};
    std::array<int, GlockLocalWaves.size()> localWaveDownloads{};
    std::array<unsigned short, GlockEvents.size()> events{};
    int viewModelString{}, playerModelString{}, worldModelString{};
    int inventoryItemString{}, particleScriptString{};
    int inventoryDownload{}, particleDownload{};
    std::array<int, ShotDecals.size()> decalDownloads{};
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
ResourceResult PrepareRevolverResources(const ResourceAuthority&, RevolverResources&) noexcept;
// Same map-only contract, with the actual Glock resources and all43 timings.
ResourceResult PrepareGlockResources(const ResourceAuthority&, GlockResources&) noexcept;
}
