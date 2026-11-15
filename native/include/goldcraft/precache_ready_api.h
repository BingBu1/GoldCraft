// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// Synchronous engine-owner-thread notification after the real native signon
// transition returns. Register during cold initialization, before joining.
// The retired callback receives the old generation after authority advances and
// before original cleanup; stop owned voices/resources there. Engine-owned cache
// memory may still be in use by the original. Nested cleanup advances authority
// without recursively notifying retirement. Registration survives cleanup.
// Either callback may be null; at least one is required. No replay for an already
// completed connection. One listener is supported; a new subscription replaces
// the previous one. Tokens are never reused; zero means unavailable/exhausted.
// Callbacks may unsubscribe, replace the listener or disconnect synchronously.
// The callback must validate its connection generation before publishing work;
// it must return normally (no exception/longjmp across this ABI).
typedef void (*GoldCraftPrecacheReadyCallback)(void* context, uint64_t connectionGeneration);
struct GoldCraftPrecacheReadyAPI {
    uint32_t size;
    uint32_t version;
    uint64_t (*subscribe)(GoldCraftPrecacheReadyCallback ready,
                          GoldCraftPrecacheReadyCallback retired, void* context);
    uint32_t (*unsubscribe)(uint64_t subscription);
};
typedef const GoldCraftPrecacheReadyAPI* (*GoldCraftGetPrecacheReadyAPI)();
extern "C" const GoldCraftPrecacheReadyAPI* GoldCraft_GetPrecacheReadyAPI();
