// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_player_runtime.h"
#include "scenario_death_owner.h"
#include <map>

// The original event 0x22 carries an opaque reward ID. A local request is not
// an inventory grant: an explicit account/reward consumer must acknowledge it.
struct CSOLScenarioDropReward
{
    enum class Status { AwaitingHandler, Delivered, Rejected };
    std::uint64_t sequence{}, playerGeneration{};
    EHANDLE player;
    std::int32_t rewardId{};
    Status status{Status::AwaitingHandler};
};

class CCSOLScenarioDropItem;

class CCSOLScenarioDrops final : public std::enable_shared_from_this<CCSOLScenarioDrops>
{
public:
    CCSOLScenarioDrops(std::shared_ptr<CCSOLScenarioSession> session,
        std::shared_ptr<CCSOLScenarioPlayerRuntime> players)
        : m_session(std::move(session)), m_players(std::move(players)) {}
    static void Precache();
    bool Activate();
    bool Active() const;
    // Counts are the rules' cached CT snapshot, not freshly scanned counts.
    bool MonsterKilled(CSOLMonsterDeathCall& call, int aliveCt, int totalCt,
        csol::scenario::ConfigError& error);
    // Called while player identity is still valid, before map/round teardown.
    // Removing an unclaimed box follows the same event path as the original.
    void RemoveItems();
    void Deactivate();
    std::span<const CSOLScenarioDropReward> Rewards() const { return m_rewards; }
    std::size_t PendingRewardCount() const { return m_pendingRewards.size(); }
    bool Acknowledge(std::uint64_t sequence, std::uint64_t playerGeneration,
        CSOLScenarioDropReward::Status status);

private:
    friend class CCSOLScenarioDropItem;
    CBasePlayer* Recipient(const EHANDLE& player, std::uint64_t generation) const;
    // Register while the box is configured. Only an issued ticket can submit
    // after shutdown; this does not resolve a disconnected/recycled player.
    std::uint64_t RegisterReward(const EHANDLE& player, std::uint64_t generation, std::int32_t reward);
    bool SubmitReward(std::uint64_t ticket);
    void DiscardReward(std::uint64_t ticket);
    bool SameMap() const;
    bool Position(CSOLMonsterDeathCall& call, const Vector& source, const EHANDLE& recipient,
        std::size_t created, Vector& result);
    bool SpawnItem(CSOLMonsterDeathCall& call, const csol::scenario::DropItemRule& rule,
        const EHANDLE& recipient, std::size_t& created);

    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> m_players;
    std::vector<EHANDLE> m_items;
    std::vector<CSOLScenarioDropReward> m_rewards;
    std::map<std::uint64_t, CSOLScenarioDropReward> m_pendingRewards;
    std::uint64_t m_sequence{}, m_mapGeneration{};
    bool m_active{}, m_removing{}, m_closing{};
};

// ReGameDLL's UpdateOnRemove is nonvirtual. UTIL_Remove calls this narrow
// dispatch before FL_KILLME; raw private-data release also enters OnDestroy.
bool CSOL_ScenarioItemRemoving(CBaseEntity* entity);
