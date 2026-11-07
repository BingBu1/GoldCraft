// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include <cstdint>

namespace csol::scenario
{
class DifficultyResources;
struct WallRewardSettings
{
    std::int32_t threshold{}, money{}, experience{};
    float moneyRatio{}, experienceRatio{};
};
// Original physical round metadata rows 2/3/4, column 1, and difficulty
// columns 0x15/0x14. These are map settings, not per-round numbered rows.
bool ReadWallRewardSettings(const RoundTable& rounds, const DifficultyResources& difficulty,
    WallRewardSettings& output, ConfigError& error);
bool ScaleWallReward(std::int32_t input, float ratio, std::int32_t& output);
// Upgrade/rule owners supply authoritative values. Level zero is retained;
// the original caps above the limit but does not clamp the lower end to one.
bool ResolvePlayerWallDamage(float input, float ratio, std::uint8_t& attackLevel,
    bool elevenLevelLimit, std::uint8_t characterFeature, float& output);
bool ResolveMonsterWallDamage(float input, float ratio, std::int8_t managerLevel,
    float& output);
// Original cvttss2si then low signed16 store, before threshold rewards and
// before breakable material modifiers. Out-of-range float conversion yields
// the original int32 indefinite value (whose low16 bits are zero).
bool AccumulateWallDamage(std::int16_t previous, float scaledDamage, std::int16_t& output);
}
