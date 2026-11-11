// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "vector.h"
#include <cstdint>

class CBasePlayer;

namespace guest::cof
{
// Exact original rules-v20 result, supplied by the authenticated rules owner.
// An absent provider is not equivalent to the original false branch.
enum class PlaySfxRules : unsigned char { Unsupported, Direct, Cooperative };
enum class PlaySfxStatus : unsigned char
{
    Sent, InvalidInput, MissingEngine, MissingMessage, UnsupportedRules, Cancelled, Busy
};
struct PlaySfxCall
{
    void* context{};
    bool (*current)(void*) noexcept {};
};
struct PlaySfxRequest
{
    // Cold-prepared original logical key, not the host asset pathname. The
    // client owns its proven local/remote rewrite and resource namespace map.
    const char* sample{};
    Vector origin{};
    int mode{};
    std::uint8_t enabled{};
    bool broadcast{};
    PlaySfxRules rules{PlaySfxRules::Unsupported};
};
struct PlaySfxResult
{
    PlaySfxStatus status;
    unsigned packets{};
};

// Cold authoritative-thread registration. The caller caches the ID with its
// map generation; a cancelled registration never supplies a usable ID.
int RegisterPlaySfxMessage(const PlaySfxCall&) noexcept;

// Actual engine MESSAGE_BEGIN/WRITE_*/END consumer of original 10116A40.
// Source/life/map identity is checked around SDK callbacks. Once a packet
// begins it finishes from a stack snapshot, even if a callback retires the
// source. No allocation, file I/O, global equipment gate or channel guess.
// The authority context/callback must stay alive through the entire call,
// including completion after retirement; the snapshot does not own them.
// This transport alone does not establish client audio/resource readiness.
PlaySfxResult SendPlaySfx(CBasePlayer&, const PlaySfxRequest&, int message,
                        const PlaySfxCall&) noexcept;
}
