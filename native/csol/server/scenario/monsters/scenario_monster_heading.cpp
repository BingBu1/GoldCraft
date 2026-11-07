// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_monster_heading.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace
{
const std::array<float, 256>& CosineTable()
{
    static const auto table = []
    {
        std::array<float, 256> values{};
        for (int i = 0; i < 256; ++i)
        {
            const double scaled = static_cast<double>(i) * 6.283185307179586;
            const float angle = static_cast<float>(scaled / 255.0);
            values[i] = static_cast<float>(std::cos(static_cast<double>(angle)));
        }
        return values;
    }();
    return table;
}

bool Lookup(float angle, float& value)
{
    if (!std::isfinite(angle)) return false;
    if (angle - 360.0f == angle) angle = 0;
    // Keep the original float additions/subtractions for ordinary angles.
    // A malformed enormous angle must not monopolize the server in this
    // loop; rejected inputs never silently become a valid facing direction.
    int reductions{};
    while (angle < 0)
    {
        if (++reductions > 65536) return false;
        angle += 360.0f;
    }
    while (angle >= 360.0f)
    {
        if (++reductions > 65536) return false;
        angle -= 360.0f;
    }
    const float scaled = angle * -255.0f;
    const int index = -static_cast<int>(scaled / 360.0f);
    value = CosineTable()[index];
    return true;
}
}

bool CSOL_MonsterHeading(float yaw, float& cosine, float& sine)
{
    float c{}, s{};
    if (!Lookup(yaw, c) || !Lookup(yaw - 90.0f, s)) return false;
    cosine = c;
    sine = s;
    return true;
}

bool CSOL_MonsterFacingTarget(float yaw, float deltaX, float deltaY, bool& facing)
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return false;
    const float xx = deltaX * deltaX, yy = deltaY * deltaY;
    const float distance = yy + xx;
    if (!std::isfinite(distance)) return false;
    float c{}, s{};
    if (!CSOL_MonsterHeading(yaw, c, s)) return false;
    const auto bits = std::bit_cast<std::uint32_t>(distance);
    const float y = std::bit_cast<float>(std::uint32_t{0x5f3759d5} - (bits >> 1));
    const float half = distance * 0.5f;
    const float hy = half * y;
    const float hyy = hy * y;
    const float inverse = (1.5f - hyy) * y;
    const float x = deltaX * inverse;
    const float sy = (deltaY * inverse) * s;
    // MP keeps the cosine multiply and final add in x87 until the float
    // store; double retains these float-product bits without a premature
    // float rounding. Original-instruction comparison remains necessary.
    const float dot = static_cast<float>(static_cast<double>(c) * x + static_cast<double>(sy));
    if (!std::isfinite(dot)) return false;
    facing = dot > 0.8f;
    return true;
}
