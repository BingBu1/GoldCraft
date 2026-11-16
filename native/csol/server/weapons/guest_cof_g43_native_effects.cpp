// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_g43_native_effects.hpp"
#include "guest_cof_magazine_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitG43NativeSound(const NativeSoundAuthority& authority,
    const G43Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::G43Event& event) noexcept
{
    return EmitMagazineNativeSound<G43Sound, G43Sounds, G43NativeSoundEffects>(
        authority, resources, player, weapon, event);
}
}
