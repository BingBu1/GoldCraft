// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include "precache_client.hpp"
#include "resource_lifecycle.hpp"

#include <atomic>
#include <cstring>
#include <limits>

namespace goldcraft::resource_lifecycle {
namespace {
static_assert(sizeof(void*) == 4);
using Host = resource_native_hud::Host;
using HostFrame = int (__cdecl*)(float, int, int*);
Host host;
HostFrame original_frame{};
Host::VidInit original_vidinit{};
Handler handler;
resource_native_hud::Binding native_binding;
DWORD thread{};
bool attempted{};
std::atomic<const char*> error{};
std::atomic<bool> blocked{};
std::uint64_t serial{}, clear_epoch{}, initial_vidinit{}, consumed_clear{};
std::uint64_t active_frame{}, dispatch{}, dispatch_frame{}, dispatch_epoch{};
bool dispatch_called{};
struct Invocation {
    std::uint64_t id{}, frame{};
    Completion completion{};
} invocation;

void fail(const char* message) noexcept {
    const char* expected = nullptr;
    error.compare_exchange_strong(expected, message, std::memory_order_relaxed);
}

bool on_thread() noexcept {
    // This check precedes every access to mutable observation state. No lock or
    // stack-owned guard may span a call which Host_Error can longjmp through.
    if (!thread) return false;
    if (GetCurrentThreadId() != thread) {
        fail("native resource lifecycle called from another thread");
        return false;
    }
    return error.load(std::memory_order_relaxed) == nullptr;
}

std::uint64_t next() noexcept {
    if (serial == std::numeric_limits<std::uint64_t>::max()) {
        fail("native resource lifecycle token exhausted");
        return 0;
    }
    return ++serial;
}

bool pending_callback() noexcept {
    return invocation.id && invocation.completion == Completion::none;
}

int untracked_vidinit() {
    return blocked.load(std::memory_order_relaxed) ? 0 : original_vidinit();
}

void clear_returned() noexcept {
    if (!on_thread()) return;
    clear_epoch = next();
    initial_vidinit = 0;
    // ClearState occurs BEFORE Host_Error's longjmp. Neither invocation nor
    // dispatch is closed here, even if the resource cancellation runs next.
}

int invoke(bool managed) {
    if (!on_thread()) return managed ? 0 : untracked_vidinit();
    if (pending_callback()) {
        fail("native HUD callback is nested or has no return proof");
        return managed ? 0 : untracked_vidinit();
    }
    if (managed && !host.OwnerCurrent()) return 0;
    const auto token = next();
    if (!token) return managed ? 0 : untracked_vidinit();
    const auto epoch = clear_epoch;
    invocation = {token, active_frame, Completion::none};
    // The real trampoline is captured by Host; this gateway observes the same
    // original call without presenting itself as Host's trampoline.
    const int result = original_vidinit();
    if (on_thread() && invocation.id == token) {
        invocation.completion = Completion::returned;
        if (dispatch && dispatch_epoch == epoch && clear_epoch == epoch && result > 0)
            dispatch_called = true;
    }
    return managed && (!on_thread() || !host.OwnerCurrent()) ? 0 : result;
}

int tracked_vidinit(void*) { return invoke(true); }
int resolution(void*) noexcept {
    if (!on_thread()) return 0;
    const auto native = host.GetBinding();
    return native.current_resolution ? native.current_resolution(native.context) : 0;
}
void delete_texture(void*, int texture) noexcept {
    // As with Host's Binding, retirement belongs to the captured Surface, even
    // when a newer Surface is current. The bridge guarantees that this owner
    // remains alive and performs retirement on the owning thread.
    native_binding.delete_texture(native_binding.context, texture);
}

int __cdecl vidinit() {
    if (!on_thread()) return untracked_vidinit();
    if (dispatch || pending_callback()) {
        fail("native HUD export reentered before its previous dispatch returned");
        return untracked_vidinit();
    }
    const auto previous = observe();
    const auto token = next();
    if (!token) return untracked_vidinit();
    const Entry entry{token, active_frame, previous, clear_epoch && clear_epoch != consumed_clear};
    consumed_clear = clear_epoch;
    dispatch = token;
    dispatch_frame = active_frame;
    dispatch_epoch = clear_epoch;
    dispatch_called = false;
    // Capture cannot run in LoadClient: hook transactions are still deferred
    // there, and the original client may not have acquired its Surface yet.
    const char* capture_error = host.Capture(original_vidinit);
    if (capture_error) fail(capture_error);
    else native_binding = host.GetBinding();
    const int result = capture_error ? untracked_vidinit() :
        (handler.vid_init ? handler.vid_init(handler.context, entry) : invoke(false));
    if (on_thread() && dispatch == token) {
        if (result > 0 && dispatch_called && clear_epoch && clear_epoch == entry.previous.clear_epoch)
            initial_vidinit = token;
        dispatch = dispatch_frame = dispatch_epoch = 0;
        dispatch_called = false;
    }
    // A handler failure is its result. Never call the original a second time.
    return result;
}

int __cdecl frame(float time, int state, int* output) {
    if (!on_thread()) return original_frame(time, state, output);
    if (active_frame) {
        fail("native outer Host_Frame is nested or did not return");
        return original_frame(time, state, output);
    }
    const auto token = next();
    if (!token) return original_frame(time, state, output);
    active_frame = token;
    const int result = original_frame(time, state, output);
    if (on_thread() && active_frame == token) {
        if (pending_callback() && invocation.frame == token)
            invocation.completion = Completion::frame_returned;
        if (dispatch && dispatch_frame == token) {
            dispatch = dispatch_frame = dispatch_epoch = 0;
            dispatch_called = false;
        }
        active_frame = 0;
    }
    // This is a return proof only. It does not refresh/free resources, report
    // successful native initialization, or resume a download/signon gate.
    return result;
}

bool frame_bytes(std::byte* base, std::size_t size) noexcept {
    constexpr std::size_t entry = 0x1d28b0, buffer = 0x1249e40, caller = 0x2211c5;
    constexpr unsigned char prefix[]{0x55,0x8b,0xec,0x83,0xec,0x14,0x53,0x56,0x57,0x6a,0x00,0x68};
    constexpr unsigned char suffix[]{0xe8,0x39,0xb1,0x0d,0x00,0x83,0xc4,0x08,0x85,0xc0,0x0f,0x85,0x57,0x02,0x00,0x00};
    constexpr unsigned char callsite[]{0x8d,0x4d,0xfc,0x66,0x0f,0x5a,0xc1,0x51,0x50,0x51,0xf3,0x0f,0x11,0x04,0x24,0xe8,0xd7,0x16,0xfb,0xff,0x83,0xc4,0x0c,0x3b,0x46,0x08};
    if (!base || size < buffer + sizeof(void*)) return false;
    std::uintptr_t operand{};
    std::memcpy(&operand, base + entry + sizeof(prefix), sizeof(operand));
    return operand == reinterpret_cast<std::uintptr_t>(base) + buffer &&
        !std::memcmp(base + entry, prefix, sizeof(prefix)) &&
        !std::memcmp(base + entry + sizeof(prefix) + sizeof(operand), suffix, sizeof(suffix)) &&
        !std::memcmp(base + caller, callsite, sizeof(callsite));
}
}

const char* install(metahook_api_s* api, Host::ScreenInfo screen, Handler value) {
    if (attempted) return "native resource lifecycle installation is one-shot";
    attempted = true;
    thread = GetCurrentThreadId();
    if (!api || !api->InlineHook || !api->UnHook) {
        fail("native resource lifecycle hook API is unavailable");
        return failure();
    }
    if (const auto* message = host.Resolve(api, screen)) {
        fail(message); return failure();
    }
    void* address{};
    auto* engine = static_cast<std::byte*>(api->GetEngineBase());
    if (api->ResolveGameSymbol(engine, "GoldCraftNativeHud_HostFrame", MH_GAMESYMBOL_KIND_FUNCTION,
            &address) != MH_GAMESYMBOL_OK || address != engine + 0x1d28b0 ||
        !frame_bytes(engine, api->GetEngineSize())) {
        fail("native resource lifecycle requires the verified outer Host_Frame");
        return failure();
    }
    handler = value;
    const auto frame_hook = api->InlineHook(address, reinterpret_cast<void*>(frame),
        reinterpret_cast<void**>(&original_frame));
    if (!frame_hook) {
        fail("native resource lifecycle could not hook Host_Frame"); return failure();
    }
    if (!api->InlineHook(host.Entry(), reinterpret_cast<void*>(vidinit),
            reinterpret_cast<void**>(&original_vidinit))) {
        if (!api->UnHook(frame_hook)) {
            fail("native resource lifecycle partial installation could not be removed");
            return failure();
        }
        fail("native resource lifecycle could not hook HUD_VidInit"); return failure();
    }
    client_precache::set_clear_state_observer(clear_returned);
    return nullptr;
}

resource_native_hud::Binding binding() noexcept {
    if (!on_thread()) return {};
    auto result = host.GetBinding();
    if (result.hud.empty()) return {};
    result.context = &host;
    result.vid_init = tracked_vidinit;
    result.current_resolution = resolution;
    result.delete_texture = delete_texture;
    return result;
}

Observation observe() noexcept {
    if (!on_thread()) return {};
    return {clear_epoch, initial_vidinit, invocation.id, invocation.frame, invocation.completion, true};
}

bool current(const Entry& entry) noexcept {
    return on_thread() && entry.event && dispatch == entry.event && dispatch_frame == entry.frame &&
        clear_epoch == entry.previous.clear_epoch && host.OwnerCurrent();
}

const char* failure() noexcept { return error.load(std::memory_order_relaxed); }

bool block_untracked_calls(bool block) noexcept {
    if (!thread || GetCurrentThreadId() != thread) return false;
    if (!block && (!on_thread() || pending_callback())) return false;
    blocked.store(block, std::memory_order_relaxed);
    return true;
}
}
