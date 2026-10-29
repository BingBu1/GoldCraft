// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_point_manager.h"
#include "scenario_trigger_manager.h"
#include "scenario_player_scan.h"
#include "scenario_entity_access.h"
#include "../scenario/map_resources.hpp"
#include "../scenario/death_rules.hpp"
#include "../scenario/trigger_round.hpp"

#include <array>
#include <memory>

class CHalfLifeMultiplay;

enum class CSOLScenarioMapState { Empty, ResourcesLoaded, EntitiesCollected, RoundActive, Failed };

struct CSOLScenarioFrame
{
    bool valid{};
    csol::scenario::ScheduleStep schedule{csol::scenario::ScheduleStep::Waiting};
    csol::scenario::WaveTickResult waves{};
    csol::scenario::TriggerResult triggers{};
};

struct CSOLActiveWave
{
    std::uint16_t id{};
    float offset{};
};

// Owns the engine-backed resources and snapshots for one subtype 1 map. The
// owning GameRules must call CollectEntities after ServerActivate's classname
// registration, and BeginRound before respawning players. This is not a mode
// selector and must not consume ordinary CS/ZP map markers automatically.
class CCSOLScenarioMap
{
public:
    bool BeginMap(std::string_view mapName);
    bool EndMap();
    // ServerDeactivate is a notification, not a veto of engine destruction.
    // Revoke immediately even while EndMap cannot clear an active callback.
    void RevokeEntityAccess() { m_entityAccess->Revoke(); }
    bool EntityAccessAllowed() const { return m_entityAccess->Allowed(); }
    bool Busy() const;
    std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess() const { return m_entityAccess; }
    bool CollectEntities();
    bool ApplyDifficulty(const csol::scenario::DifficultyResources& difficulty);
    bool BeginRound(std::size_t roundIndex, float now, float freezeTime,
        csol::scenario::TriggerWaveEvents& events);
    // Called from scenario GameRules after its continuation timers. Read actual
    // rule gates, but use its separately maintained alive-CT snapshot; total CT
    // count and the scanner's current slot are not substitutes for that value.
    CSOLScenarioFrame UpdateFrame(CHalfLifeMultiplay& rules, float thinkTime,
        int aliveCtCount, csol::scenario::WaveRunner::Dispatch dispatch, void* context,
        csol::scenario::TriggerWaveEvents& events);

    CSOLScenarioMapState State() const { return m_state; }
    const csol::scenario::ResourceError& Error() const { return m_error; }
    const csol::scenario::MapTables* Tables() const { return m_resources.Tables(); }
    const csol::scenario::MonsterDeathRuleTable& DeathRules() const { return m_deathRules.Rules(); }
    // A death-table stop affects controller dispatch only. It is neither a
    // victory nor a pause of the manager clock, player scan or active programs.
    bool StopWaveDispatch();
    bool WaveDispatchStopped() const { return m_waveDispatchStopped; }
    // Ordinary pooled creation enables progress only after overlap handling,
    // while the actual spawned monster still has positive health.
    bool EnableMonsterClearPoint(float points);
    // Death adds the current type's value after the root kill callback.
    // Unlike Enable, this also adds to -1 and clamps only the upper bound.
    bool AddMonsterClearPoint(float points, bool& reached);
    // Only the trigger-present branch consumes progress, after OnControls
    // and optional protection. Never let an old callback clear a new round.
    bool ConsumeMonsterClearPoint(std::uint64_t roundGeneration);
    float ClearPoint() const { return m_clearPoint; }
    // Subtype1 keeps these ushort counters across formal stages. They belong
    // to the attacker's native team, independently of the monster's team.
    bool RecordTeamMonsterKill(int team);
    std::uint16_t TeamMonsterKills(int team) const
    { return team == 1 || team == 2 ? m_teamMonsterKills[team - 1] : 0; }
    // The manager retains a stable team filter across nested death callbacks.
    // Advancing the clock precedes each batch; zero is the original idle value.
    bool ScheduleMonsterDrain(float now, std::int8_t team);
    bool AdvanceMonsterDrain(float now, bool& due);
    void CompleteMonsterDrain() { m_monsterDrainAt = 0; }
    float MonsterDrainAt() const { return m_monsterDrainAt; }
    const std::int8_t& MonsterDrainTeam() const { return m_monsterDrainTeam; }
    std::optional<std::size_t> RoundIndex() const { return m_roundIndex; }
    std::uint64_t RoundGeneration() const { return m_roundGeneration; }
    CCSOLSpawnPointManager& SpawnPoints() { return m_points; }
    const CCSOLSpawnPointManager& SpawnPoints() const { return m_points; }
    const CCSOLTriggerManager& TriggerVolumes() const { return m_volumes; }
    csol::scenario::TriggerRound& Round() { return m_round; }
    const csol::scenario::TriggerRound& Round() const { return m_round; }
    CCSOLTriggerPlayerScan& PlayerScan() { return m_scan; }
    const CCSOLTriggerPlayerScan& PlayerScan() const { return m_scan; }
    // Original local cache only; a dedicated server does not refresh it.
    // Expose its valid prefix, never stale entries beyond the recorded count.
    std::span<const CSOLActiveWave> WaveSummary() const
    {
        return {m_waveSummary.data(), m_waveSummaryCount};
    }

private:
    class WaveEvents final : public csol::scenario::TriggerWaveEvents
    {
    public:
        WaveEvents(CCSOLScenarioMap& owner, csol::scenario::TriggerWaveEvents& events)
            : m_owner(owner), m_events(events), m_access(owner.EntityAccess()), m_round(owner.RoundGeneration()) {}
        bool Check(csol::scenario::ConfigError& error) const override;
        void WaveActivated(std::uint16_t id) override { csol::scenario::ConfigError error; if (Check(error)) m_events.WaveActivated(id); }
        void BeforeSpatialWaveEnd(std::uint16_t id) override { csol::scenario::ConfigError error; if (Check(error)) m_events.BeforeSpatialWaveEnd(id); }
        void WavesChanged(std::span<const std::uint16_t> active) override;
        void GoalReached() override { csol::scenario::ConfigError error; if (Check(error)) m_events.GoalReached(); }
    private:
        CCSOLScenarioMap& m_owner;
        csol::scenario::TriggerWaveEvents& m_events;
        std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
        std::uint64_t m_round{};
        mutable bool m_current{true};
        mutable csol::scenario::ConfigError m_status;
    };
    bool Fail(const char* message);
    void Clear();
    void RefreshWaveSummary(csol::scenario::TriggerWaveEvents* events = nullptr);

    csol::scenario::MapResources m_resources;
    csol::scenario::MonsterDeathRuleResources m_deathRules;
    CCSOLSpawnPointManager m_points;
    CCSOLTriggerManager m_volumes;
    csol::scenario::TriggerRound m_round;
    CCSOLTriggerPlayerScan m_scan;
    csol::scenario::ResourceError m_error;
    std::optional<std::size_t> m_roundIndex;
    // Retained across maps so the same round index cannot revive old callbacks.
    std::uint64_t m_roundGeneration{};
    std::array<CSOLActiveWave, 30> m_waveSummary{};
    std::size_t m_waveSummaryCount{};
    CSOLScenarioMapState m_state{CSOLScenarioMapState::Empty};
    bool m_busy{};
    bool m_waveDispatchStopped{};
    float m_clearPoint{-1.0f};
    std::array<std::uint16_t, 2> m_teamMonsterKills{};
    float m_monsterDrainAt{};
    std::int8_t m_monsterDrainTeam{};
    std::shared_ptr<CSOLScenarioEntityAccess> m_entityAccess{std::make_shared<CSOLScenarioEntityAccess>()};
};
