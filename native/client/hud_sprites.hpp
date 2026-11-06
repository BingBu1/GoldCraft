#pragma once
#include <metahook.h>
#include "resource_native_hud.hpp"

namespace goldcraft::hud_sprites {
using Loader = decltype(cl_enginefunc_t::pfnSPR_GetList);
using SourceRelease = void (*)(void*) noexcept;
// Client initialization only. Reject rebinding while a transaction is pending.
// Copied engine sources remain retained across client rebinds until the exact
// engine allocator is bound; the old native HUD must already be retired.
bool bind(Loader original);
client_sprite_t* list(char* path, int* count);
// Bind the verified hw SPR_GetList calloc/free owner. Existing ordinary HUD
// loading remains available before this binding, but updates fail preflight.
// The caller must resolve the exact engine free, never the plugin/client CRT.
bool bind_source_release(SourceRelease release);
resource_native_hud::DirectoryOwner resource_owner();
}
