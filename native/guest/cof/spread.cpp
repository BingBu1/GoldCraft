// SPDX-License-Identifier: GPL-3.0-or-later
#include "spread.hpp"

#include <bit>
#include <cmath>

namespace guest::cof
{
namespace
{
constexpr std::array<std::uint32_t, 10> SpreadBits{
    0x3C0F0846u, 0x3C8EF34Du, 0x3CD67770u, 0x3D0EF34Du, 0x3D32AAE3u,
    0x3D566278u, 0x3D7A0F91u, 0x3D8EDE55u, 0x3DA0AFA3u, 0x3DB280F1u
};
}

void UpdateSpread(SpreadState& weapon, SpreadTransitions& transitions,
                  const SpreadFrame& frame) noexcept
{
    if (!frame.hasOwner)
        return;

    const float xSquared = frame.velocityX * frame.velocityX;
    const float ySquared = frame.velocityY * frame.velocityY;
    const float speedSquared = xSquared + ySquared;
    const float speed = static_cast<float>(std::sqrt(static_cast<double>(speedSquared)));
    float scalar = weapon.scalar;

    if (speed > 10.0f && !(frame.flags & 0x44000u))
    {
        if (scalar < 3.0f)
        {
            transitions.minimum = 3;
            scalar = 3.0f;
        }
        if (!transitions.moving)
        {
            scalar = static_cast<float>(static_cast<double>(scalar) * 1.5);
            transitions.moving = true;
        }
    }
    else
        transitions.moving = false;

    if (frame.flags & 0x40000u)
    {
        if (scalar < 7.0f)
        {
            transitions.minimum = 7;
            scalar = 7.0f;
        }
        if (!transitions.sprinting)
        {
            transitions.sprinting = true;
            scalar *= 3.0f;
        }
    }
    else
        transitions.sprinting = false;

    if (frame.flags & 0x4000u)
    {
        if (!transitions.ducking)
        {
            transitions.ducking = true;
            scalar *= 0.5f;
        }
        if (speed > 10.0f && scalar < 2.0f)
        {
            transitions.minimum = 2;
            scalar = 2.0f;
        }
    }
    else
        transitions.ducking = false;

    if (!(frame.flags & 0x200u))
    {
        if (!transitions.airborne)
        {
            transitions.airborne = true;
            scalar += 8.0f;
        }
        if (scalar < 8.0f)
        {
            transitions.minimum = 8;
            scalar = 8.0f;
        }
    }
    else
        transitions.airborne = false;

    if (!transitions.airborne && !transitions.moving && !transitions.ducking && !transitions.sprinting)
        transitions.minimum = 0;

    if (frame.weaponId == 15)
        scalar = 7.0f;

    if (scalar < 1.0f)
        weapon.vector = {};
    else if (scalar >= 1.0f)
    {
        // The original ordered comparisons leave the previous vector on NaN.
        std::size_t bin = 0;
        while (bin < SpreadBits.size() - 1 && scalar >= static_cast<float>(bin + 2))
            ++bin;
        const float spread = std::bit_cast<float>(SpreadBits[bin]);
        weapon.vector = {spread, spread, spread};
    }

    if (frame.time > weapon.recoveryDeadline)
    {
        scalar -= 1.0f;
        weapon.recoveryDeadline = static_cast<float>(static_cast<double>(frame.time) + 0.1);
    }
    if (scalar < 0.0f)
        scalar = 0.0f;
    if (scalar < static_cast<float>(transitions.minimum))
        scalar = static_cast<float>(transitions.minimum);
    if (scalar > 10.0f)
        scalar = 10.0f;
    weapon.scalar = scalar;
}
}
