// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_messages.h"
#include "scenario_wall_rewards.h"
#include "../scenario/upgrade_progress.hpp"
#include <array>
#include <optional>

// The character/effect subsystem owns these original consumers. Each operation
// must report missing data; an absent implementation is not a successful reward.
class CCSOLScenarioUpgradeServices
{
public:
    virtual ~CCSOLScenarioUpgradeServices() = default;
    virtual bool Current() const = 0;
    // Apply actual character cap overrides and INITIAL_*/MAX_* effects to the
    // retained attributes. XP is intentionally not part of this initialization.
    virtual bool Initialize(const EHANDLE& player, int classId,
        std::array<csol::scenario::UpgradeAttribute, 5>& attributes) = 0;
    virtual bool ReadBonus(const EHANDLE& player, csol::scenario::UpgradeStat stat, int& value) = 0;
    // Ordered float effect evaluation followed by one cvttss2si, then the
    // caller applies difficulty 0x12 with its own float/int conversion.
    virtual bool ModifyExperience(const EHANDLE& player, int& amount) = 0;
    // Component 12 / addon 0x13 AddPercent. Absence preserves the input integer
    // without a float round trip; it is distinct from an active zero percent.
    virtual bool MoneyAddPercent(const EHANDLE& player, std::optional<float>& percent) = 0;
    virtual bool ModifyJump(const EHANDLE& player, float& gravity) = 0;
    // Original one-time mark 3 and service event 0x26, not a heal operation.
    virtual bool ReachedEffectiveCap(const EHANDLE& player) = 0;
};

class CCSOLScenarioPlayerUpgrades final : public csol::scenario::UpgradeEvents,
    public std::enable_shared_from_this<CCSOLScenarioPlayerUpgrades>
{
public:
    static std::shared_ptr<CCSOLScenarioPlayerUpgrades> Create(CBasePlayer& player,
        std::shared_ptr<const csol::scenario::UpgradeTables> tables,
        std::shared_ptr<CCSOLScenarioUpgradeServices> services,
        CCSOLScenarioMessages messages, float experienceRatio, std::uint64_t generation);
    bool Configure(int classId, std::uint8_t feature, bool elevenLevelLimit);
    void Invalidate();
    bool Publish();
    bool RefreshBonuses();
    bool AddExperience(int amount);
    bool ModifyMoney(int& amount);
    bool ApplyMovement(float weaponSpeed);
    // Called from the real Spawn equipment stage after native health=100.
    bool ApplySpawnHealth();
    bool StatValue(csol::scenario::UpgradeStat stat, float& value) const;
    std::shared_ptr<CSOLScenarioWallPlayer> WallState() const { return m_wall; }
    const csol::scenario::ConfigError& Error() const { return m_error; }
    const csol::scenario::UpgradeProgressState* State() const { return m_progress.State(); }
    // A failed/reentrant reconfiguration must not count the previous class as
    // a valid record for the new character. Class zero is valid but uncounted.
    int ConfiguredClass() const { return m_progress.State() ? m_classId : -1; }

    bool Current() const override;
    bool ReadBonus(csol::scenario::UpgradeStat stat, std::int32_t& output) override;
    bool BonusChanged(csol::scenario::UpgradeStat stat, int level, int cap) override;
    bool LevelIncreased(csol::scenario::UpgradeStat stat, const csol::scenario::UpgradeProgressState& state) override;
    bool ReachedEffectiveCap(const csol::scenario::UpgradeProgressState&) override;
    bool HealAfterUpgrade() override;
private:
    bool PublishCurrent();
    bool Fail(const char* reason);
    CBasePlayer* Player() const;
    bool ApplyHealth();
    bool ResetSpeed();
    bool SendStat(csol::scenario::UpgradeStat stat);
    std::shared_ptr<CSOLScenarioWallPlayer> m_wall;
    std::shared_ptr<const csol::scenario::UpgradeTables> m_tables;
    std::shared_ptr<CCSOLScenarioUpgradeServices> m_services;
    CCSOLScenarioMessages m_messages;
    csol::scenario::UpgradeProgress m_progress;
    csol::scenario::ConfigError m_error;
    float m_experienceRatio{};
    int m_classId{-1};
    bool m_valid{true}, m_busy{};
};

// Rules own one registry per map. Disconnect/replace invalidates the previous
// player before an edict is reused. Round changes do not implicitly erase XP
// or the legacy accumulated wall damage.
class CCSOLScenarioPlayers final : public CCSOLScenarioWallPlayers
{
public:
    ~CCSOLScenarioPlayers() override { Clear(); }
    bool Bind(std::shared_ptr<CCSOLScenarioPlayerUpgrades> player);
    void Remove(const EHANDLE& player);
    void Clear();
    std::shared_ptr<CSOLScenarioWallPlayer> Find(const EHANDLE& player) override;
    bool ModifyMoney(const EHANDLE& player, int& amount) override;
    bool AddExperience(const EHANDLE& player, int amount) override;
    std::shared_ptr<CCSOLScenarioPlayerUpgrades> Upgrades(const EHANDLE& player) const;
    bool CountDamageScalePlayers(int& count) const;
private:
    std::array<std::shared_ptr<CCSOLScenarioPlayerUpgrades>, 33> m_players;
};
