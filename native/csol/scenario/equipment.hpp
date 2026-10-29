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
        // Same original 0xCC record: five caps followed by nine groups of five
        // item slots. Unsent slots retain -1; an incoming u16 stays unsigned.
        std::array<std::int32_t, 45> slots = []
        { std::array<std::int32_t, 45> result; result.fill(-1); return result; }();
    };
    std::vector<std::int32_t> buffItems;
    std::vector<std::int32_t> carriedItems;
    std::array<std::int32_t, 10> equippedItems{};
    std::int16_t extraItem{};
    std::map<std::size_t, std::int32_t> addonLevels;
    // Original ClassModManager stores these per player. A missing record uses
    // the normal class cap; the character model ID cannot synthesize a record.
    std::vector<ClassCaps> classCaps;
    // Explicit trusted import boundary. An empty default profile is unknown,
    // not evidence that the player's current ClassMod list is empty.
    bool classModsComplete{};
};

struct ClassModDefinition
{
    std::int32_t item{}, category{};
    std::int16_t type{}, subtype{};
    std::array<float, 7> values{};
};

struct ClassModTable
{
    CsvDocument source;
    std::map<std::int32_t, ClassModDefinition> definitions;
};

struct ClassModQuery
{
    bool (*current)(const void*){}; // Pure lifetime check.
    const void* owner{};
    bool (*mode)(const void*, int&){};
    const void* modeOwner{};
};

struct ClassModCurrent
{
    std::shared_ptr<const ClassModTable> table;
    std::vector<const ClassModDefinition*> records;
    std::int32_t classId{};
    bool complete{}, building{};
};

bool ReadClassMods(std::string_view bytes, ClassModTable& output, ConfigError& error);
bool ClassModEligible(const ClassModDefinition& definition, const ClassModQuery& query,
    bool& eligible, ConfigError& error);
// Clears before building, preserving the ordered prefix on a later failure.
// identity0 leaves the old state unchanged. Input records must remain alive
// through the call; the host cancels if its authoritative import is replaced.
bool RebuildClassMods(std::uint32_t identity, std::int32_t classId,
    std::span<const EquipmentLoadout::ClassCaps> records, std::shared_ptr<const ClassModTable> table,
    ClassModCurrent& current, const ClassModQuery& query, ConfigError& error);
// v54 rechecks mode for each matching type; v50's exact-pair query does not.
bool FindClassModType(const ClassModCurrent& current, int type, const ClassModQuery& query,
    const ClassModDefinition*& result, ConfigError& error);
bool FindClassModPair(const ClassModCurrent& current, int type, int subtype,
    const ClassModDefinition*& result, ConfigError& error);

class ClassModResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const ClassModTable> Snapshot() const { return m_table; }
private:
    std::shared_ptr<const ClassModTable> m_table;
    bool m_busy{};
};

// Original manager+4C contains defaults by classId. This is separate from
// the player's imported records and never marks a loadout complete by itself.
struct ClassModPresetTable
{
    CsvDocument source;
    std::array<std::int32_t, 9> categorySlots{};
    std::map<std::int32_t, EquipmentLoadout::ClassCaps> records;
};

bool ReadClassModCategorySlots(std::string_view json,
    std::array<std::int32_t, 9>& output, ConfigError& error);
bool ReadClassModPresets(std::string_view bytes, const std::array<std::int32_t, 9>& categorySlots,
    const CsvDocument& items, ClassModPresetTable& output, ResourceError& error);

class ClassModPresetResources
{
public:
    // Reuse the item catalog already loaded by the map's weapon owner.
    // A null snapshot is unknown, unlike a valid catalog with a missing item.
    // Pure lifetime check, also called after each file load/release callback.
    // Cancellation returns false with no resource error and publishes nothing.
    bool Load(ResourceFileApi files, std::shared_ptr<const CsvDocument> items,
        bool (*current)(const void*), const void* owner, ResourceError& error);
    bool Clear();
    std::shared_ptr<const ClassModPresetTable> Snapshot() const { return m_table; }
private:
    std::shared_ptr<const ClassModPresetTable> m_table;
    bool m_busy{};
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
