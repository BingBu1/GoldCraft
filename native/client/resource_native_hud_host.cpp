// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <VGUI/ISurface.h>
#include "resource_native_hud_host.hpp"

#include <cstring>

namespace goldcraft::resource_native_hud {
namespace {
static_assert(sizeof(void*) == 4);
constexpr std::uintptr_t vidinit_rva = 0x47790, surface_getter_rva = 0x78320;
constexpr std::uintptr_t surface_pointer_rva = 0x1355e4;

bool ResolveAt(metahook_api_t* api, void* base, const char* name,
               mh_gamesymbol_kind_t kind, std::uintptr_t rva, void*& out) {
    return api->ResolveGameSymbol(base, name, kind, &out) == MH_GAMESYMBOL_OK &&
        out && reinterpret_cast<std::uintptr_t>(out) == reinterpret_cast<std::uintptr_t>(base) + rva;
}
}

const char* Host::Resolve(metahook_api_s* api, ScreenInfo screen) {
    if (entry_) return "native HUD host is already resolved";
    if (!api || !api->GetClientBase || !api->GetEngineBase || !api->GetClientSize ||
        !api->GetEngineSize || !api->GetModuleCRC64 || !api->ResolveGameSymbol || !screen)
        return "native HUD host API is unavailable";
    auto* client = static_cast<std::byte*>(api->GetClientBase());
    auto* engine = static_cast<std::byte*>(api->GetEngineBase());
    std::uint64_t client_crc{}, engine_crc{};
    if (!client || !engine || api->GetModuleCRC64(client, &client_crc) != MH_GAMESYMBOL_OK ||
        api->GetModuleCRC64(engine, &engine_crc) != MH_GAMESYMBOL_OK ||
        client_crc != 0x124d6034a6b28b40ULL || engine_crc != 0x6ef7192cd8254e2dULL)
        return "native HUD host module identity mismatch";
    const std::span client_image{client, static_cast<std::size_t>(api->GetClientSize())};
    const std::span engine_image{engine, static_cast<std::size_t>(api->GetEngineSize())};
    // The matching catalog generators verify original-file SHA256. MetaHook's
    // runtime CRC is computed from that module's original file, not hooked code.
    if (const auto* error = ValidateImage(client_image, reinterpret_cast<std::uintptr_t>(client),
                                          client_sha256, engine_sha256)) return error;
    if (const auto* error = ValidateSourceAllocator(engine_image, reinterpret_cast<std::uintptr_t>(engine),
                                                    engine_sha256)) return error;
    if (client_image.size() < surface_pointer_rva + sizeof(void*))
        return "native HUD surface owner is outside the image";
    std::uint32_t operand{};
    std::memcpy(&operand, client + surface_getter_rva + 1, sizeof(operand));
    if (client[surface_getter_rva] != std::byte{0xa1} ||
        client[surface_getter_rva + 5] != std::byte{0xc3} ||
        operand != reinterpret_cast<std::uintptr_t>(client) + surface_pointer_rva)
        return "native HUD surface accessor changed";

    void *hud{}, *entry{}, *release{}, *surface{}, *source{};
    if (!ResolveAt(api, client, "GoldCraftNativeHud_State", MH_GAMESYMBOL_KIND_GLOBAL, hud_rva, hud) ||
        !ResolveAt(api, client, "GoldCraftNativeHud_VidInit", MH_GAMESYMBOL_KIND_FUNCTION, vidinit_rva, entry) ||
        !ResolveAt(api, client, "GoldCraftNativeHud_DeleteArray", MH_GAMESYMBOL_KIND_FUNCTION, delete_array_rva, release) ||
        !ResolveAt(api, client, "GoldCraftNativeHud_GetSurface", MH_GAMESYMBOL_KIND_FUNCTION, surface_getter_rva, surface) ||
        !ResolveAt(api, engine, "GoldCraftNativeHud_SourceFree", MH_GAMESYMBOL_KIND_FUNCTION, source_free_rva, source))
        return "native HUD host symbol does not match its verified module";

    // Publish the binding only after every address and owner instruction passed.
    hud_ = {static_cast<std::byte*>(hud), hud_size};
    entry_ = entry;
    delete_array_ = reinterpret_cast<Release>(release);
    source_release_ = reinterpret_cast<Release>(source);
    get_surface_ = reinterpret_cast<decltype(get_surface_)>(surface);
    screen_info_ = screen;
    return nullptr;
}

const char* Host::Capture(VidInit trampoline) {
    if (!entry_ || !trampoline || reinterpret_cast<void*>(trampoline) == entry_)
        return "native HUD requires its original hook trampoline";
    auto* surface = get_surface_();
    if (!surface) return "native HUD surface is not initialized";
    if (trampoline_ && (trampoline_ != trampoline || surface_ != surface))
        return "native HUD owner changed after capture";
    surface_ = surface;
    trampoline_ = trampoline;
    return nullptr;
}

bool Host::OwnerCurrent() const noexcept {
    return trampoline_ && surface_ && get_surface_() == surface_;
}

Binding Host::GetBinding() noexcept {
    if (!OwnerCurrent()) return {};
    return {hud_, delete_array_, this, CallVidInit, DeleteTexture, CurrentResolution};
}

int Host::CallVidInit(void* context) {
    auto& self = *static_cast<Host*>(context);
    if (!self.OwnerCurrent()) return 0;
    const auto result = self.trampoline_();
    return self.OwnerCurrent() ? result : 0;
}

int Host::CurrentResolution(void* context) noexcept {
    auto& self = *static_cast<Host*>(context);
    if (!self.OwnerCurrent()) return 0;
    SCREENINFO info{};
    info.iSize = sizeof(info);
    if (self.screen_info_(&info) <= 0 || info.iWidth <= 0) return 0;
    return info.iWidth < 640 ? 320 : 640;
}

void Host::DeleteTexture(void* context, int texture) noexcept {
    // Refresh checks the current owner/resolution before retirement. Use the
    // captured original ISurface, never another factory or a GL texture name.
    static_cast<Host*>(context)->surface_->DeleteTextureByID(texture);
}

}
