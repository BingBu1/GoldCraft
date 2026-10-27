// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace csol::scenario
{
namespace
{
constexpr std::size_t MaxFileBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxFieldBytes = 64 * 1024;
constexpr std::size_t MaxRecords = 100000;
constexpr std::size_t MaxColumns = 256;

bool Fail(ConfigError& error, std::size_t offset, std::string message)
{
    error = {offset, std::move(message)};
    return false;
}

bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && Space(value.front())) value.remove_prefix(1);
    while (!value.empty() && Space(value.back())) value.remove_suffix(1);
    return value;
}

bool Empty(const CsvRecord& record)
{
    return std::all_of(record.fields.begin(), record.fields.end(),
        [](const auto& field) { return Trim(field).empty(); });
}

bool Integer(std::string_view value, int& output)
{
    value = Trim(value);
    if (value.empty()) return false;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return false;
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool TimeInteger(std::string_view value, int& output)
{
    value = Trim(value);
    if (value.empty())
    {
        output = 0;
        return true;
    }
    // MP reads times through the CSV integer getter (atoi), even when the
    // source says 5.5. Parse the integer prefix without floating-point rounding.
    // Keep our range checks and reject nonnumeric data instead of hiding it as 0.
    const auto begin = value.front() == '+' || value.front() == '-' ? 1u : 0u;
    return Integer(value.substr(0, value.find_first_not_of("0123456789", begin)), output);
}

bool IntegerList(std::string_view value, std::vector<int>& output)
{
    value = Trim(value);
    while (!value.empty())
    {
        const auto end = value.find_first_of(" \t\r\n");
        int item;
        if (!Integer(value.substr(0, end), item)) return false;
        output.push_back(item);
        if (end == value.npos) break;
        value = Trim(value.substr(end));
    }
    return !output.empty();
}

bool Columns(const CsvRecord& header, const std::vector<std::string_view>& names,
    std::vector<std::size_t>& columns, ConfigError& error)
{
    for (auto name : names)
    {
        auto column = header.fields.size();
        for (std::size_t i = 0; i < header.fields.size(); ++i)
        {
            if (Trim(header.fields[i]) != name) continue;
            if (column != header.fields.size())
                return Fail(error, header.byteOffset, "duplicate column: " + std::string(name));
            column = i;
        }
        if (column == header.fields.size())
            return Fail(error, header.byteOffset, "missing column: " + std::string(name));
        columns.push_back(column);
    }
    return true;
}

std::size_t Header(const CsvDocument& source, std::string_view firstColumn)
{
    for (std::size_t i = 0; i < source.records.size(); ++i)
    {
        const auto& fields = source.records[i].fields;
        if (!fields.empty() && Trim(fields.front()) == firstColumn) return i;
    }
    return source.records.size();
}
}

bool ReadCsv(std::string_view bytes, CsvDocument& output, ConfigError& error)
{
    error = {};
    if (bytes.empty() || bytes.size() > MaxFileBytes)
        return Fail(error, 0, "CSV must contain 1..8388608 bytes");
    const auto nul = bytes.find('\0');
    if (nul != bytes.npos) return Fail(error, nul, "CSV contains an embedded NUL byte");

    CsvDocument candidate;
    candidate.sourceBytes.assign(bytes);
    std::size_t at = bytes.starts_with("\xef\xbb\xbf") ? 3 : 0;
    while (at < bytes.size())
    {
        if (candidate.records.size() == MaxRecords)
            return Fail(error, at, "CSV exceeds 100000 records");
        CsvRecord record{at, {}};
        for (;;)
        {
            if (record.fields.size() == MaxColumns)
                return Fail(error, at, "CSV exceeds 256 columns");
            std::string field;
            if (at < bytes.size() && bytes[at] == '"')
            {
                ++at;
                bool closed = false;
                while (at < bytes.size())
                {
                    auto c = bytes[at++];
                    if (c == '"')
                    {
                        if (at == bytes.size() || bytes[at] != '"')
                        {
                            closed = true;
                            break;
                        }
                        ++at;
                    }
                    if (field.size() == MaxFieldBytes)
                        return Fail(error, at - 1, "CSV field exceeds 65536 bytes");
                    field.push_back(c);
                }
                if (!closed) return Fail(error, at, "unterminated quoted CSV field");
                if (at < bytes.size() && bytes[at] != ',' && bytes[at] != '\r' && bytes[at] != '\n')
                    return Fail(error, at, "unexpected character after quoted CSV field");
            }
            else
            {
                const auto begin = at;
                while (at < bytes.size() && bytes[at] != ',' && bytes[at] != '\r' && bytes[at] != '\n')
                {
                    if (bytes[at] == '"') return Fail(error, at, "quote inside unquoted CSV field");
                    if (at - begin == MaxFieldBytes)
                        return Fail(error, at, "CSV field exceeds 65536 bytes");
                    ++at;
                }
                field.assign(bytes.substr(begin, at - begin));
            }
            record.fields.push_back(std::move(field));
            if (at < bytes.size() && bytes[at] == ',')
            {
                ++at;
                continue; // Includes the empty field after a trailing comma.
            }
            if (at < bytes.size() && bytes[at] == '\r')
            {
                ++at;
                if (at < bytes.size() && bytes[at] == '\n') ++at;
            }
            else if (at < bytes.size() && bytes[at] == '\n') ++at;
            break;
        }
        candidate.records.push_back(std::move(record));
    }
    output = std::move(candidate);
    return true;
}

bool ReadWaves(std::string_view bytes, WaveTable& output, ConfigError& error)
{
    WaveTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    const auto header = Header(candidate.source, "waveID");
    if (header == candidate.source.records.size())
        return Fail(error, 0, "waveID header is missing");
    std::vector<std::size_t> columns;
    if (!Columns(candidate.source.records[header], {"waveID", "spawnPoint", "startTime",
        "endTime", "intervalTime", "zombi", "zombiRatio", "eventmessage"}, columns, error))
        return false;

    for (auto i = header + 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (Empty(record)) continue;
        // Original tables omit the final event field on many ordinary spawn
        // rows. Keep the raw record intact; an absent optional event is empty.
        if (record.fields.size() <= *std::max_element(columns.begin(), columns.begin() + 7))
            return Fail(error, record.byteOffset, "wave row has missing fields");
        WaveRow row;
        row.recordIndex = i;
        int interval;
        if (!Integer(record.fields[columns[0]], row.waveId) ||
            !Integer(record.fields[columns[1]], row.spawnPoint) ||
            !TimeInteger(record.fields[columns[2]], row.startTime) ||
            !TimeInteger(record.fields[columns[3]], row.endTime) ||
            !TimeInteger(record.fields[columns[4]], interval))
            return Fail(error, record.byteOffset, "invalid wave ID, spawn point or time");
        if (interval < 0 || interval > std::numeric_limits<std::int16_t>::max())
            return Fail(error, record.byteOffset, "wave interval must fit a nonnegative signed 16-bit value");
        row.interval = static_cast<std::int16_t>(interval);
        std::vector<int> weights;
        if (!IntegerList(record.fields[columns[5]], row.types) ||
            !IntegerList(record.fields[columns[6]], weights) || row.types.size() != weights.size())
            return Fail(error, record.byteOffset, "wave types and weights must be matching integer lists");
        std::uint8_t total = 0;
        for (int weight : weights)
        {
            if (weight < -128 || weight > 127)
                return Fail(error, record.byteOffset, "wave weight does not fit a signed byte");
            row.weights.push_back(static_cast<std::int8_t>(weight));
            total = static_cast<std::uint8_t>(total + static_cast<std::uint8_t>(weight));
        }
        row.weightTotal = std::bit_cast<std::int8_t>(total);
        if (columns[7] < record.fields.size()) row.eventMessage = record.fields[columns[7]];
        candidate.groups[WaveKey(row.waveId)][row.spawnPoint].push_back(candidate.rows.size());
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}

bool ReadRounds(std::string_view bytes, RoundTable& output, ConfigError& error)
{
    RoundTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    candidate.headerRecord = Header(candidate.source, "round");
    if (candidate.headerRecord == candidate.source.records.size())
        return Fail(error, 0, "round header is missing");
    std::vector<std::size_t> columns;
    if (!Columns(candidate.source.records[candidate.headerRecord], {"round", "waveTypes"}, columns, error))
        return false;
    for (auto i = candidate.headerRecord + 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (Empty(record)) continue;
        if (record.fields.size() <= std::max(columns[0], columns[1]))
            return Fail(error, record.byteOffset, "round row has missing fields");
        RoundRow row;
        row.recordIndex = i;
        if (!Integer(record.fields[columns[0]], row.round) ||
            !IntegerList(record.fields[columns[1]], row.waves))
            return Fail(error, record.byteOffset, "invalid round or wave list");
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}

std::vector<MissingReference> FindMissingReferences(const RoundTable& rounds,
    const WaveTable& waves, const SpawnPoints& points, int monsterTeam)
{
    std::vector<MissingReference> missing;
    for (const auto& round : rounds.rows)
    {
        for (const int wave : round.waves)
        {
            const auto found = waves.groups.find(WaveKey(wave));
            if (found == waves.groups.end())
            {
                missing.push_back({MissingReferenceKind::Wave, round.recordIndex,
                    std::numeric_limits<std::size_t>::max(), round.round, wave, 0});
                continue;
            }
            for (const auto& [point, rows] : found->second)
            {
                if (point == 9999 || points.FindMonsterPoint(monsterTeam, point)) continue;
                for (auto index : rows)
                    missing.push_back({MissingReferenceKind::MonsterPoint, round.recordIndex,
                        waves.rows[index].recordIndex, round.round, wave, point});
            }
        }
    }
    return missing;
}
}
