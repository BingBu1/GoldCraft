#pragma once

#include <cstdint>
#include <span>

struct model_s;
struct cache_user_s;

namespace goldcraft::resource_model_cache {

// The caller selects this complete owner/dependency closure only after verifying
// the new content hashes. These are stable native objects, not precache indices.
struct Model {
    model_s* value{};
    int* crc_checked{};
    std::uint32_t* crc_initial{};
    std::span<cache_user_s> skins;
};

struct Engine {
    void (*stop_sounds)(int clear){};
    void (*cache_free)(cache_user_s*){};
    // Call the resolved entry, including the Renderer hook, not its trampoline.
    void (*unload_sprite_textures)(model_s*){};
};

struct Owners {
    void* context{};
    // Both callbacks are mandatory for models. Validate has no side effects and
    // confirms the main thread, stopped scene/HUD consumers, full dependency
    // closure, and working Renderer + CSOL component invalidation interfaces.
    const char* (*validate)(void*, std::span<const Model>){};
    // Release GPU/material/bone resources, wait for asynchronous readers, and
    // release component references BEFORE native headers are freed. Keep native
    // main/sequence/skin cache objects and the passed spans alive and unchanged.
    // Returning false aborts admission; it does not promise owner rollback.
    bool (*release)(void*, std::span<const Model>){};
};

struct Result {
    const char* error{};
    std::uint32_t models{}, cache_blocks{}, sprites{};
    bool consumers_released{};
    explicit operator bool() const { return error == nullptr; }
};

// Run synchronously while old resource consumers are stopped, before reopening
// the new scene. Sound cache_user objects stay alive across stop_sounds, although
// their data may be freed there for stream channels. No native caches are freed
// on an unavailable owner interface, unsupported model type, or failed preflight.
// A failure after consumers_released requires cancelling this admission.
Result Invalidate(std::span<const Model> models,
                  std::span<cache_user_s* const> sounds,
                  const Engine& engine, const Owners& owners);

}
