// SPDX-License-Identifier: GPL-3.0-or-later
#include "death_rules.hpp"
#include "map_resources.hpp"
#include "../config_codec.hpp"

#include <algorithm>
#include <charconv>
#include <memory>
#include <utility>

namespace csol::scenario
{
namespace
{
bool Integer(std::string_view value, std::int32_t& output)
{
    if (value.empty()) return false;
    output = 0;
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == value.npos) return true;
    value.remove_prefix(first);
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return true;
    }
    // Original CSV integer conversion consumes a signed decimal prefix.
    // Retain zero for nonnumeric cells, but reject undefined int32 overflow.
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}

bool MapNameValid(std::string_view name)
{
    return !name.empty() && name.size() <= 63 &&
        std::all_of(name.begin(), name.end(), [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}
}

const MonsterDeathRule* MonsterDeathRuleTable::Find(std::int32_t typeId) const
{
    const auto found = byType.find(typeId);
    if (found == byType.end() || found->second >= rows.size()) return nullptr;
    const auto& row = rows[found->second];
    return row.typeId == typeId ? &row : nullptr;
}

bool ReadMonsterDeathRules(std::string_view bytes, MonsterDeathRuleTable& output,
    ConfigError& error)
{
    MonsterDeathRuleTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    for (std::size_t index = 1; index < candidate.source.records.size(); ++index)
    {
        const auto& record = candidate.source.records[index];
        if (record.fields.size() < 3)
        {
            error = {record.byteOffset, "scenario death rule requires three columns"};
            return false;
        }
        if (record.fields[0].empty() || record.fields[1].empty() || record.fields[2].empty())
        {
            error = {record.byteOffset, "scenario death rule contains an empty field with undefined original output"};
            return false;
        }
        std::int32_t type{}, drain{}, stop{};
        if (!Integer(record.fields[0], type) || !Integer(record.fields[1], drain) ||
            !Integer(record.fields[2], stop))
        {
            error = {record.byteOffset, "scenario death rule overflows the original int32 getter"};
            return false;
        }
        candidate.byType.try_emplace(type, candidate.rows.size());
        candidate.rows.push_back({index, type, drain != 0, stop != 0});
    }
    output = std::move(candidate);
    error = {};
    return true;
}

bool ReadMonsterDeathRuleResource(std::span<const std::byte> encoded,
    MonsterDeathRuleTable& output, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    {
        error.message = "invalid encrypted scenario death rule size";
        return false;
    }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    const auto embedded = plain.find('\0');
    if (embedded != plain.npos)
    {
        error = {embedded, "embedded NUL in scenario death rule"};
        return false;
    }
    return ReadMonsterDeathRules(plain, output, error);
}

std::optional<std::int32_t> MonsterDeathTarget(const MonsterType& type,
    std::int32_t actualType)
{
    const auto target = type.column19 > 0 ? type.column19 :
        (type.priority != 0 ? actualType : -1);
    return target >= 0 ? std::optional<std::int32_t>(target) : std::nullopt;
}

bool MonsterDeathRuleResources::Clear()
{
    if (loading_) return false;
    rules_ = {};
    mapName_.clear();
    present_ = false;
    return true;
}

bool MonsterDeathRuleResources::BeginMap(std::string_view mapName,
    ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (loading_)
    {
        error.detail.message = "scenario death rules are already loading";
        return false;
    }
    // The caller may be restarting with this object's own MapName view.
    std::string nextMap(mapName);
    Clear();
    if (!MapNameValid(nextMap))
    {
        error.detail.message = "invalid scenario death rule map name";
        return false;
    }
    mapName_ = std::move(nextMap);
    const std::string path = "maps/ZBS_DIE_EVENT_" + mapName_ + ".cso";
    if (!files.load || !files.release)
    {
        error = {path, {0, "scenario death rule load and release callbacks are required"}};
        return false;
    }
    struct LoadScope
    {
        bool& loading;
        explicit LoadScope(bool& flag) : loading(flag) { loading = true; }
        ~LoadScope() { loading = false; }
    } operation(loading_);
    MonsterDeathRuleTable candidate;
    ConfigError detail;
    {
        int length = 0;
        std::unique_ptr<unsigned char, decltype(files.release)> bytes(
            files.load(path.c_str(), &length), files.release);
        if (!bytes)
        {
            error = {};
            return true;
        }
        if (length <= 0 || length > 8 * 1024 * 1024 || length % 8)
            detail.message = "invalid encrypted scenario death rule size";
        else if (ReadMonsterDeathRuleResource(
            std::as_bytes(std::span(bytes.get(), static_cast<std::size_t>(length))), candidate, detail))
        {
            // Release the engine buffer while the operation is still guarded,
            // before publishing parsed rules to the map owner.
            bytes.reset();
            rules_ = std::move(candidate);
            present_ = true;
            error = {};
            return true;
        }
    }
    // A release callback may attempt a nested load and write its own error.
    // Publish this operation's failure only after that callback has returned.
    error = {path, std::move(detail)};
    return false;
}
}
