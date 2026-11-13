// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_resources.hpp"
#include "guest_cof_native_sound.hpp"
#include "native/guest/cof/server/glock_weapon.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t GlockNativeSoundEffects =
    (1u << static_cast<unsigned>(guest::cof::server::GlockEffect::EmptyClick)) |
    (1u << static_cast<unsigned>(guest::cof::server::GlockEffect::Sleeve)) |
    (1u << static_cast<unsigned>(guest::cof::server::GlockEffect::PickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::GlockEffect::AmmoPickupSound)) |
    (1u << static_cast<unsigned>(guest::cof::server::GlockEffect::AttachmentSound));

// Actual engine EmitSound consumer. No allocation, resource discovery or RNG.
// Class already owns pickup eligibility and sleeve RNG/armor-reset ordering.
// EmptyClick clears the real SDK m_iPlayEmptySound after a surviving emit.
NativeSoundStatus EmitGlockNativeSound(const NativeSoundAuthority&,
    const GlockResources&, CBasePlayer&, CBasePlayerWeapon&,
    const guest::cof::server::GlockEvent&) noexcept;

}
