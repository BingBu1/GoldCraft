// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_m16_native_effects.hpp"
#include "guest_cof_magazine_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitM16NativeSound(const NativeSoundAuthority& authority,
    const M16Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::M16Event& event) noexcept
{
    return EmitMagazineNativeSound<M16Sound, M16Sounds, M16NativeSoundEffects>(
        authority, resources, player, weapon, event);
}
}
