// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/vp70_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t VP70NativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::VP70Effect::FireSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::VP70Effect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::VP70Effect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::VP70Effect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::VP70Effect::AmmoPickupSound));
NativeSoundStatus EmitVP70NativeSound(const NativeSoundAuthority&,
    const VP70Resources&,CBasePlayer&,CBasePlayerWeapon&,const guest::cof::server::VP70Event&) noexcept;
}
