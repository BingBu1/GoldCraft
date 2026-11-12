// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "crosshair_dispatch.hpp"
#include "engine_message.hpp"
namespace guest::cof
{
CrosshairSend SendCrosshairSnapshot(CBasePlayer& player,const CrosshairSnapshot& snapshot,int message,
                                   const PlaySfxCall& authority) noexcept
{
    if(detail::messageOpen)return CrosshairSend::Busy;
    const auto call=authority;
    if(!call.current||!call.current(call.context))return CrosshairSend::Cancelled;
    if(message<64||message>255||!g_engfuncs.pfnMessageBegin||!g_engfuncs.pfnMessageEnd||
       !g_engfuncs.pfnWriteByte)return CrosshairSend::MissingBackend;
    CrosshairPacket bytes{};
    if(!EncodeCrosshair(snapshot,bytes)||!player.pev)return CrosshairSend::Invalid;
    const EntityHandle<CBasePlayer> owner(&player);
    auto* recipient=player.edict();
    if(!recipient||recipient->free||!call.current(call.context)||owner.Get<CBasePlayer>()!=&player)
        return CrosshairSend::Cancelled;
    {
        detail::MessagePacket packet;
        MESSAGE_BEGIN(MSG_ONE_UNRELIABLE,message,nullptr,recipient);
        for(const auto value:bytes)WRITE_BYTE(std::to_integer<unsigned char>(value));
        MESSAGE_END();
    }
    return call.current(call.context)&&owner.Get<CBasePlayer>()==&player?CrosshairSend::Sent:CrosshairSend::Cancelled;
}
}
