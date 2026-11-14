// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/automatic_sound.hpp"
#include <array>

class CBasePlayer;
class CBasePlayerWeapon;

namespace csol::server::cof
{
struct AutomaticNativeSounds
{
    std::uint64_t generation{};
    // Cold-prepared Fire/Tail catalog entries. Paths remain immutable and alive
    // throughout the retained map, including synchronous callback retirement.
    std::array<int, 2> handles{};
    std::array<const char*, 2> paths{};
};

// Actual SDK EmitSound boundary for the original native cooperative branch.
// Includes SND_STOP; does not rescale the controller's final quiet volume.
// Caller owns valid native references on the authoritative thread. Capture
// their serials before invoking authority callbacks that may retire them.
NativeSoundStatus EmitAutomaticNativeSound(const NativeSoundAuthority&, const AutomaticNativeSounds&,
    CBasePlayer&, CBasePlayerWeapon&, const guest::cof::AutomaticSoundEvent&) noexcept;
}
