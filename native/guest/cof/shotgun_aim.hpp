// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shotgun_control.hpp"
#include <array>

namespace guest::cof
{
struct ShotgunAimMetadata
{
    std::array<float, 3> offset{};
    int zoom{};
};
enum class ShotgunAimEventKind : unsigned char { Presentation, CameraZoom, PlayerFov, ViewAnimation };
struct ShotgunAimEvent
{
    ShotgunAimEventKind kind;
    std::array<float, 3> offset{};
    float amount{};
    int value{};
    bool enabled{};
};
struct ShotgunAimHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const ShotgunAimEvent&) noexcept {};
    // Read the actual owner.weaponanim after the view consumer. Original
    // 1000F390 returns zero for a sequence beyond the model's count.
    bool (*duration)(void*, float&) noexcept {};
    bool (*autoAim)(void*, float slope) noexcept {};
};

// Original1006D0F0/1006CE20. Metadata is prepared from the immutable inventory
// description consumed by100DFF70; no file parsing belongs in this hot call.
ShotgunControlResult ApplyShotgunAim(ShotgunActionState&, bool enabled,
    const ShotgunAimMetadata&, const ShotgunAimHost&) noexcept;
ShotgunControlResult ToggleShotgunAim(ShotgunActionState&, bool pressed, bool toggleMode,
    const ShotgunAimMetadata&, const ShotgunAimHost&) noexcept;
// Original1006EC30: autoaim precedes even a rejected idle, then sequence15/0.
ShotgunControlResult ShotgunIdle(ShotgunActionState&, const ShotgunAimHost&) noexcept;
}
