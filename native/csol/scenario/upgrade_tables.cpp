// SPDX-License-Identifier: GPL-3.0-or-later
#include "upgrade_tables.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace csol::scenario
{
namespace
{
std::string_view TrimStart(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n\v\f");
    return first == text.npos ? std::string_view{} : text.substr(first);
}

bool Integer(std::string_view text, std::int32_t& output)
{
    output = 0;
    text = TrimStart(text);
    if (text.empty()) return true;
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9') return true;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}

bool Number(std::string_view text, float& output)
{
    output = 0;
    text = TrimStart(text);
    if (text.empty()) return true;
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '+' || text.front() == '-') return true;
    }
    auto magnitude = text;
    if (magnitude.starts_with('-')) magnitude.remove_prefix(1);
    if (magnitude.starts_with("0x") || magnitude.starts_with("0X")) return false;
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec != std::errc{} || !std::isfinite(value)) return false;
    output = static_cast<float>(value);
    return std::isfinite(output);
}

UpgradeStat CharacterStat(char character)
{
    switch (character)
    {
    case 'd': return UpgradeStat::Damage;
    case 's': return UpgradeStat::Speed;
    case 'a': return UpgradeStat::Armor;
    case 'm': return UpgradeStat::Ammo;
    default: return UpgradeStat::Health;
    }
}

template<class Table, class Reader>
bool LoadFile(const char* path, ResourceFileApi files, Reader reader, Table& output, ResourceError& error)
{
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024 || length % 8)
    { error = {path, {0, "scenario upgrade resource is missing or has an invalid size"}}; return false; }
    ConfigError detail;
    if (!reader(std::as_bytes(std::span(bytes.get(), static_cast<std::size_t>(length))), output, detail))
    { error = {path, std::move(detail)}; return false; }
    return true;
}
}

const UpgradeLevel* UpgradeLevels::ExperienceLevel(int level) const
{
    // Original XP lookup clamps only above 100. Attribute lookup does not.
    if (level < 0) return nullptr;
    const auto index = static_cast<std::size_t>(std::min(level, 100));
    return index < rows.size() ? &rows[index] : nullptr;
}

bool UpgradeLevels::StatValue(UpgradeStat stat, int level, float& output) const
{
    const auto index = static_cast<std::size_t>(stat);
    if (level < 0 || static_cast<std::size_t>(level) >= rows.size() || index >= UpgradeStatCount) return false;
    output = rows[level].values[index];
    return true;
}

const UpgradeClass* UpgradeClasses::Find(std::int32_t id) const
{
    const auto found = byId.find(id);
    return found == byId.end() ? nullptr : &found->second;
}

bool ReadUpgradeLevels(std::string_view bytes, UpgradeLevels& output, ConfigError& error)
{
    UpgradeLevels candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    for (std::size_t index = 1; index < candidate.source.records.size(); ++index)
    {
        const auto& record = candidate.source.records[index];
        if (record.fields.size() < 8)
        { error = {record.byteOffset, "scenario upgrade level requires eight columns"}; return false; }
        UpgradeLevel level;
        if (record.fields[1].empty() || !Integer(record.fields[1], level.experience))
        { error = {record.byteOffset, "scenario upgrade experience has no valid int32 value"}; return false; }
        for (std::size_t stat = 0; stat < UpgradeStatCount; ++stat)
            // Real levels 31..100 retain XP but leave the five attributes
            // empty. The original atof path reads these as zero.
            if (!Number(record.fields[stat + 3], level.values[stat]))
            { error = {record.byteOffset, "scenario upgrade attribute has no finite float value"}; return false; }
        candidate.rows.push_back(level);
    }
    // The original has unconditional access to record 100 for high levels.
    if (candidate.rows.size() < 101)
    { error.message = "scenario upgrade levels must include records zero through one hundred"; return false; }
    output = std::move(candidate);
    return true;
}

bool ReadUpgradeClasses(std::string_view bytes, UpgradeClasses& output, ConfigError& error)
{
    UpgradeClasses candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    candidate.byId.emplace(0, UpgradeClass{{20, 20, 20, 20, 20},
        {UpgradeStat::Health, UpgradeStat::Damage, UpgradeStat::Speed, UpgradeStat::Armor, UpgradeStat::Ammo}});
    for (std::size_t index = 1; index < candidate.source.records.size(); ++index)
    {
        const auto& record = candidate.source.records[index];
        if (record.fields.size() < 7)
        { error = {record.byteOffset, "scenario upgrade class requires seven columns"}; return false; }
        std::int32_t id{};
        UpgradeClass type;
        if (record.fields[0].empty() || !Integer(record.fields[0], id))
        { error = {record.byteOffset, "scenario upgrade class ID is invalid"}; return false; }
        for (std::size_t stat = 0; stat < UpgradeStatCount; ++stat)
            if (record.fields[stat + 1].empty() || !Integer(record.fields[stat + 1], type.increases[stat]))
            { error = {record.byteOffset, "scenario upgrade class count is invalid"}; return false; }
        for (const char character : record.fields[6]) type.order.push_back(CharacterStat(character));
        // Retain empty/malformed gameplay orders for a diagnostic at player
        // initialization; do not silently substitute the default class.
        candidate.byId[id] = std::move(type);
    }
    output = std::move(candidate);
    return true;
}

bool UpgradeResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (m_busy || !files.load || !files.release)
    { error.detail.message = "scenario upgrade loader is busy or unavailable"; return false; }
    struct Busy { bool& flag; ~Busy() { flag = false; } } busy{m_busy};
    m_busy = true;
    // A new map must not retain a previous map's snapshot after a load error.
    m_tables.reset();
    auto candidate = std::make_shared<UpgradeTables>();
    if (!LoadFile("resource/ZBSVER2/ZBS_neededexp.cso", files, ReadUpgradeLevelResource, candidate->levels, error) ||
        !LoadFile("resource/ZBSVER2/ZBS_class.cso", files, ReadUpgradeClassResource, candidate->classes, error)) return false;
    m_tables = std::move(candidate);
    return true;
}

bool UpgradeResources::Clear()
{
    if (m_busy) return false;
    m_tables.reset();
    return true;
}
}
