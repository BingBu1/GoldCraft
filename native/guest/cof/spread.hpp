// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace guest::cof
{
struct SpreadState
{
    float scalar{};
    float recoveryDeadline{};
    std::array<float, 3> vector{};
};

// Original globals are explicit: share this state to reproduce CoF, or isolate
// it per host player as a documented host deviation. No implicit reset policy.
struct SpreadTransitions
{
    std::int32_t minimum{};
    bool moving{}, airborne{}, ducking{}, sprinting{};
};

struct SpreadFrame
{
    float velocityX{}, velocityY{}, time{};
    std::uint32_t flags{};
    std::int32_t weaponId{};
    bool hasOwner{};
};

// hl.dll 0036b91c, 1011BB50 (RVA 11BB50). Authoritative game thread;
// precise floating point required. Writes vector before the single decay.
void UpdateSpread(SpreadState& weapon, SpreadTransitions& transitions,
                  const SpreadFrame& frame) noexcept;
}
