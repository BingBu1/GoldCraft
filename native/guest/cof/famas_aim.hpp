// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "famas_control.hpp"

namespace guest::cof
{
struct FamasAimMetadata
{
    std::array<float, 3> offset;
    int zoom;
};
enum class FamasAimEventKind : unsigned char { Presentation, CameraZoom, PlayerFov, ViewAnimation };
struct FamasAimEvent
{
    FamasAimEventKind kind;
    std::array<float, 3> offset{};
    int value{};
    float amount{};
    bool enabled{};
};
struct FamasAimHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const FamasAimEvent&) noexcept {};
    // Query the actual owner.weaponanim after the view consumer. An original
    // out-of-range sequence has duration0; unavailable data is a separate failure.
    bool (*duration)(void*, float&) noexcept {};
};

// Original enter1003B130/exit1003AE60: fixed sequences15/18, including empty clip.
FamasControlResult ApplyFamasAim(MagazineActionState&, bool enabled,
    const FamasAimMetadata&, const FamasAimHost&) noexcept;
// Original1003C880. The separate hold-release producer is not inferred here.
FamasControlResult ToggleFamasAim(MagazineActionState&, bool pressed, bool toggleMode,
    const FamasAimMetadata&, const FamasAimHost&) noexcept;
}
