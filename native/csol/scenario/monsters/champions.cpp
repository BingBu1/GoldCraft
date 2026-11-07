// SPDX-License-Identifier: GPL-3.0-or-later
#include "champions.hpp"
#include "../../config_codec.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <memory>

namespace csol::scenario
{
namespace
{
std::string_view TrimStart(std::string_view value)
{
    const auto at = value.find_first_not_of(" \t\r\n\v\f");
    return at == value.npos ? std::string_view{} : value.substr(at);
}
bool Short(std::string_view value, std::int16_t& output)
{
    value = TrimStart(value);
    if (value.starts_with('+')) value.remove_prefix(1);
    std::int32_t number{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || parsed.ec != std::errc{}) return false;
    output = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(number));
    return true;
}
bool Number(std::string_view value, float& output)
{
    value = TrimStart(value);
    if (value.starts_with('+')) value.remove_prefix(1);
    double number{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || parsed.ec != std::errc{} || !std::isfinite(number)) return false;
    output = static_cast<float>(number);
    return std::isfinite(output);
}
enum class ReadResult { Absent, Loaded, Invalid };
ReadResult ReadFile(const std::string& path, ResourceFileApi files,
    ChampionTable& output, ConfigError& error)
{
    int size{};
    std::unique_ptr<unsigned char, decltype(files.release)> data(files.load(path.c_str(), &size), files.release);
    if (!data || size == 0) return ReadResult::Absent;
    if (size < 0 || size > 8 * 1024 * 1024 || size % 8)
    { error = {0, "invalid encrypted champion table size"}; return ReadResult::Invalid; }
    return ReadChampionResource(std::as_bytes(std::span(data.get(), static_cast<std::size_t>(size))),
        output, error) ? ReadResult::Loaded : ReadResult::Invalid;
}
}

std::map<std::uint16_t, std::size_t> ChampionTable::Select(std::uint16_t difficulty) const
{
    std::map<std::uint16_t, std::size_t> selected;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].difficulty == static_cast<int>(difficulty))
            selected[static_cast<std::uint16_t>(rows[i].type.id)] = i;
    return selected;
}

bool ReadChampions(std::string_view bytes, ChampionTable& output, ConfigError& error)
{
    ChampionTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    const auto fail = [&](std::size_t at, const char* reason)
    { error = {at, reason}; return false; };
    constexpr std::string_view columns[]{";Difficulty", "spawnstarttime", "interval", "spawnendtime",
        "maxnum", "zombitype", "appearance", "attackDamage", "attackRange", "attackInterval",
        "health", "armor", "speed", "bonusMoney", "priority", "skill", "color", "Killtype",
        "EventClass", "propensity", "clearpoint", "dropItem", "exp"};
    if (candidate.source.records.empty() || candidate.source.records.front().fields.size() < std::size(columns))
        return fail(0, "champion table header requires 23 columns");
    const auto& header = candidate.source.records.front();
    for (std::size_t i = 0; i < std::size(columns); ++i)
    {
        // Original resources can retain a trailing CR before their CRLF.
        auto value = std::string_view(header.fields[i]);
        while (value.ends_with('\r')) value.remove_suffix(1);
        if (value != columns[i]) return fail(header.byteOffset, "unexpected champion table column");
    }
    for (std::size_t i = 1; i < candidate.source.records.size(); ++i)
    {
        const auto& record = candidate.source.records[i];
        if (std::all_of(record.fields.begin(), record.fields.end(),
            [](const auto& field) { return TrimStart(field).empty(); })) continue;
        if (record.fields.size() < std::size(columns)) return fail(record.byteOffset, "incomplete champion row");
        ChampionRow row;
        row.recordIndex = i;
        if (!Short(record.fields[0], row.difficulty) || !Number(record.fields[1], row.start) ||
            !Number(record.fields[2], row.interval) || !Number(record.fields[3], row.end) ||
            !Short(record.fields[4], row.maximum)) return fail(record.byteOffset, "invalid champion schedule");
        CsvRecord typeRecord{record.byteOffset, {record.fields.begin() + 5, record.fields.begin() + 23}};
        if (!ReadMonsterTypeRecord(typeRecord, i, row.type, error)) return false;
        row.type.champion = true;
        if (row.type.priority < 0 || row.type.priority > 2)
            return fail(record.byteOffset, "champion priority is outside the original pool categories");
        // The old MP loader copies an uninitialized EXP field. The shipped
        // resource explicitly supplies exp; use that deterministic data rather
        // than emulating a stack read. No extra difficulty multiplier applies.
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    error = {};
    return true;
}

bool ReadChampionResource(std::span<const std::byte> encoded, ChampionTable& output, ConfigError& error)
{
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    { error = {0, "invalid encrypted champion table size"}; return false; }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    return ReadChampions(plain, output, error);
}

bool LoadChampions(std::string_view mapName, ResourceFileApi files,
    ChampionTable& output, std::string& sourcePath, ResourceError& error)
{
    if (mapName.empty() || mapName.size() > 63 || !std::all_of(mapName.begin(), mapName.end(), [](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }) ||
        !files.load || !files.release)
    { error = {{}, {0, "champion loading requires a valid map name and file callbacks"}}; return false; }
    ChampionTable candidate;
    ConfigError detail;
    std::string path = "maps/ZBS_Champ_ZT_" + std::string(mapName) + ".cso";
    auto result = ReadFile(path, files, candidate, detail);
    if (result == ReadResult::Absent)
    {
        path = "maps/ZBS_Champ_ZT_Default.cso";
        result = ReadFile(path, files, candidate, detail);
    }
    if (result != ReadResult::Loaded)
    {
        if (result == ReadResult::Absent) detail = {0, "default champion table is missing or empty"};
        error = {path, std::move(detail)};
        return false;
    }
    output = std::move(candidate);
    sourcePath = std::move(path);
    error = {};
    return true;
}
}
