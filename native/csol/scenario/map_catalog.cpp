// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_catalog.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace csol::scenario
{
namespace
{
std::string_view Trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == value.npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\v\f");
    return value.substr(first, last - first + 1);
}

std::string_view Field(const CsvRecord& row, std::size_t column)
{
    return column < row.fields.size() ? row.fields[column] : std::string_view{};
}

bool Integer(std::string_view value, int& output)
{
    value = Trim(value);
    if (value.empty()) { output = 0; return true; }
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return false;
    }
    return std::from_chars(value.data(), value.data() + value.size(), output).ec == std::errc{};
}
}

const MapCatalogRow* MapCatalog::Find(int id) const
{
    const auto found = byId.find(id);
    return found != byId.end() && found->second < rows.size() ? &rows[found->second] : nullptr;
}

bool MapCatalog::ResolveScenarioType(std::string_view fileName, int& output, ConfigError& error) const
{
    error = {};
    const MapCatalogRow* selected = nullptr;
    for (const auto& row : rows)
    {
        if (row.fileName != fileName || Find(row.id) != &row) continue;
        if (selected && selected->scenarioType != row.scenarioType)
        {
            error.message = "map filename has conflicting scenario subtypes; a numeric map ID is required";
            if (row.recordIndex < source.records.size())
                error.byteOffset = source.records[row.recordIndex].byteOffset;
            return false;
        }
        selected = &row;
    }
    if (!selected)
    {
        error.message = "map filename is absent from the CSOL map catalog";
        return false;
    }
    output = selected->scenarioType;
    return true;
}

bool ReadMapCatalog(std::string_view bytes, MapCatalog& output, ConfigError& error)
{
    MapCatalog candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    const auto fail = [&](std::size_t offset, std::string message)
    {
        error = {offset, std::move(message)};
        return false;
    };
    if (candidate.source.records.empty()) return fail(0, "map catalog header is missing");
    const auto& header = candidate.source.records.front();
    if (Trim(Field(header, 0)) != ";map_id" || Trim(Field(header, 1)) != "map_name" ||
        Trim(Field(header, 2)) != "map_filename" || Trim(Field(header, 58)) != "SubType")
        return fail(header.byteOffset, "unexpected map catalog columns 0/1/2/58");

    for (std::size_t i = 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (std::all_of(record.fields.begin(), record.fields.end(),
            [](const auto& field) { return Trim(field).empty(); })) continue;
        if (Trim(Field(record, 0)).starts_with(';')) continue;
        MapCatalogRow row;
        row.recordIndex = i;
        if (Trim(Field(record, 0)).empty() || !Integer(Field(record, 0), row.id) ||
            !Integer(Field(record, 58), row.scenarioType))
            return fail(record.byteOffset, "invalid map catalog numeric field");
        row.nameToken = Field(record, 1);
        row.fileName = Field(record, 2);
        if (row.fileName.empty()) return fail(record.byteOffset, "map filename is missing");
        candidate.byId.emplace(row.id, candidate.rows.size());
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}
}
