// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace vgui { class ISurface; }
namespace csol::client
{
bool ConsumeWallUpgradeHud(int size, const void* data);
bool WallUpgradeHudVisible();
void PrepareWallUpgradeHud(vgui::ISurface* surface);
void PaintWallUpgradeHud(vgui::ISurface* surface);
void ResetWallUpgradeHud();
void InvalidateWallUpgradeHud(vgui::ISurface* surface);
void ShutdownWallUpgradeHud(vgui::ISurface* surface);
}
