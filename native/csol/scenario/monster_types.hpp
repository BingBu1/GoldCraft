// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <span>

namespace csol::scenario
{
struct MonsterType
{
    std::size_t recordIndex{};
    std::int32_t id{};
    std::string appearance;
    std::int16_t attackDamage{};
    std::int16_t attackRange{};
    float attackInterval{};
    std::int32_t health{};
    std::int32_t armor{};
    std::int16_t speed{};
    std::int16_t bonusMoney{};
    std::int8_t priority{};
    std::int16_t skill{};
    std::array<float, 3> color{};
    std::int16_t killType{};
    std::int16_t eventClass{};
    std::int8_t propensity{};
    float clearPoint{};
    std::vector<std::int32_t> dropItems;
    std::int32_t experience{};

    // These mode-15 extension columns have verified storage/defaults, but
    // their gameplay meanings are still under investigation. Keep that clear
    // rather than inventing labels. Positive column20 overrides the map key,
    // not the original id stored in this record.
    bool column18{};
    std::int32_t column19{-1};
    std::int32_t column20{-1};
    std::array<float, 3> column21{};
    std::int32_t column22{-1};
    std::int32_t Key() const { return column20 > 0 ? column20 : id; }
};

struct MonsterTypeTable
{
    CsvDocument source;
    std::vector<MonsterType> rows;
    // Full int32 identity. Preserve all source rows; the last equal key wins.
    std::map<std::int32_t, std::size_t> byId;
    const MonsterType* Find(std::int32_t id) const;
};

// Parse file rows only. Appearance resolution uses the original catalog;
// model paths, skill programs and synthetic runtime types are separate stages.
// Reject missing core fields/color components whose original loader otherwise
// reads uninitialized stack memory. Failure preserves the previous table.
bool ReadMonsterTypes(std::string_view bytes, MonsterTypeTable& output, ConfigError& error);
bool ReadMonsterTypeResource(std::span<const std::byte> encoded,
    MonsterTypeTable& output, ConfigError& error);
}
