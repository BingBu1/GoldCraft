// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../status_icons.hpp"

namespace csol::client
{
void InitializeStatusIcons();
void ResetStatusIcons();
void StatusIconsFrame();
void StatusIconsVideoInit();
void DrawStatusIcons(float time);
void ShutdownStatusIcons();

// Graphics are prepared outside HUD_Redraw and invalidated separately from
// message state. A video mode refresh must not discard server-owned labels.
void PrepareStatusIconGraphics();
void ResetStatusIconGraphics();
void DrawStatusIconGraphics(const status::ProtectionIcons& icons, int width, int height);
}
