// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "monster_types.hpp"

namespace csol::scenario
{
struct RoundMonsterDeltas
{
    std::int16_t damage{};
    std::int16_t range{};
    std::int16_t health{};
    std::int16_t armor{};
    std::int16_t speed{};
    std::int16_t interval{}; // Present in the table; base mode15 does not add it.
    std::int16_t money{};
};

struct MonsterAttributes
{
    float health{};
    float armor{};
    float attackDamage{};
    float attackRange{};
    float attackInterval{};
    float speed{};
    std::int32_t bonusMoney{};
};

// Read the selected source row using original fixed columns 3..9. Keep this
// separate from the basic round/wave importer: these are mode15 attributes,
// not a universal interpretation of every scenario's round format.
bool ReadRoundMonsterDeltas(const RoundTable& table, std::size_t index,
    RoundMonsterDeltas& output, ConfigError& error);

// Base mode15/SubType1 arithmetic, before appearance/map-specific adjustments.
// Reject nonfinite source interval; retain original signed dword wrap for
// health/armor additions and signed-short deltas for the other fields.
bool ResolveMonsterAttributes(const MonsterType& type, const RoundMonsterDeltas& delta,
    MonsterAttributes& output, ConfigError& error);
}
