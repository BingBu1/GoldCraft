// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "oberon_combat.hpp"
#include <cmath>

namespace csol::scenario::boss_room_wall
{
inline constexpr int Classification = 38; // BossRoomWall::Classify, MP 108F70D0

inline bool BlocksGrasshopper(float wallPitch, float wallYaw, float bossPitch, float bossYaw)
{
    // MP 109827F6..109828AD compares the XY projections without renormalizing.
    const auto wall = oberon::HorizontalForward(wallPitch, wallYaw);
    const auto boss = oberon::HorizontalForward(bossPitch, bossYaw);
    const float dot = boss[1] * wall[1] + boss[0] * wall[0];
    const float radians = static_cast<float>(std::acos(static_cast<double>(dot)));
    const float degrees = (radians * 180.0f) / 3.141592025756836f;
    return !(degrees > 80.0f && degrees < 110.0f);
}
}
