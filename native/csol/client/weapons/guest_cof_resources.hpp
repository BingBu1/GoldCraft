// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_audio.hpp"
#include "guest_cof_particles.hpp"
#include "guest_cof_decals.hpp"
#include <cstdint>

struct GoldCraftConnectionAPI;
struct GoldCraftSoundPrecacheAPI;
struct GoldCraftPrecacheReadyAPI;

namespace csol::client
{
// Authored resource budget, independent of original audio/particle rules.
// There are no implicit defaults or changes to the original particle limit.
struct GuestCofResourceBudget
{
    std::uint32_t voicesPerSample{}, pcmBytes{};
};
enum class GuestCofResourceStatus : unsigned char
{
    Uninitialized, Unavailable, Armed, Preparing, NoAssets, InvalidBudget,
    NativeSoundUnavailable, ResourceFailed, AudioFailed, ParticleFailed,
    Cancelled, Prepared, DecalFailed
};
struct GuestCofResourceState
{
    GuestCofResourceStatus status{GuestCofResourceStatus::Uninitialized};
    std::uint64_t generation{};
    std::uint32_t attempts{}, samples{};
    const char* resource{}; // Immutable catalogue entry, never a VFS pointer.
    guest::cof::client::AudioResult audio{guest::cof::client::AudioResult::Inactive};
    guest::cof::client::AuroraResult particles{guest::cof::client::AuroraResult::NotPrepared};
    // Additional Studio/recoil subset only; never the whole-weapon effect mask.
    bool studioAndRecoilPrepared{};
    bool decalsPrepared{};
};

// Subscribe during cold initialization, after the real consumers are installed.
// Preparation runs once at native signon completion. This establishes cold
// resource preparation, not full weapon/effect readiness.
void InitializeGuestCofResources(GuestCofResourceBudget);
void InitializeGuestCofResources(GuestCofResourceBudget,
    const GoldCraftConnectionAPI*, const GoldCraftSoundPrecacheAPI*, const GoldCraftPrecacheReadyAPI*);
void ShutdownGuestCofResources() noexcept;
// After downstream HUD_VidInit, rebuild particles without restarting audio.
void GuestCofResourcesVideoInit() noexcept;
GuestCofResourceState GuestCofResourcesState() noexcept;
}
