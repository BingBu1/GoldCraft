// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <com_model.h>
#include <IGameUI.h>
#include "resource_bridge.hpp"
#include "precache_client.hpp"
#include "resource_lifecycle.hpp"
#include "resource_overlay.hpp"
#include "resource_sprite_handles.hpp"
#include "hud_sprites.hpp"
#include "goldcraft/csol_resource_api.hpp"
#include "goldcraft/renderer_resource_api.hpp"
#include "goldcraft/resource_progress_api.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <iomanip>
#include <ostream>

namespace goldcraft::resource_bridge {
namespace {
using Completion = resource_lifecycle::Completion;
using Admission = resource_native_hud::Admission;
enum class Stage { idle, queued, checking, refreshing, cancelling, base, failed };
metahook_api_t* api{};
cl_enginefunc_t* engine{};
void (*original_frame)(double){};
void (*send_connection)(){};
std::chrono::steady_clock::time_point next_keepalive{};
void (*host_error)(const char*, ...){};
void (*loading_start)(const char*, int){};
void (*loading_advance)(const char*, int, float){};
void (*loading_text)(const char*){};
void (*loading_stop)(){};
void (*loading_file_progress)(float){};
void (*loading_file_text)(const char*){};
IGameUI** game_ui{};
int *client_state{}, *uploading{};
char* resource_url{};
const csol_resources::Api* csol{};
const renderer_resources::Api* renderer{};
resource_model_cache::Engine cache;
resource_sprite_handles::Binding sprites;
resource_native_hud::Refresh hud;
std::unique_ptr<resource_update::Session> session;
std::vector<resource_update::Resource> incoming;
std::vector<resource_sprite_handles::Handle> handles;
std::vector<model_s*> models;
std::array<char, 1024> owner_error{};
std::string error;
std::uint64_t serial{}, token{}, generation{}, initial{}, epoch{};
Stage stage{};
bool installed{}, in_frame{}, owner_started{}, cancelled{}, caches_changed{};
bool base_needed{}, native_reset{}, retry_after_reset{};
bool base_transaction{};
bool verified_download{};
bool abort_pending{}, abort_queued{};
bool loading_dispatch{};
client_precache::ManifestProgress displayed_manifest;
std::uint64_t abort_epoch{}, abort_initial{};
resource_update::Phase loading_phase{resource_update::Phase::idle};
unsigned loading_total{}, loading_completed{};
// A native reconnect can reuse the same dialog after retiring the session.
// Keep its last layout until the next owned progress update closes it.
resource_update::DownloadProgress loading_file;
std::chrono::steady_clock::time_point next_loading_update{};
resource_overlay::Snapshot base_previous;
DWORD thread{};
std::uint64_t frames{};
struct Profile {
    resource_timing::Counter pauseOwners, rendererValidate, rendererRelease, retire, reload;
    resource_timing::Counter hudPrepare, hudRebuild, hudCommit, csolRebuild, nativeResume;
} profile;
using resource_timing::Measure;

bool ReadProgress(resource_progress::Snapshot* output) {
    if (!output || !installed || GetCurrentThreadId() != thread || !session ||
        (stage != Stage::checking && stage != Stage::refreshing)) return false;
    const auto& value = session->GetStatus();
    using Phase = resource_progress::Phase;
    const bool downloading = value.phase == resource_update::Phase::downloading ||
        session->GetDownloadProgress().size != 0;
    output->phase = stage == Stage::refreshing ? Phase::refreshing :
        downloading ? Phase::downloading : value.phase == resource_update::Phase::verifying ?
        Phase::verifying : Phase::checking;
    output->completed = stage == Stage::refreshing ? 0u : unsigned(downloading ? value.downloaded : value.checked);
    output->total = stage == Stage::refreshing ? 0u : unsigned(downloading ? value.queued : value.total);
    output->downloaded = unsigned(value.downloaded);
    return true;
}
const resource_progress::Api progress_api{sizeof(resource_progress::Api), 1, ReadProgress};
IBaseInterface* ProgressInterface() {
    return reinterpret_cast<IBaseInterface*>(const_cast<resource_progress::Api*>(&progress_api));
}
InterfaceReg progress_registration(ProgressInterface, resource_progress::interface_name);

bool Unwound(const resource_lifecycle::Observation& observation) {
    return observation.valid && (!observation.callback || observation.completion != Completion::none);
}
void Fail(const char* reason) {
    // No Host_Error here: GL owner cleanup and C++ frames must return normally.
    // The console command is processed by the engine after this frame returns.
    error = reason && *reason ? reason : "Resource refresh failed";
    engine->Con_Printf("GoldCraft resource update: %s\n", error.c_str());
    cancelled = true;
    loading_dispatch = false;
    stage = Stage::cancelling;
    abort_pending = *client_state == 3;
    abort_queued = false;
    abort_epoch = epoch; abort_initial = initial;
}
void AbortAdmission() {
    // Runs as an engine command after C++ owner cleanup has returned. A
    // cancelled/replaced connection must never receive an old queued error.
    if (!installed || !abort_pending) return;
    abort_queued = false;
    if (stage != Stage::idle && stage != Stage::failed) return;
    abort_pending = false;
    const auto observed = resource_lifecycle::observe();
    if (*client_state != 3 || !observed.valid || observed.clear_epoch != abort_epoch ||
        observed.initial_vidinit != abort_initial) return;
    host_error("GoldCraft resource update failed: %s", error.c_str());
}
void QueueAbort() {
    if (!abort_pending || abort_queued) return;
    abort_queued = true;
    engine->pfnClientCmd(const_cast<char*>("gc_resource_abort\n"));
}
template<class T> bool Resolve(T& output, const char* name, mh_gamesymbol_kind_t kind) {
    void* address{};
    if (api->ResolveGameSymbol(api->GetEngineBase(), name, kind, &address) != MH_GAMESYMBOL_OK || !address)
        return false;
    output = reinterpret_cast<T>(address);
    return true;
}
template<class T> const T* Interface(const char* plugin, const char* name, unsigned version = 1) {
    const auto module = api->GetPluginModuleHandleByBaseFileName(plugin);
    const auto factory = module ? Sys_GetFactory(module) : nullptr;
    const auto* result = factory ? reinterpret_cast<const T*>(factory(name, nullptr)) : nullptr;
    return result && result->size == sizeof(T) && result->version == version ? result : nullptr;
}
bool Providers() {
    if (!installed || GetCurrentThreadId() != thread || resource_lifecycle::failure()) return false;
    if (!csol) csol = Interface<csol_resources::Api>("CSOLSupport", csol_resources::interface_name, csol_resources::api_version);
    if (!renderer) renderer = Interface<renderer_resources::Api>("Renderer_AVX2", renderer_resources::interface_name);
    if (!renderer) renderer = Interface<renderer_resources::Api>("Renderer", renderer_resources::interface_name);
    return csol && csol->available && csol->beforeSwitch && csol->afterSwitch &&
        csol->cancelSwitch && csol->resetToBase && csol->available() && renderer &&
        renderer->available && renderer->validate && renderer->release && renderer->available();
}
bool Available() { return Providers() && stage != Stage::failed; }
bool NewToken() {
    if (serial == std::numeric_limits<std::uint64_t>::max()) return false;
    token = ++serial;
    return true;
}
bool PauseOwners() {
    resource_timing::Scope timing(profile.pauseOwners);
    if (owner_started) return true;
    if (!Providers() || !NewToken()) { error = "Resource owner providers are unavailable"; return false; }
    // A provider can own a partial transaction even when Before returns zero.
    owner_started = true;
    return csol->beforeSwitch(token, owner_error.data(), unsigned(owner_error.size())) != 0;
}
const char* ValidateOwners(void*, std::span<const resource_model_cache::Model> owners) {
    if (!in_frame || !owner_started || !paused()) return "Resource consumers are not paused at inner HUD_Frame";
    models.clear();
    for (const auto& owner : owners) models.push_back(owner.value);
    const auto* reason = Measure(profile.rendererValidate, [&] { return renderer->validate(models.data(), unsigned(models.size())); });
    static bool diagnosed{};
    if (reason && !diagnosed) {
        diagnosed = true;
        for (const auto* model : models) if (model->type == mod_sprite && model->cache.data) {
            // Diagnostic only: same HL10210 header already read by Renderer.
            // Keep the original refusal; never repair guessed fields.
            const auto* words = static_cast<const unsigned*>(model->cache.data);
            engine->Con_Printf("GoldCraft sprite owner: %.64s needload=%d header=%p words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                model->name, model->needload, model->cache.data, words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
        }
    }
    return reason;
}
bool ReleaseOwners(void*, std::span<const resource_model_cache::Model>) {
    const auto* reason = Measure(profile.rendererRelease, [&] { return renderer->release(models.data(), unsigned(models.size())); });
    if (reason) error = reason;
    return reason == nullptr;
}
bool Retire() {
    resource_timing::Scope timing(profile.retire);
    if (const auto* reason = resource_sprite_handles::Capture(sprites, handles)) { error = reason; return false; }
    const auto result = client_precache::retire_resource_caches(true, true, cache,
        {nullptr, ValidateOwners, ReleaseOwners});
    // Remember partial invalidation before invoking any original loader.
    caches_changed = caches_changed || result.consumers_released;
    if (!result) { error = result.error; return false; }
    caches_changed = true;
    return true;
}
bool Reload(bool base_view) {
    resource_timing::Scope timing(profile.reload);
    if (base_view) {
        // Withdrawn server-only sprites have no readers after owner retirement.
        // Their stable registry slots remain unloaded until SPR_Load requests
        // them again; do not invent a replacement file in an ordinary server.
        std::erase_if(handles, [](const auto& handle) {
            return !resource_overlay::OpenReader(handle.name.c_str());
        });
    }
    if (const auto* reason = resource_sprite_handles::Reload(sprites, handles)) { error = reason; return false; }
    return true;
}
Admission Admit(bool base_view = false) {
    Admission result;
    result.connection = token;
    result.initial_vidinit = initial;
    result.state = *client_state;
    result.uploading = *uploading;
    result.manifest_complete = stage == Stage::refreshing;
    result.hashes_verified = session && session->GetStatus().phase == resource_update::Phase::ready;
    result.consumers_paused = paused() && owner_started && in_frame;
    if (base_view) {
        result.view_proof = resource_native_hud::ResourceViewProof::restored_base;
        result.base_view_verified = resource_overlay::Capture().paths.empty() && caches_changed;
    }
    return result;
}
const char* Begin(std::uint64_t value, const std::vector<resource_update::Resource>& resources) {
    const auto observed = resource_lifecycle::observe();
    if (!Available() || stage != Stage::idle || cancelled || abort_pending || native_reset || hud.pending() ||
        *client_state != 3 || *uploading || !observed.initial_vidinit || !Unwound(observed))
        return "Resource update requires an initialized, paused connection";
    try { incoming = resources; }
    catch (...) { return "Cannot retain resource update manifest"; }
    profile = {};
    generation = value; initial = observed.initial_vidinit; epoch = observed.clear_epoch;
    loading_phase = resource_update::Phase::idle;
    loading_total = loading_completed = 0; next_loading_update = {};
    stage = Stage::queued;
    resource_lifecycle::block_untracked_calls(true);
    return nullptr;
}
void Cancel() {
    // This runs inside CL_ClearState/Host_Error. Do not free, restore a mount,
    // query native arrays, or call GL here; the abandoned callback may be live.
    verified_download = false;
    loading_dispatch = false;
    displayed_manifest = {};
    abort_pending = abort_queued = false;
    if (stage != Stage::idle) {
        cancelled = true;
        stage = Stage::cancelling;
    }
    if (session) base_needed = true;
}
bool DeferNativeDownload() {
    if (!installed) return false;
    // v4 completion clears its own gate before resuming the native downloader.
    if (verified_download) {
        verified_download = false;
        const auto observed = resource_lifecycle::observe();
        if (stage == Stage::idle && !cancelled && observed.valid &&
            observed.clear_epoch == epoch && observed.initial_vidinit == initial)
            return false;
    }
    if (stage != Stage::idle || native_reset || hud.pending()) return true;
    base_needed = !resource_overlay::Capture().paths.empty();
    if (!base_needed) return false;
    const auto observed = resource_lifecycle::observe();
    initial = observed.initial_vidinit; epoch = observed.clear_epoch;
    stage = Stage::base;
    resource_lifecycle::block_untracked_calls(true);
    return true;
}
int NativeInitialize(void*, const resource_lifecycle::Entry& entry) {
    const auto binding = resource_lifecycle::binding();
    if (!native_reset && !hud.pending() && stage == Stage::idle)
        return binding.vid_init ? binding.vid_init(binding.context) : 0;
    if (cancelled || stage != Stage::idle || !entry.engine_vidinit || !entry.new_clear_state ||
        !Unwound(entry.previous) || !resource_lifecycle::current(entry)) {
        // A new serverinfo can arrive before the next inner Frame. Abort this
        // admission and retry only after cleanup; never fake HUD success.
        retry_after_reset = true;
        host_error("GoldCraft resource recovery is pending; reconnecting after cleanup");
        return 0;
    }
    const resource_native_hud::NativeInitialization event{
        entry.event, entry.engine_vidinit, entry.new_clear_state, true, true,
        Unwound(entry.previous), binding.current_resolution(binding.context)};
    const auto result = hud.Initialize(event, binding, hud_sprites::resource_owner());
    if (!result) {
        host_error("GoldCraft native resource recovery failed: %s", result.error);
        return 0;
    }
    native_reset = false;
    resource_lifecycle::block_untracked_calls(false);
    return 1;
}
bool Cleanup() {
    if (session && !session->Reset()) { error = session->GetStatus().error; return false; }
    if (base_transaction) {
        if (!resource_overlay::Restore(base_previous)) { error = resource_overlay::Error(); return false; }
        base_transaction = false; base_previous = {};
    }
    // A partially rebuilt native HUD still owns both generations. Keep the
    // restored previous view until a natural VidInit has rebound its readers.
    if (caches_changed) {
        if (!owner_started && !PauseOwners()) return false;
        if (!Retire() || !Reload(false)) return false;
    }
    if (owner_started) {
        if (!csol->cancelSwitch(token, owner_error.data(), unsigned(owner_error.size()))) return false;
        owner_started = false;
    }
    incoming.clear();
    cancelled = false;
    native_reset = native_reset || hud.pending() || caches_changed;
    caches_changed = false;
    stage = Stage::idle;
    if (!native_reset) resource_lifecycle::block_untracked_calls(false);
    return true;
}
void RestoreBase() {
    // Ordinary-server downloads are held at state 3. Disconnected UI never
    // renders a HUD and waits for a natural initialization of the base view.
    if (!PauseOwners()) { Fail(owner_error.data()); return; }
    base_previous = resource_overlay::Capture();
    base_transaction = true;
    resource_overlay::Clear();
    if (!Retire() || !Reload(true)) { Fail(error.c_str()); return; }
    const bool admission = client_precache::native_download_pending() && *client_state == 3 && initial;
    if (admission) {
        const auto gate = Admit(true);
        auto result = Measure(profile.hudPrepare, [&] { return hud.Prepare(gate, resource_lifecycle::binding(), hud_sprites::resource_owner()); });
        if (result) result = Measure(profile.hudRebuild, [&] { return hud.Rebuild(gate); });
        if (result) result = Measure(profile.hudCommit, [&] { return hud.Commit(gate); });
        if (!result) { Fail(result.error); return; }
    } else native_reset = true;
    if (!csol->resetToBase(token, owner_error.data(), unsigned(owner_error.size()))) { Fail(owner_error.data()); return; }
    base_transaction = false; base_previous = {};
    owner_started = false; caches_changed = false; base_needed = false; stage = Stage::idle;
    if (!native_reset) resource_lifecycle::block_untracked_calls(false);
    if (admission) client_precache::resume_native_download();
}
bool CurrentAdmission() {
    const auto observed = resource_lifecycle::observe();
    return !cancelled && observed.valid && observed.clear_epoch == epoch &&
        observed.initial_vidinit == initial && *client_state == 3 && !*uploading;
}
void UpdateManifestLoadingProgress() {
    if (*client_state != 3 || *uploading || native_reset || hud.pending()) return;
    const auto progress = client_precache::manifest_progress();
    if (!progress.connection) return;
    const bool new_connection = progress.connection != displayed_manifest.connection;
    if (!new_connection && progress.received == displayed_manifest.received) return;
    const auto now = std::chrono::steady_clock::now();
    if (!new_connection && now < next_loading_update) return;
    const auto still_receiving = [&] {
        return stage == Stage::idle && !cancelled && *client_state == 3 && !*uploading &&
            client_precache::manifest_progress().connection == progress.connection;
    };
    // Update the existing dialog only from the client frame, after parsing.
    // Each GameUI callback can retire or replace the connection.
    if (new_connection) {
        loading_dispatch = true;
        loading_start("GoldCraftResources", int(progress.total));
        loading_dispatch = false;
        if (!still_receiving()) return;
        loading_dispatch = true;
        loading_text("#GameUI_ParseServerInfo");
        loading_dispatch = false;
        if (!still_receiving()) return;
    }
    loading_dispatch = true;
    loading_advance("GoldCraftResources", int(progress.received), 0.f);
    loading_dispatch = false;
    if (!still_receiving()) return;
    displayed_manifest = progress;
    next_loading_update = now + std::chrono::milliseconds(33);
}
bool UpdateLoadingProgress() {
    // Use the verified GameUI slots directly: the engine wrappers also run
    // SCR_UpdateScreen. Normal frames paint this state without extra renders.
    // Plugin UI callbacks can still cancel; retain only scalar progress.
    const auto& status = session->GetStatus();
    if (status.phase == resource_update::Phase::failed) return true;
    const auto file = session->GetDownloadProgress();
    // HTTP can be in flight while the first VFS pass is still discovering files.
    const auto phase = file.size ? resource_update::Phase::downloading : status.phase;
    const bool downloading = phase == resource_update::Phase::downloading;
    const unsigned total = unsigned(downloading ? status.queued : status.total);
    const unsigned completed = std::min(total, unsigned(downloading ? status.downloaded : status.checked));
    const bool open_file_bar = file.size && !loading_file.size;
    const bool close_file_bar = !file.size && loading_file.size;
    const bool phase_changed = phase != loading_phase || open_file_bar || close_file_bar;
    const bool restart = phase_changed || total != loading_total;
    const bool update_primary = restart || completed != loading_completed;
    const bool update_file = file != loading_file;
    const auto now = std::chrono::steady_clock::now();
    if (!phase_changed && ((!update_primary && !update_file) || now < next_loading_update)) return true;
    const bool new_file = file.size && (open_file_bar || file.resourceIndex != loading_file.resourceIndex);
    const auto dispatch = [owner = token](auto callback, auto... args) {
        loading_dispatch = true;
        callback(args...);
        loading_dispatch = false;
        return CurrentAdmission() && owner == token && stage == Stage::checking;
    };
    next_loading_update = now + std::chrono::milliseconds(33);
    loading_phase = phase; loading_total = total; loading_completed = completed;
    // Start reuses the same GameUI dialog, including its secondary layout.
    // Close that layout through GameUI before starting the single-bar phase.
    if (close_file_bar) {
        loading_file = {};
        if (!dispatch(loading_stop)) return false;
    }
    if (restart && !dispatch(loading_start, "GoldCraftResources", int(std::max(1u, total)))) return false;
    loading_file = file;
    // GameUI only opens the secondary layout when the initial fraction is
    // at most .99. Resetting also starts a fresh ETA for the next file.
    if (new_file && !dispatch(loading_file_progress, 0.f)) return false;
    if (restart && !dispatch(loading_text, downloading ?
        "#GameUI_VerifyingAndDownloading" : "#GameUI_VerifyingResources")) return false;
    if (update_primary && !dispatch(loading_advance, "GoldCraftResources", int(completed), 0.f)) return false;
    if (new_file) {
        // Own the name across the callback in case it cancels the session.
        char path[256];
        const auto& name = session->Resources()[file.resourceIndex].name;
        path[name.copy(path, sizeof(path) - 1)] = '\0';
        if (!dispatch(loading_file_text, path)) return false;
    }
    if (file.size && update_file && (!new_file || file.received))
        return dispatch(loading_file_progress, float(std::min(file.received, file.size)) / float(file.size));
    return true;
}
bool RestoreNativeLoadingProgress() {
    // Native connect starts Connecting/12 (101A1D01..10). Its resource
    // precache/register consumers then advance ClientConnect at 7/8/9.
    const auto owner = token;
    loading_dispatch = true;
    loading_start("Connecting", 12);
    loading_dispatch = false;
    if (!CurrentAdmission() || owner != token || stage != Stage::refreshing) return false;
    loading_dispatch = true;
    loading_text("#GameUI_PrecachingResources");
    loading_dispatch = false;
    if (!CurrentAdmission() || owner != token || stage != Stage::refreshing) return false;
    loading_dispatch = true;
    loading_advance("ClientConnect", 6, 0.f);
    loading_dispatch = false;
    return CurrentAdmission() && owner == token && stage == Stage::refreshing;
}
void Pump() {
    if (!installed || stage == Stage::failed) return;
    const auto observed = resource_lifecycle::observe();
    if (!Unwound(observed)) return;
    if (cancelled || stage == Stage::cancelling) {
        if (!Cleanup()) {
            stage = Stage::failed;
            engine->Con_Printf("GoldCraft resource cleanup failed: %s %s\n", error.c_str(), owner_error.data());
            QueueAbort();
            return;
        }
        if (abort_pending) { QueueAbort(); return; }
    }
    if (stage == Stage::idle && base_needed && !native_reset && !hud.pending()) {
        base_needed = !resource_overlay::Capture().paths.empty();
        if (base_needed && *client_state < 3) stage = Stage::base;
    }
    if (stage == Stage::base) { RestoreBase(); return; }
    if (stage == Stage::idle) {
        if (retry_after_reset) {
            retry_after_reset = false;
            engine->pfnClientCmd(const_cast<char*>("retry\n"));
        } else {
            UpdateManifestLoadingProgress();
        }
        return;
    }
    if (observed.clear_epoch != epoch || *client_state != 3 || *uploading) {
        Fail("Connection changed during resource update"); return;
    }
    if (stage == Stage::queued) {
        if (!PauseOwners()) { Fail(owner_error.data()); return; }
        const auto length = strnlen(resource_url, 128);
        if (!length || length == 128) { Fail("Missing server HTTP resource URL"); return; }
        if (!session->Begin(generation, std::string(resource_url, length), std::move(incoming))) {
            Fail(session->GetStatus().error.c_str()); return;
        }
        stage = Stage::checking;
        next_keepalive = {};
    }
    if (!CurrentAdmission()) { Fail("Connection changed while pausing resource owners"); return; }
    if (!UpdateLoadingProgress()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_keepalive && send_connection) {
        next_keepalive = now + std::chrono::seconds(1);
        // Actual hw10210 CL_SendMove's state3 branch transmits only the
        // existing netchan. It never manufactures gameplay input or auth.
        send_connection();
        if (!CurrentAdmission()) { Fail("Connection changed during download keepalive"); return; }
    }
    session->Pump();
    const auto phase = session->GetStatus().phase;
    if (phase == resource_update::Phase::failed) { Fail(session->GetStatus().error.c_str()); return; }
    if (!UpdateLoadingProgress()) return;
    if (phase != resource_update::Phase::ready) return;
    stage = Stage::refreshing;
    if (!RestoreNativeLoadingProgress()) return;
    // Invalidate once per changed generation, not on unchanged map loads.
    if (!session->Changed().empty()) {
        if (!Retire() || !Reload(false)) { Fail(error.c_str()); return; }
        if (!CurrentAdmission()) { Fail("Connection changed during resource cache reload"); return; }
        const auto gate = Admit();
        auto result = Measure(profile.hudPrepare, [&] { return hud.Prepare(gate, resource_lifecycle::binding(), hud_sprites::resource_owner()); });
        if (result) result = Measure(profile.hudRebuild, [&] { return hud.Rebuild(gate); });
        if (!CurrentAdmission()) { Fail("Connection changed during native HUD refresh"); return; }
        if (result) result = Measure(profile.hudCommit, [&] { return hud.Commit(gate); });
        if (!result) { Fail(result.error); return; }
    }
    if (!Measure(profile.csolRebuild, [&] { return csol->afterSwitch(token, owner_error.data(), unsigned(owner_error.size())); })) { Fail(owner_error.data()); return; }
    owner_started = false;
    if (!CurrentAdmission()) { Fail("Connection changed while completing resource owners"); return; }
    if (!session->Commit()) { Fail("Verified resource generation did not commit"); return; }
    const auto& status = session->GetStatus();
    engine->Con_Printf("GoldCraft resource update: verified %u files, repaired %u, cache %u, HTTP %u\n",
        unsigned(status.total), unsigned(session->Changed().size()), unsigned(status.cached), unsigned(status.downloaded));
    caches_changed = false; base_needed = false; stage = Stage::idle;
    resource_lifecycle::block_untracked_calls(false);
    verified_download = true;
    if (!Measure(profile.nativeResume, [&] { return client_precache::resume_resource_sync(); })) {
        verified_download = false;
        Fail("Resource download gate changed before resume");
    }
}
void Frame(double time) {
    ++frames;
    original_frame(time);
    if (loading_dispatch) return;
    client_precache::refresh_resource_capability();
    in_frame = true;
    try { Pump(); }
    catch (const std::exception& failure) { Fail(failure.what()); }
    in_frame = false;
}
}

const char* install(metahook_api_s* incoming_api, mh_interface_s* interfaces,
                    mh_enginesave_s* saved, cl_enginefuncs_s* incoming_engine) {
    if (api) return "Resource bridge installation is one-shot";
    api = incoming_api; engine = incoming_engine; thread = GetCurrentThreadId();
    if (!api || !engine || !engine->pfnAddCommand || !saved || !saved->pExportFuncs || !api->GetPluginModuleHandleByBaseFileName)
        return "Resource bridge SDK is incomplete";
    csol = Interface<csol_resources::Api>("CSOLSupport", csol_resources::interface_name, csol_resources::api_version);
    if (!csol) return "Resource bridge requires the coordinated CSOL HUD directory provider";
    if (!Resolve(resource_url, "GoldCraft_resource_url", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(client_state, "GoldCraft_client_state", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(uploading, "GoldCraft_client_uploading", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(sprites.entries, "GoldCraft_sprite_handles", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(sprites.capacity, "GoldCraft_sprite_capacity", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(sprites.load, "GoldCraft_SPR_Load", MH_GAMESYMBOL_KIND_FUNCTION) ||
        !Resolve(cache.stop_sounds, "GoldCraft_S_StopAllSounds", MH_GAMESYMBOL_KIND_FUNCTION) ||
        !Resolve(cache.cache_free, "GoldCraft_Cache_Free", MH_GAMESYMBOL_KIND_FUNCTION) ||
        !Resolve(cache.unload_sprite_textures, "GoldCraft_Mod_UnloadSpriteTextures", MH_GAMESYMBOL_KIND_FUNCTION) ||
        !Resolve(host_error, "GoldCraft_Host_Error", MH_GAMESYMBOL_KIND_FUNCTION) ||
        !Resolve(game_ui, "GoldCraft_GameUI", MH_GAMESYMBOL_KIND_GLOBAL) ||
        !Resolve(send_connection, "GoldCraft_CL_SendMove", MH_GAMESYMBOL_KIND_FUNCTION))
        return "Resource bridge requires the verified host symbols";
    auto* frame = GetProcAddress(static_cast<HMODULE>(api->GetClientBase()), "HUD_Frame");
    if (!frame || reinterpret_cast<void*>(frame) != reinterpret_cast<void*>(saved->pExportFuncs->HUD_Frame))
        return "Resource bridge requires the original client HUD_Frame export";
    if (!resource_overlay::Install(api, interfaces)) return resource_overlay::Error();
    if (const auto* reason = resource_lifecycle::install(api, engine->pfnGetScreenInfo, {nullptr, NativeInitialize}, true))
        return reason;
    if (!hud_sprites::bind_source_release(resource_lifecycle::source_release())) return "Native HUD source owner is unavailable";
    if (!api->InlineHook(reinterpret_cast<void*>(frame), reinterpret_cast<void*>(Frame), reinterpret_cast<void**>(&original_frame)))
        return "Cannot install inner HUD_Frame resource pump";
    const auto game = std::filesystem::current_path();
    const char* mod = engine->pfnGetGameDirectory ? engine->pfnGetGameDirectory() : nullptr;
    auto gameDir = (game / (mod && *mod ? mod : "cstrike")).lexically_normal();
    if (!gameDir.has_filename()) gameDir = gameDir.parent_path();
    session = std::make_unique<resource_update::Session>((game / "gcr").string(),
        (game / "goldcraft-resource-cache").string(), gameDir.string() + "_downloads");
    installed = true;
    loading_start = [](const char* type, int steps) { if (*game_ui) (*game_ui)->StartProgressBar(type, steps); };
    loading_advance = [](const char*, int point, float fraction) { if (*game_ui) (*game_ui)->ContinueProgressBar(point, fraction); };
    loading_text = [](const char* text) { if (*game_ui) (*game_ui)->SetProgressBarStatusText(text); };
    loading_stop = [] { if (*game_ui) (*game_ui)->StopProgressBar(false, "", ""); };
    loading_file_progress = [](float progress) { if (*game_ui) (*game_ui)->SetSecondaryProgressBar(progress); };
    loading_file_text = [](const char* path) { if (*game_ui) (*game_ui)->SetSecondaryProgressBarText(path); };
    engine->pfnAddCommand(const_cast<char*>("gc_resource_abort"), AbortAdmission);
    client_precache::set_resource_sync({Available, Begin, Cancel});
    client_precache::set_native_download_gate(DeferNativeDownload);
    return nullptr;
}
bool paused() noexcept { return installed && (stage != Stage::idle || native_reset || hud.pending()); }
void write_status(std::ostream& out) {
    out << "\"resourceSync\":{\"installed\":" << installed << ",\"available\":" << Available()
        << ",\"stage\":" << int(stage) << ",\"paused\":" << paused()
        << ",\"nativeReset\":" << native_reset << ",\"token\":" << token
        << ",\"frames\":" << frames
        << ",\"capability\":" << (engine && engine->pfnGetCvarFloat ? engine->pfnGetCvarFloat(resource_sync::capability) : 0.f)
        << ",\"error\":" << std::quoted(error);
    auto counter = [&](const char* name, const resource_timing::Counter& value) {
        out << ",\"" << name << "\":{\"calls\":" << value.calls
            << ",\"totalMs\":" << value.totalMs << ",\"maxMs\":" << value.maxMs << '}';
    };
    out << ",\"profile\":{\"inclusive\":true";
    counter("pauseOwners", profile.pauseOwners);
    counter("rendererValidate", profile.rendererValidate);
    counter("rendererRelease", profile.rendererRelease);
    counter("retire", profile.retire);
    counter("reload", profile.reload);
    counter("hudPrepare", profile.hudPrepare);
    counter("hudRebuild", profile.hudRebuild);
    counter("hudCommit", profile.hudCommit);
    counter("csolRebuild", profile.csolRebuild);
    counter("nativeResume", profile.nativeResume);
    if (session) {
        const auto& p = session->GetStatus().profile;
        counter("openReader", p.openReader);
        counter("backgroundSource", p.backgroundSource);
        counter("closeReader", p.closeReader);
        counter("cacheOpen", p.cacheOpen);
        counter("submitHash", p.submitHash);
        counter("readAt", p.readAt);
        counter("hashUpdate", p.hashUpdate);
        counter("hashFinish", p.hashFinish);
        counter("unchanged", p.unchanged);
        counter("cacheStore", p.cacheStore);
        counter("mount", p.mount);
        counter("leaseOpen", p.leaseOpen);
        counter("leaseCheck", p.leaseCheck);
    }
    out << '}';
    if (session) {
        const auto& status = session->GetStatus();
        out << ",\"phase\":" << int(status.phase) << ",\"total\":" << status.total
            << ",\"checked\":" << status.checked << ",\"downloaded\":" << status.downloaded
            << ",\"cached\":" << status.cached << ",\"current\":" << std::quoted(status.current)
            << ",\"hashedBytes\":" << status.hashedBytes << ",\"pumps\":" << status.pumps
            << ",\"reused\":" << status.reused << ",\"leased\":" << status.leased
            << ",\"elapsedMs\":" << status.elapsedMs << ",\"pumpMaxMs\":" << status.pumpMaxMs
            << ",\"pumpWorkMs\":" << status.pumpWorkMs
            << ",\"sessionError\":" << std::quoted(status.error);
    }
    out << '}';
}
void shutdown() {
    installed = false;
    client_precache::set_resource_sync({});
    client_precache::set_native_download_gate(nullptr);
    // Native consumers are being shut down; retain their pending arrays until
    // module teardown rather than attempting an artificial HUD initialization.
    session.reset();
    resource_overlay::Shutdown();
}
}
