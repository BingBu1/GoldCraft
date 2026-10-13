#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <amxxmodule.h>
#ifndef EXT_FUNC
#define EXT_FUNC
#endif
#include <interface.h>
#include "effect_adapter.hpp"
#include "effect_service.hpp"

#include <limits>
#include <map>
#include <new>

namespace goldcraft::amxx_effects {
namespace {
static_assert(sizeof(cell) == 4, "this candidate requires the pinned 32-bit Pawn cell ABI");
static_assert(sizeof(float) == 4);

// Positive cells are module-local aliases, never truncated service tokens.
// No alias is reused during this module lifetime, even across AMXX map reloads.
constexpr std::size_t maximum_callers = 64;
constexpr std::size_t maximum_aliases = 1024;
constexpr cell bad_arity = -1, bad_caller = -2, unavailable = -3;
constexpr cell bad_handle = -4, bad_input = -5, local_capacity = -6;
constexpr cell alias_exhausted = -7, allocation_failure = -8;
constexpr cell service_failure = -9;

enum class AliasKind { target, effect };
struct Caller { std::uint64_t owner; };
struct Alias {
    AMX* caller;
    std::uint64_t owner;
    AliasKind kind;
    effects::Target target;
    std::uint64_t effect = 0;
    std::uint64_t expires = 0;
};

IEffectService* service = nullptr;
ServiceContext context;
bool plugins_live = false;
effect_clock::Clock clock;
std::uint64_t next_alias = 1;
std::map<AMX*, Caller> callers;
std::map<cell, Alias> aliases;

cell failure(ServiceStatus status) noexcept {
    const auto value = static_cast<std::uint32_t>(status);
    return value <= static_cast<std::uint32_t>(ServiceStatus::clock_failed)
        ? -100 - static_cast<cell>(value) : service_failure;
}

bool valid_caller(AMX* amx) {
    if (!amx) return false;
    const int script = MF_FindScriptByAmx(amx);
    return script >= 0 && MF_GetScriptAmx(script) == amx;
}

bool arity(const cell* params, unsigned count) noexcept {
    return params && params[0] == static_cast<cell>(count * sizeof(cell));
}

void erase_aliases(AMX* amx) {
    for (auto alias = aliases.begin(); alias != aliases.end();) {
        if (alias->second.caller == amx) alias = aliases.erase(alias);
        else ++alias;
    }
}

void clear_local() {
    aliases.clear();
    callers.clear();
    // next_alias deliberately survives every local/map reset.
}

ServiceStatus release_caller(AMX* amx, std::uint64_t now) {
    const auto found = callers.find(amx);
    if (found == callers.end()) return ServiceStatus::ok;
    const auto status = service
        ? service->RevokeOwner(context, found->second.owner, now)
        : ServiceStatus::not_ready;
    // Fail closed locally even if the provider is temporarily unavailable.
    // Its independent lease pump remains responsible for bounded expiry.
    erase_aliases(amx);
    callers.erase(found);
    return status;
}

void sweep(std::uint64_t now) {
    for (auto caller = callers.begin(); caller != callers.end();) {
        if (!valid_caller(caller->first)) {
            AMX* removed = caller->first;
            ++caller;
            release_caller(removed, now);
        } else ++caller;
    }
    for (auto alias = aliases.begin(); alias != aliases.end();) {
        if (alias->second.kind == AliasKind::effect && alias->second.expires <= now)
            alias = aliases.erase(alias);
        else ++alias;
    }
}

cell clock_failure() noexcept {
    // No provider mutation with zero/last-good/invented time. The public 002
    // fault path revokes eligibility even when this adapter owns live effects.
    if (service) service->ClockFault();
    clear_local();
    return failure(ServiceStatus::clock_failed);
}

cell refresh(std::uint64_t& now) {
    if (!plugins_live) return unavailable;
    if (!service) {
        const HMODULE module = GetModuleHandleA("mp.dll");
        const auto factory = module
            ? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface")) : nullptr;
        service = factory ? static_cast<IEffectService*>(factory(service_version, nullptr)) : nullptr;
        if (!service) return unavailable;
    }
    if (clock.failed()) return clock_failure();
    ServiceContext current;
    const auto result = service->GetContext(&current);
    if (result == ServiceStatus::clock_failed) return clock_failure();
    if (result != ServiceStatus::ok) return failure(result);
    if (!current.world || !current.generation) return service_failure;
    effect_clock::Sample sample;
    if (clock.read(sample) != effect_clock::Error::none) return clock_failure();
    if (service->ObserveClock(sample.raw) != ServiceStatus::ok) return clock_failure();
    now = sample.milliseconds;
    if (current != context) {
        // Contract: a changed context means old provider state is already dead.
        clear_local();
        context = current;
    }
    sweep(now);
    return 0;
}

cell begin(AMX* amx, const cell* params, unsigned count, std::uint64_t& now) {
    if (!arity(params, count)) return bad_arity;
    if (!valid_caller(amx)) return bad_caller;
    return refresh(now);
}

cell ensure_owner(AMX* amx, std::uint64_t now, std::uint64_t& owner) {
    if (const auto caller = callers.find(amx); caller != callers.end()) {
        owner = caller->second.owner;
        return 0;
    }
    if (callers.size() == maximum_callers) return local_capacity;
    const auto result = service->CreateOwner(context, now, &owner);
    if (result != ServiceStatus::ok) return failure(result);
    if (!owner) return service_failure;
    try { callers.emplace(amx, Caller{owner}); }
    catch (...) { service->RevokeOwner(context, owner, now); throw; }
    return 0;
}

cell alias_room() noexcept {
    if (aliases.size() == maximum_aliases) return local_capacity;
    if (next_alias > static_cast<std::uint64_t>(std::numeric_limits<cell>::max())) return alias_exhausted;
    return 0;
}

cell insert_alias(const Alias& value) {
    const auto token = static_cast<cell>(next_alias);
    aliases.emplace(token, value);
    ++next_alias;
    return token;
}

auto find_alias(AMX* caller, cell token, AliasKind kind) {
    auto found = aliases.find(token);
    if (token <= 0 || found == aliases.end() || found->second.caller != caller ||
        found->second.kind != kind) return aliases.end();
    const auto owner = callers.find(caller);
    if (owner == callers.end() || owner->second.owner != found->second.owner) return aliases.end();
    return found;
}

bool parse_state(const cell* params, effects::MovementState& state, std::uint32_t& lease) {
    if (params[2] < 0 || params[3] < 0 || params[4] < 0 || params[7] <= 0 ||
        static_cast<std::uint32_t>(params[7]) > maximum_lease_ms) return false;
    state = {static_cast<std::uint32_t>(params[2]), static_cast<std::uint32_t>(params[3]),
             static_cast<std::uint32_t>(params[4]), amx_ctof(params[5]), amx_ctof(params[6])};
    lease = static_cast<std::uint32_t>(params[7]);
    return effects::valid_movement(state) && state.gravityFactor <= effects::maximum_gravity_factor;
}

template<class Function> cell guarded(Function&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return allocation_failure; }
    catch (...) { return service_failure; }
}

cell AMX_NATIVE_CALL api_version(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 0, now)) return error;
        return 1;
    });
}

cell AMX_NATIVE_CALL bind_target(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 3, now)) return error;
        if (params[1] != static_cast<cell>(effects::TargetKind::player) &&
            params[1] != static_cast<cell>(effects::TargetKind::object)) return bad_input;
        const auto kind = static_cast<effects::TargetKind>(params[1]);
        // Signed Pawn cells carry unsigned halves. The shift occurs only after
        // widening to uint64, never as a signed 32-bit shift or float conversion.
        const auto key = static_cast<std::uint64_t>(static_cast<std::uint32_t>(params[2])) |
                         (static_cast<std::uint64_t>(static_cast<std::uint32_t>(params[3])) << 32);
        if (!key || (kind == effects::TargetKind::player && key > 64)) return bad_input;
        effects::Target target;
        const auto result = service->ResolveTarget(context, kind, key, now, &target);
        if (result != ServiceStatus::ok) return failure(result);
        if (!effects::valid_target(target) || target.world != context.world ||
            target.kind != kind || target.key != key) return service_failure;
        std::uint64_t owner;
        if (const auto error = ensure_owner(amx, now, owner)) return error;
        for (auto alias = aliases.begin(); alias != aliases.end();) {
            const auto& value = alias->second;
            if (value.caller == amx && value.target.kind == kind && value.target.key == key) {
                if (value.target != target) { alias = aliases.erase(alias); continue; }
                if (value.kind == AliasKind::target) return alias->first;
            }
            ++alias;
        }
        if (const auto error = alias_room()) return error;
        return insert_alias(Alias{amx, owner, AliasKind::target, target});
    });
}

cell AMX_NATIVE_CALL apply(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 7, now)) return error;
        effects::MovementState state;
        std::uint32_t lease;
        if (!parse_state(params, state, lease) || lease > std::numeric_limits<std::uint64_t>::max() - now) return bad_input;
        const auto target = find_alias(amx, params[1], AliasKind::target);
        if (target == aliases.end()) return bad_handle;
        if (const auto error = alias_room()) return error;
        std::uint64_t handle = 0;
        const auto& value = target->second;
        const auto result = service->Apply(context, value.owner, value.target, state, lease, now, &handle);
        if (result != ServiceStatus::ok) return failure(result);
        if (!handle) return service_failure;
        try { return insert_alias(Alias{amx, value.owner, AliasKind::effect, value.target, handle, now + lease}); }
        catch (...) { service->Remove(context, value.owner, handle, now); throw; }
    });
}

cell AMX_NATIVE_CALL update(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 7, now)) return error;
        effects::MovementState state;
        std::uint32_t lease;
        if (!parse_state(params, state, lease) || lease > std::numeric_limits<std::uint64_t>::max() - now) return bad_input;
        const auto found = find_alias(amx, params[1], AliasKind::effect);
        if (found == aliases.end()) return bad_handle;
        auto& value = found->second;
        const auto result = service->Update(context, value.owner, value.effect, state, lease, now);
        if (result != ServiceStatus::ok) {
            if (result == ServiceStatus::missing_effect || result == ServiceStatus::invalid_target ||
                result == ServiceStatus::invalid_owner) aliases.erase(found);
            return failure(result);
        }
        value.expires = now + lease;
        return 1;
    });
}

cell AMX_NATIVE_CALL drop(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 1, now)) return error;
        const auto found = aliases.find(params[1]);
        if (params[1] <= 0 || found == aliases.end() || found->second.caller != amx) return bad_handle;
        const auto& value = found->second;
        const auto caller = callers.find(amx);
        if (caller == callers.end() || caller->second.owner != value.owner) return bad_handle;
        if (value.kind == AliasKind::effect) {
            const auto result = service->Remove(context, value.owner, value.effect, now);
            if (result != ServiceStatus::ok && result != ServiceStatus::missing_effect &&
                result != ServiceStatus::invalid_target && result != ServiceStatus::invalid_owner)
                return failure(result);
        }
        aliases.erase(found);
        return 1;
    });
}

cell AMX_NATIVE_CALL release_owner(AMX* amx, cell* params) {
    return guarded([&]() -> cell {
        std::uint64_t now;
        if (const auto error = begin(amx, params, 0, now)) return error;
        const auto result = release_caller(amx, now);
        return result == ServiceStatus::ok ? 1 : failure(result);
    });
}

const AMX_NATIVE_INFO natives[] = {
    {"gc_effect_api_version", api_version}, {"gc_effect_target", bind_target},
    {"gc_effect_apply", apply}, {"gc_effect_update", update},
    {"gc_effect_drop", drop}, {"gc_effect_owner_release", release_owner}, {nullptr, nullptr}
};
} // namespace

void OnAttach() { MF_AddNatives(natives); }

void OnPluginsLoaded() {
    // Normal AMXX already called Unloading; also make an unexpected duplicate
    // Loaded callback revoke prior registrations instead of abandoning them.
    OnPluginsUnloading();
    plugins_live = true;
    // Resolve on demand: this candidate fails unavailable while no provider exists.
}

void OnPluginsUnloading() {
    plugins_live = false;
    if (service) {
        ServiceContext current;
        const auto status = service->GetContext(&current); // Host samples before adapter.
        if (clock.failed() || status == ServiceStatus::clock_failed) {
            (void)clock_failure();
        } else if (status == ServiceStatus::ok) {
            effect_clock::Sample sample;
            if (clock.read(sample) != effect_clock::Error::none) (void)clock_failure();
            else if (service->ObserveClock(sample.raw) != ServiceStatus::ok) (void)clock_failure();
            else if (current == context) for (const auto& [amx, caller] : callers) {
                (void)amx;
                service->RevokeOwner(context, caller.owner, sample.milliseconds);
            }
        }
    }
    clear_local();
    context = {};
    service = nullptr;
}

void OnDetach() { OnPluginsUnloading(); }

void OnFrame() {
    if (!plugins_live || !service) return;
    // A real FN_StartFrame wrapper must call this and then return MRES_IGNORED.
    // Polling detects missing AMX scripts, not paused scripts or same-address ABA.
    guarded([]() -> cell { std::uint64_t now; return refresh(now); });
}

} // namespace goldcraft::amxx_effects
