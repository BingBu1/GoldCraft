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

// Server media indices are a different namespace from model_at's known-model
// indices. This cold scan does not load models or lend the growable slot table.
enum GoldCraftModelScanResult : uint32_t {
    GoldCraftModelScanStop = 0,
    GoldCraftModelScanSkip = 1,
    GoldCraftModelScanBrush = 2,
    GoldCraftModelScanNative = 3
};
struct GoldCraftModelScanAPI {
    uint32_t size;
    uint32_t version;
    uint64_t (*generation)();
    // Main thread only. A brush pointer belongs to the current model owner;
    // consumers must stop using it on retirement. Native requires the original getter;
    // Stop includes stale/retiring queries and the dense table's terminator.
    uint32_t (*inspect)(uint64_t generation, int server_index, model_s** brush);
};
typedef const GoldCraftModelScanAPI* (*GoldCraftGetModelScanAPI)();

#ifdef _WINDOWS_
inline const GoldCraftModelScanAPI* GoldCraftModelScanInterface() {
    static const GoldCraftModelScanAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetModelScanAPI>(
            GetProcAddress(module, "GoldCraft_GetModelScanAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftModelScanAPI) &&
            candidate->generation && candidate->inspect)
            cached = candidate;
    }
    return cached;
}
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
