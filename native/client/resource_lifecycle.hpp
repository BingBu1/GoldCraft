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
    bool engine_vidinit{}; // Inside the verified engine wrapper, not just an export call.
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
// observe_engine additionally hooks the verified engine VidInit wrapper. Only
// its active invocation can mark Entry.engine_vidinit; direct exports cannot.
const char* install(metahook_api_s*, resource_native_hud::Host::ScreenInfo, Handler = {},
                    bool observe_engine = false);
resource_native_hud::Binding binding() noexcept;
resource_native_hud::Host::Release source_release() noexcept;
Observation observe() noexcept;
bool current(const Entry&) noexcept;
const char* failure() noexcept;
// Set before a bridge starts a resource transaction. If observation fails,
// ordinary entries still pass through unless this explicit gate is held.
// This gate is not evidence that consumers are paused. An invalid lifecycle
// cannot release it; recovery requires the bridge's new module lifetime.
bool block_untracked_calls(bool block) noexcept;

}
