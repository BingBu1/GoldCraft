// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace guest::cof::client {struct PointLight;}
namespace csol::client {
// Cold HUD_Frame map preparation; no resource preparation takes place in Draw.
void GuestCofSceneFrame() noexcept;
void DrawGuestCofOpaqueScene() noexcept;
void ClearGuestCofScene() noexcept;
// This reports an initialized map backend that has passed a real host opaque
// frame. It does not admit an entire weapon or promise unsupported portal views.
bool GuestCofPointLightsReady() noexcept;
bool GuestCofMainStudioView() noexcept;
guest::cof::client::PointLight* AllocateGuestCofPointLight(int key) noexcept;
}
