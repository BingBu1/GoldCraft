// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_factory.h"
#include "scenario_player_upgrades.h"
#include "../scenario/monster_rewards.hpp"

enum class CSOLMonsterRewardResult { Awarded, Skipped, Cancelled, Failed };

// Mode15's real hit reward consumer. Rules own it for one map and invalidate
// it before player services or monster storage are detached. It calls the
// actual upgrade service and hookable CBasePlayer::AddAccount, not a spy.
class CCSOLMonsterHitRewards final : public std::enable_shared_from_this<CCSOLMonsterHitRewards>
{
public:
    static std::shared_ptr<CCSOLMonsterHitRewards> Create(
        const csol::scenario::DifficultyResources& difficulty,
        std::shared_ptr<CCSOLScenarioPlayers> players, csol::scenario::ConfigError& error);
    void Invalidate() { m_valid = false; }
    // A server-owned item/room state must supply both arguments. In particular
    // the scenario character class and addon19 do not imply moneyExtractor.
    CSOLMonsterRewardResult Credit(CSOLMonsterSpawnCall& call, const EHANDLE& player,
        const EHANDLE& inflictor, float damage, bool moneyExtractor, int roomOption,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
    const csol::scenario::ConfigError& Error() const { return m_error; }
private:
    CSOLMonsterRewardResult Fail(const char* reason);
    csol::scenario::MonsterHitRewardSettings m_settings;
    std::shared_ptr<CCSOLScenarioPlayers> m_players;
    csol::scenario::ConfigError m_error;
    bool m_valid{true}, m_busy{};
};
