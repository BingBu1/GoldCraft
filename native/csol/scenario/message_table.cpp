// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_table.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace csol::scenario
{
namespace
{
std::string_view Field(const CsvRecord& record, std::size_t column)
{
    return column < record.fields.size() ? record.fields[column] : std::string_view{};
}

std::string_view LeadingSpace(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    return first == value.npos ? std::string_view{} : value.substr(first);
}

bool Integer(std::string_view value, std::int32_t& output)
{
    if (value.empty()) return true; // An empty CSV cell does not write the field.
    value = LeadingSpace(value);
    output = 0;
    if (value.empty()) return true;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return true;
    }
    // atoi accepts a decimal prefix, and produces zero without one. Keep that
    // behavior, but reject overflow whose original CRT result is not established.
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}

bool Float(std::string_view value, float& output)
{
    if (value.empty()) return true;
    value = LeadingSpace(value);
    output = 0.0f;
    if (value.empty()) return true;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() == '+' || value.front() == '-') return true;
    }
    auto magnitude = value;
    if (magnitude.starts_with('-')) magnitude.remove_prefix(1);
    // Do not silently parse a hex float as decimal zero. Exceptional CRT
    // spellings and nonfinite/out-of-range values are outside this importer.
    if (magnitude.starts_with("0x") || magnitude.starts_with("0X")) return false;
    double number{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec != std::errc{} || !std::isfinite(number)) return false;
    // Original atof returns double before the caller stores a float.
    output = static_cast<float>(number);
    return std::isfinite(output);
}
}

const ScenarioMessage* MessageTable::Find(std::uint16_t id) const
{
    const auto found = byId.find(id);
    return found == byId.end() ? nullptr : &rows[found->second];
}

bool ReadMessageTable(std::string_view bytes, MessageTable& output, ConfigError& error)
{
    MessageTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    for (std::size_t i = 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (std::all_of(record.fields.begin(), record.fields.end(),
            [](const auto& field) { return field.empty(); })) continue;
        const auto fail = [&]()
        {
            error = {record.byteOffset, "invalid scenario message field"};
            return false;
        };
        // An absent ID was not established as a valid original table row.
        if (Field(record, 0).empty()) return fail();
        ScenarioMessage row;
        row.recordIndex = i;
        std::int32_t id{};
        if (!Integer(Field(record, 0), id)) return fail();
        row.id = static_cast<std::uint16_t>(id);
        row.text = Field(record, 1);
        row.image = Field(record, 2);
        row.sound = Field(record, 3);
        for (std::size_t channel = 0; channel < row.color.size(); ++channel)
        {
            std::int32_t color = row.color[channel];
            if (!Integer(Field(record, channel + 4), color)) return fail();
            row.color[channel] = static_cast<std::uint8_t>(color);
        }
        if (!Float(Field(record, 7), row.fadeIn) || !Float(Field(record, 8), row.show) ||
            !Float(Field(record, 9), row.fadeOut) || !Integer(Field(record, 10), row.type) ||
            !Integer(Field(record, 11), row.textType) || !Float(Field(record, 12), row.flickerTime) ||
            !Float(Field(record, 13), row.xCenterRatio) || !Float(Field(record, 14), row.yCenterRatio))
            return fail();
        candidate.byId[row.id] = candidate.rows.size();
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}
}
