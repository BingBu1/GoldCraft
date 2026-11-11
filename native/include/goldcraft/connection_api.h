// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// Process-local, main-thread-only connection identity. Read and cache this
// interface during cold initialization; generation() itself performs no lookup,
// allocation or resource work. A changed value retires all previous authority.
// Values are opaque: nested clear/disconnect entries may advance more than once.
// Zero means permanently exhausted/invalid, never a reusable connection.
struct GoldCraftConnectionAPI {
    uint32_t size;
    uint32_t version;
    uint64_t (*generation)();
};
typedef const GoldCraftConnectionAPI* (*GoldCraftGetConnectionAPI)();

extern "C" const GoldCraftConnectionAPI* GoldCraft_GetConnectionAPI();

#ifdef _WINDOWS_
inline const GoldCraftConnectionAPI* GoldCraftConnectionInterface() {
    static const GoldCraftConnectionAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetConnectionAPI>(
            GetProcAddress(module, "GoldCraft_GetConnectionAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftConnectionAPI) &&
            candidate->generation)
            cached = candidate;
    }
    return cached;
}
#endif
