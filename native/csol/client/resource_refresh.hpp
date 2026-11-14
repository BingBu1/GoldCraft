// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef CSOL_RESOURCE_REFRESH
#define CSOL_RESOURCE_REFRESH 0
#endif
#include "../../include/goldcraft/csol_resource_api.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace csol::client {
#if CSOL_RESOURCE_REFRESH
namespace resource_refresh_detail { inline bool readsAllowed=true,readingNewGeneration{}; }
inline bool ResourceReadsAllowed(){return resource_refresh_detail::readsAllowed;}
inline bool ResourceContentReadsAllowed(){return ResourceReadsAllowed()||resource_refresh_detail::readingNewGeneration;}
const goldcraft::csol_resources::Api& ResourceRefreshApi();
#else
inline constexpr bool ResourceReadsAllowed(){return true;}
inline constexpr bool ResourceContentReadsAllowed(){return true;}
#endif
// Default builds retain native GDI registration without transaction storage.
// Enabled refresh builds save exact bytes for a failed-update rollback.
// A null resource means immutable embedded DLL data; a null family is owned by
// the separately reviewed HUD scheme. owner must have static storage duration.
void* RegisterResourceFont(void*& owner,const char* resource,const char* family,
                           std::span<const std::byte> bytes,unsigned long* count);

// Resource owners retire only their presentation data. Hook registrations,
// interfaces and authoritative gameplay state survive this operation.
void InvalidateHudStyleResources();
void InvalidateChallengePhaseResources();
void InvalidateRoundHudResources();
void InvalidateRespawnResources();
void InvalidateCharacterResources();
void InvalidateBuyResources();
void InvalidateMapVoteResources();
void InvalidateDamageResources();
void InvalidateDamageOptionResources();
void InvalidateUpgradeResources();
void InvalidateAbilityResources();
void InvalidateBossResources();
void InvalidateWallResources();
void InvalidateScenarioEffectResources();
void InvalidateSkillEffectResources();
void InvalidateKillResources();
void InvalidateMusicResources();
void InvalidateModelReferences();
const char* ActiveHudResourceScheme();
std::uint64_t DamageCatalogRevision();
bool ReloadDamageResources(std::string& error);
void ResetResourceSession();
struct ResourceFontRecipe {
    std::uint32_t handle{};
    std::string family;
    int tall{},weight{},blur{},scanlines{},flags{},lowRange{},highRange{};
};
struct ResourceFontSource {void** owner{};std::string resource,family;};
struct ResourceFontPreparation {
    std::vector<ResourceFontRecipe> recipes;
    std::vector<ResourceFontSource> sources;
};
// Parse only presentation metadata in the already verified new VFS. Owners
// retain their handles; the VGUI transaction stages changed glyph recipes.
void PrepareCharacterResourceFonts(ResourceFontPreparation&);
void PrepareBuyResourceFonts(ResourceFontPreparation&);
void PrepareMapVoteResourceFonts(ResourceFontPreparation&);
#if CSOL_RESOURCE_REFRESH
bool ResourceFontIsTracked(void** owner);
#else
inline constexpr bool ResourceFontIsTracked(void**){return false;}
#endif
}
