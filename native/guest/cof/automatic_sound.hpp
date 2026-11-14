// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace guest::cof
{
// Process-local original weapon+1A5 state, owned by one authenticated item.
struct AutomaticSoundState { bool playing{}; };
struct AutomaticSoundFrame
{
    int clip{}, action{};
    bool loading{}, ownerAlive{}, attackHeld{}, nativeMultiplayer{}, quietAudio{};
    // Original nonzero sound-string references, not implicit resource readiness.
    bool hasFire{}, hasTail{};
};
enum class AutomaticSoundSample : std::uint8_t { Fire, Tail };
struct AutomaticSoundEvent
{
    AutomaticSoundSample sample{};
    int channel{}, flags{}, pitch{};
    float volume{}, attenuation{};
};
struct AutomaticSoundHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    // Resolve the cold-prepared sample and emit these final native parameters.
    // Quiet scaling has already been applied; do not apply it a second time.
    bool (*event)(void*, const AutomaticSoundEvent&) noexcept {};
};
enum class AutomaticSoundResult : std::uint8_t
{
    Unchanged, Started, Stopped, InvalidInput, UnsupportedRules, MissingHost, Cancelled
};

// Original TMP1007F788/MP51005BA23 native branch. Called only after a real
// successful shot; this function does not authorize or simulate that shot.
AutomaticSoundResult StartAutomaticNativeSound(AutomaticSoundState&, const AutomaticSoundFrame&,
    const AutomaticSoundHost&) noexcept;
// Ordinary (non-dual) original base PostFrame1011D6BA..1011D81B audio stage.
// Invoke at that stage, before the later common reload/input arbitration.
AutomaticSoundResult AdvanceAutomaticNativeSound(AutomaticSoundState&, const AutomaticSoundFrame&,
    const AutomaticSoundHost&) noexcept;
}
