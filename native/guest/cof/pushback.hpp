// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace guest::cof
{
using PushbackVector = std::array<float, 3>;
// An authenticated in-process target token, never a wire ID or a CS offset.
using PushbackTarget = std::uintptr_t;
enum class PushbackSample : unsigned char { Swing, Hit };
enum class PushbackResult : unsigned char
{ Applied, Ineligible, Cancelled, MissingBackend, InvalidInput };

struct PushbackSound
{
    PushbackTarget emitter;
    PushbackSample sample;
    int channel;
    float volume, attenuation;
    int flags, pitch;
};
struct PushbackDamage
{
    float amount;
    // Describes the original consumer. The SDK adapter must translate its
    // flinch/melee semantics; these are NOT CS TakeDamage bit flags.
    std::uint32_t originalBits;
};
struct PushbackHost
{
    void* context{};
    PushbackTarget owner{};
    bool (*current)(void*) noexcept {};
    bool (*stamina)(void*, float&) noexcept {};
    bool (*infiniteStamina)(void*, bool&) noexcept {};
    bool (*setStamina)(void*, float) noexcept {};
    // Preserve the original sound wrapper's current quiet-audio rule here.
    bool (*sound)(void*, const PushbackSound&) noexcept {};
    bool (*makeVectors)(void*) noexcept {};
    bool (*gunPosition)(void*, PushbackVector&) noexcept {};
    bool (*recoil)(void*, const PushbackVector&) noexcept {};
    // Engine sphere iteration in engine order, including self. Zero ends it.
    bool (*next)(void*, PushbackTarget previous, const PushbackVector&, float,
                 PushbackTarget&) noexcept {};
    bool (*monster)(void*, PushbackTarget, PushbackTarget&) noexcept {};
    bool (*alive)(void*, PushbackTarget, bool&) noexcept {};
    // Return delivery success, not the target's TakeDamage result. Original
    // hit audio follows TakeDamage even when the target reports no damage.
    bool (*damage)(void*, PushbackTarget, const PushbackDamage&) noexcept {};
};

// Original CoF hl.dll 0036b91c /100E8B40, after its player-owned .23F task.
// Scheduling, authoritative stamina ownership and target leases belong to the
// host. This operation neither needs nor revokes an active weapon binding.
PushbackResult LongGunPushback(const PushbackHost&) noexcept;
}
