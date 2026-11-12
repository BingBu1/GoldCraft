// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../guest/cof/client/weapon_effects.hpp"
#include "../../../guest/cof/client/view_punch.hpp"
struct GoldCraftConnectionAPI;
struct cl_enginefuncs_s;
namespace csol::client
{
bool InitializeGuestCofEffects();
bool InitializeGuestCofEffects(const GoldCraftConnectionAPI*);
void ShutdownGuestCofEffects() noexcept;
// Cold, after native signon and the real CoF audio preparation; no hot lookup.
bool PrepareGuestCofEffects() noexcept;
void InvalidateGuestCofEffects() noexcept;
// Prepared model/audio adapters plus installed hook/completed event registration.
// This does not prove rendering, event delivery or server zoominmdl acceptance.
bool GuestCofEffectsReady() noexcept;
bool GuestCofEffectsOwnsWeapon(guest::cof::client::Weapon) noexcept;
std::uint64_t GuestCofEffectsResourceSerial() noexcept;
bool ResetGuestCofForwardDisplacement() noexcept;
void GuestCofEffectsVideoInit() noexcept;
void GuestCofForwardDisplacementViewStage(const ref_params_s&) noexcept;
// Borrowed owner-thread table with exact original resource keys remapped to
// cold-admitted handles. No CL_LoadModel/VFS call is made by its model adapters.
// This is resource preparation only; the muzzle renderer has its own admission.
cl_enginefuncs_s* GuestCofMuzzleEngine() noexcept;
void SetGuestCofEffectsWeapon(guest::cof::client::Weapon) noexcept;
// Only a proven AttachCam lifecycle may supply nonzero state. Original BSS is zero.
void SetGuestCofEffectsCamera(bool attached, int mode) noexcept;
// True owns the event, including explicit backend failure; never a success claim.
bool GuestCofStudioEvent(const mstudioevent_s*, const cl_entity_s*) noexcept;
guest::cof::client::EffectResult GuestCofLastStudioEffect() noexcept;
guest::cof::client::PunchResult GuestCofLastViewPunch() noexcept;
class GuestCofEffectsViewScope
{
public:
    explicit GuestCofEffectsViewScope(const ref_params_s&) noexcept;
    ~GuestCofEffectsViewScope();
    GuestCofEffectsViewScope(const GuestCofEffectsViewScope&) = delete;
    GuestCofEffectsViewScope& operator=(const GuestCofEffectsViewScope&) = delete;
private:
    const ref_params_s* previous_{};
    std::uint64_t serial_{};
};
}
