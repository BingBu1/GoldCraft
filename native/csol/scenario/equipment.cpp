// SPDX-License-Identifier: GPL-3.0-or-later
#include "equipment.hpp"

#include <algorithm>

namespace csol::scenario
{
namespace
{
constexpr std::array<int, UpgradeStatCount> BonusTypes{4, 3, 8, 6, 13};

void Select(const EquipmentBuff*& selected, const EquipmentBuff& candidate)
{
    if (!selected || candidate.priority > selected->priority ||
        (candidate.priority == selected->priority && candidate.item > selected->item)) selected = &candidate;
}
}

bool ClassModEligible(const ClassModDefinition& definition, const ClassModQuery& query,
    bool& eligible, ConfigError& error)
{
    eligible = false;
    const auto mode = [&](int& value)
    {
        if (!query.current || !query.current(query.owner)) return false;
        if (!query.mode || !query.mode(query.modeOwner, value))
        { if (query.current(query.owner)) error.message = "class mod mode query is unavailable"; return false; }
        return query.current(query.owner);
    };
    int value{};
    if (!mode(value)) return false;
    if (value == -2 && !mode(value)) return false;
    if (value == 58 || value == 60) return true;
    switch (definition.type)
    {
    case 2:
        if (!mode(value)) return false;
        eligible = value != 0 && value != 6 && value != 7 && value != 42;
        return true;
    case 5:
        if (!mode(value)) return false;
        eligible = value == 14 || value == 54 || value == 44 || value == 24 || value == 45 ||
            value == 41 || value == 55 || value == 60 || value == 56;
        return true;
    case 8: case 10:
        if (!mode(value)) return false;
        if (value == 15) { eligible = true; return true; }
        if (!mode(value)) return false;
        eligible = value == 48;
        return true;
    case 9:
        if (!mode(value)) return false;
        eligible = value == 15;
        return true;
    default:
        // Includes original signed types outside2..10, including the real
        // directory's1001..1030 entries. It is an explicit original branch.
        eligible = true;
        return true;
    }
}

namespace
{
bool FindCurrentType(const ClassModCurrent& current, int type, const ClassModQuery& query,
    const ClassModDefinition*& result, ConfigError& error)
{
    result = nullptr;
    if (!type) return true;
    for (const auto* definition : current.records)
    {
        if (!query.current || !query.current(query.owner)) return false;
        if (!definition) { error.message = "class mod current list contains a missing definition"; return false; }
        if (definition->type != type) continue;
        bool eligible{};
        if (!ClassModEligible(*definition, query, eligible, error)) return false;
        if (eligible) { result = definition; return true; }
    }
    return true;
}
}

bool FindClassModType(const ClassModCurrent& current, int type, const ClassModQuery& query,
    const ClassModDefinition*& result, ConfigError& error)
{
    error = {};
    result = nullptr;
    if (!current.complete || current.building || !current.table)
    { error.message = "class mod current list has not been rebuilt from complete records"; return false; }
    if (!query.current || !query.current(query.owner)) return false;
    return FindCurrentType(current, type, query, result, error);
}

bool FindClassModPair(const ClassModCurrent& current, int type, int subtype,
    const ClassModDefinition*& result, ConfigError& error)
{
    error = {};
    result = nullptr;
    if (!current.complete || current.building || !current.table)
    { error.message = "class mod current list has not been rebuilt from complete records"; return false; }
    // Original v50 compares the two low16 halves as one packed DWORD.
    if (!type) return true;
    for (const auto* definition : current.records)
    {
        if (!definition) { error.message = "class mod current list contains a missing definition"; return false; }
        if (static_cast<std::uint16_t>(definition->type) == static_cast<std::uint16_t>(type) &&
            static_cast<std::uint16_t>(definition->subtype) == static_cast<std::uint16_t>(subtype))
        { result = definition; break; }
    }
    return true;
}

bool RebuildClassMods(std::uint32_t identity, std::int32_t classId,
    std::span<const EquipmentLoadout::ClassCaps> records, std::shared_ptr<const ClassModTable> table,
    ClassModCurrent& current, const ClassModQuery& query, ConfigError& error)
{
    if (!identity) return true;
    error = {};
    if (!query.current || !query.current(query.owner)) return false;
    if (current.building) { error.message = "class mod list rebuild is already active"; return false; }
    current.building = true;
    struct Scope { bool& building; ~Scope() { building = false; } } scope{current.building};
    current.complete = false;
    current.records.clear();
    current.table = table;
    current.classId = classId;
    if (!table) { error.message = "class mod definition directory is unavailable"; return false; }
    for (const auto& record : records)
    {
        if (record.classId != classId) continue;
        for (const int item : record.slots)
        {
            if (item == -1 || item == 0) continue;
            const auto found = table->definitions.find(item);
            if (found == table->definitions.end()) continue;
            bool eligible{};
            if (!ClassModEligible(found->second, query, eligible, error)) return false;
            if (eligible) current.records.push_back(&found->second);
        }
    }
    const ClassModDefinition* combined{};
    if (!FindCurrentType(current, 4, query, combined, error)) return false;
    if (combined)
    {
        for (const int type : {2, 3})
        {
            const ClassModDefinition* definition{};
            for (const auto& [item, candidate] : table->definitions)
            {
                if (candidate.type != type || candidate.subtype != 0) continue;
                // The original uses its unordered directory iteration order.
                // Real defaults are unique. Reject ambiguous custom resources
                // instead of inventing a winner by itemID or CSV row order.
                if (definition)
                { error.message = "class mod display default is ambiguous"; return false; }
                definition = &candidate;
            }
            current.records.push_back(definition); // Preserve original null append.
        }
        if (std::find(current.records.begin(), current.records.end(), nullptr) != current.records.end())
        { error.message = "class mod combined display default is missing"; return false; }
    }
    if (!query.current(query.owner)) return false;
    current.complete = true;
    return true;
}

namespace
{
std::int32_t Truncate(float value)
{
    return static_cast<double>(value) >= -2147483648.0 && static_cast<double>(value) < 2147483648.0 ?
        static_cast<std::int32_t>(value) : INT32_MIN;
}
}

float EquipmentAddon::Parameter(std::string_view name) const
{
    const auto found = parameters.find(name);
    return found == parameters.end() ? 0.0f : found->second;
}

std::int32_t EquipmentTables::EffectType(std::string_view name) const
{
    // Original unordered_map::operator[] inserts an unknown name with value 0.
    // Immutable resource snapshots provide the same comparison without mutation.
    const auto found = effectTypes.find(name);
    return found == effectTypes.end() ? 0 : found->second;
}

bool EquipmentAbilities::Bind(std::size_t player, EquipmentLoadout loadout)
{
    if (!m_tables || !player || player > 32) return false;
    Player candidate;
    candidate.loadout = std::move(loadout);
    for (const auto item : candidate.loadout.buffItems)
    {
        const auto found = m_tables->buffs.find(item);
        if (found == m_tables->buffs.end()) continue;
        for (const auto& buff : found->second)
            for (std::size_t stat = 0; stat < BonusTypes.size(); ++stat)
                if (buff.type == BonusTypes[stat]) Select(candidate.bonuses[stat], buff);
    }
    const auto appendEffects = [&](std::int32_t item)
    {
        const auto found = m_tables->effects.find(item);
        if (found != m_tables->effects.end())
            for (const auto& effect : found->second) candidate.effects.push_back(&effect);
    };
    for (const auto item : candidate.loadout.carriedItems) appendEffects(item);
    for (const auto item : candidate.loadout.equippedItems) appendEffects(item);
    appendEffects(candidate.loadout.extraItem);
    m_players.insert_or_assign(player, std::move(candidate));
    RebuildGlobal();
    return true;
}

void EquipmentAbilities::RebuildGlobal()
{
    m_global.fill(nullptr);
    // Rebuild only when authoritative equipment changes, never per frame.
    for (const auto& [index, player] : m_players)
        for (const auto item : player.loadout.buffItems)
        {
            const auto found = m_tables->buffs.find(item);
            if (found == m_tables->buffs.end()) continue;
            for (const auto& buff : found->second)
                if (buff.global)
                    for (std::size_t stat = 0; stat < BonusTypes.size(); ++stat)
                        if (buff.type == BonusTypes[stat]) Select(m_global[stat], buff);
        }
}

void EquipmentAbilities::Remove(std::size_t player)
{
    if (m_players.erase(player)) RebuildGlobal();
}

void EquipmentAbilities::Clear()
{
    m_players.clear();
    m_global.fill(nullptr);
}

bool EquipmentAbilities::HasPlayer(std::size_t player) const { return m_players.contains(player); }

bool EquipmentAbilities::Bonus(std::size_t player, UpgradeStat stat, std::int32_t& output) const
{
    const auto found = m_players.find(player);
    const auto index = static_cast<std::size_t>(stat);
    if (found == m_players.end() || index >= BonusTypes.size()) return false;
    const auto* selected = m_global[index];
    if (const auto* local = found->second.bonuses[index]) Select(selected, *local);
    output = selected ? Truncate(selected->values[0]) : 0;
    return true;
}

bool EquipmentAbilities::Effect(std::size_t player, std::string_view name, float& value) const
{
    const auto found = m_players.find(player);
    if (found == m_players.end()) return false;
    const auto type = m_tables->EffectType(name);
    for (const auto* effect : found->second.effects)
    {
        if (effect->type != type) continue;
        switch (effect->operation)
        {
        case 1: value = effect->value * value; break;
        // MAXSS selects its second operand for equal values, signed zero or NaN.
        case 2: value = value > effect->value ? value : effect->value; break;
        case 3: value = effect->value + value; break;
        default: break;
        }
    }
    return true;
}

bool EquipmentAbilities::IntegerEffect(std::size_t player, std::string_view name, std::int32_t& value) const
{
    float candidate = static_cast<float>(value);
    if (!Effect(player, name, candidate)) return false;
    value = Truncate(candidate);
    return true;
}

bool EquipmentAbilities::MoneyAddPercent(std::size_t player, std::optional<float>& output) const
{
    const auto found = m_players.find(player);
    if (found == m_players.end()) return false;
    const auto level = found->second.loadout.addonLevels.find(19);
    output.reset();
    if (level == found->second.loadout.addonLevels.end() || level->second <= 0) return true;
    if (m_tables->addons.size() <= 19) { output = 0; return true; }
    const auto& addon = m_tables->addons[19];
    float percent = addon.Parameter("AddPercent");
    if (level->second > 1)
    {
        const float increase = static_cast<float>(level->second - 1) * addon.Parameter("AddPercentAdd");
        percent = percent + increase;
    }
    output = percent;
    return true;
}
}
