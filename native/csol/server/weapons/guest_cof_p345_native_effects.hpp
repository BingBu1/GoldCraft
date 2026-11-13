// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/p345_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t P345NativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::P345Effect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::P345Effect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::P345Effect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::P345Effect::AmmoPickupSound));
NativeSoundStatus EmitP345NativeSound(const NativeSoundAuthority&,
    const P345Resources&,CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::P345Event&) noexcept;
}
