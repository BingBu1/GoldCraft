// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/m16_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t M16NativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::M16Effect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::M16Effect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::M16Effect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::M16Effect::AmmoPickupSound));

// Original common native sounds only. FireSound uses original10116A40 transport;
// delayed player pushback owns its separate emitter and lifetime.
NativeSoundStatus EmitM16NativeSound(const NativeSoundAuthority&, const M16Resources&,
    CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::M16Event&) noexcept;
}
