// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "difficulty.hpp"

namespace csol::scenario
{
struct MonsterHitRewardSettings
{
    float minimumExperience{}, maximumExperience{}, experiencePerDamage{};
    float maximumMoney{}, moneyPerDamage{};
};

struct MonsterHitRewardArguments
{
    std::int32_t experience{};
    float randomFirst{}, randomSecond{};
};

bool ReadMonsterHitRewardSettings(const DifficultyResources& difficulty,
    MonsterHitRewardSettings& output, ConfigError& error);

// D is the damage at MP 11386B1A, before the final health clamp and later
// weapon/context overrides. Do not substitute actual health lost or kill XP.
bool PrepareMonsterHitReward(const MonsterHitRewardSettings& settings, float damage,
    MonsterHitRewardArguments& output);
// The caller draws RANDOM_FLOAT(first, second) without sorting the arguments.
// moneyExtractor is the authoritative item353 flag, not characterFeature.
bool ResolveMonsterHitMoney(float randomValue, bool moneyExtractor, int roomOption,
    std::int32_t& output);
// Death's slot bonus is already an integer. An absent extractor (or room800)
// must preserve it without a float round trip, including values above 2^24.
std::int32_t ResolveMonsterKillMoney(std::int32_t baseMoney, bool moneyExtractor, int roomOption);
}
