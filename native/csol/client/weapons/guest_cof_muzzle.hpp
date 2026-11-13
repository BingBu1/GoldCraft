// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct cl_entity_s;
struct model_s;
struct mstudioevent_s;
struct GoldCraftConnectionAPI;
namespace csol::client
{
void InitializeGuestCofMuzzle();
void InitializeGuestCofMuzzle(const GoldCraftConnectionAPI*);
bool PrepareGuestCofMuzzle() noexcept;
void InvalidateGuestCofMuzzle() noexcept;
void ShutdownGuestCofMuzzle() noexcept;
bool GuestCofMuzzleReady() noexcept;
bool GuestCofMuzzleReady(unsigned originalWeapon) noexcept;
bool GuestCofMuzzleWorldPending() noexcept;
bool GuestCofMuzzleStudioEvent(const mstudioevent_s*,const cl_entity_s*) noexcept;
// Actual StudioDrawPoints boundary, after the held model's bone merge.
// Matrices are the current SDK StudioGetLightTransform result, borrowed here.
void GuestCofMuzzleWorldPoints(const cl_entity_s*,const model_s*,const void* studioHeader,
                              const float (*bones)[3][4]) noexcept;
}
