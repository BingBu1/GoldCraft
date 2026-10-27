// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_rewards.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::scenario
{
namespace
{
std::int32_t Truncate(float value)
{
    // Original CVTTSS2SI returns the integer indefinite on finite overflow.
    return static_cast<double>(value) >= -2147483648.0 &&
        static_cast<double>(value) < 2147483648.0 ? static_cast<std::int32_t>(value) : INT32_MIN;
}
}

bool ReadMonsterHitRewardSettings(const DifficultyResources& difficulty,
    MonsterHitRewardSettings& output, ConfigError& error)
{
    error = {};
    MonsterHitRewardSettings candidate;
    if (!difficulty.Parameter(15, 24, candidate.experiencePerDamage, error) ||
        !difficulty.Parameter(15, 22, candidate.minimumExperience, error) ||
        !difficulty.Parameter(15, 23, candidate.maximumExperience, error) ||
        !difficulty.Parameter(15, 25, candidate.maximumMoney, error) ||
        !difficulty.Parameter(15, 26, candidate.moneyPerDamage, error)) return false;
    output = candidate;
    return true;
}

bool PrepareMonsterHitReward(const MonsterHitRewardSettings& settings, float damage,
    MonsterHitRewardArguments& output)
{
    if (!std::isfinite(damage) || !std::isfinite(settings.minimumExperience) ||
        !std::isfinite(settings.maximumExperience) || !std::isfinite(settings.experiencePerDamage) ||
        !std::isfinite(settings.maximumMoney) || !std::isfinite(settings.moneyPerDamage)) return false;
    // Keep the two original float stores and both int conversions. In
    // particular MINSS is applied to float(int(total)), not directly to total.
    const float product = settings.experiencePerDamage * damage;
    const float total = product + settings.minimumExperience;
    const float first = settings.moneyPerDamage * damage;
    if (!std::isfinite(product) || !std::isfinite(total) || !std::isfinite(first)) return false;
    const float integer = static_cast<float>(Truncate(total));
    const float capped = integer < settings.maximumExperience ? integer : settings.maximumExperience;
    output = {Truncate(capped), first, settings.maximumMoney};
    return true;
}

bool ResolveMonsterHitMoney(float randomValue, bool moneyExtractor, int roomOption,
    std::int32_t& output)
{
    if (!std::isfinite(randomValue)) return false;
    auto money = Truncate(randomValue);
    if (moneyExtractor && roomOption != 800)
    {
        const float scaled = static_cast<float>(money) * 1.5f;
        money = Truncate(scaled);
    }
    output = money;
    return true;
}
}
