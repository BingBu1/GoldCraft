// SPDX-License-Identifier: GPL-3.0-or-later
#include "wall_damage.hpp"
#include "difficulty.hpp"

#include <bit>
#include <cmath>
#include <charconv>

namespace csol::scenario
{
namespace
{
std::int32_t Truncate(float value)
{
    return static_cast<double>(value) >= -2147483648.0 &&
        static_cast<double>(value) < 2147483648.0 ? static_cast<std::int32_t>(value) : INT32_MIN;
}
}

bool ReadWallRewardSettings(const RoundTable& rounds, const DifficultyResources& difficulty,
    WallRewardSettings& output, ConfigError& error)
{
    error = {};
    WallRewardSettings candidate;
    std::int32_t* fields[]{&candidate.threshold, &candidate.money, &candidate.experience};
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (rounds.source.records.size() <= i + 2 ||
            rounds.source.records[i + 2].fields.size() < 2 ||
            rounds.source.records[i + 2].fields[1].empty())
        { error.message = "scenario wall reward metadata is missing"; return false; }
        const auto& row = rounds.source.records[i + 2];
        auto text = std::string_view(row.fields[1]);
        const auto first = text.find_first_not_of(" \t\r\n\v\f");
        if (first == text.npos) continue;
        text.remove_prefix(first);
        if (text.front() == '+')
        {
            text.remove_prefix(1);
            if (text.empty() || text.front() < '0' || text.front() > '9') continue;
        }
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *fields[i]);
        if (parsed.ec != std::errc{} && parsed.ec != std::errc::invalid_argument)
        { error = {row.byteOffset, "scenario wall reward exceeds int32"}; return false; }
    }
    // The original loops forever with a nonpositive threshold. Reject this
    // broken configuration instead of hanging the authoritative server.
    if (candidate.threshold <= 0)
    { error.message = "scenario wall reward threshold must be positive"; return false; }
    if (!difficulty.Parameter(15, 0x15, candidate.moneyRatio, error) ||
        !difficulty.Parameter(15, 0x14, candidate.experienceRatio, error)) return false;
    output = candidate;
    return true;
}

bool ScaleWallReward(std::int32_t input, float ratio, std::int32_t& output)
{
    if (!std::isfinite(ratio)) return false;
    const float scaled = static_cast<float>(input) * ratio;
    if (!std::isfinite(scaled)) return false;
    output = Truncate(scaled);
    return true;
}

bool ResolvePlayerWallDamage(float input, float ratio, std::uint8_t& attackLevel,
    bool elevenLevelLimit, std::uint8_t characterFeature, float& output)
{
    if (!std::isfinite(input) || !std::isfinite(ratio)) return false;
    const std::uint8_t cap = elevenLevelLimit ? 11 : 41;
    const auto level = attackLevel > cap ? cap : attackLevel;
    const double factor = (1.0 + static_cast<double>(static_cast<int>(level) - 1) * 0.1) *
        (characterFeature == 0x44 ? 3.0 : 1.0);
    const float upgraded = static_cast<float>(static_cast<double>(input) * factor);
    const float damage = upgraded * ratio;
    if (!std::isfinite(damage)) return false;
    attackLevel = level;
    output = damage;
    return true;
}

bool ResolveMonsterWallDamage(float input, float ratio, std::int8_t managerLevel, float& output)
{
    // MP 11398406..11398412 rounds MULSD before SUBSD. AVX2 contraction
    // leaves a nonzero residue for ratio=1, level=11, unlike that sequence.
    #pragma clang fp contract(off)
    if (!std::isfinite(input) || !std::isfinite(ratio)) return false;
    const float factor = static_cast<float>(static_cast<double>(ratio) -
        static_cast<double>(static_cast<int>(managerLevel) - 1) * 0.1);
    const float damage = factor * input;
    if (!std::isfinite(damage)) return false;
    output = damage;
    return true;
}

bool AccumulateWallDamage(std::int16_t previous, float scaledDamage, std::int16_t& output)
{
    if (!std::isfinite(scaledDamage)) return false;
    const float total = static_cast<float>(previous) + scaledDamage;
    if (!std::isfinite(total)) return false;
    const auto converted = Truncate(total);
    output = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(converted));
    return true;
}
}
