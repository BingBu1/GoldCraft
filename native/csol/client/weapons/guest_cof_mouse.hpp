// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../guest/cof/authority.hpp"
struct GoldCraftConnectionAPI;
namespace csol::client
{
bool InitializeGuestCofMouse();
bool InitializeGuestCofMouse(const GoldCraftConnectionAPI*);
bool GuestCofMouseReady() noexcept;
// Authenticated presentation output only, after the original FOV update stage.
bool SetGuestCofMouseFrame(guest::cof::AuthorityKey, int wholeFov, int defaultFov,
                           float hudSensitivity) noexcept;
void ResetGuestCofMouse() noexcept;
void GuestCofMouseFrame() noexcept;
void ShutdownGuestCofMouse() noexcept;
}
