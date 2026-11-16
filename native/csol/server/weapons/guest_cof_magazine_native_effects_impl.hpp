// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "guest_cof_native_sound.hpp"

namespace csol::server::cof
{
// Original common EmptyPlay, sleeve, weapon pickup and ammo pickup consumers.
// Resource and event identities are compile-time arguments; fire audio is separate.
template<class Sound, const auto& Samples, std::uint32_t NativeEffects, class Resources, class Event>
NativeSoundStatus EmitMagazineNativeSound(const NativeSoundAuthority& authority,
    const Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon, const Event& event) noexcept
{
    using Effect = decltype(event.kind);
    const auto call=authority;
    const auto kind=static_cast<unsigned>(event.kind);
    if(kind>=32||!(NativeEffects&(1u<<kind)))return NativeSoundStatus::Unsupported;
    if(!call.current||!call.resourceGeneration||!call.current(call.context)||!player.pev||!weapon.pev)
        return NativeSoundStatus::Cancelled;
    auto* owner=player.pev->pContainingEntity;
    auto* item=weapon.pev->pContainingEntity;
    if(!owner||!item)return NativeSoundStatus::Cancelled;
    auto* playerVars=player.pev;auto* weaponVars=weapon.pev;
    const int ownerSerial=owner->serialnumber,itemSerial=item->serialnumber;
    const auto local=[&] {
        return !owner->free&&!item->free&&owner->serialnumber==ownerSerial&&item->serialnumber==itemSerial&&
            owner->pvPrivateData==&player&&item->pvPrivateData==&weapon&&player.pev==playerVars&&weapon.pev==weaponVars&&
            playerVars==&owner->v&&weaponVars==&item->v&&playerVars->pContainingEntity==owner&&
            weaponVars->pContainingEntity==item&&!(playerVars->flags&FL_KILLME)&&!(weaponVars->flags&FL_KILLME)&&
            weapon.m_pPlayer==&player;
    };
    const auto current=[&] {
        return local()&&resources.generation==call.resourceGeneration&&call.current(call.context)&&
            local()&&resources.generation==call.resourceGeneration;
    };
    if(!current())return NativeSoundStatus::Cancelled;
    Sound sample{};
    edict_t* emitter=owner;
    int channel=CHAN_AUTO,pitch=PITCH_NORM;
    float volume=1;
    switch(event.kind)
    {
    case Effect::EmptyClick:
        sample=weapon.m_iPlayEmptySound?Sound::EmptyClick:Sound::EmptyNoClick;
        volume=.8F;break;
    case Effect::Sleeve:
        if(event.value<0||event.value>2||event.other<95||event.other>105)return NativeSoundStatus::InvalidInput;
        sample=static_cast<Sound>(static_cast<unsigned>(Sound::Sleeve1)+event.value);
        pitch=event.other;break;
    case Effect::PickupSound:
        if(call.pickupSuppressed||event.a==1.F)return NativeSoundStatus::Suppressed;
        sample=Sound::WeaponGet;channel=CHAN_ITEM;break;
    case Effect::AmmoPickupSound:
        if(weapon.pev->armorvalue==1.F)return NativeSoundStatus::Suppressed;
        sample=Sound::ItemGet;channel=CHAN_ITEM;emitter=item;break;
    default:return NativeSoundStatus::Unsupported;
    }
    const auto index=static_cast<unsigned>(sample);
    if(resources.sounds[index]<=0)return NativeSoundStatus::MissingResource;
    if(!g_engfuncs.pfnEmitSound)return NativeSoundStatus::MissingEngine;
    if(call.quietAudio)volume=static_cast<float>(static_cast<double>(volume)*.25);
    if(!current())return NativeSoundStatus::Cancelled;
    EMIT_SOUND_DYN2(emitter,channel,Samples[index].host,volume,.8F,0,pitch);
    if(!current())return NativeSoundStatus::Cancelled;
    if(event.kind==Effect::EmptyClick)weapon.m_iPlayEmptySound=0;
    return NativeSoundStatus::Emitted;
}
}
