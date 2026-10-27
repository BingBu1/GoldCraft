// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_nav_portal.h"

#include <cmath>
#include <cstdlib>

namespace
{
float ClampExtent(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}
}

bool CSOL_ComputeScenarioPortal(const CNavArea& from, const CNavArea& to,
    int direction, Vector& output)
{
    if (direction < NORTH || direction > WEST) return false;
    const auto& fromExtent = *from.GetExtent();
    const auto& toExtent = *to.GetExtent();
    const bool alongX = direction == NORTH || direction == SOUTH;
    const float fromLow = alongX ? fromExtent.lo.x : fromExtent.lo.y;
    const float fromHigh = alongX ? fromExtent.hi.x : fromExtent.hi.y;
    const float toLow = alongX ? toExtent.lo.x : toExtent.lo.y;
    const float toHigh = alongX ? toExtent.hi.x : toExtent.hi.y;
    // E/W use the destination's entering edge; N/S use the source edge.
    const float edge = alongX ? (direction == NORTH ? fromExtent.lo.y : fromExtent.hi.y)
        : (direction == WEST ? toExtent.hi.x : toExtent.lo.x);
    if (!std::isfinite(fromLow) || !std::isfinite(fromHigh) ||
        !std::isfinite(toLow) || !std::isfinite(toHigh) || !std::isfinite(edge) ||
        fromLow > fromHigh || toLow > toHigh)
        return false;

    const float low = ClampExtent(fromLow > toLow ? fromLow : toLow, fromLow, fromHigh);
    const float high = ClampExtent(fromHigh < toHigh ? fromHigh : toHigh, fromLow, fromHigh);
    const float width = high - low;
    const float margin = width < 37.5f ? 12.5f : 18.75f;
    const float start = low + margin;
    const float end = high - margin;
    const float span = end - start;
    if (!std::isfinite(width) || !std::isfinite(start) ||
        !std::isfinite(end) || !std::isfinite(span))
        return false;

    const float fraction = static_cast<float>(std::rand() % 10) / 9.0f;
    const float offset = fraction * span;
    const float position = start + offset;
    // A narrow or disjoint portal may reverse the internal range. The original
    // does not clamp it again or add IsEdge margins to the random selection.
    if (alongX) { output.x = position; output.y = edge; }
    else { output.x = edge; output.y = position; }
    return true;
}
