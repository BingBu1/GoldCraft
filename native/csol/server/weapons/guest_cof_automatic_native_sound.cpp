// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_automatic_native_sound.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitAutomaticNativeSound(const NativeSoundAuthority& authority, const AutomaticNativeSounds& resources,
    CBasePlayer& player, CBasePlayerWeapon& weapon, const guest::cof::AutomaticSoundEvent& event) noexcept
{
    const auto call = authority;
    const auto index = static_cast<unsigned>(event.sample);
    if (index >= resources.paths.size()) return NativeSoundStatus::Unsupported;
    const bool stop = event.flags == SND_STOP;
    if (event.channel != CHAN_WEAPON || event.pitch != PITCH_NORM ||
        (stop ? index != 0 || event.volume != 0 || event.attenuation != 0 :
            event.flags != 0 || event.volume != (call.quietAudio ? .25F : 1.F) || event.attenuation != .8F))
        return NativeSoundStatus::InvalidInput;
    if (!call.current || !call.resourceGeneration || !player.pev || !weapon.pev)
        return NativeSoundStatus::Cancelled;
    auto* owner = player.pev->pContainingEntity;
    auto* item = weapon.pev->pContainingEntity;
    if (!owner || !item) return NativeSoundStatus::Cancelled;
    auto* playerVars = player.pev;
    auto* weaponVars = weapon.pev;
    const int ownerSerial = owner->serialnumber, itemSerial = item->serialnumber;
    const auto local = [&] {
        return !owner->free && !item->free && owner->serialnumber == ownerSerial && item->serialnumber == itemSerial &&
            owner->pvPrivateData == &player && item->pvPrivateData == &weapon && player.pev == playerVars && weapon.pev == weaponVars &&
            playerVars == &owner->v && weaponVars == &item->v && playerVars->pContainingEntity == owner &&
            weaponVars->pContainingEntity == item && !(playerVars->flags & FL_KILLME) && !(weaponVars->flags & FL_KILLME) &&
            weapon.m_pPlayer == &player;
    };
    const auto current = [&] {
        return resources.generation == call.resourceGeneration && call.current(call.context) &&
            resources.generation == call.resourceGeneration && local();
    };
    if (!current()) return NativeSoundStatus::Cancelled;
    const char* sample = resources.paths[index];
    if (resources.handles[index] <= 0 || !sample || !*sample) return NativeSoundStatus::MissingResource;
    if (!g_engfuncs.pfnEmitSound) return NativeSoundStatus::MissingEngine;
    if (!current()) return NativeSoundStatus::Cancelled;
    EMIT_SOUND_DYN2(owner, event.channel, sample, event.volume, event.attenuation, event.flags, event.pitch);
    return current() ? NativeSoundStatus::Emitted : NativeSoundStatus::Cancelled;
}
}
