// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_character_skills/elite_manager.hpp"
#include "../scenario/elite.hpp"

struct CSOLOrdinaryMonsterState;

// Map-owned production state behind the original elite manager. The latest
// successful monster point is scenario+230, not TriggerRound::Group(). The
// elapsed clock is independent of the HUD clock and freeze-period thaw.
class CCSOLScenarioEliteServices final
{
public:
    CCSOLScenarioEliteServices(std::weak_ptr<CCSOLScenarioRuntime> runtime,
        std::shared_ptr<const csol::scenario::EliteSettings> settings);
    bool Current() const;
    void Invalidate();

    // Call once after the formal map BeginRound advances RoundGeneration.
    //10828560 preserves the previous round duration;11382DB6 clears point230.
    bool BeginRound(float now, csol::scenario::ConfigError& error, bool replay = false);
    bool QualifiedPlayers(CSOLMonsterSpawnCall& call, int& count,
        csol::scenario::ConfigError& error) const;
    // Call immediately after the successful RulesMonsterSpawned callback in
    // Create's positive-health branch. Do not re-test health after that call:
    //113859AC still records the requested point if that callback kills it.
    bool RecordSuccessfulSpawn(CSOLMonsterSpawnCall& call, int requestedPoint,
        csol::scenario::ConfigError& error);
    bool SpawnFollower(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error);
    bool SpawnPrimary(csol::scenario::ConfigError& error);
    bool ReadSpawnClock(CCSOLScenarioEliteManager::SpawnClock& clock,
        csol::scenario::ConfigError& error) const;
    bool Update(csol::scenario::ConfigError& error);
    //109679F0 D0 tail: NAV progress/near target, then strict20s expiry or
    // the actual host navigation-expired flag. Selects any alive player.
    bool UpdateRecovery(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& ordinary,
        float& changedAt, int& previousArea, bool& relocate,
        csol::scenario::ConfigError& error) const;

    // Trusted server event16 in1141C880 calls108260E0 with its float payload.
    // The proven constructor default is zero; never manufacture a reference
    // rate from player count/frags or accept a client userinfo value here.
    bool SetFragRateReference(float value, csol::scenario::ConfigError& error);
    // These are the exact continuation clock writers, for the corresponding
    // mode lifecycle. They do not perform the other save/restore operations.
    bool CaptureContinuation(float now, csol::scenario::ConfigError& error);
    bool AccumulateContinuation(float now, csol::scenario::ConfigError& error);
    bool ResetContinuation(csol::scenario::ConfigError& error);

private:
    struct State
    {
        std::shared_ptr<const CSOLScenarioEntityAccess> access;
        std::uint64_t runtimeGeneration{}, round{};
        float roundStarted{}, previousRoundDuration{}, fragRateReference{};
        std::int32_t savedElapsed{};
        int lastSuccessfulPoint{};
        //1137A9FC initializes the RoundRetry countdown to-1. The item300
        // producer/resume sequence is not implemented; ordinary rounds must
        // not pretend that a HUD or wave countdown is a pending retry.
        std::int8_t retryCountdown{-1};
        bool roundReady{};
    };
    struct Call;
    bool Spawn(bool follower, csol::scenario::ConfigError& error);
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<const csol::scenario::EliteSettings> m_settings;
    std::shared_ptr<const csol::scenario::MonsterType> m_baseType;
    int m_typeDifficulty{-1};
    std::shared_ptr<State> m_state;
};
