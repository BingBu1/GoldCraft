// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/g43_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t G43NativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::G43Effect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::G43Effect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::G43Effect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::G43Effect::AmmoPickupSound));

// Original common native sounds only. FireSound uses original10116A40 transport;
// delayed player pushback owns its separate emitter and lifetime.
NativeSoundStatus EmitG43NativeSound(const NativeSoundAuthority&, const G43Resources&,
    CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::G43Event&) noexcept;
}
