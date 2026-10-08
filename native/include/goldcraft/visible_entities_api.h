#pragma once
#include <stdint.h>

struct cl_entity_s;

// Process-local pointers only. This interface does not change network edicts,
// model handles, sound handles, or the existing MetaRenderer ABI.
struct GoldCraftVisibleEntitiesAPI {
    uint32_t size;
    uint32_t version;
    int capacity;
    cl_entity_s** entries;
    int* count;
    void (*bind_consumer)(unsigned consumer, cl_entity_s*** entries, int** count);
    void (*sprite_drawn)(const cl_entity_s* entity);
};
typedef const GoldCraftVisibleEntitiesAPI* (*GoldCraftGetVisibleEntitiesAPI)();

#ifdef _WINDOWS_
inline const GoldCraftVisibleEntitiesAPI* GoldCraftVisibleEntitiesInterface() {
    static const GoldCraftVisibleEntitiesAPI* cached = nullptr;
    if (!cached) {
        const auto module = GetModuleHandleA("GoldCraft.dll");
        const auto query = module ? reinterpret_cast<GoldCraftGetVisibleEntitiesAPI>(
            GetProcAddress(module, "GoldCraft_GetVisibleEntitiesAPI")) : nullptr;
        const auto candidate = query ? query() : nullptr;
        if (candidate && candidate->version == 1 && candidate->size >= sizeof(GoldCraftVisibleEntitiesAPI))
            cached = candidate;
    }
    return cached;
}
#endif
