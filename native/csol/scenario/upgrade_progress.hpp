// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "upgrade_tables.hpp"

namespace csol::scenario
{
struct UpgradeAttribute
{
    std::int32_t level{}, cap{}, bonus{};
};

struct UpgradeProgressState
{
    std::array<UpgradeAttribute, UpgradeStatCount> attributes;
    std::int32_t level{}, cap{}, bonus{}, experience{};
    std::size_t cursor{};
};

// Concrete server integration must publish original notices and apply health/
// speed changes. These are synchronous cancellation points, not queued rewards.
class UpgradeEvents
{
public:
    virtual ~UpgradeEvents() = default;
    virtual bool Current() const = 0;
    virtual bool ReadBonus(UpgradeStat stat, std::int32_t& output) = 0;
    virtual bool BonusChanged(UpgradeStat stat, int effectiveLevel, int cap) = 0;
    virtual bool LevelIncreased(UpgradeStat stat, const UpgradeProgressState& state) = 0;
    virtual bool ReachedEffectiveCap(const UpgradeProgressState& state) = 0;
    virtual bool HealAfterUpgrade() = 0;
};

// XP progression is independent of the legacy purchasable wall attack level.
// Initialization takes actual character/effect-adjusted levels and caps; it
// does not guess that a spawn or new round resets every attribute to one.
class UpgradeProgress
{
public:
    bool Initialize(std::shared_ptr<const UpgradeTables> tables, int classId,
        const std::array<UpgradeAttribute, UpgradeStatCount>& attributes,
        std::int32_t experience, ConfigError& error);
    void Clear() { m_state.reset(); }
    // amount already passed through EXP_RATIO and difficulty 0x12, with
    // separate int truncation at each original boundary. Negative modifiers
    // are retained here; the raw XP entry's negative-input gate is earlier.
    bool AddAdjustedExperience(std::int32_t amount, std::shared_ptr<UpgradeEvents> events, ConfigError& error);
    bool RefreshBonuses(std::shared_ptr<UpgradeEvents> events, ConfigError& error);
    const UpgradeProgressState* State() const { return m_state ? &m_state->value : nullptr; }
    bool StatValue(UpgradeStat stat, float& output) const;
private:
    struct StateData
    {
        UpgradeProgressState value;
        std::shared_ptr<const UpgradeTables> tables;
        const UpgradeClass* type{};
        bool busy{};
    };
    using StatePtr = std::shared_ptr<StateData>;
    bool Current(const StatePtr& state, const UpgradeEvents& events) const;
    bool Bonuses(const StatePtr& state, UpgradeEvents& events, ConfigError& error);
    bool NextAttribute(StateData& state, ConfigError& error);
    StatePtr m_state;
};
}
