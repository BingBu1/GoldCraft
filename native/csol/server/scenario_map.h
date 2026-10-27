// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_point_manager.h"
#include "scenario_trigger_manager.h"
#include "scenario_player_scan.h"
#include "../scenario/map_resources.hpp"
#include "../scenario/death_rules.hpp"
#include "../scenario/trigger_round.hpp"

#include <array>

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
            : m_owner(owner), m_events(events) {}
        void WaveActivated(std::uint16_t id) override { m_events.WaveActivated(id); }
        void BeforeSpatialWaveEnd(std::uint16_t id) override { m_events.BeforeSpatialWaveEnd(id); }
        void WavesChanged(std::span<const std::uint16_t> active) override;
        void GoalReached() override { m_events.GoalReached(); }
    private:
        CCSOLScenarioMap& m_owner;
        csol::scenario::TriggerWaveEvents& m_events;
    };
    bool Busy() const;
    bool Fail(const char* message);
    void Clear();
    void RefreshWaveSummary();

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
};
