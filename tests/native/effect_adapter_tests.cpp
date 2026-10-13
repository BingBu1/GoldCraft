// Tests the exact adapter translation unit with real pinned SDK declarations.
// This is not an AMXX VM, loaded AMXX module, ReHLDS, or gameplay test.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <amxxmodule.h>
#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "../../native/amxx/effect_service.hpp"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>

static_assert(sizeof(void*) == 4, "run the candidate against the matched x86 ABI");
static_assert(sizeof(cell) == 4 && sizeof(REAL) == 4);

namespace adapter_test {
using goldcraft::amxx_effects::IEffectService;
using goldcraft::amxx_effects::ServiceContext;
using goldcraft::amxx_effects::ServiceStatus;
using goldcraft::effects::MovementState;
using goldcraft::effects::Target;
using goldcraft::effects::TargetKind;

AMX scriptA{}, scriptB{}, foreignScript{};
std::array<AMX*, 2> loadedScripts{&scriptA, &scriptB};
bool mismatchScriptLookup = false;
bool moduleAvailable = true, factoryAvailable = true, providerAvailable = true;
std::uint64_t clockMs = 10000;
bool clockSucceeds = true;
std::int64_t clockFrequency = 1000000;
const AMX_NATIVE_INFO* registeredNatives = nullptr;
int registrationCalls = 0, clockCalls = 0, moduleCalls = 0, factoryCalls = 0;

// Deliberately independent, small service double. It records all boundary
// arguments and models only service ownership/liveness/lease responses. It does
// not copy native parsing, alias allocation, caller lookup, or adapter cleanup.
class FakeService final : public IEffectService {
public:
    struct Owner { std::uint64_t token = 0; bool live = false; };
    struct Effect {
        std::uint64_t token = 0, owner = 0, expires = 0;
        Target target;
        MovementState state;
        bool live = false;
    };
    static constexpr std::uint64_t firstOwner = 0x1122334400000101ULL;
    static constexpr std::uint64_t firstEffect = 0x8877665500000202ULL;
    ServiceContext current{7, 1};
    ServiceStatus contextResult = ServiceStatus::ok;
    ServiceStatus nextUpdateResult = ServiceStatus::ok;
    std::uint32_t serial = 123, life = 4;
    std::uint64_t nextOwner = firstOwner, nextEffect = firstEffect;
    std::uint64_t lastOwner = 0, lastEffect = 0, lastKey = 0, lastNow = 0;
    Target lastTarget;
    MovementState lastState;
    std::uint32_t lastLease = 0;
    goldcraft::effect_clock::Clock observedClock{nullptr};
    ServiceStatus ObserveClock(const goldcraft::effect_clock::RawSample& sample) noexcept override {
        if (contextResult == ServiceStatus::clock_failed) return contextResult;
        goldcraft::effect_clock::Sample qualified;
        if (observedClock.observe(sample, qualified) == goldcraft::effect_clock::Error::none) return ServiceStatus::ok;
        ClockFault();
        return ServiceStatus::clock_failed;
    }
    int clockFaults = 0;
    void ClockFault() noexcept override {
        ++clockFaults; contextResult = ServiceStatus::clock_failed; owners = {}; effects = {};
    }
    int contexts = 0, creates = 0, resolves = 0, applies = 0, updates = 0, removes = 0, revokes = 0;
    std::array<Owner, 16> owners{};
    std::array<Effect, 32> effects{};

    void reset_fixture() noexcept {
        current = {7, 1}; contextResult = nextUpdateResult = ServiceStatus::ok;
        serial = 123; life = 4; nextOwner = firstOwner; nextEffect = firstEffect;
        lastOwner = lastEffect = lastKey = lastNow = 0; lastTarget = {}; lastState = {}; lastLease = 0;
        clockFaults = 0;
        observedClock = goldcraft::effect_clock::Clock(nullptr);
        contexts = creates = resolves = applies = updates = removes = revokes = 0;
        owners = {}; effects = {};
    }
    void change_context() noexcept {
        ++current.world; ++current.generation;
        owners = {}; effects = {};
    }
    void change_life() noexcept {
        ++life;
        for (auto& effect : effects) effect.live = false;
    }
    void host_pump(std::uint64_t now) noexcept {
        for (auto& effect : effects) if (effect.live && effect.expires <= now) effect.live = false;
    }
    bool live_owner(std::uint64_t token) const noexcept {
        for (const auto& owner : owners) if (owner.live && owner.token == token) return true;
        return false;
    }
    Effect* find_effect(std::uint64_t token) noexcept {
        for (auto& effect : effects) if (effect.live && effect.token == token) return &effect;
        return nullptr;
    }
    bool same_context(const ServiceContext& context) const noexcept { return context == current; }

    ServiceStatus GetContext(ServiceContext* output) noexcept override {
        ++contexts;
        if (contextResult != ServiceStatus::ok) return contextResult;
        *output = current;
        return ServiceStatus::ok;
    }
    ServiceStatus CreateOwner(const ServiceContext& context, std::uint64_t now,
                              std::uint64_t* output) noexcept override {
        ++creates; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        for (auto& owner : owners) if (!owner.live) {
            owner = {nextOwner++, true}; *output = owner.token; lastOwner = owner.token;
            return ServiceStatus::ok;
        }
        return ServiceStatus::capacity_exhausted;
    }
    ServiceStatus ResolveTarget(const ServiceContext& context, TargetKind kind, std::uint64_t key,
                                std::uint64_t now, Target* output) noexcept override {
        ++resolves; lastKey = key; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        *output = {current.world, kind, key, serial, life};
        lastTarget = *output;
        return ServiceStatus::ok;
    }
    ServiceStatus Apply(const ServiceContext& context, std::uint64_t owner, const Target& target,
                        const MovementState& state, std::uint32_t lease, std::uint64_t now,
                        std::uint64_t* output) noexcept override {
        ++applies; lastOwner = owner; lastTarget = target; lastState = state; lastLease = lease; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        if (!live_owner(owner)) return ServiceStatus::invalid_owner;
        if (target.world != current.world || target.serial != serial || target.life != life)
            return ServiceStatus::invalid_target;
        for (auto& effect : effects) if (!effect.live) {
            effect = {nextEffect++, owner, now + lease, target, state, true};
            *output = effect.token; lastEffect = effect.token;
            return ServiceStatus::ok;
        }
        return ServiceStatus::capacity_exhausted;
    }
    ServiceStatus Update(const ServiceContext& context, std::uint64_t owner, std::uint64_t handle,
                         const MovementState& state, std::uint32_t lease, std::uint64_t now) noexcept override {
        ++updates; lastOwner = owner; lastEffect = handle; lastState = state; lastLease = lease; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        if (nextUpdateResult != ServiceStatus::ok) {
            const auto result = nextUpdateResult; nextUpdateResult = ServiceStatus::ok;
            return result;
        }
        host_pump(now);
        Effect* effect = find_effect(handle);
        if (!effect) return ServiceStatus::missing_effect;
        if (effect->owner != owner) return ServiceStatus::wrong_owner;
        effect->state = state; effect->expires = now + lease;
        return ServiceStatus::ok;
    }
    ServiceStatus Remove(const ServiceContext& context, std::uint64_t owner, std::uint64_t handle,
                         std::uint64_t now) noexcept override {
        ++removes; lastOwner = owner; lastEffect = handle; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        Effect* effect = find_effect(handle);
        if (!effect) return ServiceStatus::missing_effect;
        if (effect->owner != owner) return ServiceStatus::wrong_owner;
        effect->live = false;
        return ServiceStatus::ok;
    }
    ServiceStatus RevokeOwner(const ServiceContext& context, std::uint64_t owner,
                              std::uint64_t now) noexcept override {
        ++revokes; lastOwner = owner; lastNow = now;
        if (!same_context(context)) return ServiceStatus::stale_context;
        for (auto& entry : owners) if (entry.token == owner) entry.live = false;
        for (auto& effect : effects) if (effect.owner == owner) effect.live = false;
        return ServiceStatus::ok;
    }
} provider;

int add_natives(const AMX_NATIVE_INFO* table) { ++registrationCalls; registeredNatives = table; return 1; }
int find_script(const AMX* amx) {
    for (std::size_t i = 0; i < loadedScripts.size(); ++i)
        if (loadedScripts[i] && loadedScripts[i] == amx) return static_cast<int>(i);
    return -1;
}
AMX* get_script(int index) {
    if (index < 0 || index >= static_cast<int>(loadedScripts.size())) return nullptr;
    if (mismatchScriptLookup) return &foreignScript;
    return loadedScripts[static_cast<std::size_t>(index)];
}
// SDK amx_ctof is a fourth pointer, not an MF lifecycle API. Its exact 32-bit
// bit conversion is supplied here; no Pawn VM or private memory is emulated.
REAL cell_to_real(cell value) { return std::bit_cast<float>(value); }

HMODULE WINAPI module_handle(LPCSTR name) {
    ++moduleCalls;
    return moduleAvailable && name && std::strcmp(name, "mp.dll") == 0
        ? reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1)) : nullptr;
}
FARPROC WINAPI proc_address(HMODULE module, LPCSTR name);
bool qpc(goldcraft::effect_clock::RawSample& output) noexcept {
    ++clockCalls;
    if (!clockSucceeds) return false;
    output = {static_cast<std::int64_t>(clockMs * 1000), clockFrequency};
    return true;
}
} // namespace adapter_test

// Exactly the three MF imports used by the adapter, with the pinned typedefs.
PFN_ADD_NATIVES g_fn_AddNatives = adapter_test::add_natives;
PFN_FIND_AMXSCRIPT_BYAMX g_fn_FindAmxScriptByAmx = adapter_test::find_script;
PFN_GET_AMXSCRIPT g_fn_GetAmxScript = adapter_test::get_script;
PFN_CELL_TO_REAL g_fn_CellToReal = adapter_test::cell_to_real;

extern "C" IBaseInterface* CreateInterface(const char* name, int* status) {
    ++adapter_test::factoryCalls;
    const bool found = adapter_test::providerAvailable && name &&
        std::strcmp(name, goldcraft::amxx_effects::service_version) == 0;
    if (status) *status = found ? IFACE_OK : IFACE_FAILED;
    return found ? &adapter_test::provider : nullptr;
}
FARPROC WINAPI adapter_test::proc_address(HMODULE module, LPCSTR name) {
    return factoryAvailable && module && name && std::strcmp(name, "CreateInterface") == 0
        ? reinterpret_cast<FARPROC>(&CreateInterface) : nullptr;
}

// Include the real implementation. Windows/SDK headers above have already been
// parsed; only module lookup/export calls are substituted. The clock uses its
// injected raw-reader boundary; no Win32 declaration is rewritten.
#define GetModuleHandleA adapter_test::module_handle
#define GetProcAddress adapter_test::proc_address
#include "../../native/amxx/effect_adapter.cpp"
#undef GetModuleHandleA
#undef GetProcAddress

namespace adapter_test {
namespace api = goldcraft::amxx_effects;
int checks = 0, failures = 0;
void expect(bool value, const char* expression, int line) {
    ++checks;
    if (!value) { ++failures; std::cerr << "line " << line << ": " << expression << '\n'; }
}
#define CHECK(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

AMX_NATIVE lookup(const char* name) {
    if (registeredNatives) for (const AMX_NATIVE_INFO* native = registeredNatives; native->name; ++native)
        if (std::strcmp(native->name, name) == 0) return native->func;
    std::cerr << "missing registered native " << name << '\n';
    std::abort();
}
cell call(const char* name, AMX* amx, std::initializer_list<cell> arguments = {}) {
    std::array<cell, 12> params{};
    if (arguments.size() >= params.size()) std::abort();
    params[0] = static_cast<cell>(arguments.size() * sizeof(cell));
    std::size_t index = 1;
    for (cell value : arguments) params[index++] = value;
    return lookup(name)(amx, params.data());
}
cell real(float value) { return std::bit_cast<cell>(value); }
cell target(AMX* amx = &scriptA) { return call("gc_effect_target", amx, {1, 1, 0}); }
cell apply(cell identity, AMX* amx = &scriptA, cell lease = 100) {
    return call("gc_effect_apply", amx, {identity, 4, 0, 0, real(80), real(1), lease});
}
cell update(cell effect, AMX* amx = &scriptA, cell lease = 100) {
    return call("gc_effect_update", amx, {effect, 4, 0, 0, real(40), real(1), lease});
}
void fixture() {
    api::OnPluginsUnloading();
    clockMs += 1000;
    clockSucceeds = true; clockFrequency = 1000000;
    // New clock only in the boundary fixture, never a production lifecycle reset.
    api::clock = goldcraft::effect_clock::Clock(qpc); // Alias allocator still survives.
    provider.reset_fixture(); loadedScripts = {&scriptA, &scriptB}; mismatchScriptLookup = false;
    moduleAvailable = factoryAvailable = providerAvailable = true;
    api::OnPluginsLoaded();
}

void actual_table_and_arity() {
    api::OnAttach();
    CHECK(registrationCalls == 1 && registeredNatives != nullptr);
    constexpr const char* names[] = {"gc_effect_api_version", "gc_effect_target", "gc_effect_apply",
        "gc_effect_update", "gc_effect_drop", "gc_effect_owner_release"};
    constexpr unsigned expectedArguments[] = {0, 3, 7, 7, 1, 0};
    std::size_t count = 0;
    for (const AMX_NATIVE_INFO* native = registeredNatives; native->name; ++native) ++count;
    CHECK(count == 6);
    fixture();
    const int callsBefore = provider.contexts;
    for (std::size_t i = 0; i < 6; ++i) {
        std::array<cell, 12> params{};
        params[0] = static_cast<cell>((expectedArguments[i] + 1) * sizeof(cell));
        CHECK(lookup(names[i])(&scriptA, params.data()) == -1);
        params[0] = -4; CHECK(lookup(names[i])(&scriptA, params.data()) == -1);
        params[0] = 3; CHECK(lookup(names[i])(&scriptA, params.data()) == -1);
        CHECK(lookup(names[i])(&scriptA, nullptr) == -1);
    }
    CHECK(provider.contexts == callsBefore);
    CHECK(call("gc_effect_api_version", &scriptA) == 1);
}

void caller_identity_and_unavailable_provider() {
    fixture();
    CHECK(call("gc_effect_api_version", nullptr) == -2);
    CHECK(call("gc_effect_api_version", &foreignScript) == -2);
    mismatchScriptLookup = true;
    CHECK(call("gc_effect_api_version", &scriptA) == -2);
    mismatchScriptLookup = false;
    CHECK(provider.contexts == 0 && provider.creates == 0);
    moduleAvailable = false;
    CHECK(call("gc_effect_api_version", &scriptA) == -3);
    moduleAvailable = true; factoryAvailable = false;
    CHECK(call("gc_effect_api_version", &scriptA) == -3);
    factoryAvailable = true; providerAvailable = false;
    CHECK(call("gc_effect_api_version", &scriptA) == -3);
    providerAvailable = true; provider.contextResult = ServiceStatus::not_ready;
    CHECK(call("gc_effect_api_version", &scriptA) == -101);
    provider.contextResult = ServiceStatus::ok;
    CHECK(call("gc_effect_api_version", &scriptA) == 1);
    CHECK(provider.creates == 0); // Availability queries do not manufacture owners.
}

void ownership_aliases_and_64_bit_identity() {
    fixture();
    const cell aTarget = target();
    CHECK(aTarget > 0 && target() == aTarget && provider.creates == 1);
    const cell effect = apply(aTarget);
    CHECK(effect > aTarget);
    CHECK(provider.lastOwner == FakeService::firstOwner);
    CHECK(provider.lastEffect == FakeService::firstEffect);
    CHECK(provider.lastTarget == (Target{7, TargetKind::player, 1, 123, 4}));
    CHECK(provider.lastState == (MovementState{4, 0, 0, 80, 1}));
    const int applies = provider.applies, updates = provider.updates, removes = provider.removes;
    CHECK(apply(aTarget, &scriptB) == -4);
    CHECK(update(effect, &scriptB) == -4);
    CHECK(call("gc_effect_drop", &scriptB, {effect}) == -4);
    CHECK(provider.applies == applies && provider.updates == updates && provider.removes == removes);
    CHECK(update(aTarget) == -4); // Correct owner, wrong alias type.
    CHECK(apply(effect) == -4);
    CHECK(update(effect) == 1);
    CHECK(provider.lastOwner == FakeService::firstOwner && provider.lastEffect == FakeService::firstEffect);
    CHECK(call("gc_effect_drop", &scriptA, {effect}) == 1);
    CHECK(provider.lastOwner == FakeService::firstOwner && provider.lastEffect == FakeService::firstEffect);
    CHECK(provider.find_effect(FakeService::firstEffect) == nullptr);

    constexpr std::uint64_t objectKey = 0xFEDCBA9876543210ULL;
    const cell low = std::bit_cast<cell>(std::uint32_t{0x76543210});
    const cell high = std::bit_cast<cell>(std::uint32_t{0xFEDCBA98});
    const cell object = call("gc_effect_target", &scriptA, {2, low, high});
    CHECK(object > 0 && provider.lastKey == objectKey);
    const cell objectEffect = apply(object);
    CHECK(objectEffect > 0 && provider.lastTarget.key == objectKey);
    CHECK(provider.lastTarget.kind == TargetKind::object);
    CHECK(call("gc_effect_target", &scriptA, {1, 1, 1}) == -5);
    CHECK(call("gc_effect_target", &scriptA, {1, 65, 0}) == -5);
    CHECK(call("gc_effect_target", &scriptA, {2, 0, 0}) == -5);
    CHECK(call("gc_effect_target", &scriptA, {3, 1, 0}) == -5);
}

void payload_rejection_and_update_results() {
    fixture();
    const cell identity = target();
    const int before = provider.applies;
    CHECK(call("gc_effect_apply", &scriptA, {identity, 4, 0, 0, real(std::numeric_limits<float>::quiet_NaN()), real(1), 100}) == -5);
    CHECK(call("gc_effect_apply", &scriptA, {identity, 8, 0, 0, real(0), real(std::numeric_limits<float>::infinity()), 100}) == -5);
    CHECK(call("gc_effect_apply", &scriptA, {identity, 16, 0, 0, real(0), real(1), 100}) == -5);
    CHECK(call("gc_effect_apply", &scriptA, {identity, 1, 16, 0, real(0), real(1), 100}) == -5);
    CHECK(call("gc_effect_apply", &scriptA, {identity, 2, 0, 8, real(0), real(1), 100}) == -5);
    CHECK(apply(identity, &scriptA, 0) == -5 && apply(identity, &scriptA, 60001) == -5);
    CHECK(provider.applies == before);
    const cell effect = apply(identity);
    CHECK(effect > 0);
    provider.nextUpdateResult = ServiceStatus::invalid_state;
    CHECK(update(effect) == -107);
    CHECK(provider.find_effect(FakeService::firstEffect)->state.wishSpeedCap == 80);
    CHECK(update(effect) == 1); // Rejected payload does not discard a valid alias.
    provider.nextUpdateResult = ServiceStatus::missing_effect;
    CHECK(update(effect) == -105);
    const int called = provider.updates;
    CHECK(update(effect) == -4 && provider.updates == called);

    const cell second = apply(identity);
    provider.nextUpdateResult = ServiceStatus::invalid_target;
    CHECK(update(second) == -104);
    CHECK(update(second) == -4);
    const cell third = apply(identity);
    provider.nextUpdateResult = ServiceStatus::invalid_owner;
    CHECK(update(third) == -103);
    CHECK(update(third) == -4);
}

void lease_and_clock_boundaries() {
    fixture();
    const cell identity = target();
    const cell effect = apply(identity, &scriptA, 10);
    const auto handle = provider.lastEffect;
    const auto deadline = clockMs + 10;
    ++clockMs;
    provider.nextUpdateResult = ServiceStatus::invalid_state;
    CHECK(update(effect, &scriptA, 100) == -107);
    CHECK(provider.find_effect(handle)->expires == deadline);
    clockMs = deadline - 1;
    api::OnFrame();
    CHECK(provider.find_effect(handle) != nullptr);
    clockMs = deadline;
    provider.host_pump(clockMs); // Independent provider frame, no Pawn heartbeat.
    api::OnFrame();
    const int before = provider.updates;
    CHECK(update(effect) == -4 && provider.updates == before);
    CHECK(provider.find_effect(handle) == nullptr);

    const cell renewed = apply(identity, &scriptA, 10);
    const auto renewedHandle = provider.lastEffect;
    clockMs += 9;
    CHECK(update(renewed, &scriptA, 20) == 1);
    const auto newDeadline = clockMs + 20;
    ++clockMs; api::OnFrame();
    CHECK(update(renewed, &scriptA, 20) == 1);
    CHECK(provider.find_effect(renewedHandle)->expires > newDeadline);
    const auto monotonic = clockMs;
    --clockMs;
    const int previousUpdates = provider.updates;
    CHECK(update(renewed) == -115);
    CHECK(provider.clockFaults == 1 && provider.updates == previousUpdates);
    CHECK(provider.find_effect(renewedHandle) == nullptr);
    clockMs = monotonic;
    CHECK(update(renewed) == -115 && provider.updates == previousUpdates);
    api::OnPluginsUnloading(); api::OnPluginsLoaded();
    CHECK(target() == -115); // Map/plugin reload does not reset the failed clock.
}

void clock_failure_on_unload_never_revokes_with_bad_time() {
    fixture();
    const auto identity = target(), effect = apply(identity);
    CHECK(effect > 0);
    const auto revokes = provider.revokes;
    clockSucceeds = false;
    api::OnPluginsUnloading();
    CHECK(provider.clockFaults == 1 && provider.revokes == revokes);
    CHECK(provider.contextResult == ServiceStatus::clock_failed);
    clockSucceeds = true;
    api::OnPluginsLoaded();
    CHECK(target() == -115 && provider.revokes == revokes);
}

void frequency_change_closes_before_provider_mutation() {
    fixture();
    const auto identity = target(), effect = apply(identity);
    CHECK(effect > 0);
    const auto updates = provider.updates, resolves = provider.resolves;
    ++clockFrequency;
    CHECK(update(effect) == -115);
    CHECK(provider.clockFaults == 1 && provider.updates == updates && provider.resolves == resolves);
    --clockFrequency;
    CHECK(target() == -115 && provider.resolves == resolves);
}

void unload_context_and_identity_changes() {
    fixture();
    const cell initialTarget = target();
    const cell initialEffect = apply(initialTarget);
    const auto initialOwner = provider.lastOwner;
    api::OnPluginsUnloading();
    CHECK(!provider.live_owner(initialOwner));
    CHECK(call("gc_effect_api_version", &scriptA) == -3);
    api::OnPluginsLoaded();
    const cell replacementTarget = target();
    const cell replacementEffect = apply(replacementTarget);
    CHECK(replacementTarget > initialEffect && replacementEffect > replacementTarget);
    CHECK(update(initialEffect) == -4);
    CHECK(provider.lastOwner != initialOwner);

    provider.change_context();
    CHECK(update(replacementEffect) == -4);
    const cell newWorldTarget = target();
    CHECK(newWorldTarget > replacementEffect);
    const cell beforeLife = apply(newWorldTarget);
    provider.change_life();
    const cell newLife = target();
    CHECK(newLife > beforeLife);
    CHECK(update(beforeLife) == -4);
    const cell newLifeEffect = apply(newLife);
    CHECK(provider.lastTarget.life == 5 && newLifeEffect > newLife);

    CHECK(call("gc_effect_owner_release", &scriptA) == 1);
    CHECK(update(newLifeEffect) == -4);
    const cell afterRelease = target();
    CHECK(afterRelease > newLifeEffect);
    const cell effectA = apply(afterRelease);
    const auto ownerA = provider.lastOwner;
    const cell targetB = target(&scriptB);
    const cell effectB = apply(targetB, &scriptB);
    const auto ownerB = provider.lastOwner;
    CHECK(ownerA != ownerB && effectA != effectB);
    loadedScripts[0] = nullptr;
    api::OnFrame();
    CHECK(!provider.live_owner(ownerA) && provider.live_owner(ownerB));
    CHECK(update(effectA) == -2);
    CHECK(update(effectB, &scriptB) == 1);
    api::OnDetach();
    CHECK(!provider.live_owner(ownerB));
}
} // namespace adapter_test

int main() {
    using namespace adapter_test;
    actual_table_and_arity();
    caller_identity_and_unavailable_provider();
    ownership_aliases_and_64_bit_identity();
    payload_rejection_and_update_results();
    lease_and_clock_boundaries();
    clock_failure_on_unload_never_revokes_with_bad_time();
    frequency_change_closes_before_provider_mutation();
    unload_context_and_identity_changes();
    api::OnDetach();
    std::cout << checks << " adapter boundary checks, " << failures
              << " failures; no AMXX VM/ReHLDS or physical effect execution\n";
    return failures ? 1 : 0;
}
