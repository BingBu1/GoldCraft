// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct GoldCraftConnectionAPI;
namespace csol::client
{
void InitializeGuestCofAdmission();
void InitializeGuestCofAdmission(const GoldCraftConnectionAPI*);
void InvalidateGuestCofAdmission() noexcept;
void ShutdownGuestCofAdmission() noexcept;
// HUD_Frame, after cold scene and HUD preparation. Never call from input/Draw.
void GuestCofAdmissionFrame() noexcept;
bool GuestCofAdmitted() noexcept;
bool GuestCofAdmitted(int hostWeaponId) noexcept;
void GuestCofAdmissionDiagnostics(std::ostream&);
}
