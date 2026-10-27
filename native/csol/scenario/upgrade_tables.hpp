// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "map_resources.hpp"
#include <array>
#include <memory>

namespace csol::scenario
{
enum class UpgradeStat : std::uint8_t { Health, Damage, Speed, Armor, Ammo };
constexpr std::size_t UpgradeStatCount = 5;

struct UpgradeLevel
{
    std::int32_t experience{};
    std::array<float, UpgradeStatCount> values{};
};

struct UpgradeLevels
{
    CsvDocument source;
    // Append physical rows after the header. Column zero is not an index.
    std::vector<UpgradeLevel> rows;
    const UpgradeLevel* ExperienceLevel(int level) const;
    bool StatValue(UpgradeStat stat, int level, float& output) const;
};

struct UpgradeClass
{
    std::array<std::int32_t, UpgradeStatCount> increases{};
    std::vector<UpgradeStat> order;
};

struct UpgradeClasses
{
    CsvDocument source;
    // Original default key 0 precedes the file; later equal IDs replace it.
    std::map<std::int32_t, UpgradeClass> byId;
    const UpgradeClass* Find(std::int32_t id) const;
};

bool ReadUpgradeLevels(std::string_view bytes, UpgradeLevels& output, ConfigError& error);
bool ReadUpgradeClasses(std::string_view bytes, UpgradeClasses& output, ConfigError& error);
bool ReadUpgradeLevelResource(std::span<const std::byte> encoded, UpgradeLevels& output, ConfigError& error);
bool ReadUpgradeClassResource(std::span<const std::byte> encoded, UpgradeClasses& output, ConfigError& error);

struct UpgradeTables
{
    UpgradeLevels levels;
    UpgradeClasses classes;
};

// One shared snapshot for a map. In-flight player callbacks can retain their
// old tables until their generation check cancels them after map shutdown.
class UpgradeResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const UpgradeTables> Snapshot() const { return m_tables; }
private:
    std::shared_ptr<const UpgradeTables> m_tables;
    bool m_busy{};
};
}
