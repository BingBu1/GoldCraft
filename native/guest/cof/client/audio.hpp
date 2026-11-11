// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "presentation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace guest::cof::client
{
enum class AudioResult
{
    Applied, Inactive, WrongThread, InvalidLimits, InvalidSample, InvalidWave,
    UnsupportedFormat, DuplicateSample, NotPrepared, Capacity, DeviceFailure
};

struct AudioLimits
{
    std::uint32_t samples;
    std::uint32_t voices;
    std::uint32_t pcmBytes;
};

struct AudioProviders
{
    void* context;
    float (*volume)(void*) noexcept;
    float (*randomFloat)(void*, float low, float high) noexcept;
};

struct AudioState
{
    std::uint32_t preparedSamples{}, preparedVoices{}, activeVoices{}, pausedVoices{}, loopingVoices{};
    std::uint32_t retainedPcmBytes{};
    bool ready{}, globallyPaused{}, hasLoopSlot{};
};

struct AudioVoiceState
{
    std::string_view sample;
    float volume{}, speed{};
    float requestedVolume{}, requestedSpeed{}; // Values submitted to XAudio2.
    std::uint32_t buffersQueued{};
    bool active{}, paused{}, looped{}, globalSlot{};
};

// Independent local PlaySFX 2D service. All methods, including destruction,
// belong to the initializing thread. No equipment or player authority lives here.
// XAudio2 is a documented host adaptation; original irrKlang ABI is not reused.
class LocalAudio
{
public:
    LocalAudio() noexcept;
    ~LocalAudio();
    LocalAudio(const LocalAudio&) = delete;
    LocalAudio& operator=(const LocalAudio&) = delete;

    // Cold path: bounded allocations, system device/voices, immutable PCM.
    AudioResult Initialize(AudioLimits limits, AudioProviders providers) noexcept;
    // Keys are already rewritten by Presentation::Sound. RIFF bytes are copied;
    // the caller can release the input after this call. No file access occurs.
    AudioResult Prepare(std::string_view sample, std::span<const std::byte> wave,
                        std::uint32_t simultaneousVoices) noexcept;
    AudioResult Activate() noexcept;

    // Hot path: no application allocations, file access or voice creation.
    // bool/origin are ignored exactly as original10080410. Mode1 is looped and
    // replaces one global slot; mode2 stops that slot; every other mode is an
    // independent one-shot, including duplicate paths.
    AudioResult Play(const LocalSound& request) noexcept;
    AudioResult Update() noexcept; // Refresh only the global slot's volume.
    AudioResult SetPaused(bool paused) noexcept; // Affect currently active voices.
    AudioResult Shutdown() noexcept;

    // Diagnostics query actual XAudio2 voice state; avoid these in draw paths.
    AudioResult Inspect(AudioState& state) noexcept;
    AudioResult InspectVoice(std::uint32_t index, AudioVoiceState& state) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    // Host callbacks may retire/reprepare the service. This identity stays
    // outside Impl so even an allocator reusing its address cannot revive a
    // pending operation's references. Owner-thread only, no atomic hot cost.
    std::uint64_t lifecycle_{1};
    bool dispatching_{};
};
}
