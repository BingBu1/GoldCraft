// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "effects_dispatch.hpp"
#include "engine_message.hpp"
#include "../particle_protocol.hpp"

#include <cmath>

namespace guest::cof
{
namespace
{
bool Current(const PlaySfxCall& call, const EntityHandle<CBasePlayer>& lease,
             const CBasePlayer* expected) noexcept
{
    return call.current(call.context) && lease.Get<CBasePlayer>() == expected;
}
bool Finite(const Vector& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

WeaponEffectStatus EjectWeaponShell(const ShellRequest& request, const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return WeaponEffectStatus::Busy;
    const auto call = authority;
    if (!call.current) return WeaponEffectStatus::InvalidInput;
    if (!call.current(call.context)) return WeaponEffectStatus::Cancelled;
    const auto value = request;
    // Native PF_WriteAngle_I delegates to MSG_WriteAngle, which first reduces
    // a finite angle with fmod. Preserve every finite value, including wraps.
    if (!detail::NativeCoordinates(value.origin) || !detail::NativeCoordinates(value.velocity) ||
        !std::isfinite(value.angle) ||
        value.model <= 0 || value.model > 65535)
        return WeaponEffectStatus::InvalidInput;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteCoord || !g_engfuncs.pfnWriteAngle || !g_engfuncs.pfnWriteShort)
        return WeaponEffectStatus::MissingEngine;
    {
        detail::MessagePacket packet;
        // Original1011CF70, including PVS origin, native TE_MODEL, bounce and
        // life25. Finish the complete packet even if a hook retires authority.
        MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, value.origin);
        WRITE_BYTE(TE_MODEL);
        WRITE_COORD(value.origin.x); WRITE_COORD(value.origin.y); WRITE_COORD(value.origin.z);
        WRITE_COORD(value.velocity.x); WRITE_COORD(value.velocity.y); WRITE_COORD(value.velocity.z);
        WRITE_ANGLE(value.angle);
        WRITE_SHORT(value.model);
        WRITE_BYTE(value.bounce);
        WRITE_BYTE(25);
        MESSAGE_END();
    }
    return call.current(call.context) ? WeaponEffectStatus::Issued : WeaponEffectStatus::Cancelled;
}

WeaponEffectStatus CooperativeViewRecoil(CBasePlayer& player, const Vector& impulse,
    unsigned short preparedEvent, const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return WeaponEffectStatus::Busy;
    const auto call = authority;
    if (!call.current || !player.pev) return WeaponEffectStatus::InvalidInput;
    if (!call.current(call.context)) return WeaponEffectStatus::Cancelled;
    if (!preparedEvent) return WeaponEffectStatus::MissingMessage;
    if (!g_engfuncs.pfnPlaybackEvent) return WeaponEffectStatus::MissingEngine;
    Vector value = impulse;
    if (!Finite(value)) return WeaponEffectStatus::InvalidInput;
    const EntityHandle<CBasePlayer> lease(&player);
    if (!Current(call, lease, &player)) return WeaponEffectStatus::Cancelled;
    {
        // PLAYBACK_EVENT may itself reach native message hooks. Coordinate it
        // with this family's message senders without claiming an engine lock.
        detail::MessagePacket packet;
        // Original100F91B0 cooperative branch: both vector arguments are the
        // impulse, flags16, delay0, and every scalar/event parameter is zero.
        static_assert(FEV_HOSTONLY == 16);
        PLAYBACK_EVENT_FULL(FEV_HOSTONLY, player.edict(), preparedEvent, 0,
            value, value, 0, 0, 0, 0, 0, 0);
    }
    return Current(call, lease, &player) ? WeaponEffectStatus::Issued : WeaponEffectStatus::Cancelled;
}

WeaponEffectStatus SendLocalGunsmoke(CBasePlayer& player, std::uint8_t originalWeapon,
    std::int16_t weaponEntity, int preparedMessage, const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return WeaponEffectStatus::Busy;
    const auto call = authority;
    if (!call.current || !player.pev) return WeaponEffectStatus::InvalidInput;
    if (!call.current(call.context)) return WeaponEffectStatus::Cancelled;
    if (preparedMessage <= 0 || preparedMessage > 255) return WeaponEffectStatus::MissingMessage;
    ParticlePacket bytes{};
    if (!EncodeParticle({ParticleOperation::Add, originalWeapon, false, weaponEntity}, bytes))
        return WeaponEffectStatus::InvalidInput;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return WeaponEffectStatus::MissingEngine;
    const EntityHandle<CBasePlayer> lease(&player);
    if (!Current(call, lease, &player)) return WeaponEffectStatus::Cancelled;
    {
        detail::MessagePacket packet;
        // Host CoFParticle v1 maps this local operation to the original script,
        // model and attachment at the authenticated client resource consumer.
        MESSAGE_BEGIN(MSG_ONE, preparedMessage, nullptr, player.edict());
        for (const auto byte : bytes) WRITE_BYTE(std::to_integer<unsigned char>(byte));
        MESSAGE_END();
    }
    return Current(call, lease, &player) ? WeaponEffectStatus::Issued : WeaponEffectStatus::Cancelled;
}

int RegisterPresentationMessage(const PlaySfxCall& authority) noexcept
{
    const auto call = authority;
    if (detail::messageOpen || !call.current || !g_engfuncs.pfnRegUserMsg ||
        !call.current(call.context)) return 0;
    const int message = REG_USER_MSG(StateMessage, -1);
    return message >= 64 && message <= 255 && call.current(call.context) ? message : 0;
}

WeaponEffectStatus SendPresentationState(CBasePlayer& player, const PresentationState& state,
    int preparedMessage, const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return WeaponEffectStatus::Busy;
    const auto call = authority;
    if (!call.current || !player.pev) return WeaponEffectStatus::InvalidInput;
    if (!call.current(call.context)) return WeaponEffectStatus::Cancelled;
    if (preparedMessage < 64 || preparedMessage > 255) return WeaponEffectStatus::MissingMessage;
    StatePacket legacy{};
    RevolverStatePacket revolver{};
    std::span<const std::byte> bytes;
    if (state.revolverAngle)
    {
        if (!EncodeState(state, revolver)) return WeaponEffectStatus::InvalidInput;
        bytes = revolver;
    }
    else
    {
        if (!EncodeState(state, legacy)) return WeaponEffectStatus::InvalidInput;
        bytes = legacy;
    }
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return WeaponEffectStatus::MissingEngine;
    const EntityHandle<CBasePlayer> lease(&player);
    if (!Current(call, lease, &player)) return WeaponEffectStatus::Cancelled;
    {
        detail::MessagePacket packet;
        MESSAGE_BEGIN(MSG_ONE, preparedMessage, nullptr, player.edict());
        for (const auto byte : bytes) WRITE_BYTE(std::to_integer<unsigned char>(byte));
        MESSAGE_END();
    }
    return Current(call, lease, &player) ? WeaponEffectStatus::Issued : WeaponEffectStatus::Cancelled;
}
}
