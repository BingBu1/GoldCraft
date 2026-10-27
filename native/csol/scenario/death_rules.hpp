// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "monster_types.hpp"

#include <optional>

namespace csol::scenario
{
struct ResourceFileApi;
struct ResourceError;

struct MonsterDeathRule
{
    std::size_t recordIndex{};
    std::int32_t typeId{};
    bool drainMonsters{};
    bool stopWaves{};
};

struct MonsterDeathRuleTable
{
    CsvDocument source;
    std::vector<MonsterDeathRule> rows;
    // The original table keeps the first row for a repeated full-width key.
    std::map<std::int32_t, std::size_t> byType;
    const MonsterDeathRule* Find(std::int32_t typeId) const;
};

// Row zero is skipped, without requiring translated header names. Missing
// columns and empty fields are rejected rather than reproducing uninitialized
// or stale getter output. Nonempty whitespace cells retain atoi's zero result.
// Pure parsing preserves output on failure; the map loader below clears first.
bool ReadMonsterDeathRules(std::string_view bytes, MonsterDeathRuleTable& output,
    ConfigError& error);
bool ReadMonsterDeathRuleResource(std::span<const std::byte> encoded,
    MonsterDeathRuleTable& output, ConfigError& error);

// Resolve only the mm_zb_die_<id> suffix. The original Use argument is integer
// 2; its CSO semantic mapping remains separate from this data selection.
std::optional<std::int32_t> MonsterDeathTarget(const MonsterType& type,
    std::int32_t actualType);

// Optional per-map ZBS_DIE_EVENT table. Missing files mean an empty new table,
// never reuse of the preceding map's rules. A present malformed file is an
// explicit error. This does not execute targets, drain the pool or end a round.
class MonsterDeathRuleResources
{
public:
    bool BeginMap(std::string_view mapName, ResourceFileApi files, ResourceError& error);
    bool Clear();
    const MonsterDeathRuleTable& Rules() const { return rules_; }
    std::string_view MapName() const { return mapName_; }
    bool Present() const { return present_; }
private:
    MonsterDeathRuleTable rules_;
    std::string mapName_;
    bool present_{};
    bool loading_{};
};
}
