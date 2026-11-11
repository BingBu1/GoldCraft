// SPDX-License-Identifier: GPL-3.0-or-later
#include "aim_fov.hpp"

#include <limits>

#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
std::int32_t TruncateFov(float value) noexcept
{
    // CVTTSS2SI returns integer-indefinite for NaN, infinity and overflow.
    if (value >= -2147483648.0f && value < 2147483648.0f)
        return static_cast<std::int32_t>(value);
    return std::numeric_limits<std::int32_t>::min();
}
}

AimFovOutput UpdateAimFov(AimFovState& state, const AimFovFrame& frame) noexcept
{
    const float target = frame.mode > 3 ? static_cast<float>(frame.mode)
        : frame.mode == 3 ? 30.0f
        : frame.mode == 2 ? 60.0f : static_cast<float>(frame.defaultFov);
    const float time = frame.clientTime;

    if (state.current < 0.0f)
    {
        state.current = target;
        state.settledTarget = target;
    }
    else
    {
        float difference = target - state.current;
        if (difference < 0.0f)
            difference *= -1.0f;
        // MAXSS selects its second operand when unordered; retain that NaN.
        float multiplier = difference < 30.0f ? 30.0f : difference;
        if (frame.mode == 3 || state.settledTarget == 30.0f)
            multiplier += multiplier;
        multiplier /= 30.0f;

        const bool custom = frame.mode > 3 && frame.customSpeed != 0.0f;
        if (custom && (frame.mode == 666 || frame.customSpeed == 666.0f))
            state.current = target; // The original snap leaves settledTarget.
        else if (target > state.current)
        {
            float step = time - state.previousTime;
            step *= custom ? frame.customSpeed : frame.zoomSpeed;
            step *= multiplier;
            state.current += step;
            if (state.current > target)
            {
                state.settledTarget = target;
                state.current = target;
            }
        }
        else if (state.current > target)
        {
            float step = time - state.previousTime;
            step *= custom ? frame.customSpeed : frame.zoomSpeed;
            step *= multiplier;
            state.current -= step;
            if (target > state.current)
            {
                state.settledTarget = target;
                state.current = target;
            }
        }
    }

    state.previousTime = time;
    const std::int32_t wholeFov = TruncateFov(state.current);
    float sensitivity = 0.0f;
    if (wholeFov != frame.defaultFov)
    {
        sensitivity = state.current / static_cast<float>(frame.defaultFov);
        sensitivity *= frame.sensitivity;
        sensitivity *= frame.zoomSensitivityRatio;
    }
    return {wholeFov, sensitivity, state.current * frame.fovMultiplier};
}
}
