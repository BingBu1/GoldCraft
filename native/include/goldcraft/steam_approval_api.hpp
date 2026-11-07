// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define GC_STEAM_APPROVAL_CALL __cdecl
#else
#define GC_STEAM_APPROVAL_CALL
#endif

namespace goldcraft::steamapproval {
inline constexpr char ApiName[] = "GoldCraft.SteamApproval001";
inline constexpr std::uint32_t ApiVersion = 1;

// Values copied across the engine/GameDLL boundary. No engine/client pointers,
// account strings, credentials or ticket contents are exposed.
struct Identity {
    std::uint32_t size{};
    std::uint32_t slot{}; // 1..32, matching the player edict index.
    std::int32_t userid{};
    std::uint32_t reserved{};
    std::uint64_t account{}; // Approved public individual SteamID64.
    std::uint64_t connection{}; // Process-lifetime non-reused connection serial.
    bool operator==(const Identity&) const = default;
};
static_assert(sizeof(Identity) == 32 && offsetof(Identity, account) == 16);

// Retrieve through the matching ReHLDS GetPluginApi(ApiName). Check version,
// size and function pointers. Main engine thread only. Missing API, pending,
// LAN, bot, proxy, denial or a stale userid means unapproved, never a fallback
// to GETPLAYERAUTHID. Query again before applying an asynchronous reply.
struct Api {
    std::uint32_t version;
    std::uint32_t size;
    int (GC_STEAM_APPROVAL_CALL* getApproved)(std::uint32_t slot,
        std::int32_t userid, Identity* output, std::uint32_t outputBytes);
    int (GC_STEAM_APPROVAL_CALL* isCurrent)(const Identity* identity,
        std::uint32_t identityBytes);
};
}
