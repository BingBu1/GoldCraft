// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/shotgun_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t ShotgunNativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::ShotgunEffect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::ShotgunEffect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::ShotgunEffect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::ShotgunEffect::AmmoPickupSound));

// Original common native consumers only. FireSound uses PlaySFX transport;
// Studio insert/pump sounds and delayed shell tasks have separate consumers.
NativeSoundStatus EmitShotgunNativeSound(const NativeSoundAuthority&, const ShotgunResources&,
    CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::ShotgunEvent&) noexcept;
}
