// SPDX-License-Identifier: GPL-3.0-or-later
// Actual precache implementation and extracted Renderer enumeration, with only
// the engine loader and sink replaced. No second model registry or classifier.
#include "../../native/client/precache_client.cpp"
#include <iostream>
#include <thread>

cl_enginefunc_t gEngfuncs{};
using namespace goldcraft::client_precache;

namespace {
const GoldCraftModelScanAPI* provider{};
const GoldCraftModelScanAPI* test_model_scan_interface() { return provider; }
int EngineGetMaxClientModels() { return model_capacity(); }
}
#define GoldCraftModelScanInterface test_model_scan_interface
#include "renderer_model_scan.inc"
#undef GoldCraftModelScanInterface

namespace {
unsigned scan_checks{}, failures{}, loads{}, calls{};
bool retire_in_loader{}, retire_in_sink{}, fail_loader{};
std::vector<model_t*> visited;
void check(bool condition, const char* label) {
    ++scan_checks;
    if (!condition) { ++failures; std::cerr << label << '\n'; }
}
model_t* native_getter(int index) {
    ++calls;
    if (retire_in_loader) retire_connection();
    if (fail_loader) return nullptr;
    auto* model = extended ? models[index] : legacy_model_precache[index];
    if (model && (model->needload == 1 || model->needload == 2)) {
        ++loads;
        model->needload = 0;
        model->type = deferred_precache_name(model->name) ? mod_studio : mod_brush;
    }
    return model;
}
void sink(model_t* model) {
    visited.push_back(model);
    if (retire_in_sink) retire_connection();
}
void scan() { calls = loads = 0; visited.clear(); EngineForEachClientBrushModel(sink); }
}

int main() {
    enabled = true;
    connection_hooks_ready = true;
    connection_owner_thread = GetCurrentThreadId();
    connection_generation_value = 7;
    provider = GoldCraft_GetModelScanAPI();
    check(provider && provider->version == 1, "new server-media interface available");
    gEngfuncs.hudGetModelByIndex = native_getter;
    model_for_index_original = native_getter;
    std::array<model_t, 4> known{};
    int count = int(known.size());
    original_models = known.data(); original_model_count = &count;
    std::array<model_t*, 512> legacy{};
    legacy_model_precache = legacy.data();
    model_t world{}, inline_bsp{}, pending_bsp{}, player{}, monster{};
    std::strcpy(world.name, "maps/a.bsp"); world.type = mod_brush;
    std::strcpy(inline_bsp.name, "*1"); inline_bsp.type = mod_brush; inline_bsp.needload = 3;
    std::strcpy(player.name, "models/player/csol7/csol7.mdl"); player.needload = 1;
    std::strcpy(pending_bsp.name, "models/door.bsp"); pending_bsp.needload = 2;
    monster.type = mod_studio;
    legacy[1] = &world; legacy[2] = &monster; legacy[3] = &inline_bsp;
    scan();
    check(calls == 0 && visited == std::vector<model_t*>{&world, &inline_bsp}, "loaded legacy BSPs include inline/client-owned models");
    provider = nullptr;
    scan();
    check(calls == 4 && visited.size() == 2, "absent/old interface retains stock dense scan");
    provider = GoldCraft_GetModelScanAPI();
    legacy[2] = &player;
    scan();
    check(loads == 1 && player.type == mod_studio && visited.size() == 2, "ordinary lazy models retain original loading");

    extended = sync_verified = true;
    models.assign(703, &monster); models[0] = nullptr; models[702] = nullptr;
    models[1] = &world; models[2] = &player; models[3] = &inline_bsp; models[4] = &pending_bsp;
    models[600] = &world;
    check(model_at(1) == &known[1] && model_at(1) != models[1], "known and server indices deliberately differ");
    for (int state : {1, 2}) {
        player.type = mod_brush; player.needload = state;
        pending_bsp.needload = 2;
        scan();
        check(loads == 1 && calls == 1, "only pending non-player resource calls native loader");
        check(visited == std::vector<model_t*>{&world, &inline_bsp, &pending_bsp, &world}, "high server slot and all BSP resources preserved");
        check(player.needload == state && player.type == mod_brush, "deferred placeholder stays unloaded and is not mistaken for a BSP");
    }
    auto* actual = model_for_index(2);
    check(actual == &player && player.needload == 0 && player.type == mod_studio && loads == 2,
          "actual entity lookup still performs original on-demand load");
    scan();
    check(calls == 0 && loads == 0 && visited.size() == 4, "already loaded studio remains excluded");
    player.needload = 1; player.type = mod_brush;
    sync_verified = false;
    scan();
    check(loads == 1 && player.type == mod_studio, "unverified admission cannot skip pending native work");
    sync_verified = true; player.needload = 1; player.type = mod_brush;
    cvar_t lazy{}; lazy.value = 0; lazy_players = &lazy;
    scan();
    check(calls == 0 && player.needload == 1, "changing policy does not eagerly load existing deferred models");

    const auto generation = connection_generation();
    model_t* out = &world;
    for (int index : {-1, 0, 702, 703, 1000000})
        check(inspect_model_scan(generation, index, &out) == GoldCraftModelScanStop && !out, "bounds and null terminator rejected");
    check(inspect_model_scan(generation, 1, nullptr) == GoldCraftModelScanStop, "null output rejected");
    check(inspect_model_scan(generation + 1, 1, &out) == GoldCraftModelScanStop && !out, "stale token rejected");
    receiving = true; scan(); check(calls == 0 && visited.empty(), "receiving table cannot be scanned"); receiving = false;
    sound_query_retiring = true; scan(); check(calls == 0 && visited.empty(), "retiring table cannot be scanned"); sound_query_retiring = false;
    std::thread other([&] {
        model_t* wrong = &world;
        check(connection_generation() == 0 && inspect_model_scan(generation, 1, &wrong) == GoldCraftModelScanStop && !wrong,
              "off-thread call cannot read engine tables");
    }); other.join();
    enabled = false; scan(); check(calls == 0 && visited.empty(), "cached provider becomes inert after disable"); enabled = true;

    retire_in_sink = true; scan();
    check(visited == std::vector<model_t*>{&world} && calls == 0, "sink retirement stops before next media slot");
    retire_in_sink = false; sound_query_retiring = false;
    pending_bsp.needload = 1; models[1] = &pending_bsp;
    retire_in_loader = true; scan();
    check(visited.empty() && calls == 1, "native load retirement cannot publish an old brush");
    retire_in_loader = false; sound_query_retiring = false;
    pending_bsp.needload = 1; fail_loader = true; scan();
    check(visited.empty() && calls == 1, "failed native load preserves stop semantics");
    fail_loader = false;

    // Execute the real clear/reset order with valid patch storage.
    metahook_api_t patches{};
    patches.WriteDWORD = [](void* p, DWORD v) { std::memcpy(p, &v, 4); };
    patches.WriteMemory = [](void* p, void* v, DWORD n) -> DWORD { std::memcpy(p, v, n); return n; };
    api = &patches;
    std::array<DWORD, 7> operands{};
    for (unsigned i = 0; i < model_limits.size(); ++i) model_limits[i] = &operands[i];
    sound_end_patch = &operands[5]; consistency_table_patch = &operands[6];
    byte bits{}; consistency_bits_patch = &bits;
    int consistency{}, force{}; consistency_count = &consistency; force_consistency = &force;
    clear_client_original = []() -> int {
        model_t* value = nullptr;
        check(inspect_model_scan(connection_generation(), 1, &value) == GoldCraftModelScanStop,
              "new token remains unavailable during original clear");
        return 9;
    };
    const auto old = connection_generation();
    check(clear_client() == 9 && !extended && models.size() == 513, "actual native cleanup resets media namespace");
    check(inspect_model_scan(old, 1, &out) == GoldCraftModelScanStop, "old scan token stays retired after reset");
    legacy[1] = &world; legacy[2] = nullptr;
    scan(); check(visited == std::vector<model_t*>{&world}, "reconnect reads current legacy table");
    connection_hooks_ready = false;
    scan(); check(calls == 0 && visited.empty(), "uninstalled connection hooks disable cached scans");
    std::cout << "{\"checks\":" << scan_checks << ",\"failures\":" << failures << ",\"passed\":" << (failures ? "false" : "true") << "}\n";
    return failures ? 1 : 0;
}
