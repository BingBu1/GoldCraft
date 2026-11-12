// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "screen_shake.hpp"
#include "engine_message.hpp"
#include <xmmintrin.h>
#include <emmintrin.h>

namespace guest::cof
{
namespace
{
unsigned FixedWord(float value, float scale) noexcept
{
    // Original101166D0, including INT_MIN for unordered/out-of-range cvtt.
    const int converted = _mm_cvttss_si32(_mm_mul_ss(_mm_set_ss(value), _mm_set_ss(scale)));
    return converted < 0 ? 0u : converted > 65535 ? 65535u : unsigned(converted);
}
float Distance(const Vector& first, const Vector& second) noexcept
{
    const auto x = _mm_sub_ss(_mm_set_ss(first.x), _mm_set_ss(second.x));
    const auto y = _mm_sub_ss(_mm_set_ss(first.y), _mm_set_ss(second.y));
    const auto z = _mm_sub_ss(_mm_set_ss(first.z), _mm_set_ss(second.z));
    const auto xy = _mm_add_ss(_mm_mul_ss(y, y), _mm_mul_ss(x, x));
    const auto sum = _mm_add_ss(xy, _mm_mul_ss(z, z));
    const auto wide = _mm_cvtss_sd(_mm_setzero_pd(), sum);
    return _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), _mm_sqrt_sd(wide, wide)));
}
}
ScreenShakeResult ShakePlayers(const ScreenShakeRequest& request, int message,
    const PlaySfxCall& authority) noexcept
{
    if (detail::messageOpen) return {WeaponEffectStatus::Busy};
    const auto call = authority;
    if (!call.current || !gpGlobals) return {WeaponEffectStatus::InvalidInput};
    if (!call.current(call.context)) return {WeaponEffectStatus::Cancelled};
    if (message < 64 || message > 255) return {WeaponEffectStatus::MissingMessage};
    if (!g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnMessageBegin ||
        !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteShort)
        return {WeaponEffectStatus::MissingEngine};
    const auto value = request;
    const int clients = gpGlobals->maxClients;
    if (clients < 0 || clients > 32) return {WeaponEffectStatus::InvalidInput};
    const unsigned duration = FixedWord(value.duration, 4096.0f);
    const unsigned frequency = FixedWord(value.frequency, 256.0f);
    unsigned packets{};
    for (int index = 1; index <= clients; ++index)
    {
        auto* entity = INDEXENT(index);
        if (!call.current(call.context)) return {WeaponEffectStatus::Cancelled, packets};
        // Original10119680 returns private data only for an existing nonfree edict.
        auto* player = entity && !entity->free ? static_cast<CBasePlayer*>(entity->pvPrivateData) : nullptr;
        if (!player || !player->pev || !(player->pev->flags & FL_ONGROUND)) continue;
        if (!(value.radius <= 0) && !(Distance(value.origin, player->pev->origin) < value.radius)) continue;
        // The original parity branch retains NaN; its fixed-point word is zero.
        if (value.amplitude == 0) continue;
        const EntityHandle<CBasePlayer> lease(player);
        if (!call.current(call.context) || lease.Get<CBasePlayer>() != player)
            return {WeaponEffectStatus::Cancelled, packets};
        {
            detail::MessagePacket packet;
            MESSAGE_BEGIN(MSG_ONE, message, nullptr, entity);
            WRITE_SHORT(FixedWord(value.amplitude, 4096.0f));
            WRITE_SHORT(duration);
            WRITE_SHORT(frequency);
            MESSAGE_END();
        }
        ++packets;
        if (!call.current(call.context) || lease.Get<CBasePlayer>() != player)
            return {WeaponEffectStatus::Cancelled, packets};
    }
    return {WeaponEffectStatus::Issued, packets};
}
}
