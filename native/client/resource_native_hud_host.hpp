// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "resource_native_hud.hpp"

struct metahook_api_s;
struct SCREENINFO_s;
namespace vgui { class ISurface; }

namespace goldcraft::resource_native_hud {

// One instance per native client lifetime, on the client thread. Resolve before
// installing the native export hook; Capture after InlineHook returns its real
// trampoline and the original client has acquired VGUI_Surface026. The bridge
// owns the hook, natural ClearState events, pause gates and module lifetime.
// This object neither installs hooks nor asserts those lifecycle conditions.
class Host {
public:
    using VidInit = int (*)();
    using ScreenInfo = int (*)(SCREENINFO_s*);
    using Release = void (*)(void*) noexcept;

    Host() = default;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    const char* Resolve(metahook_api_s*, ScreenInfo);
    const char* Capture(VidInit native_trampoline);
    // Check this before Prepare/Initialize/recovery, including interrupted
    // callbacks: a different surface must not inherit the old texture IDs.
    bool OwnerCurrent() const noexcept;
    Binding GetBinding() noexcept;
    void* Entry() const noexcept { return entry_; }
    Release SourceRelease() const noexcept { return source_release_; }

private:
    static int CallVidInit(void*);
    static int CurrentResolution(void*) noexcept;
    static void DeleteTexture(void*, int) noexcept;

    std::span<std::byte> hud_;
    void* entry_{};
    Release delete_array_{}, source_release_{};
    vgui::ISurface* (*get_surface_)(){};
    vgui::ISurface* surface_{};
    ScreenInfo screen_info_{};
    VidInit trampoline_{};
};

}
