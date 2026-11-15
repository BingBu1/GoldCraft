// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// GoldCraftSoundPrecache001 is independent of the existing model-only API.
enum GoldCraftSoundPrecacheState : uint32_t {
    GoldCraftSoundSignonRegistered = 1,
    GoldCraftSoundPcmResident = 2
};
struct GoldCraftSoundPrecacheAPI {
    uint32_t size;
    uint32_t version;
    // Cold, owner-thread-only snapshot for an exact, non-NUL name of 1..63 bytes.
    // Zero also covers an unavailable, stale or incomplete connection. Registered
    // means this connection's active signon table and sound epoch agree.
    // Resident additionally means cache.data is non-null now; it does not pin PCM,
    // validate future residency, or promise that native playback performs no I/O.
    // No loader, cache touch, allocation or filesystem query is performed.
    uint32_t (*inspect)(uint64_t expectedGeneration, const char* name, uint32_t length) noexcept;
};
typedef const GoldCraftSoundPrecacheAPI* (*GoldCraftGetSoundPrecacheAPI)();
extern "C" const GoldCraftSoundPrecacheAPI* GoldCraft_GetSoundPrecacheAPI();

#ifdef _WINDOWS_
inline const GoldCraftSoundPrecacheAPI* GoldCraftSoundPrecacheInterface() {
    static const GoldCraftSoundPrecacheAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetSoundPrecacheAPI>(
            GetProcAddress(module, "GoldCraft_GetSoundPrecacheAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftSoundPrecacheAPI) &&
            candidate->inspect) cached = candidate;
    }
    return cached;
}
#endif
