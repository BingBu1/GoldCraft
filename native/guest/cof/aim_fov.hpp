// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace guest::cof
{
// Caller owns initialization and lifecycle. A negative current selects the
// original initialization branch; the helper does not invent a reset policy.
struct AimFovState
{
    float current;
    float previousTime;
    float settledTarget;
};

struct AimFovFrame
{
    std::int32_t mode;
    std::int32_t defaultFov;
    float clientTime;
    float customSpeed;
    float zoomSpeed;
    float sensitivity;
    float zoomSensitivityRatio;
    float fovMultiplier;
};

struct AimFovOutput
{
    std::int32_t wholeFov;
    float sensitivity;
    float clientFov;
};

// client.dll d2a04641: 10073824..10073A42 and 100780C9..100780DB.
// Client thread, precise single-precision operations; disable FP contraction.
// No viewmodel xyz transform or equip/death cancellation is implied.
AimFovOutput UpdateAimFov(AimFovState& state, const AimFovFrame& frame) noexcept;
}
