// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_shotgun_native_effects.hpp"
#include "guest_cof_magazine_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitShotgunNativeSound(const NativeSoundAuthority& authority,
    const ShotgunResources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::ShotgunEvent& event) noexcept
{
    return EmitMagazineNativeSound<ShotgunSound, ShotgunSounds, ShotgunNativeSoundEffects>(
        authority, resources, player, weapon, event);
}
}
