// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../guest/cof/client/custom_decal.hpp"
struct GoldCraftConnectionAPI;
namespace csol::client
{
enum class GuestCofDecalStatus : unsigned char
{
    Inactive, Armed, ResourcesPrepared, Prepared, MissingBackend,
    ResourceFailed, MapFailed, Cancelled, WrongContext
};
struct GuestCofDecalState
{
    GuestCofDecalStatus status{};
    std::uint64_t generation{};
    std::size_t marks{}, pending{};
    guest::cof::client::CustomDecalResult message{guest::cof::client::CustomDecalResult::NoSurface};
    guest::cof::client::CustomDecalDrawResult draw{guest::cof::client::CustomDecalDrawResult::MissingResources};
    bool resourcesPrepared{}, mapPrepared{}, coreProfile{};
};
// Installs only the namespaced message, never original/ordinary CS messages.
void InitializeGuestCofDecals();
void InitializeGuestCofDecals(const GoldCraftConnectionAPI*);
// Called by the single actual signon resource owner after admission. No I/O is
// performed here; the plan can be released on return. Real owner GL required.
bool PrepareGuestCofDecalResources(const std::array<std::span<const std::uint8_t>,5>&) noexcept;
// Cold actual Renderer QueryWorld lifecycle supplies this model and stride.
// World identity alone never establishes draw visibility/depth readiness.
bool PrepareGuestCofDecalMap(model_s&, bool hl25) noexcept;
void DrawGuestCofDecals(const model_s&, const guest::cof::client::CustomDecalFrame&) noexcept;
void PrepareGuestCofDecalFrame() noexcept;
// Scene's world retirement may occur after signon preparation. Retire only the
// map/marks here, retaining admitted textures; connection retirement is below.
void ClearGuestCofDecalMap() noexcept;
// Before world release, connection retirement and resource refresh.
void InvalidateGuestCofDecalResources() noexcept;
// Before native video/context destruction. Refuses wrong-thread/context release.
bool GuestCofDecalsVideoInit() noexcept;
void ShutdownGuestCofDecals() noexcept;
GuestCofDecalState GuestCofDecalsState() noexcept;
}
