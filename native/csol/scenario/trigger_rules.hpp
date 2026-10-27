// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include "trigger_volumes.hpp"

namespace csol::scenario
{
struct TriggerRule
{
    std::size_t recordIndex{};
    std::int16_t group{};
    char command{};
    std::string suffix;
    std::int16_t trigger{};
    std::vector<std::uint16_t> waves;
    std::int16_t cooldown{};
    std::int8_t destinationRatio{};
    std::vector<std::uint16_t> endWaves;
};

struct TriggerRuleTable
{
    CsvDocument source;
    std::vector<TriggerRule> rows;
    std::map<std::int16_t, std::vector<std::size_t>> groups;
};

// TRG has a header and grouped rules; MAP_TRG is a different, headerless
// geometry fallback. Preserve all extra TRG columns without assigning guessed
// behavior to hostCrashReset/roundTimeBase, which this loader does not consume.
bool ReadTriggerRules(std::string_view bytes, TriggerRuleTable& output, ConfigError& error);
}
