// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace csol::server::cof
{
struct NativeSoundAuthority
{
    void* context{};
    // Caller authenticates map/life/weapon ownership and the immutable policy
    // snapshot before native entity reads and again after engine callbacks.
    bool (*current)(void*) noexcept {};
    std::uint64_t resourceGeneration{};
    bool quietAudio{};       // Original10226489: ordinary WAV volume * .25.
    bool pickupSuppressed{}; // Original10226480: independent pickup-only gate.
};
enum class NativeSoundStatus : unsigned char
{ Emitted, Suppressed, Unsupported, InvalidInput, MissingResource, MissingEngine, Cancelled };
constexpr bool NativeSoundSucceeded(NativeSoundStatus value) noexcept
{ return value==NativeSoundStatus::Emitted||value==NativeSoundStatus::Suppressed; }
}
