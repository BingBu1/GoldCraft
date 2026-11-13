// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_p345_native_effects.hpp"

namespace csol::server::cof
{
NativeSoundStatus EmitP345NativeSound(const NativeSoundAuthority& authority,
    const P345Resources& resources, CBasePlayer& player, CBasePlayerWeapon& weapon,
    const guest::cof::server::P345Event& event) noexcept
{
    using guest::cof::server::P345Effect;
    const auto call=authority;
    const auto kind=static_cast<unsigned>(event.kind);
    if(kind>=32||!(P345NativeSoundEffects&(1u<<kind)))return NativeSoundStatus::Unsupported;
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
    P345Sound sample{};
    edict_t* emitter=owner;
    int channel=CHAN_AUTO,pitch=PITCH_NORM;
    float volume=1;
    switch(event.kind)
    {
    case P345Effect::EmptyClick:
        sample=weapon.m_iPlayEmptySound?P345Sound::EmptyClick:P345Sound::EmptyNoClick;
        volume=.8F;break;
    case P345Effect::Sleeve:
        if(event.value<0||event.value>2||event.other<95||event.other>105)return NativeSoundStatus::InvalidInput;
        sample=static_cast<P345Sound>(static_cast<unsigned>(P345Sound::Sleeve1)+event.value);
        pitch=event.other;break;
    case P345Effect::PickupSound:
        if(call.pickupSuppressed||event.a==1.F)return NativeSoundStatus::Suppressed;
        sample=P345Sound::WeaponGet;channel=CHAN_ITEM;break;
    case P345Effect::AmmoPickupSound:
        if(weapon.pev->armorvalue==1.F)return NativeSoundStatus::Suppressed;
        sample=P345Sound::ItemGet;channel=CHAN_ITEM;emitter=item;break;
    default:return NativeSoundStatus::Unsupported;
    }
    const auto index=static_cast<unsigned>(sample);
    if(resources.sounds[index]<=0)return NativeSoundStatus::MissingResource;
    if(!g_engfuncs.pfnEmitSound)return NativeSoundStatus::MissingEngine;
    if(call.quietAudio)volume=static_cast<float>(static_cast<double>(volume)*.25);
    if(!current())return NativeSoundStatus::Cancelled;
    EMIT_SOUND_DYN2(emitter,channel,P345Sounds[index].host,volume,.8F,0,pitch);
    if(!current())return NativeSoundStatus::Cancelled;
    if(event.kind==P345Effect::EmptyClick)weapon.m_iPlayEmptySound=0;
    return NativeSoundStatus::Emitted;
}
}
