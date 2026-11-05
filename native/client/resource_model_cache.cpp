#include <metahook.h>
#include <com_model.h>
#include "resource_model_cache.hpp"

#include <cstddef>
#include <new>
#include <unordered_set>
#include <vector>

namespace goldcraft::resource_model_cache {
namespace {
static_assert(sizeof(model_t) == 392 && sizeof(cache_user_t) == 4);
static_assert(offsetof(model_t, submodels) == 124);
static_assert(offsetof(model_t, texinfo) == 172);
static_assert(offsetof(model_t, cache) == 388);

// HL10210 R_GetAnim 101F5C30 allocates 16 cache_user_t records separately from
// the main Cache_Alloc block. The array stays owned by the native model object.
constexpr std::size_t sequence_slots = 16;
bool invalidating{}; // Main-thread API; also reject recursive owner callbacks.

struct Guard {
    Guard() { invalidating = true; }
    ~Guard() { invalidating = false; }
};

struct Plan {
    std::vector<cache_user_t*> auxiliary;
    std::vector<cache_user_t*> main;
    std::vector<cache_user_t*> sounds;
    std::unordered_set<cache_user_t*> slots;
    std::unordered_set<void*> blocks;

    bool add(cache_user_t* cache, std::vector<cache_user_t*>& destination) {
        if (!cache) return false;
        if (!slots.insert(cache).second) return true;
        // Distinct cache_user records cannot own the same Cache_Alloc block.
        if (cache->data && !blocks.insert(cache->data).second) return false;
        destination.push_back(cache);
        return true;
    }
};
}

Result Invalidate(std::span<const Model> models,
                  std::span<cache_user_t* const> sounds,
                  const Engine& engine, const Owners& owners) {
    Result result;
    auto fail = [&](const char* message) { result.error = message; return result; };
    if (invalidating) return fail("recursive resource cache invalidation");
    if (models.empty() && sounds.empty()) return result;
    if (!engine.cache_free) return fail("native cache release is unavailable");
    if (!sounds.empty() && !engine.stop_sounds) return fail("native sound stop is unavailable");
    if (!models.empty() && (!owners.validate || !owners.release))
        return fail("model resource owner interface is unavailable");

    Guard guard;
    Plan plan;
    try {
        std::unordered_set<model_t*> unique_models;
        for (const auto& entry : models) {
            auto* model = entry.value;
            if (!model || !entry.crc_checked || !entry.crc_initial)
                return fail("incomplete model cache ownership");
            if (!unique_models.insert(model).second)
                return fail("duplicate model cache owner");
            if (model->type != mod_studio && model->type != mod_sprite)
                return fail("unsupported model cache allocation type");
            if (model->type == mod_sprite) {
                if (!engine.unload_sprite_textures)
                    return fail("sprite texture owner is unavailable");
                if (!entry.skins.empty()) return fail("sprite cannot own studio skin caches");
                // Sprite headers are Hunk_AllocName blocks, not Cache_Alloc.
                if (model->cache.data && !plan.blocks.insert(model->cache.data).second)
                    return fail("aliased sprite cache allocation");
            } else {
                if (model->submodels) {
                    auto* sequences = reinterpret_cast<cache_user_t*>(model->submodels);
                    for (std::size_t i = 0; i < sequence_slots; ++i)
                        if (!plan.add(sequences + i, plan.auxiliary))
                            return fail("aliased sequence cache allocation");
                }
                for (auto& skin : entry.skins)
                    if (!plan.add(&skin, plan.auxiliary))
                        return fail("aliased skin cache allocation");
                if (!plan.add(&model->cache, plan.main))
                    return fail("aliased studio cache allocation");
            }
        }
        for (auto* sound : sounds)
            if (!plan.add(sound, plan.sounds)) return fail("invalid or aliased sound cache allocation");
    } catch (const std::bad_alloc&) {
        return fail("cannot prepare resource cache invalidation");
    }

    if (!models.empty()) {
        if (const auto* error = owners.validate(owners.context, models)) return fail(error);
        result.consumers_released = true;
        if (!owners.release(owners.context, models))
            return fail("model resource owner release failed");
    }
    if (!sounds.empty()) {
        result.consumers_released = true;
        // S_StopAllSounds 101FEA30 is cdecl(clear). S_FreeChannel frees stream
        // samples first; recheck each data pointer below to avoid a second free.
        engine.stop_sounds(1);
    }
    auto release = [&](const auto& caches) {
        for (auto* cache : caches) {
            if (!cache->data) continue;
            engine.cache_free(cache);
            ++result.cache_blocks;
        }
    };
    release(plan.sounds);
    release(plan.auxiliary);

    // Renderer and component owners no longer retain the old headers. Sprite
    // GPU identifiers still need the hooked engine unload before discarding the
    // hunk pointer; the low-hunk lifecycle, never Cache_Free, owns its memory.
    for (const auto& entry : models) {
        if (entry.value->type == mod_sprite) {
            engine.unload_sprite_textures(entry.value);
            entry.value->cache.data = nullptr;
            ++result.sprites;
        }
    }
    release(plan.main);
    for (const auto& entry : models) {
        if (entry.value->type == mod_studio) entry.value->texinfo = nullptr;
        entry.value->needload = 1;
        // Same initialization as Mod_FindName 1023E6CF for a new/reused slot.
        // Preserve crc.enabled and every native subsequent CRC comparison.
        *entry.crc_checked = 0;
        *entry.crc_initial = 0;
        ++result.models;
    }
    return result;
}
}
