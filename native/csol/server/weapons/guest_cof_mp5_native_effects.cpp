// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_mp5_native_effects.hpp"
#include "guest_cof_automatic_weapon_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitMP5NativeSound(const NativeSoundAuthority& authority,
    const MP5Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::TMPEvent& event) noexcept
{
    return EmitAutomaticWeaponNativeSound<MP5Resources, MP5Sound, MP5Sounds>(
        authority, resources, player, weapon, event);
}
}
