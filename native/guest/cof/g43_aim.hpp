// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "g43_control.hpp"

namespace guest::cof
{
struct G43AimMetadata
{
    std::array<float, 3> offset;
    int zoom;
};
enum class G43AimEventKind : unsigned char { Presentation, CameraZoom, PlayerFov, ViewAnimation };
struct G43AimEvent
{
    G43AimEventKind kind;
    std::array<float, 3> offset{};
    int value{};
    float amount{};
    bool enabled{};
};
struct G43AimHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const G43AimEvent&) noexcept {};
    // Resolve actual owner.weaponanim after the view callback, not the
    // requested sequence. The original helper returns zero beyond numseq.
    bool (*duration)(void*, float&) noexcept {};
};

// Matched10043AD0/100437F0. The caller owns the incoming aim bit; this method
// writes it after the view consumer, as the original virtual methods do.
G43ControlResult ApplyG43Aim(MagazineActionState&, bool enabled,
    const G43AimMetadata&, const G43AimHost&) noexcept;
// Matched100453B0. Toggle/hold mode and pressed bit are authoritative inputs.
G43ControlResult ToggleG43Aim(MagazineActionState&, bool pressed, bool toggleMode,
    const G43AimMetadata&, const G43AimHost&) noexcept;
}
