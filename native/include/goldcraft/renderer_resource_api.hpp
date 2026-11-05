// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
struct model_s;
namespace goldcraft::renderer_resources {
inline constexpr char interface_name[] = "GoldCraftRendererResource001";
inline constexpr std::uint32_t api_version = 1;
// Host resource ownership only; no CSOL gameplay or network state. Calls must
// run in the inner HUD_Frame, outside rendering, on the Renderer GL thread.
// Version 1 requires ALL known Studio/SPR models to avoid guessing dependency
// filenames or invalidating a shared texture while another model still uses it.
// It is used only for a verified content change, not for an unchanged map load.
struct Api {
    std::uint32_t size;
    std::uint32_t version;
    bool (__cdecl* available)();
    const char* (__cdecl* validate)(model_s* const* models, std::uint32_t count);
    const char* (__cdecl* release)(model_s* const* models, std::uint32_t count);
};
}
