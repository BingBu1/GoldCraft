// SPDX-License-Identifier: GPL-3.0-or-later
#include "round_settings.hpp"

#include <bit>
#include <charconv>

namespace csol::scenario
{
namespace
{
bool IntegerCell(const CsvRecord& record, std::size_t column, std::int32_t& output)
{
    output = 0;
    if (column >= record.fields.size()) return true;
    std::string_view value = record.fields[column];
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == value.npos) return true;
    value.remove_prefix(first);
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return true;
    }
    // The original CSV wrapper uses atoi and then stores the low byte/short.
    // Keep its prefix conversion and zero for nonnumeric input, but reject
    // values outside int32 rather than reproduce an overflowing CRT getter.
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}
}

bool ReadRoundSettings(const RoundTable& table, std::size_t index,
    RoundSettings& output, ConfigError& error)
{
    error = {};
    if (table.headerRecord != 6 || index >= table.rows.size() ||
        table.rows[index].recordIndex != index + 7 ||
        table.rows[index].recordIndex >= table.source.records.size())
    {
        error.message = "scenario round settings have no source round";
        return false;
    }
    const auto& record = table.source.records[table.rows[index].recordIndex];
    std::int32_t music{}, playerSpawn{};
    if (!IntegerCell(record, 10, music) || !IntegerCell(record, 11, playerSpawn))
    {
        error = {record.byteOffset, "scenario round setting overflows the original int32 getter"};
        return false;
    }
    output = {static_cast<std::uint8_t>(music),
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(playerSpawn))};
    return true;
}

bool ReadRewardRound(const RoundTable& table, std::int32_t& output, ConfigError& error)
{
    error = {};
    if (table.headerRecord != 6 || table.source.records.size() <= 5)
    { error.message = "scenario reward round has no metadata row"; return false; }
    std::int32_t value{};
    const auto& record = table.source.records[5];
    // The original getter does not write a null/empty cell. This host has no
    // previous external manager value to preserve, so reject that input.
    if (record.fields.size() <= 1 || record.fields[1].empty())
    { error = {record.byteOffset, "scenario reward round metadata is empty"}; return false; }
    if (!IntegerCell(record, 1, value))
    { error = {record.byteOffset, "scenario reward round overflows the original int32 getter"}; return false; }
    output = value;
    return true;
}
}
