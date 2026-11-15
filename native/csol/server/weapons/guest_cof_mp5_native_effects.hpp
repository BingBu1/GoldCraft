// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_tmp_native_effects.hpp"

namespace csol::server::cof
{
inline constexpr std::uint32_t MP5NativeSoundEffects = TMPNativeSoundEffects;
NativeSoundStatus EmitMP5NativeSound(const NativeSoundAuthority&,
    const MP5Resources&, CBasePlayer&, CBasePlayerWeapon&, const guest::cof::server::TMPEvent&) noexcept;
}
