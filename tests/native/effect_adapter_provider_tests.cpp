// Actual adapter -> actual service provider -> actual registry, using pinned
// x86 AMXX/HLSDK declarations. OS lookup, clock and AMXX script discovery are
// harness boundaries. This is not a Pawn VM, loaded DLL, engine or physics test.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <amxxmodule.h>
#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "../../native/server/effect_service_provider.hpp"
#include "allocation_faults.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <initializer_list>
#include <iostream>

static_assert(sizeof(void*) == 4 && sizeof(cell) == 4 && sizeof(REAL) == 4);
namespace integrated {
using namespace goldcraft::amxx_effects;
using namespace goldcraft::effects;
AMX a{}, b{}, unknown{};
std::array<AMX*, 2> scripts{&a, &b};
std::uint64_t now = 10000;
bool clockSucceeds = true;
EffectServiceProvider* provider = nullptr;
const AMX_NATIVE_INFO* natives = nullptr;
int add_natives(const AMX_NATIVE_INFO* value) { natives = value; return 1; }
int find_script(const AMX* amx) {
    for (int i = 0; i < 2; ++i) if (scripts[i] && scripts[i] == amx) return i;
    return -1;
}
AMX* get_script(int index) { return index >= 0 && index < 2 ? scripts[index] : nullptr; }
REAL cell_to_real(cell value) { return std::bit_cast<float>(value); }
HMODULE WINAPI module_handle(LPCSTR name) {
    return provider && name && std::strcmp(name, "mp.dll") == 0
        ? reinterpret_cast<HMODULE>(std::uintptr_t{1}) : nullptr;
}
FARPROC WINAPI proc_address(HMODULE module, LPCSTR name);
bool qpc(goldcraft::effect_clock::RawSample& output) noexcept {
    if (!clockSucceeds) return false;
    output = {static_cast<std::int64_t>(now * 1000), 1000000};
    return true;
}
}
PFN_ADD_NATIVES g_fn_AddNatives = integrated::add_natives;
PFN_FIND_AMXSCRIPT_BYAMX g_fn_FindAmxScriptByAmx = integrated::find_script;
PFN_GET_AMXSCRIPT g_fn_GetAmxScript = integrated::get_script;
PFN_CELL_TO_REAL g_fn_CellToReal = integrated::cell_to_real;

extern "C" IBaseInterface* CreateInterface(const char* name, int* status) {
    if (integrated::provider)
        return goldcraft::amxx_effects::QueryEffectServiceInterface(*integrated::provider, name, status);
    if (status) *status = IFACE_FAILED;
    return nullptr;
}
FARPROC WINAPI integrated::proc_address(HMODULE module, LPCSTR name) {
    return module && name && std::strcmp(name, "CreateInterface") == 0
        ? reinterpret_cast<FARPROC>(&CreateInterface) : nullptr;
}
#define GetModuleHandleA integrated::module_handle
#define GetProcAddress integrated::proc_address
#include "../../native/amxx/effect_adapter.cpp"
#undef GetModuleHandleA
#undef GetProcAddress

namespace integrated {
namespace api = goldcraft::amxx_effects;
int checks = 0, failures = 0;
void expect(bool pass, const char* expression, int line) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "line " << line << ": " << expression << '\n'; }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)
cell call(const char* name, AMX* script, std::initializer_list<cell> args = {}) {
    std::array<cell, 12> params{};
    if (args.size() >= params.size()) std::abort();
    params[0] = static_cast<cell>(args.size() * sizeof(cell));
    std::size_t i = 1;
    for (const auto value : args) params[i++] = value;
    for (auto* native = natives; native && native->name; ++native)
        if (std::strcmp(native->name, name) == 0) return native->func(script, params.data());
    std::abort();
}
cell real(float value) { return std::bit_cast<cell>(value); }
cell bind(AMX* amx = &a, TargetKind kind = TargetKind::player, std::uint64_t key = 1) {
    return call("gc_effect_target", amx, {static_cast<cell>(kind),
        std::bit_cast<cell>(static_cast<std::uint32_t>(key)),
        std::bit_cast<cell>(static_cast<std::uint32_t>(key >> 32))});
}
cell apply(cell target, AMX* amx = &a, cell lease = 100, float speed = 80) {
    return call("gc_effect_apply", amx, {target, wish_speed_cap, 0, 0, real(speed), real(1), lease});
}
cell update(cell effect, AMX* amx = &a, cell lease = 100, float speed = 40) {
    return call("gc_effect_update", amx, {effect, wish_speed_cap, 0, 0, real(speed), real(1), lease});
}
struct Fixture {
    EffectServiceProvider service;
    ServiceContext ctx{};
    Target target{7, TargetKind::player, 1, 101, 1};
    explicit Fixture(ProviderLimits limits = {}) : service(limits, 0x1122334400000101ULL) {
        api::OnDetach();
        now += 1000;
        clockSucceeds = true;
        api::clock = goldcraft::effect_clock::Clock(qpc); // Test boundary only.
        scripts = {&a, &b};
        provider = &service;
        CHECK(service.BeginWorld(target.world, now) == ServiceStatus::ok);
        CHECK(service.GetContext(&ctx) == ServiceStatus::ok);
        CHECK(service.RegisterTarget(ctx, target, now) == ServiceStatus::ok);
        api::OnPluginsLoaded();
    }
    ~Fixture() { api::OnDetach(); provider = nullptr; }
    Snapshot read() {
        Snapshot out{};
        CHECK(service.ReadSnapshot(ctx, target, now, &out) == ServiceStatus::ok);
        return out;
    }
};

void composition_and_owner_isolation() {
    Fixture f;
    CHECK(call("gc_effect_api_version", &a) == 1);
    CHECK(call("gc_effect_api_version", &unknown) == -2);
    CHECK(bind(&a, TargetKind::player, 2) == -104); // No implicit target admission.
    const auto ta = bind(), tb = bind(&b);
    CHECK(ta > 0 && tb > ta && bind() == ta);
    const auto ea = apply(ta), eb = apply(tb, &b, 100, 30);
    CHECK(ea > tb && eb > ea);
    CHECK(f.read().movement.wishSpeedCap == 30);
    CHECK(update(ea, &b) == -4 && apply(ta, &b) == -4);
    CHECK(call("gc_effect_drop", &b, {ea}) == -4);
    CHECK(f.read().movement.wishSpeedCap == 30);
    CHECK(call("gc_effect_owner_release", &b) == 1);
    CHECK(f.read().movement.wishSpeedCap == 80);
    CHECK(update(eb, &b) == -4);
    CHECK(update(ea) == 1 && f.read().movement.wishSpeedCap == 40);
    CHECK(call("gc_effect_drop", &a, {ea}) == 1);
    CHECK(f.read().movement == MovementState{});
}

void expiry_without_pawn_callbacks_and_unload() {
    Fixture f;
    const auto ta = bind(), tb = bind(&b);
    const auto ea = apply(ta, &a, 10), eb = apply(tb, &b, 30, 30);
    now += 10;
    // No adapter OnFrame or Pawn callback runs to drive the provider deadline.
    CHECK(f.service.Pump(f.ctx, now) == ServiceStatus::ok);
    CHECK(f.read().movement.wishSpeedCap == 30);
    CHECK(update(ea) == -4);
    scripts[1] = nullptr;
    api::OnFrame();
    CHECK(f.read().movement == MovementState{});
    CHECK(update(eb, &b) == -2);
    scripts[1] = &b;
    const auto tb2 = bind(&b), eb2 = apply(tb2, &b);
    CHECK(tb2 > eb && eb2 > tb2);
    api::OnPluginsUnloading();
    CHECK(f.read().movement == MovementState{});
    CHECK(call("gc_effect_api_version", &a) == -3);
    api::OnPluginsLoaded();
    CHECK(update(eb2, &b) == -4);
    CHECK(bind() > eb2);
}

void identity_change_world_stop_and_object_reissue() {
    Fixture f;
    const auto oldTarget = bind(), oldEffect = apply(oldTarget);
    CHECK(f.service.InvalidateTarget(f.ctx, f.target, now) == ServiceStatus::ok);
    CHECK(apply(oldTarget) == -104 && update(oldEffect) == -105);
    CHECK(f.service.RegisterTarget(f.ctx, f.target, now) == ServiceStatus::invalid_target);
    ++f.target.life;
    CHECK(f.service.RegisterTarget(f.ctx, f.target, now) == ServiceStatus::ok);
    const auto nextTarget = bind(), nextEffect = apply(nextTarget);
    CHECK(nextTarget > oldEffect && nextEffect > nextTarget);
    CHECK(call("gc_effect_drop", &a, {oldTarget}) == -4);
    CHECK(f.read().movement.wishSpeedCap == 80);
    constexpr auto key = 0xFEDCBA9876543210ULL;
    Target object{7, TargetKind::object, key, 400, 1};
    CHECK(f.service.RegisterTarget(f.ctx, object, now) == ServiceStatus::ok);
    const auto objectTarget = bind(&a, TargetKind::object, key), objectEffect = apply(objectTarget);
    CHECK(objectEffect > objectTarget && objectTarget > nextEffect);
    CHECK(f.service.InvalidateTarget(f.ctx, object, now) == ServiceStatus::ok);
    ++object.serial;
    CHECK(f.service.RegisterTarget(f.ctx, object, now) == ServiceStatus::ok);
    CHECK(bind(&a, TargetKind::object, key) > objectEffect);
    CHECK(update(objectEffect) == -4);
    CHECK(f.read().movement.wishSpeedCap == 80);

    CHECK(f.service.EndWorld(f.ctx, now) == ServiceStatus::ok);
    CHECK(update(nextEffect) == -101 && bind() == -101);
    api::OnFrame();
    CHECK(f.service.BeginWorld(2, now) == ServiceStatus::ok);
    CHECK(f.service.GetContext(&f.ctx) == ServiceStatus::ok);
    f.target.world = 2;
    CHECK(f.service.RegisterTarget(f.ctx, f.target, now) == ServiceStatus::ok);
    CHECK(update(nextEffect) == -4);
    const auto worldTarget = bind();
    CHECK(worldTarget > objectEffect);
    CHECK(f.read().movement == MovementState{});
    CHECK(apply(worldTarget) > worldTarget);
}

void rejected_update_preserves_lease_and_aggregate() {
    Fixture f;
    const auto target = bind();
    const auto effect = apply(target, &a, 10);
    const auto initial = f.read();
    CHECK(update(effect, &a, 0) == -5);
    CHECK(call("gc_effect_update", &a, {effect, gravity_factor, 0, 0, real(0), real(65), 100}) == -5);
    CHECK(f.read() == initial);
    now += 10;
    CHECK(f.service.Pump(f.ctx, now) == ServiceStatus::ok);
    CHECK(f.read().movement == MovementState{});
    CHECK(update(effect) == -4);
}

// Actual failure at either layer must not leak a registry owner/effect when the
// adapter cannot publish its alias. Restrict owners/effects to one so leaked
// admissions make the unfaulted retry fail observably.
void adapter_fault_closes_actual_provider() {
    Fixture f;
    const auto identity = bind(), effect = apply(identity);
    CHECK(effect > 0 && f.read().movement.wishSpeedCap == 80);
    const auto previous = now;
    --now;
    CHECK(update(effect) == -115);
    Snapshot untouched{};
    const auto sentinel = untouched;
    CHECK(f.service.ReadSnapshot(f.ctx, f.target, previous, &untouched) == ServiceStatus::clock_failed);
    CHECK(untouched == sentinel);
    CHECK(f.service.BeginWorld(12345, previous) == ServiceStatus::clock_failed);
    now = previous;
    api::OnPluginsUnloading(); api::OnPluginsLoaded();
    CHECK(bind() == -115);
}
void read_failure_closes_actual_provider_on_unload() {
    Fixture f;
    const auto identity = bind(), effect = apply(identity);
    CHECK(effect > 0);
    clockSucceeds = false;
    api::OnPluginsUnloading();
    ServiceContext untouched{99, 88};
    CHECK(f.service.GetContext(&untouched) == ServiceStatus::clock_failed);
    CHECK((untouched == ServiceContext{99, 88}));
    clockSucceeds = true;
    api::OnPluginsLoaded();
    CHECK(bind() == -115);
}

void adapter_observation_rejected_by_host_stops_mutation() {
    Fixture f;
    const auto identity = bind(), effect = apply(identity);
    CHECK(effect > 0);
    CHECK(f.service.ObserveClock({static_cast<std::int64_t>((now + 2) * 1000), 1000000}) == ServiceStatus::ok);
    ++now; // Adapter's own Clock advances, but the shared host clock is ahead.
    CHECK(update(effect) == -115);
    now += 10;
    CHECK(f.service.BeginWorld(8, now) == ServiceStatus::clock_failed);
    CHECK(bind() == -115);
}

void cross_layer_allocation_rollback() {
    for (const bool effectOperation : {false, true}) {
        auto run = [&](const std::size_t* point) {
            ProviderLimits limits;
            limits.registry = {1, 4, 1, 60000};
            Fixture f(limits);
            const cell target = effectOperation ? bind() : 0;
            const auto before = f.read().movement;
            const auto start = allocations::count;
            cell result;
            if (point) {
                allocations::Fault fault(*point);
                result = effectOperation ? apply(target) : bind();
            } else result = effectOperation ? apply(target) : bind();
            const auto observed = allocations::count - start;
            if (point && allocations::injected) {
                CHECK(result == -109 || result == -8);
                CHECK(f.read().movement == before);
                result = effectOperation ? apply(target) : bind();
            }
            CHECK(result > 0);
            if (effectOperation) {
                CHECK(f.read().movement.wishSpeedCap == 80);
                CHECK(call("gc_effect_drop", &a, {result}) == 1);
                CHECK(f.read().movement == MovementState{});
            } else {
                CHECK(apply(result) > result);
                CHECK(call("gc_effect_owner_release", &a) == 1);
                CHECK(f.read().movement == MovementState{});
            }
            return observed;
        };
        const auto total = run(nullptr);
        CHECK(total > 0 && total < 64);
        if (!total || total >= 64) continue;
        for (std::size_t point = 0; point <= total; ++point) {
            CHECK(run(&point) == (point < total ? point + 1 : total));
            CHECK(allocations::injected == (point < total));
        }
        std::cout << "adapter/provider allocation sweep " << (effectOperation ? "effect" : "target")
                  << ": " << total << " observed nodes\n";
    }
}
}
int main() {
    using namespace integrated;
    api::OnAttach();
    composition_and_owner_isolation();
    expiry_without_pawn_callbacks_and_unload();
    identity_change_world_stop_and_object_reissue();
    rejected_update_preserves_lease_and_aggregate();
    cross_layer_allocation_rollback();
    adapter_fault_closes_actual_provider();
    read_failure_closes_actual_provider_on_unload();
    adapter_observation_rejected_by_host_stops_mutation();
    std::cout << "actual adapter/provider/registry: " << checks << " checks, " << failures
              << " failures; no Pawn VM, loaded engine, or physical execution\n";
    return failures ? 1 : 0;
}
