// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_session.h"
#include "scenario_monster_factory.h"
#include "scenario_event_sink.h"

// One live composition of the already implemented map, death dispatcher,
// occupancy grid and factory. Services must be the concrete rules owners;
// this class never substitutes an inert AI or accepts an unhandled death.
class CCSOLScenarioGameplay
{
public:
    // Keep automatic destination validation aligned with actual gameplay
    // support as more scenario maps and managers are implemented.
    static bool SupportsMap(std::string_view name, int subtype)
    { return name == "zs_deadend" && subtype == 1; }
    CCSOLScenarioGameplay(CCSOLScenarioSession& session,
        CCSOLMonsterDeathServices& deathServices, CCSOLSpawnClearDeaths& spawnDeaths,
        CCSOLMonsterFactoryServices& factoryServices, int mapId,
        std::shared_ptr<const CCSOLMonsterSpawnOwner> owner = {});
    bool Activate();
    // Invalidate callbacks and release borrowed type records before installing
    // the next round. Player respawns and map cleanup are the caller's next
    // two operations, in that order. Continuation must not enter this method.
    bool BeginRound(std::size_t roundIndex, float now, float freezeTime,
        csol::scenario::TriggerWaveEvents& events);
    bool Update(CHalfLifeMultiplay& rules, float thinkTime, int aliveCtCount,
        csol::scenario::TriggerWaveEvents& events);
    // Enter through the complete gameplay update so pending end-round deaths
    // run before the native rule checks a restart/final-result deadline.
    bool ScheduleRoundDrain(float now);
    CCSOLPlayerSpawn* SelectSpawn(CBasePlayer& player);
    bool ResolveSpawnOverlap(CSOLMonsterSpawnCall& call,
        std::span<const std::size_t> protectedSlots, csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
    bool Deactivate();
    bool Busy() const { return m_updating || m_resetting || m_selecting; }

    CCSOLMonsterDeathOwner& Deaths() { return m_deaths; }
    CCSOLScenarioOccupancy& Occupancy() { return m_occupancy; }
    CCSOLMonsterFactory& Factory() { return m_factory; }
    const csol::scenario::ConfigError& Error() const { return m_error; }
    bool Active() const { return m_active && OwnerCurrent() && m_error.message.empty() && m_session.Map().EntityAccessAllowed(); }

private:
    static void Dispatch(void* context, const csol::scenario::WaveRow& row,
        csol::scenario::RowClock& clock);
    bool Fail(const char* reason);
    bool OwnerCurrent() const;

    // Services and session outlive this composition and every pinned behavior.
    CCSOLScenarioSession& m_session;
    std::weak_ptr<const CCSOLMonsterSpawnOwner> m_owner;
    bool m_hasOwner{};
    CCSOLSpawnClearDeaths& m_spawnDeaths;
    CCSOLMonsterDeathOwner m_deaths;
    CCSOLScenarioOccupancy m_occupancy;
    CCSOLScenarioPlayerSpawns m_spawns;
    CCSOLMonsterFactory m_factory;
    csol::scenario::ConfigError m_error;
    std::int32_t m_spawnGroupOffset{};
    std::uint64_t m_generation{};
    bool m_active{}, m_updating{}, m_resetting{}, m_selecting{};
};
