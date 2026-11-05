#pragma once
#include <stdint.h>

struct model_s;

// Optional, process-local ABI shared by MetaHook plugins. All model and cache
// objects stay at stable addresses; an index is never a pointer subtraction.
struct GoldCraftPrecacheAPI {
    uint32_t size;
    uint32_t version;
    int (*model_count)();
    int (*model_index)(const model_s*);
    model_s* (*model_at)(int);
    int (*client_model_capacity)();
};
typedef const GoldCraftPrecacheAPI* (*GoldCraftGetPrecacheAPI)();

// Resource ownership outlives the connection's expanded media table. This
// separate ABI is for retirement only; it does not expand a stock connection.
struct GoldCraftModelOwnersAPI {
    uint32_t size;
    uint32_t version;
    int (*index_limit)();
    int (*model_index)(const model_s*);
    model_s* (*model_at)(int);
};
typedef const GoldCraftModelOwnersAPI* (*GoldCraftGetModelOwnersAPI)();

#ifdef _WINDOWS_
inline const GoldCraftPrecacheAPI* GoldCraftPrecacheInterface() {
    static const GoldCraftPrecacheAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetPrecacheAPI>(
            GetProcAddress(module, "GoldCraft_GetPrecacheAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftPrecacheAPI))
            cached = candidate;
    }
    return cached;
}
inline const GoldCraftModelOwnersAPI* GoldCraftModelOwnersInterface() {
    static const GoldCraftModelOwnersAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetModelOwnersAPI>(
            GetProcAddress(module, "GoldCraft_GetModelOwnersAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftModelOwnersAPI))
            cached = candidate;
    }
    return cached;
}
#endif
