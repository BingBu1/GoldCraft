// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_native_effects.hpp"
#include <array>

namespace csol::server::cof
{
namespace
{
struct Lease
{
    CBasePlayer& player;
    CBasePlayerWeapon& weapon;
    entvars_t* playerVars;
    entvars_t* weaponVars;
    edict_t* owner;
    edict_t* item;
    int ownerSerial;
    int itemSerial;
    bool Local() const noexcept
    {
        return owner && item && !owner->free && !item->free &&
            owner->serialnumber == ownerSerial && item->serialnumber == itemSerial &&
            owner->pvPrivateData == &player && item->pvPrivateData == &weapon &&
            player.pev == playerVars && weapon.pev == weaponVars &&
            playerVars == &owner->v && weaponVars == &item->v &&
            playerVars->pContainingEntity == owner && weaponVars->pContainingEntity == item &&
            !(playerVars->flags & FL_KILLME) && !(weaponVars->flags & FL_KILLME) &&
            weapon.m_pPlayer == &player;
    }
    bool Current(const NativeSoundAuthority& authority, const GlockResources& resources) const noexcept
    {
        return Local() && resources.generation == authority.resourceGeneration &&
            authority.current(authority.context) && Local() &&
            resources.generation == authority.resourceGeneration;
    }
};
}
NativeSoundStatus EmitGlockNativeSound(const NativeSoundAuthority& authority,
    const GlockResources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::GlockEvent& event) noexcept
{
    const auto call = authority; // Freeze callback and generation across reentry.
    using guest::cof::server::GlockEffect;
    const auto kind = static_cast<unsigned>(event.kind);
    if (kind >= 32 || !(GlockNativeSoundEffects & (1u << kind)))
        return NativeSoundStatus::Unsupported;
    // Authenticate before dereferencing native entities or resource storage.
    if (!call.current || !call.resourceGeneration || !call.current(call.context))
        return NativeSoundStatus::Cancelled;
    if (!player.pev || !weapon.pev) return NativeSoundStatus::Cancelled;
    auto* owner = player.pev->pContainingEntity;
    auto* item = weapon.pev->pContainingEntity;
    if (!owner || !item) return NativeSoundStatus::Cancelled;
    const Lease lease{player, weapon, player.pev, weapon.pev, owner, item,
        owner->serialnumber, item->serialnumber};
    if (!lease.Current(call, resources)) return NativeSoundStatus::Cancelled;

    GlockSound sample{};
    edict_t* emitter = owner;
    int channel = 0, pitch = 100;
    float volume = 1.0f;
    switch (event.kind)
    {
    case GlockEffect::EmptyClick:
        sample = weapon.m_iPlayEmptySound ? GlockSound::EmptyClick : GlockSound::EmptyNoClick;
        volume = .8f;
        break;
    case GlockEffect::Sleeve:
        if (event.value < 0 || event.value > 2 || event.other < 95 || event.other > 105)
            return NativeSoundStatus::InvalidInput;
        sample = static_cast<GlockSound>(static_cast<unsigned>(GlockSound::Sleeve1) + event.value);
        pitch = event.other;
        break;
    case GlockEffect::PickupSound:
        if (call.pickupSuppressed || event.a == 1.0f) return NativeSoundStatus::Suppressed;
        sample = GlockSound::WeaponGet;
        channel = 3;
        break;
    case GlockEffect::AmmoPickupSound:
        if (weapon.pev->armorvalue == 1.0f) return NativeSoundStatus::Suppressed;
        sample = GlockSound::ItemGet;
        channel = 3;
        emitter = item;
        break;
    case GlockEffect::AttachmentSound:
    {
        constexpr std::array samples{GlockSound::Sleeve2, GlockSound::RifleInsert, GlockSound::Sleeve3};
        if (event.value < 0 || event.value >= static_cast<int>(samples.size()))
            return NativeSoundStatus::InvalidInput;
        sample = samples[event.value];
        channel = 2;
        emitter = item;
        break;
    }
    default: return NativeSoundStatus::Unsupported;
    }
    const auto index = static_cast<std::size_t>(sample);
    if (resources.sounds[index] <= 0) return NativeSoundStatus::MissingResource;
    if (!g_engfuncs.pfnEmitSound) return NativeSoundStatus::MissingEngine;
    // 100FC9F0: preserve the original float -> double multiply -> float path.
    if (call.quietAudio) volume = static_cast<float>(static_cast<double>(volume) * .25);
    if (!lease.Current(call, resources)) return NativeSoundStatus::Cancelled;
    EMIT_SOUND_DYN2(emitter, channel, GlockSounds[index].host, volume, .8f, 0, pitch);
    if (!lease.Current(call, resources)) return NativeSoundStatus::Cancelled;
    if (event.kind == GlockEffect::EmptyClick) weapon.m_iPlayEmptySound = 0;
    return NativeSoundStatus::Emitted;
}
}
