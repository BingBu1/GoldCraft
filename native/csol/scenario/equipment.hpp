// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "upgrade_tables.hpp"
#include <optional>
#include <utility>

namespace csol::scenario
{
struct EquipmentBuff
{
    std::int32_t item{}, type{}, priority{};
    bool global{}, wornOnly{}, cumulative{};
    std::int32_t accumulationId{}, accumulationLimit{};
    std::array<float, 6> values{};
};

struct EquipmentEffect
{
    std::int32_t operation{}, type{};
    float value{};
    std::string resourceName;
};

struct EquipmentAddon
{
    std::int32_t item{};
    std::map<std::string, float, std::less<>> parameters;
    float Parameter(std::string_view name) const;
};

struct EquipmentTables
{
    CsvDocument buffSource;
    std::map<std::int32_t, std::vector<EquipmentBuff>> buffs;
    std::map<std::string, std::int32_t, std::less<>> effectTypes;
    std::map<std::int32_t, std::vector<EquipmentEffect>> effects;
    // The original indexes this vector by the activated addon number, not ID.
    std::vector<EquipmentAddon> addons;
    std::int32_t EffectType(std::string_view name) const;
};

bool ReadEquipmentBuffs(std::string_view bytes, EquipmentTables& output, ConfigError& error);
bool ReadEquipmentBuffResource(std::span<const std::byte> encoded, EquipmentTables& output, ConfigError& error);
bool ReadEquipmentSettings(std::string_view json, EquipmentTables& output, ConfigError& error);

class EquipmentResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const EquipmentTables> Snapshot() const { return m_tables; }
private:
    std::shared_ptr<const EquipmentTables> m_tables;
    bool m_busy{};
};

// These are authoritative item IDs, not weapon IDs or character model IDs.
// No network command is allowed to populate this inventory directly.
struct EquipmentLoadout
{
    struct ClassCaps
    {
        std::int32_t classId{};
        std::array<std::int32_t, UpgradeStatCount> values{};
    };
    std::vector<std::int32_t> buffItems;
    std::vector<std::int32_t> carriedItems;
    std::array<std::int32_t, 10> equippedItems{};
    std::int16_t extraItem{};
    std::map<std::size_t, std::int32_t> addonLevels;
    // Original ClassModManager stores these per player. A missing record uses
    // the normal class cap; the character model ID cannot synthesize a record.
    std::vector<ClassCaps> classCaps;
};

// Only the five scenario upgrade capabilities are exposed here. Other buff
// types have separate mode gates and consumers, and are not implicitly enabled.
class EquipmentAbilities
{
public:
    explicit EquipmentAbilities(std::shared_ptr<const EquipmentTables> tables) : m_tables(std::move(tables)) {}
    bool Bind(std::size_t player, EquipmentLoadout loadout);
    void Remove(std::size_t player);
    void Clear();
    bool HasPlayer(std::size_t player) const;
    bool Bonus(std::size_t player, UpgradeStat stat, std::int32_t& output) const;
    bool Effect(std::size_t player, std::string_view name, float& value) const;
    bool IntegerEffect(std::size_t player, std::string_view name, std::int32_t& value) const;
    bool MoneyAddPercent(std::size_t player, std::optional<float>& output) const;
private:
    struct Player
    {
        EquipmentLoadout loadout;
        std::array<const EquipmentBuff*, UpgradeStatCount> bonuses{};
        std::vector<const EquipmentEffect*> effects;
    };
    void RebuildGlobal();
    std::shared_ptr<const EquipmentTables> m_tables;
    std::map<std::size_t, Player> m_players;
    std::array<const EquipmentBuff*, UpgradeStatCount> m_global{};
};
}
