// SPDX-License-Identifier: GPL-3.0-or-later
#include "difficulty_monsters.hpp"

#include <bit>
#include <cmath>

namespace csol::scenario
{
namespace
{
bool Multiply(std::int32_t value, float multiplier, std::int32_t& output)
{
    const float input = static_cast<float>(value);
    const float scaled = input * multiplier;
    // CVTTSS2SI truncates toward zero. Reject its nonfinite/out-of-range
    // indefinite result instead of invoking undefined C++ conversions.
    if (!std::isfinite(scaled) || scaled < -2147483648.0f || scaled >= 2147483648.0f)
        return false;
    output = static_cast<std::int32_t>(scaled);
    return true;
}
}

bool ApplyMonsterDifficulty(MonsterTypeTable& types, const DifficultyResources& difficulty,
    ConfigError& error)
{
    error = {};
    if (difficulty.State() != DifficultyState::Ready)
    {
        error.message = "monster difficulty requires the loaded scenario parameter table";
        return false;
    }
    struct Change { std::size_t row; std::int32_t health; std::int16_t speed; };
    std::vector<Change> changes;
    changes.reserve(types.byId.size());
    for (const auto& [key, row] : types.byId)
    {
        if (row >= types.rows.size() || types.rows[row].Key() != key)
        {
            error.message = "monster difficulty has an invalid effective type index";
            return false;
        }
        const auto& type = types.rows[row];
        if (type.priority < 0 || type.priority > 2) continue;
        const auto healthColumn = static_cast<std::size_t>(10 + type.priority * 3);
        float healthFactor{}, speedFactor{};
        if (!difficulty.Parameter(15, healthColumn, healthFactor, error) ||
            !difficulty.Parameter(15, healthColumn + 1, speedFactor, error)) return false;
        std::int32_t health{}, speed{};
        if (!Multiply(type.health, healthFactor, health) || !Multiply(type.speed, speedFactor, speed))
        {
            error.message = "monster difficulty multiplication exceeds the original int32 conversion";
            return false;
        }
        changes.push_back({row, health, std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(speed))});
    }
    for (const auto& change : changes)
    {
        types.rows[change.row].health = change.health;
        types.rows[change.row].speed = change.speed;
    }
    return true;
}
}
