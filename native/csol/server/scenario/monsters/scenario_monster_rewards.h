// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_factory.h"
#include "../core/scenario_player_upgrades.h"
#include "../../../scenario/monsters/monster_rewards.hpp"
#include "../../../scenario/custom/refund_cap.hpp"

enum class CSOLMonsterRewardResult { Awarded, Skipped, Cancelled, Failed };

// Mode15's real hit reward consumer. Rules own it for one map and invalidate
// it before player services or monster storage are detached. It calls the
// actual upgrade service and hookable CBasePlayer::AddAccount, not a spy.
class CCSOLMonsterHitRewards final : public std::enable_shared_from_this<CCSOLMonsterHitRewards>
{
public:
    static std::shared_ptr<CCSOLMonsterHitRewards> Create(
        const csol::scenario::DifficultyResources& difficulty,
        std::shared_ptr<CCSOLScenarioPlayers> players, csol::scenario::ConfigError& error,
        std::shared_ptr<csol::scenario::custom::RefundCap> refunds = {});
    void Invalidate() { m_valid = false; }
    // A server-owned item/room state must supply both arguments. In particular
    // the scenario character class and addon19 do not imply moneyExtractor.
    CSOLMonsterRewardResult Credit(CSOLMonsterSpawnCall& call, const EHANDLE& player,
        const EHANDLE& inflictor, float damage, bool moneyExtractor, int roomOption,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr,
        csol::scenario::custom::RefundPlayer refundPlayer = {});
    // lightzb_bomb TakeDamage 109885A0 applies the same mode15 attacker and
    // "grenade" gates and draw/XP/AddAccount order to a map-owned non-monster
    // target. draw computes both values from Settings() with one RANDOM_FLOAT;
    // false is a failure, never a skipped reward.
    using EntityHitDraw = bool (*)(const csol::scenario::MonsterHitRewardSettings& settings,
        void* context, std::int32_t& experience, std::int32_t& money);
    CSOLMonsterRewardResult CreditEntityHit(const EHANDLE& player, const EHANDLE& inflictor,
        EntityHitDraw draw, void* drawContext, bool (*ownerValid)(const void*), const void* ownerContext);
    const csol::scenario::ConfigError& Error() const { return m_error; }
private:
    CSOLMonsterRewardResult Award(bool (*isActive)(const void*), const void* activeContext,
        const EHANDLE& player, const EHANDLE& inflictor, EntityHitDraw draw, void* drawContext,
        const csol::scenario::custom::RefundPair* refundPair = nullptr);
    CSOLMonsterRewardResult Fail(const char* reason);
    csol::scenario::MonsterHitRewardSettings m_settings;
    std::shared_ptr<CCSOLScenarioPlayers> m_players;
    std::shared_ptr<csol::scenario::custom::RefundCap> m_refunds;
    csol::scenario::ConfigError m_error;
    bool m_valid{true}, m_busy{};
};
