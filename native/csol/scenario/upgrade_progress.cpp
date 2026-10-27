// SPDX-License-Identifier: GPL-3.0-or-later
#include "upgrade_progress.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace csol::scenario
{
namespace
{
bool Fail(ConfigError& error, const char* message)
{
    if (error.message.empty()) error.message = message;
    return false;
}
bool Int32(std::int64_t value)
{
    return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}
std::int32_t WrapAdd(std::int32_t first, std::int32_t second)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(first) + static_cast<std::uint32_t>(second));
}
int Effective(const UpgradeAttribute& attribute)
{
    return static_cast<int>(std::min(static_cast<std::int64_t>(attribute.level) + attribute.bonus,
        static_cast<std::int64_t>(attribute.cap)));
}
struct Busy
{
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
}

bool UpgradeProgress::Initialize(std::shared_ptr<const UpgradeTables> tables, int classId,
    const std::array<UpgradeAttribute, UpgradeStatCount>& attributes,
    std::int32_t experience, ConfigError& error)
{
    error = {};
    const auto* type = tables ? tables->classes.Find(classId) : nullptr;
    if (!type || type->order.empty()) return Fail(error, "scenario player upgrade class has no progression order");
    std::int64_t level = 1, cap = 1;
    for (const auto& attribute : attributes)
    {
        if (attribute.level < 1 || attribute.cap < 1 ||
            attribute.cap >= static_cast<std::int64_t>(tables->levels.rows.size()) ||
            static_cast<std::int64_t>(attribute.level) + attribute.bonus < 0)
            return Fail(error, "scenario player upgrade attribute exceeds its level table");
        level += attribute.level - 1;
        cap += attribute.cap - 1;
    }
    if (!Int32(level) || !Int32(cap))
        return Fail(error, "scenario player upgrade totals exceed int32");
    for (const auto stat : type->order)
        if (static_cast<std::size_t>(stat) >= UpgradeStatCount)
            return Fail(error, "scenario player upgrade order has an invalid attribute");
    auto state = std::make_shared<StateData>();
    state->tables = std::move(tables);
    state->type = type;
    // Initialize retains per-stat bonuses but clears the aggregate until the
    // next bonus refresh, exactly like the original character-change path.
    state->value = {attributes, static_cast<int>(level), static_cast<int>(cap), 0, experience, 0};
    m_state = std::move(state);
    return true;
}

bool UpgradeProgress::Current(const StatePtr& state, const UpgradeEvents& events) const
{
    return state && m_state == state && events.Current() && m_state == state;
}

bool UpgradeProgress::StatValue(UpgradeStat stat, float& output) const
{
    const auto index = static_cast<std::size_t>(stat);
    return m_state && index < UpgradeStatCount &&
        m_state->tables->levels.StatValue(stat, Effective(m_state->value.attributes[index]), output);
}

bool UpgradeProgress::NextAttribute(StateData& state, ConfigError& error)
{
    const auto& order = state.type->order;
    for (std::size_t count = 0; count < order.size(); ++count)
    {
        state.value.cursor = (state.value.cursor + 1) % order.size();
        const auto& attribute = state.value.attributes[static_cast<std::size_t>(order[state.value.cursor])];
        // Original next-stat uses base level, even when bonuses fill a stat.
        if (attribute.level < attribute.cap) return true;
    }
    return Fail(error, "scenario upgrade order has no remaining attribute below its cap");
}

bool UpgradeProgress::Bonuses(const StatePtr& state, UpgradeEvents& events, ConfigError& error)
{
    auto& value = state->value;
    value.bonus = 0;
    constexpr UpgradeStat order[]{UpgradeStat::Health, UpgradeStat::Damage, UpgradeStat::Armor,
        UpgradeStat::Speed, UpgradeStat::Ammo};
    for (const auto stat : order)
    {
        // The original queries each ability after the previous attribute's
        // health/speed callback. A single five-value snapshot changes that
        // observable order when a callback replaces character effects.
        std::int32_t bonus{};
        const bool read = events.ReadBonus(stat, bonus);
        if (!Current(state, events)) return false;
        if (!read) return Fail(error, "scenario character upgrade bonus is unavailable");
        const auto index = static_cast<std::size_t>(stat);
        auto& attribute = value.attributes[index];
        const int previous = Effective(attribute);
        const auto effective = std::min(static_cast<std::int64_t>(attribute.level) + bonus,
            static_cast<std::int64_t>(attribute.cap));
        if (effective < 0) return Fail(error, "scenario character bonus would address a negative level");
        const auto added = std::min(static_cast<std::int64_t>(bonus),
            static_cast<std::int64_t>(attribute.cap) - attribute.level);
        const auto total = static_cast<std::int64_t>(value.bonus) + added;
        if (!Int32(total) || !Int32(total + value.level))
            return Fail(error, "scenario character bonus total exceeds int32");
        attribute.bonus = bonus;
        value.bonus = static_cast<int>(total);
        // SetBonus only eagerly applies health and movement. Other attributes
        // are consumed from their current levels by damage/armor/ammo rules.
        if (effective != previous && (stat == UpgradeStat::Health || stat == UpgradeStat::Speed))
        {
            const bool changed = events.BonusChanged(stat, static_cast<int>(effective), attribute.cap);
            if (!Current(state, events)) return false;
            if (!changed) return Fail(error, "scenario character bonus could not update the player");
        }
    }
    // Clamp only the upper total. Negative bonuses are retained.
    value.bonus = std::min(value.bonus, value.cap - value.level);
    return true;
}

bool UpgradeProgress::RefreshBonuses(std::shared_ptr<UpgradeEvents> events, ConfigError& error)
{
    error = {};
    auto state = m_state;
    if (!events || !Current(state, *events) || state->busy) return false;
    Busy busy(state->busy);
    return Bonuses(state, *events, error);
}

bool UpgradeProgress::AddAdjustedExperience(std::int32_t amount,
    std::shared_ptr<UpgradeEvents> events, ConfigError& error)
{
    error = {};
    auto state = m_state;
    if (!events || !Current(state, *events) || state->busy) return false;
    Busy busy(state->busy);
    auto& value = state->value;
    if (value.level >= value.cap)
    { value.level = value.cap; value.experience = 0; return true; }
    value.experience = WrapAdd(value.experience, amount);
    if (!Bonuses(state, *events, error)) return false;
    while (true)
    {
        const auto* record = state->tables->levels.ExperienceLevel(value.level + value.bonus);
        if (!record || record->experience <= 0)
            return Fail(error, "scenario upgrade experience threshold is missing or nonpositive");
        const auto threshold = record->experience;
        if (value.experience < threshold) break;
        if (value.level >= value.cap)
        { value.level = value.cap; value.experience = 0; return true; }
        auto stat = state->type->order[value.cursor];
        const auto& pending = value.attributes[static_cast<std::size_t>(stat)];
        if (static_cast<std::int64_t>(pending.level) + pending.bonus >= pending.cap)
        {
            if (!NextAttribute(*state, error)) return false;
            stat = state->type->order[value.cursor];
        }
        ++value.attributes[static_cast<std::size_t>(stat)].level;
        ++value.level;
        if (!Bonuses(state, *events, error)) return false;
        const bool applied = events->LevelIncreased(stat, value);
        if (!Current(state, *events)) return false;
        if (!applied) return Fail(error, "scenario level increase could not update the player");
        if (value.level + value.bonus >= value.cap)
        {
            const bool reached = events->ReachedEffectiveCap(value);
            if (!Current(state, *events)) return false;
            if (!reached) return Fail(error, "scenario full upgrade event failed");
            if (value.bonus > 0 && !NextAttribute(*state, error)) return false;
        }
        else
        {
            if (!NextAttribute(*state, error)) return false;
            const bool healed = events->HealAfterUpgrade();
            if (!Current(state, *events)) return false;
            if (!healed) return Fail(error, "scenario level healing failed");
        }
        value.experience = WrapAdd(value.experience, -threshold);
    }
    if (value.level >= value.cap) value.experience = 0;
    return true;
}
}
