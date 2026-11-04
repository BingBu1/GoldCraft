// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>

namespace csol::client {
// VGUI2Extension sorts BaseUI callbacks by descending altitude and runs Paint
// in that order (VGUI2ExtensionInternal.cpp BaseUI_Paint): a larger altitude
// paints first, underneath. Equal altitudes are ordered by an unstable sort.
// The same altitude also orders Key_Event, where menus must keep priority, so
// overlays keep their altitudes and only queue their post-paint here. One
// compositor registered below all of them paints the queue bottom to top.
// Original evidence: client 2A9C130 draws the bottom panels (2A9F6D0 at
// 02A9C344) before the skill effects (02A9C447..02A9C531).
// Bottom to top. The previous altitudes (20..35) encoded this order as
// "larger is above", which VGUI2Extension painted inverted; ties keep their
// registration-independent order here.
enum class OverlayLayer : unsigned char
{
    WallStatus, HudStyle, ScenarioKill, ScenarioUpgrades, CharacterSkills, BossHealth,
    ScenarioAbilities, ScenarioRoundHud, ScenarioRespawnUi, CharacterMenu, BuyMenu, MapVote, Count
};

namespace overlay_detail {
inline constexpr auto LayerCount = static_cast<std::size_t>(OverlayLayer::Count);
inline std::array<void (*)(), LayerCount> queued{};
inline bool active{};
}

// Without an active compositor the overlay paints immediately, as before.
inline void QueueOverlayPaint(OverlayLayer layer, void (*paint)())
{
    const auto index = static_cast<std::size_t>(layer);
    if (!paint || index >= overlay_detail::LayerCount) return;
    if (overlay_detail::active) overlay_detail::queued[index] = paint;
    else paint();
}

// Paints this frame's queue bottom to top. A painter cannot requeue into it.
inline void PaintQueuedOverlays()
{
    const auto frame = overlay_detail::queued;
    overlay_detail::queued = {};
    for (const auto paint : frame)
        if (paint) paint();
}

void InitializeOverlayCompositor();
void ShutdownOverlayCompositor();
}
