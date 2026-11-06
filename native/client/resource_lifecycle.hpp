// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "resource_native_hud_host.hpp"

namespace goldcraft::resource_lifecycle {

enum class Completion { none, returned, frame_returned };

struct Observation {
    std::uint64_t clear_epoch{}, initial_vidinit{};
    std::uint64_t callback{}, callback_frame{};
    Completion completion{};
    bool valid{};
};

struct Entry {
    std::uint64_t event{}, frame{};
    Observation previous;
    bool new_clear_state{};
};

struct Handler {
    void* context{};
    // Invoked once per native export entry. Use binding() for every actual
    // native call, including recovery. Returning an error never falls back to
    // a second original call. The bridge keeps consumers paused on failure.
    int (*vid_init)(void*, const Entry&){};
};

// Install once in LoadClient after precache installation, before executing
// engine/client callbacks. MetaHook may publish trampolines after LoadClient.
// Storage lasts until MetaHook removes its hooks on shutdown. No hot reset or
// uninstallation while an original call can still be on the stack is offered.
// This observes native entry, NOT its caller: a bridge must independently
// establish the engine/reset source before setting engine_vidinit=true.
const char* install(metahook_api_s*, resource_native_hud::Host::ScreenInfo, Handler = {});
resource_native_hud::Binding binding() noexcept;
Observation observe() noexcept;
bool current(const Entry&) noexcept;
const char* failure() noexcept;
// Set before a bridge starts a resource transaction. If observation fails,
// ordinary entries still pass through unless this explicit gate is held.
// This gate is not evidence that consumers are paused. An invalid lifecycle
// cannot release it; recovery requires the bridge's new module lifetime.
bool block_untracked_calls(bool block) noexcept;

}
