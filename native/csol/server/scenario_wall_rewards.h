// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_breakable.h"
#include "../scenario/wall_damage.hpp"

// Owned by the server's player/upgrade state, and invalidated on disconnect
// and round reset before an edict can be reused. This legacy attack level is
// separate from the five XP-driven attributes in ZBSVER2.
struct CSOLScenarioWallPlayer
{
    EHANDLE player;
    std::uint64_t generation{};
    std::uint8_t attackLevel{}, characterFeature{};
    std::int16_t accumulatedDamage{};
    bool elevenLevelLimit{}, crediting{};
};

class CCSOLScenarioWallPlayers
{
public:
    virtual ~CCSOLScenarioWallPlayers() = default;
    virtual std::shared_ptr<CSOLScenarioWallPlayer> Find(const EHANDLE& player) = 0;
    // Actual character component 12 / rules v250, after difficulty scaling.
    virtual bool ModifyMoney(const EHANDLE& player, int& amount) = 0;
    // Actual ZBSVER2 XP entry: EXP_RATIO, difficulty 0x12, repeated upgrades,
    // health/speed and client notices. A successful no-op is not a provider.
    virtual bool AddExperience(const EHANDLE& player, int amount) = 0;
};

// Executes the original wall award order and writes the real CS account.
// The rules must bind the actual player/upgrade provider before use.
class CCSOLScenarioWallRewards final : public CCSOLScenarioWallRules
{
public:
    static std::shared_ptr<CCSOLScenarioWallRewards> Create(
        const csol::scenario::RoundTable& rounds,
        const csol::scenario::DifficultyResources& difficulty,
        std::shared_ptr<CCSOLScenarioWallPlayers> players, csol::scenario::ConfigError& error);
    bool PlayerDamage(const EHANDLE& player, float input, float ratio, float& output) override;
    bool MonsterDamage(float input, float ratio, float& output) override;
    bool CreditPlayerDamage(const EHANDLE& player, float scaledDamage) override;
    void SetMonsterLevel(std::int8_t level) { m_monsterLevel = level; }
    const csol::scenario::ConfigError& Error() const { return m_error; }
private:
    bool Fail(const char* reason);
    csol::scenario::WallRewardSettings m_settings;
    std::shared_ptr<CCSOLScenarioWallPlayers> m_players;
    csol::scenario::ConfigError m_error;
    std::int8_t m_monsterLevel{1}; // Original manager constructor.
};
