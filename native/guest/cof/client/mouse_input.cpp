// SPDX-License-Identifier: GPL-3.0-or-later
#include "mouse_input.hpp"
#include <bit>
#include <emmintrin.h>
#pragma clang fp contract(off)

namespace guest::cof::client
{
namespace
{
std::int32_t Filter(std::int32_t previous, std::int32_t current) noexcept
{
    // Original ADD wraps before the exact double*.5 and CVTTSD2SI. Int32
    // division by two has that same truncation, including negative odd sums.
    return std::bit_cast<std::int32_t>(std::uint32_t(previous) + std::uint32_t(current)) / 2;
}
std::int32_t Scale(std::int32_t value, float sensitivity) noexcept
{
    const float input = static_cast<float>(value);
    const float scaled = input * sensitivity;
    // Preserve CVTTSS2SI's indefinite value for NaN/overflow; a C++ float-to-
    // int cast would have undefined behavior for those original inputs.
    return _mm_cvtt_ss2si(_mm_set_ss(scaled));
}
}
MouseOutput ScaleMouse(const MouseHistory& history, const MouseInput& input) noexcept
{
    const bool filter = input.filter != 0.0F;
    const auto x = filter ? Filter(history.x, input.x) : input.x;
    const auto y = filter ? Filter(history.y, input.y) : input.y;
    const float sensitivity = input.hudSensitivity != 0.0F ? input.hudSensitivity : input.sensitivity;
    MouseOutput result{Scale(x, sensitivity), Scale(y, sensitivity), {input.x, input.y}};
    if (input.wholeFov < input.defaultFov)
    {
        result.x /= 2;
        result.y /= 2;
    }
    return result;
}
}
