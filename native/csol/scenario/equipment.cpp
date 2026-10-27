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
