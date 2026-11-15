// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace guest::cof
{
struct StaminaCycleState
{
    float amount{}, lastSent{}, nextCycle{}, auxiliaryTime{};
    float exhaustionActionDeadline{}, voiceDeadline{}, exhaustionAudioDeadline{}, hudPulse{};
};
struct StaminaCycleOwner
{
    std::uint32_t flags{};
    float horizontalSpeed{}, voiceVariant{};
    int waterLevel{};
    bool infinite{}, alternateDrain{}, alteredCycle{}, rulesPresent{};
};
enum class StaminaVoice : unsigned char { Player, Doctor, Police1, Police2 };
struct StaminaVoiceEvent
{
    StaminaVoice sample;
    int channel;
    float volume, attenuation;
    int flags, pitch;
};
enum class StaminaHudKind : unsigned char { Begin, Byte, Coord, End };
struct StaminaHudEvent
{
    StaminaHudKind kind;
    int integer{};
    float number{}; // Coord is the original pfnWriteCoord argument, not wire float bits.
};
struct StaminaCycleHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*clock)(void*) noexcept {};
    bool (*multiplayer)(void*, bool&) noexcept {};
    bool (*random)(void*, int low, int high, int&) noexcept {};
    // Deliver through the original sound wrapper, including its quiet rule.
    bool (*voice)(void*, const StaminaVoiceEvent&) noexcept {};
    bool (*hud)(void*, const StaminaHudEvent&) noexcept {};
};
enum class StaminaCycleResult : unsigned char { Applied, Cancelled, MissingBackend, InvalidInput };

// Original100F539B..100F56F5. Called at the actual PostThink stamina phase,
// after movement/sprint decisions. No original sprint/FOV/water-tail substitute.
// alternateDrain and alteredCycle name the observed +3BC/+1EE0 branches only;
// their upstream gameplay identities must be supplied by the authoritative owner.
StaminaCycleResult AdvanceStaminaCycle(StaminaCycleState&, StaminaCycleOwner&, const StaminaCycleHost&) noexcept;
// Exact scalar/HUD subsection of Spawn100F468D..100F4724. Other state is retained.
StaminaCycleResult SpawnStaminaCycle(StaminaCycleState&, const StaminaCycleHost&) noexcept;
}
