#pragma once
#include <iosfwd>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include "resource_update.hpp"
#include "resource_model_cache.hpp"
struct metahook_api_s;
namespace goldcraft::client_precache {
// Opaque main-thread connection token, independent of resource/overlay state.
// Starts at one; zero is terminal exhaustion. Observe through the public API.
std::uint64_t connection_generation() noexcept;
// Final plugin teardown only; cached API pointers subsequently return zero.
void invalidate_connection_observer() noexcept;
struct ManifestProgress {
    std::uint64_t connection{};
    unsigned received{}, total{};
};
ManifestProgress manifest_progress() noexcept;
struct ResourceSyncCallbacks {
    bool (*available)(){};
    // Returns null on success. The receiver copies values before returning;
    // it never borrows engine resource nodes across network or frame calls.
    const char* (*begin)(std::uint64_t, const std::vector<resource_update::Resource>&){};
    void (*cancel)(){};
};
void set_resource_sync(ResourceSyncCallbacks callbacks);
bool resume_resource_sync();
// Precache policy for an admission whose complete manifest was SHA-verified:
// models/player/<directory>/<file>.mdl is registered by name and loaded on its
// first use, as the host engine's own fs_lazy_precache branch does.
constexpr bool deferred_precache_name(std::string_view name) noexcept {
    constexpr std::string_view prefix = "models/player/", suffix = ".mdl";
    const auto lower = [](char value) { return value >= 'A' && value <= 'Z' ? char(value + ('a' - 'A')) : value; };
    if (name.size() <= prefix.size() + suffix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) if (lower(name[i]) != prefix[i]) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i)
        if (lower(name[name.size() - suffix.size() + i]) != suffix[i]) return false;
    return name.find('/', prefix.size()) != std::string_view::npos;
}
static_assert(deferred_precache_name("models/player/csol12/csol12.mdl"));
static_assert(deferred_precache_name("MODELS/Player/sas/SAS.MDL"));
static_assert(!deferred_precache_name("models/player.mdl"));
static_assert(!deferred_precache_name("models/player/loose.mdl"));
static_assert(!deferred_precache_name("models/playerx/a/a.mdl"));
static_assert(!deferred_precache_name("models/player/a/a.spr"));
static_assert(!deferred_precache_name("models/v_ak47.mdl"));
static_assert(!deferred_precache_name("maps/models/player/a/a.mdl"));
void set_native_download_gate(bool (*should_defer)());
bool native_download_pending();
bool resume_native_download();
// Synchronous owner collection at a safe client-frame boundary. Never retain
// these engine pointers in an asynchronous download or across disconnect.
resource_model_cache::Result retire_resource_caches(bool models, bool sounds,
    const resource_model_cache::Engine&, const resource_model_cache::Owners&);
void set_reset_observer(void (*callback)());
// Only the real CL_ClearState original return reports this fact. The observer
// must not release resources or assume Host_Error has already unwound.
void set_clear_state_observer(void (*callback)());
bool install(metahook_api_s* api);
bool install_client();
const char* install_error();
void register_commands();
// Re-evaluate after providers complete their downstream HUD_Init chains.
void refresh_resource_capability();
void create_entities();
void write_status(std::ostream& out);
}
