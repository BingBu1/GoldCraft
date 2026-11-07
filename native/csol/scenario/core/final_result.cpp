// SPDX-License-Identifier: GPL-3.0-or-later
#include "final_result.hpp"

#include <algorithm>
#include <bit>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::scenario
{
namespace
{
std::int32_t Truncate(float value)
{
    // CVTTSS2SI produces INT32_MIN for NaN and either overflow direction.
    return static_cast<double>(value) >= -2147483648.0 &&
        static_cast<double>(value) < 2147483648.0 ? static_cast<std::int32_t>(value) : INT32_MIN;
}

std::int32_t Share(float value, float total, float weight, std::int32_t whenZero)
{
    // UCOMISS treats both signed zeroes as equal; unordered takes the divide
    // branch. Preserve separate float32 divide/multiply before truncation.
    if (total == 0.0f) return whenZero;
    const float ratio = value / total;
    const float weighted = ratio * weight;
    return Truncate(weighted);
}
}

FinalResultTotals SumFinalResultStatistics(std::span<const FinalResultStatistics> statistics)
{
    FinalResultTotals result;
    for (const auto& state : statistics)
    {
        result.bossDamage += state.bossDamage;
        result.ordinaryDamage += state.ordinaryDamage;
        result.breakableDamage += state.breakableDamage;
    }
    return result;
}

std::int32_t CalculateFinalResultScore(const FinalResultStatistics& player,
    const FinalResultTotals& totals, std::uint32_t eligiblePlayers)
{
    // MP108F5A50 adds each truncated contribution using 32-bit integer
    // arithmetic. Convert back to signed only at the original comparisons.
    std::uint32_t score = static_cast<std::uint32_t>(Share(player.ordinaryDamage, totals.ordinaryDamage, 35.0f, 0));
    score += 10u - static_cast<std::uint32_t>(std::min(player.counter1C, std::int32_t{10}));
    score += static_cast<std::uint32_t>(Share(player.breakableDamage, totals.breakableDamage, 10.0f, 5));
    score += static_cast<std::uint32_t>(std::min(Share(player.bossDamage, totals.bossDamage, 30.0f, 0), std::int32_t{30}));
    if (player.hits)
        score += static_cast<std::uint32_t>(Share(static_cast<float>(player.headHits),
            static_cast<float>(player.hits), 5.0f, 0));
    score += static_cast<std::uint32_t>(player.bestChain < 50 ? player.bestChain / 5 : 10);
    score -= static_cast<std::uint32_t>(player.retries) * 4u;
    const auto positive = std::max(std::bit_cast<std::int32_t>(score), std::int32_t{0});
    const float factor = eligiblePlayers >= 1 && eligiblePlayers <= 3 ? 0.45f :
        eligiblePlayers >= 4 && eligiblePlayers <= 6 ? 0.8f :
        eligiblePlayers >= 7 && eligiblePlayers <= 8 ? 1.4f : 2.0f;
    const float scaled = static_cast<float>(positive) * factor;
    return std::clamp(Truncate(scaled), std::int32_t{0}, std::int32_t{100});
}

std::string_view FinalResultGrade(std::int32_t score)
{
    return score >= 50 ? "S" : score >= 40 ? "A" : score >= 25 ? "B" : score >= 10 ? "C" : "D";
}

std::array<std::int32_t, 5> FinalResultValues(const FinalResultStatistics& player,
    const FinalResultTotals& totals)
{
    return {Truncate(player.bossDamage), Truncate(totals.bossDamage),
        player.headHits, player.hits, player.bestChain};
}
}
