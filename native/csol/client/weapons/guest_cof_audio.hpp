// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../../guest/cof/client/audio.hpp"

struct GoldCraftConnectionAPI;

namespace csol::client
{
struct GuestCofSoundAsset
{
    // Original consumer keys, after its local/remote rewrite. The remote asset
    // is the namespaced, already precached host path, without "sound/".
    std::string_view localSample, remoteSample, remoteAsset;
    std::span<const std::byte> wave;
    std::uint32_t voices;
};

struct GuestCofAudioPlan
{
    guest::cof::client::AudioLimits limits;
    std::span<const GuestCofSoundAsset> assets;
    void* context{};
    // Cold query against this connection's actual native signon table. Native
    // PCM may remain lazy; local WAV decoding and voice preparation are separate.
    bool (*remotePrecached)(void*, std::string_view asset) noexcept {};
};

void InitializeGuestCofAudio();
void InitializeGuestCofAudio(const GoldCraftConnectionAPI*);
// Only the resource-admission owner may invoke this cold operation. The plan
// and PCM can be released on return; any failure leaves playback inactive.
guest::cof::client::AudioResult PrepareGuestCofAudio(const GuestCofAudioPlan&) noexcept;
bool GuestCofAudioReady() noexcept;
// Studio5004 uses the same prepared local PCM and session/thread checks.
bool PlayGuestCofLocalSound(const guest::cof::client::LocalSound&) noexcept;
void GuestCofAudioFrame() noexcept;
void GuestCofAudioPause(bool paused) noexcept;
// Retire prepared data while preserving thread, connection and message hooks.
void InvalidateGuestCofAudioResources() noexcept;
void ShutdownGuestCofAudio() noexcept;
}
