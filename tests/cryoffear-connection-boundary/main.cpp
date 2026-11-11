// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the actual cleanup entries and overlay owner state, not another token
// implementation. Only engine trampolines and callbacks are controlled.
#include "../../native/client/precache_client.cpp"
#include "../../native/client/resource_overlay.hpp"
#include <cstdio>
#include <setjmp.h>
#include <thread>

cl_enginefunc_t gEngfuncs{};
namespace pc = goldcraft::client_precache;
namespace overlay = goldcraft::resource_overlay;
namespace {
unsigned checks{}, failures{}, originals{}, cancels{}, observers{};
unsigned reportedErrors{};
std::uint64_t prior{};
bool populated{}, jumpOriginal{}, nestedClear{};
overlay::Snapshot overlaySnapshot;
jmp_buf jump;
void NonFatalError(const char*, ...) { ++reportedErrors; }
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", label); }
}
bool Retired() {
    const auto value = pc::connection_generation();
    return value == 0 || value > prior;
}
void CheckOverlay() {
    Check(overlaySnapshot.paths.empty() != populated, "resource-owner overlay snapshot retains its selected state");
}
int OriginalClear() {
    ++originals;
    Check(Retired(), "authority retired before original ClearState");
    CheckOverlay();
    if (jumpOriginal) longjmp(jump, 7);
    return -37;
}
void OriginalDisconnect() {
    ++originals;
    Check(Retired(), "authority retired before original Disconnect");
    CheckOverlay();
    if (nestedClear) Check(pc::clear_client() == -37, "nested real clear preserves original result");
    if (jumpOriginal) longjmp(jump, 9);
}
void Cancel() {
    ++cancels;
    Check(Retired(), "authority retired before resource cancellation");
    CheckOverlay();
}
void ResetObserver() {
    ++observers;
    Check(Retired(), "authority retired before reset observer");
}
void SelectOverlay(bool value) {
    // The resource owner controls overlay state. Exercise its public snapshot
    // type at both boundaries; no filesystem install or unrelated owner API.
    populated = value;
    overlaySnapshot.paths.clear();
    if (value) overlaySnapshot.paths.emplace("models/example.mdl", "unused");
    CheckOverlay();
}
}

int main() {
    int consistency{}, force{};
    pc::consistency_count = &consistency;
    pc::force_consistency = &force;
    pc::clear_client_original = OriginalClear;
    pc::disconnect_original = OriginalDisconnect;
    pc::set_resource_sync({nullptr, nullptr, Cancel});
    pc::set_reset_observer(ResetObserver);
    Check(pc::connection_generation_value == 1, "initial generation storage is one");
    Check(pc::connection_generation() == 0, "uninstalled producer cannot claim readiness");
    Check(!GoldCraft_GetConnectionAPI(), "provider unavailable until verified cleanup hooks installed");
    metahook_api_t host{};
    host.GetEngineBase = []()->void* { return reinterpret_cast<void*>(1); };
    host.GetModuleCRC64 = [](void*, std::uint64_t* crc) { *crc = 0; return MH_GAMESYMBOL_OK; };
    host.GetGameSymbolStatusString = [](mh_gamesymbol_status_t)->const char* { return "fixture unsupported"; };
    Check(!pc::install(&host) && !GoldCraft_GetConnectionAPI() && pc::connection_generation()==0,
          "actual unsupported-engine install path cannot advertise a connection token");
    host.ResolveGameSymbol = [](void*,const char*,mh_gamesymbol_kind_t,void** output) {
        *output = reinterpret_cast<void*>(OriginalClear); return MH_GAMESYMBOL_OK;
    };
    host.InlineHook = [](void*,void*,void**)->hook_t* { return nullptr; };
    host.SysError = NonFatalError;
    auto temporaryOriginal = pc::clear_client_original;
    Check(!pc::hook("CL_ClearState",pc::clear_client,temporaryOriginal) && reportedErrors==1 &&
          !GoldCraft_GetConnectionAPI() && pc::connection_generation()==0,
          "actual failed-hook branch remains unavailable even when fatal handler returns in fixture");
    pc::enabled = true; // installation is controlled; no new engine hook here.
    pc::connection_owner_thread = GetCurrentThreadId();
    Check(!GoldCraft_GetConnectionAPI(), "other core readiness cannot substitute for cleanup hooks");
    pc::connection_hooks_ready = true;
    const auto* api = GoldCraft_GetConnectionAPI();
    Check(api && api->size == sizeof(GoldCraftConnectionAPI) && api->version == 1 && api->generation,
          "actual exported provider has required ABI");
    Check(api && api->generation() == 1, "exported provider reads actual initial generation");
    std::uint64_t foreign = 99;
    std::thread([&] { foreign = api->generation(); }).join();
    Check(foreign == 0 && api->generation() == 1, "foreign thread cannot observe a valid connection token");
    for (bool value : {false, true}) {
        SelectOverlay(value);
        pc::sync_resources.clear();
        // Neither resource extension nor a resource manifest is required.
        Check(!pc::extended && !pc::receiving && !pc::sync_waiting, "ordinary resource state");
        prior = api->generation();
        const auto beforeOriginal = originals, beforeCancel = cancels;
        Check(pc::clear_client() == -37, "real ClearState wrapper preserves native result");
        Check(Retired() && originals == beforeOriginal+1 && cancels == beforeCancel+1,
              "clear advances with either overlay state");
        prior = api->generation();
        pc::reset_client();
        Check(Retired(), "direct reset entry retires before its callbacks");
        prior = api->generation();
        pc::disconnect();
        Check(Retired(), "disconnect advances with either overlay state");
        prior = api->generation();
        nestedClear = true;
        pc::disconnect();
        nestedClear = false;
        Check(Retired(), "nested disconnect/clear remains monotonic");
        prior = api->generation();
        const auto beforeJumpCancel = cancels;
        jumpOriginal = true;
        if (!setjmp(jump)) pc::clear_client();
        jumpOriginal = false;
        Check(Retired() && cancels == beforeJumpCancel,
              "original ClearState longjmp cannot preserve old authority or invoke later cleanup");
        prior = api->generation();
        jumpOriginal = true;
        if (!setjmp(jump)) pc::disconnect();
        jumpOriginal = false;
        Check(Retired(), "original Disconnect longjmp cannot preserve old authority");
        const auto stable = api->generation();
        Check(api->generation() == stable && api->generation() == stable, "read does not advance generation");
    }
    pc::connection_generation_value = std::numeric_limits<std::uint64_t>::max()-1;
    prior = api->generation();
    pc::clear_client();
    Check(api->generation() == std::numeric_limits<std::uint64_t>::max(), "last valid generation is representable");
    prior = api->generation();
    pc::disconnect();
    Check(api->generation() == 0, "exhaustion fails closed to zero");
    prior = 0;
    pc::reset_client();
    pc::clear_client();
    pc::disconnect();
    Check(api->generation() == 0, "exhausted generation never wraps or revives");
    pc::invalidate_connection_observer();
    Check(!GoldCraft_GetConnectionAPI() && api->generation() == 0, "shutdown invalidates held and new API observations");
    std::printf("checks=%u failures=%u originals=%u cancellations=%u observers=%u\n",
                checks, failures, originals, cancels, observers);
    return failures ? 1 : 0;
}
