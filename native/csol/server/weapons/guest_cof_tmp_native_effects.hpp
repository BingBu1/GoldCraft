// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/tmp_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t TMPNativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::TMPEffect::AutomaticSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::TMPEffect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::TMPEffect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::TMPEffect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::TMPEffect::AmmoPickupSound));
NativeSoundStatus EmitTMPNativeSound(const NativeSoundAuthority&,
    const TMPResources&,CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::TMPEvent&) noexcept;
}
