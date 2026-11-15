// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_tmp_native_effects.hpp"
#include "guest_cof_automatic_weapon_native_effects_impl.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitTMPNativeSound(const NativeSoundAuthority& authority,
    const TMPResources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::TMPEvent& event) noexcept
{
    return EmitAutomaticWeaponNativeSound<TMPResources, TMPSound, TMPSounds>(
        authority, resources, player, weapon, event);
}
}
