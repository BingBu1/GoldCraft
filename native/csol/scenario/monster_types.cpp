// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <utility>

namespace csol::scenario
{
namespace
{
bool Space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == '\v' || value == '\f';
}

std::string_view LeadingSpace(std::string_view value)
{
    while (!value.empty() && Space(value.front())) value.remove_prefix(1);
    return value;
}

bool Integer(std::string_view value, std::int32_t& output)
{
    value = LeadingSpace(value);
    if (value.empty()) { output = 0; return true; }
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return false;
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    // The original getter uses atoi's decimal prefix, not a full-token parser.
    return result.ec == std::errc{};
}

bool Float(std::string_view value, float& output)
{
    value = LeadingSpace(value);
    if (value.empty()) { output = 0; return true; }
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() == '-' || value.front() == '+') return false;
    }
    if (value.empty()) return false;
    auto unsignedValue = value;
    if (unsignedValue.front() == '-') unsignedValue.remove_prefix(1);
    if (unsignedValue.starts_with("0x") || unsignedValue.starts_with("0X")) return false;
    double parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || !std::isfinite(parsed)) return false;
    // HW calls atof (double) and stores float; retain the same conversion order.
    output = static_cast<float>(parsed);
    return std::isfinite(output);
}

std::string_view Field(const CsvRecord& record, std::size_t column)
{
    return column < record.fields.size() ? record.fields[column] : std::string_view{};
}

bool DefaultInteger(const CsvRecord& record, std::size_t column,
    std::int32_t fallback, std::int32_t& output)
{
    const auto value = Field(record, column);
    if (value.empty()) { output = fallback; return true; }
    return Integer(value, output);
}

template<class T>
bool NarrowInteger(const CsvRecord& record, std::size_t column, T& output)
{
    std::int32_t value{};
    if (!DefaultInteger(record, column, 0, value)) return false;
    if constexpr (sizeof(T) == 1)
        output = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value));
    else
        output = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
    return true;
}

// The original color reader scans unsigned decimal digit runs, with any other
// byte separating runs. It does not parse signed/floating-point RGB values.
bool DigitRuns(std::string_view value, std::array<float, 3>& output, bool requireThree)
{
    std::size_t count = 0;
    while (!value.empty())
    {
        const auto begin = value.find_first_of("0123456789");
        if (begin == value.npos) break;
        value.remove_prefix(begin);
        const auto end = value.find_first_not_of("0123456789");
        std::int32_t item{};
        if (count == output.size() || !Integer(value.substr(0, end), item)) return false;
        output[count++] = static_cast<float>(item);
        if (end == value.npos) break;
        value.remove_prefix(end);
    }
    return !requireThree || count == output.size();
}

bool DropItems(std::string_view value, std::vector<std::int32_t>& output)
{
    while (!value.empty())
    {
        const auto end = value.find('|');
        std::string token;
        for (const char character : value.substr(0, end))
            if (!Space(character)) token.push_back(character);
        // HW drops all whitespace, ignores empty segments and limits tokens to
        // 63 bytes. Reject oversized/malformed values instead of truncating.
        if (!token.empty())
        {
            std::int32_t item{};
            auto number = std::string_view(token);
            // This list getter, unlike atoi, accepts an optional '+' followed
            // by an optional '-'. Preserve that valid original spelling.
            if (number.starts_with("+-")) number.remove_prefix(1);
            if (token.size() > 63 || !Integer(number, item)) return false;
            output.push_back(item);
        }
        if (end == value.npos) return true;
        value.remove_prefix(end + 1);
    }
    return true;
}
}

const MonsterType* MonsterTypeTable::Find(std::int32_t id) const
{
    const auto found = byId.find(id);
    return found == byId.end() ? nullptr : &rows[found->second];
}

bool ReadMonsterTypes(std::string_view bytes, MonsterTypeTable& output, ConfigError& error)
{
    MonsterTypeTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    if (candidate.source.records.empty())
    {
        error = {0, "monster type header is missing"};
        return false;
    }
    const auto fail = [&](const CsvRecord& record, std::string message)
    {
        error = {record.byteOffset, std::move(message)};
        return false;
    };
    constexpr std::string_view columns[]{"id", "appearance", "attackDamage", "attackRange",
        "attackInterval", "health", "armor", "speed", "bonusMoney", "priority", "skill",
        "color", "Killtype", "EventClass", "propensity", "clearpoint", "dropItem", "exp"};
    const auto& header = candidate.source.records.front();
    if (header.fields.size() < std::size(columns))
        return fail(header, "monster type header requires 18 columns");
    for (std::size_t i = 0; i < std::size(columns); ++i)
        if (header.fields[i] != columns[i])
            return fail(header, "unexpected monster type column: " + std::string(columns[i]));

    for (std::size_t i = 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (std::all_of(record.fields.begin(), record.fields.end(),
            [](const auto& value) { return LeadingSpace(value).empty(); })) continue;
        // Original scalar int/float getters do not write on absent/empty data.
        // Their caller leaves these fields uninitialized; reject that input.
        for (const auto column : {0u, 1u, 4u, 5u, 6u, 11u, 15u, 17u})
            if (Field(record, column).empty()) return fail(record, "missing core monster type field");
        MonsterType row;
        row.recordIndex = i;
        row.appearance = Field(record, 1);
        if (!Integer(Field(record, 0), row.id) ||
            !NarrowInteger(record, 2, row.attackDamage) ||
            !NarrowInteger(record, 3, row.attackRange) ||
            !Float(Field(record, 4), row.attackInterval) ||
            !Integer(Field(record, 5), row.health) ||
            !Integer(Field(record, 6), row.armor) ||
            !NarrowInteger(record, 7, row.speed) ||
            !NarrowInteger(record, 8, row.bonusMoney) ||
            !NarrowInteger(record, 9, row.priority) ||
            !NarrowInteger(record, 10, row.skill) ||
            !DigitRuns(Field(record, 11), row.color, true) ||
            !NarrowInteger(record, 12, row.killType) ||
            !NarrowInteger(record, 13, row.eventClass) ||
            !NarrowInteger(record, 14, row.propensity) ||
            !Float(Field(record, 15), row.clearPoint) ||
            !DropItems(Field(record, 16), row.dropItems) ||
            !Integer(Field(record, 17), row.experience))
            return fail(record, "invalid monster type value");
        std::int32_t enabled{};
        if (!DefaultInteger(record, 18, 0, enabled) ||
            !DefaultInteger(record, 19, -1, row.column19) ||
            !DefaultInteger(record, 20, -1, row.column20) ||
            !DigitRuns(Field(record, 21), row.column21, false) ||
            !DefaultInteger(record, 22, -1, row.column22))
            return fail(record, "invalid monster type extension");
        row.column18 = enabled == 1;
        candidate.byId[row.Key()] = candidate.rows.size();
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}
}
