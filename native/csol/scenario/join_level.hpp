// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace csol::scenario
{
// The entry owner supplies the actual rules/retained-player state. Map time is
// gpGlobals->time, not time since the current wave or challenge began.
struct JoinLevelContext
{
    bool gameStarted{};
    bool restoredPlayer{};
    float mapTime{};
};

inline bool JoinLevelEligible(const JoinLevelContext& context) noexcept
{
    // MP 108FA680: root+27C, strict >60, then player+33B1 == 0.
    return context.gameStarted && context.mapTime > 60.0f && !context.restoredPlayer;
}

inline std::int32_t JoinLevelTarget(std::int32_t maximumOtherEffectiveLevel, float ratio) noexcept
{
    // 108FA788..79A converts the maximum to float32, multiplies the selected
    // column 8, stores float32 and truncates. cvttss2si returns INT_MIN for an
    // unordered/out-of-range value; do not perform an undefined C++ cast.
    const float product = static_cast<float>(maximumOtherEffectiveLevel) * ratio;
    return static_cast<double>(product) >= -2147483648.0 &&
        static_cast<double>(product) < 2147483648.0 ? static_cast<std::int32_t>(product) : INT32_MIN;
}
}
