// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "control_dispatch.hpp"
#include "engine_message.hpp"
#include "../control_protocol.hpp"
namespace guest::cof
{
namespace
{
ControlStatus Send(CBasePlayer& player, ControlData value, int message, const ControlCall& authority) noexcept
{
    if (detail::messageOpen) return ControlStatus::Busy;
    const auto call = authority;
    if (!call.current) return ControlStatus::InvalidInput;
    if (!call.current(call.context)) return ControlStatus::Cancelled;
    if (message <= 0 || message > 255) return ControlStatus::MissingMessage;
    if (!player.pev) return ControlStatus::InvalidInput;
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte)
        return ControlStatus::MissingEngine;
    ControlPacket bytes{};
    if (!EncodeControl(value, bytes)) return ControlStatus::InvalidInput;
    const EntityHandle<CBasePlayer> source(&player);
    auto* recipient = player.edict();
    if (!recipient || recipient->free || !call.current(call.context) || source.Get<CBasePlayer>() != &player)
        return ControlStatus::Cancelled;
    {
        detail::MessagePacket open;
        // Original 1011C351 uses MSG_ONE_UNRELIABLE; 1011F640 MSG_ONE.
        MESSAGE_BEGIN(value.operation != ControlOperation::WeaponBody ? MSG_ONE_UNRELIABLE : MSG_ONE,
            message, nullptr, recipient);
        // Complete a begun packet from its stack snapshot, even after retirement.
        for (auto byte : bytes) WRITE_BYTE(std::to_integer<unsigned>(byte));
        MESSAGE_END();
    }
    return call.current(call.context) && source.Get<CBasePlayer>() == &player ?
        ControlStatus::Sent : ControlStatus::Cancelled;
}
}
int RegisterControlMessage(const ControlCall& authority) noexcept
{
    const auto call = authority;
    if (detail::messageOpen || !call.current || !call.current(call.context) || !g_engfuncs.pfnRegUserMsg)
        return 0;
    const int id = REG_USER_MSG(ControlMessage, int(ControlPacket{}.size()));
    return call.current(call.context) && id > 0 && id <= 255 ? id : 0;
}
ControlStatus SendWeaponDeploy(CBasePlayer& player, int message, const ControlCall& call) noexcept
{ return Send(player, {ControlOperation::WeaponDeploy}, message, call); }
ControlStatus SendShotState(CBasePlayer& player, std::uint8_t amount, int message, const ControlCall& call) noexcept
{ return Send(player, {ControlOperation::ShotState, amount}, message, call); }
ControlStatus SendWeaponBody(CBasePlayer& player, std::uint8_t body, int message, const ControlCall& call) noexcept
{ return Send(player, {ControlOperation::WeaponBody, body}, message, call); }
}
