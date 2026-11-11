// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "sound_dispatch.hpp"
#include "engine_message.hpp"
#include "../presentation_protocol.hpp"
#include "../sound_protocol.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace guest::cof
{
namespace
{
bool Sample(const char* input, std::array<char, SoundSampleLimit + 1>& output) noexcept
{
    if (!input || !*input) return false;
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        output[i] = input[i];
        if (!output[i]) return true;
    }
    return false;
}

struct Source
{
    EntityHandle<CBasePlayer> handle;
    CBasePlayer* expected;
};
bool Current(const PlaySfxCall& call, const Source& source) noexcept
{
    return call.current(call.context) && source.handle.Get<CBasePlayer>() == source.expected;
}

void Packet(int message, edict_t* recipient, const char* sample, int mode,
            std::uint8_t enabled, const Vector& origin, bool remote, int index) noexcept
{
    detail::MessagePacket open;
    // Host CoFSFX v1 layout. WRITE_COORD uses the actual ReHLDS encoding; no
    // assumption about the original CoF engine's rounding is introduced.
    MESSAGE_BEGIN(recipient ? MSG_ONE : MSG_ALL, message, nullptr, recipient);
    WRITE_BYTE(SoundProtocol);
    WRITE_BYTE(mode);
    WRITE_BYTE(enabled);
    WRITE_BYTE(remote);
    WRITE_COORD(origin.x);
    WRITE_COORD(origin.y);
    WRITE_COORD(origin.z);
    WRITE_SHORT(index);
    WRITE_STRING(sample);
    MESSAGE_END();
}
}

int RegisterPlaySfxMessage(const PlaySfxCall& authority) noexcept
{
    const auto call = authority;
    if (detail::messageOpen || !call.current || !call.current(call.context) || !g_engfuncs.pfnRegUserMsg)
        return 0;
    const int message = REG_USER_MSG(SoundMessage, -1);
    return call.current(call.context) && message > 0 && message <= 255 ? message : 0;
}

PlaySfxResult SendPlaySfx(CBasePlayer& player, const PlaySfxRequest& request, int message,
                        const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return {PlaySfxStatus::Busy};
    const auto call = authority;
    if (!call.current) return {PlaySfxStatus::InvalidInput};
    if (!call.current(call.context)) return {PlaySfxStatus::Cancelled};
    if (message <= 0 || message > 255) return {PlaySfxStatus::MissingMessage};
    if (request.rules != PlaySfxRules::Direct && request.rules != PlaySfxRules::Cooperative)
        return {PlaySfxStatus::UnsupportedRules};
    if (!gpGlobals || gpGlobals->maxClients < 1 || gpGlobals->maxClients > MAX_CLIENTS || !player.pev)
        return {PlaySfxStatus::InvalidInput};
    if (!g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte ||
        !g_engfuncs.pfnWriteCoord || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteString ||
        !g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnIndexOfEdict)
        return {PlaySfxStatus::MissingEngine};
    std::array<char, SoundSampleLimit + 1> sample{};
    if (!Sample(request.sample, sample)) return {PlaySfxStatus::InvalidInput};
    // Freeze borrowed request data before virtual/engine callbacks can retire
    // its resource owner. Prepared sample storage must be valid on entry.
    Vector origin = request.origin;
    const auto rules = request.rules;
    const int mode = request.mode;
    const auto enabled = request.enabled;
    const bool broadcast = request.broadcast;
    const Source source{EntityHandle<CBasePlayer>(&player), &player};
    if (!Current(call, source)) return {PlaySfxStatus::Cancelled};
    if (rules == PlaySfxRules::Cooperative)
    {
        origin = player.GetGunPosition();
        if (!Current(call, source)) return {PlaySfxStatus::Cancelled};
    }
    if (!detail::NativeCoordinates(origin)) return {PlaySfxStatus::InvalidInput};
    unsigned packets{};
    // Original exact needle is "weapons", case-sensitive and anywhere in the
    // path. This is distinct from the client's local/remote rewrite predicate.
    if (rules == PlaySfxRules::Cooperative && std::strstr(sample.data(), "weapons") && broadcast)
    {
        for (int index = 1; index <= gpGlobals->maxClients; ++index)
        {
            auto* edict = INDEXENT(index);
            if (!Current(call, source)) return {PlaySfxStatus::Cancelled, packets};
            if (!edict || edict->free) continue;
            auto* recipient = GET_PRIVATE<CBaseEntity>(edict);
            if (!recipient) continue;
            if (recipient->pev != &edict->v) return {PlaySfxStatus::InvalidInput, packets};
            const EntityHandle<CBaseEntity> lease(recipient);
            const bool remote = recipient != &player;
            const int receiver = g_engfuncs.pfnIndexOfEdict(edict);
            if (!Current(call, source) || lease.Get<CBaseEntity>() != recipient)
                return {PlaySfxStatus::Cancelled, packets};
            Packet(message, edict, sample.data(), mode, enabled, origin, remote, receiver);
            ++packets;
            if (!Current(call, source)) return {PlaySfxStatus::Cancelled, packets};
        }
    }
    else
    {
        // In particular a direct broadcast remains remote=0/recipient-index=1
        // for every listener, just as the original second branch specifies.
        Packet(message, broadcast ? nullptr : player.edict(), sample.data(), mode, enabled, origin, false, 1);
        ++packets;
        if (!Current(call, source)) return {PlaySfxStatus::Cancelled, packets};
    }
    return {PlaySfxStatus::Sent, packets};
}
}
