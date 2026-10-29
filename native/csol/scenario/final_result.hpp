// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace csol::scenario
{
// Values from the original per-slot result state (+10 through +30).
// counter1C retains its field identity until its full producer is recovered.
// The owner must provide actual values, including retries and wall damage;
// unrelated weapon/upgrade totals cannot supply missing result statistics.
struct FinalResultStatistics
{
    float bossDamage{}, ordinaryDamage{}, breakableDamage{};
    std::int32_t counter1C{}, hits{}, headHits{}, bestChain{}, retries{};
};

struct FinalResultTotals
{
    float bossDamage{}, ordinaryDamage{}, breakableDamage{};
};

// The original registry includes slot statistics independently of recipient
// eligibility. Preserve its order and membership; do not sum only live CTs
// or append disconnected-generation archives to the current slot registry.
FinalResultTotals SumFinalResultStatistics(std::span<const FinalResultStatistics> statistics);
// eligiblePlayers comes from the original recipient filter, not total slots.
std::int32_t CalculateFinalResultScore(const FinalResultStatistics& player,
    const FinalResultTotals& totals, std::uint32_t eligiblePlayers);
std::string_view FinalResultGrade(std::int32_t score);
// Original wire order: own Boss damage, all Boss damage, head hits, hits,
// best chain. The float fields are CVTTSS2SI, never rounded or saturated.
std::array<std::int32_t, 5> FinalResultValues(const FinalResultStatistics& player,
    const FinalResultTotals& totals);
}
