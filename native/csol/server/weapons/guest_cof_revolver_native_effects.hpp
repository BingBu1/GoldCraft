// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/revolver_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t RevolverNativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::RevolverEffect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::RevolverEffect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::RevolverEffect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::RevolverEffect::AmmoPickupSound));
NativeSoundStatus EmitRevolverNativeSound(const NativeSoundAuthority&,
    const RevolverResources&,CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::RevolverEvent&) noexcept;
}
