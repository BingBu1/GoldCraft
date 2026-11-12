// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct GoldCraftConnectionAPI;
namespace guest::cof {struct PresentationState;}
namespace csol::client
{
void InitializeGuestCofCrosshair();
void InitializeGuestCofCrosshair(const GoldCraftConnectionAPI*);
void ResetGuestCofCrosshair() noexcept;
void GuestCofCrosshairVideoInit() noexcept;
void ShutdownGuestCofCrosshair() noexcept;
void GuestCofCrosshairFrame() noexcept;
bool GuestCofCrosshairReady() noexcept;
void GuestCofCrosshairPresentation(const guest::cof::PresentationState&) noexcept;
void DrawGuestCofCrosshair(int hidden,int intermission,bool drawAmmo) noexcept;
}
