// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_rules.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace csol::scenario
{
namespace
{
std::string_view TrimStart(std::string_view value)
{
    const auto begin = value.find_first_not_of(" \t\r\n\v\f");
    return begin == value.npos ? std::string_view{} : value.substr(begin);
}

std::string_view Field(const CsvRecord& row, std::size_t column)
{
    return column < row.fields.size() ? row.fields[column] : std::string_view{};
}

bool Integer(std::string_view value, int& output)
{
    value = TrimStart(value);
    if (value.empty()) { output = 0; return true; }
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return false;
    }
    return std::from_chars(value.data(), value.data() + value.size(), output).ec == std::errc{};
}

bool WaveIds(std::string_view value, std::vector<std::uint16_t>& ids)
{
    // Original 11391EF0 scans decimal digit runs; minus signs, dots, spaces,
    // commas and other nondigits all separate runs. Preserve order/repetition.
    while (!value.empty())
    {
        const auto begin = value.find_first_of("0123456789");
        if (begin == value.npos) break;
        value.remove_prefix(begin);
        const auto end = value.find_first_not_of("0123456789");
        int id{};
        if (!Integer(value.substr(0, end), id)) return false;
        ids.push_back(WaveKey(id));
        if (end == value.npos) break;
        value.remove_prefix(end);
    }
    return true;
}
}

bool ReadTriggerRules(std::string_view bytes, TriggerRuleTable& output, ConfigError& error)
{
    TriggerRuleTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    const auto fail = [&](std::size_t offset, std::string message)
    {
        error = {offset, std::move(message)};
        return false;
    };
    if (candidate.source.records.empty()) return fail(0, "trigger rule header is missing");
    constexpr std::string_view columns[]{"id", "type", "map trigger id", "wave id",
        "cool time", "dest ratio", "end wave"};
    const auto& header = candidate.source.records.front();
    if (header.fields.size() < std::size(columns))
        return fail(header.byteOffset, "trigger rule header requires 7 columns");
    for (std::size_t i = 0; i < std::size(columns); ++i)
        if (header.fields[i] != columns[i])
            return fail(header.byteOffset, "unexpected trigger rule column: " + std::string(columns[i]));
    for (std::size_t i = 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (std::all_of(record.fields.begin(), record.fields.end(),
            [](const auto& field) { return TrimStart(field).empty(); })) continue;
        const auto command = Field(record, 1);
        if (command.empty()) return fail(record.byteOffset, "trigger command is missing");
        TriggerRule row;
        row.recordIndex = i;
        row.command = command.front();
        row.suffix = command.substr(1);
        int group{}, trigger{}, cooldown{}, ratio{};
        if (!Integer(Field(record, 0), group) || !Integer(Field(record, 2), trigger) ||
            !Integer(Field(record, 4), cooldown) || !Integer(Field(record, 5), ratio) ||
            !WaveIds(Field(record, 3), row.waves) || !WaveIds(Field(record, 6), row.endWaves))
            return fail(record.byteOffset, "invalid trigger rule value");
        row.group = TriggerKey(group);
        row.trigger = TriggerKey(trigger);
        row.cooldown = TriggerKey(cooldown);
        row.destinationRatio = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(ratio));
        candidate.groups[row.group].push_back(candidate.rows.size());
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}
}
