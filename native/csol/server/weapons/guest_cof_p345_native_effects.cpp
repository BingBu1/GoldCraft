// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_p345_native_effects.hpp"
#include "guest_cof_magazine_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitP345NativeSound(const NativeSoundAuthority& authority,
    const P345Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::P345Event& event) noexcept
{
    return EmitMagazineNativeSound<P345Sound, P345Sounds, P345NativeSoundEffects>(
        authority, resources, player, weapon, event);
}
}
