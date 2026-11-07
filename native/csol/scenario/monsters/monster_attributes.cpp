// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_attributes.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>

namespace csol::scenario
{
namespace
{
bool Short(std::string_view value, std::int16_t& output)
{
    output = 0;
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == value.npos) return true;
    value.remove_prefix(first);
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return true;
    }
    std::int32_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec != std::errc{}) return false;
    output = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(parsed));
    return true;
}

std::int32_t Add(std::int32_t value, std::int16_t delta)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) +
        static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
}
}

bool ReadRoundMonsterDeltas(const RoundTable& table, std::size_t index,
    RoundMonsterDeltas& output, ConfigError& error)
{
    error = {};
    if (table.headerRecord != 6 || index >= table.rows.size() ||
        table.rows[index].recordIndex != index + 7 ||
        table.rows[index].recordIndex >= table.source.records.size())
    {
        error.message = "scenario monster attributes have no source round";
        return false;
    }
    const auto& record = table.source.records[table.rows[index].recordIndex];
    RoundMonsterDeltas candidate;
    const std::array fields{&candidate.damage, &candidate.range, &candidate.health,
        &candidate.armor, &candidate.speed, &candidate.interval, &candidate.money};
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const std::string_view value = i + 3 < record.fields.size() ?
            std::string_view(record.fields[i + 3]) : std::string_view{};
        if (!Short(value, *fields[i]))
        {
            error = {record.byteOffset, "scenario monster delta overflows the original int32 getter"};
            return false;
        }
    }
    output = candidate;
    return true;
}

bool ResolveMonsterAttributes(const MonsterType& type, const RoundMonsterDeltas& delta,
    MonsterAttributes& output, ConfigError& error)
{
    error = {};
    if (!std::isfinite(type.attackInterval))
    {
        error.message = "scenario monster attack interval must be finite";
        return false;
    }
    MonsterAttributes candidate;
    candidate.health = static_cast<float>(std::max(10, Add(type.health, delta.health)));
    candidate.armor = static_cast<float>(std::max(0, Add(type.armor, delta.armor)));
    candidate.attackDamage = static_cast<float>(std::max(0, static_cast<int>(type.attackDamage) + delta.damage));
    candidate.attackRange = static_cast<float>(std::max(0, static_cast<int>(type.attackRange) + delta.range));
    candidate.bonusMoney = std::max(0, static_cast<int>(type.bonusMoney) + delta.money);
    candidate.speed = static_cast<float>(std::min(390, static_cast<int>(type.speed) + delta.speed));
    candidate.attackInterval = std::max(0.4f, type.attackInterval);
    output = candidate;
    return true;
}
}
