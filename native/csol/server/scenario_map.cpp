// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_map.h"
#include "../scenario/difficulty.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
class MapOperation
{
public:
    explicit MapOperation(bool& busy) : m_busy(busy) { m_busy = true; }
    ~MapOperation() { m_busy = false; }
private:
    bool& m_busy;
};
}

bool CCSOLScenarioMap::Busy() const
{
    return m_busy || m_round.IsBusy() || m_scan.IsScanning();
}

void CCSOLScenarioMap::RefreshWaveSummary()
{
    // This gate belongs to the local cache, never the simulation itself.
    if (!g_engfuncs.pfnIsDedicatedServer || g_engfuncs.pfnIsDedicatedServer()) return;
    const auto& waves = m_round.Waves();
    const auto active = waves.ActiveWaves();
    m_waveSummaryCount = std::min(active.size(), m_waveSummary.size());
    for (std::size_t i = 0; i < m_waveSummaryCount; ++i)
    {
        const auto* runner = waves.FindWave(active[i]);
        m_waveSummary[i] = {active[i], runner ? runner->StartOffset() : 0.0f};
    }
}

void CCSOLScenarioMap::WaveEvents::WavesChanged(std::span<const std::uint16_t> active)
{
    m_owner.RefreshWaveSummary();
    m_events.WavesChanged(active);
}

void CCSOLScenarioMap::Clear()
{
    // The owning engine destroys old edicts. Clearing snapshots must never
    // dereference an EHANDLE into an edict array that already belongs to a new map.
    m_points.BeginMap();
    m_volumes.BeginMap();
    m_round.BeginMap();
    m_scan.BeginMap();
    m_resources.Clear();
    m_deathRules.Clear();
    m_roundIndex.reset();
    m_waveSummaryCount = 0;
    m_waveDispatchStopped = false;
    m_error = {};
    m_state = CSOLScenarioMapState::Empty;
}

bool CCSOLScenarioMap::Fail(const char* message)
{
    m_error = {};
    m_error.detail.message = message;
    m_state = CSOLScenarioMapState::Failed;
    return false;
}

bool CCSOLScenarioMap::EndMap()
{
    if (Busy()) return false;
    MapOperation operation(m_busy);
    Clear();
    return true;
}

bool CCSOLScenarioMap::BeginMap(std::string_view mapName)
{
    if (Busy()) return false;
    // The name may refer to the previous Tables()->mapName.
    const std::string nextMap(mapName);
    MapOperation operation(m_busy);
    Clear();
    const csol::scenario::ResourceFileApi files{
        g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile};
    if (!m_resources.BeginMapFromCatalog(nextMap, files, m_error))
    {
        m_state = CSOLScenarioMapState::Failed;
        return false;
    }
    const auto* tables = m_resources.Tables();
    if (tables->scenarioType != 1)
        return Fail("scenario map lifecycle currently supports subtype 1 only");
    if (!tables->triggerRules || tables->rounds.rows.empty())
        return Fail("scenario map requires trigger rules and at least one round");
    if (!m_deathRules.BeginMap(nextMap, files, m_error))
    {
        m_state = CSOLScenarioMapState::Failed;
        return false;
    }
    m_state = CSOLScenarioMapState::ResourcesLoaded;
    return true;
}

bool CCSOLScenarioMap::CollectEntities()
{
    if (Busy()) return false;
    if (m_state == CSOLScenarioMapState::EntitiesCollected ||
        m_state == CSOLScenarioMapState::RoundActive) return true;
    if (m_state != CSOLScenarioMapState::ResourcesLoaded) return false;
    MapOperation operation(m_busy);
    const auto points = m_points.CollectMap();
    if (points == CSOLPointCollection::ProxyUnavailable)
    {
        // Proxy rejection has not consumed markers; allow a later retry after
        // the host fixes the cause. The round is still unavailable.
        m_error.detail.message = "scenario player spawn proxy could not be created";
        return false;
    }
    if (points != CSOLPointCollection::Ready)
        return Fail("scenario map needs the unsupported SP spawn-file fallback");
    if (m_volumes.CollectMap() != CSOLTriggerCollection::Ready)
        return Fail("scenario map needs the unsupported MAP_TRG volume-file fallback");
    m_error = {};
    m_state = CSOLScenarioMapState::EntitiesCollected;
    return true;
}

bool CCSOLScenarioMap::BeginRound(std::size_t roundIndex, float now, float freezeTime,
    csol::scenario::TriggerWaveEvents& events)
{
    if (Busy()) return false;
    if (m_state != CSOLScenarioMapState::EntitiesCollected &&
        m_state != CSOLScenarioMapState::RoundActive) return false;
    const auto* tables = m_resources.Tables();
    if (roundIndex >= tables->rounds.rows.size())
    {
        // GameRules owns progression. Reject an invalid host index rather than
        // silently replaying the last phase and concealing a lifecycle error.
        m_error = {};
        m_error.detail.message = "scenario round index is outside the loaded table";
        return false;
    }
    if (m_roundGeneration == std::numeric_limits<std::uint64_t>::max())
        return Fail("scenario round identity exhausted");
    MapOperation operation(m_busy);
    const auto previousIndex = m_roundIndex;
    const auto previousState = m_state;
    const bool previousStop = m_waveDispatchStopped;
    const auto previousGeneration = m_roundGeneration;
    // TriggerRound publishes its committed active summary synchronously. The
    // observer must see the same phase in both the owner and its controller.
    // Failed validation emits no callbacks, so restore the previous phase.
    m_roundIndex = roundIndex;
    m_state = CSOLScenarioMapState::RoundActive;
    // The formal restart clears the stop before its round-start callback.
    // Accepted continuation never calls BeginRound and must retain this gate.
    m_waveDispatchStopped = false;
    ++m_roundGeneration;
    m_error = {};
    csol::scenario::ConfigError detail;
    WaveEvents notifications(*this, events);
    if (!m_round.Begin(tables->waves, *tables->triggerRules, m_volumes.Volumes(),
        tables->rounds.rows[roundIndex], now, freezeTime, m_points.Points(), notifications, detail))
    {
        m_roundIndex = previousIndex;
        m_state = previousState;
        m_waveDispatchStopped = previousStop;
        m_roundGeneration = previousGeneration;
        m_error = {"", std::move(detail)};
        return false;
    }
    m_error = {};
    // Neither ordinary rounds nor the separate continuation path reset the
    // player scan cursor. Only BeginMap/EndMap starts a new scan session.
    return true;
}

bool CCSOLScenarioMap::ApplyDifficulty(const csol::scenario::DifficultyResources& difficulty)
{
    if (Busy() || (m_state != CSOLScenarioMapState::EntitiesCollected &&
        m_state != CSOLScenarioMapState::RoundActive)) return false;
    MapOperation operation(m_busy);
    csol::scenario::ConfigError detail;
    if (!m_resources.ApplyDifficulty(difficulty, detail))
    {
        m_error = {std::string(difficulty.SourcePath()), std::move(detail)};
        return false;
    }
    m_error = {};
    return true;
}

bool CCSOLScenarioMap::StopWaveDispatch()
{
    if (m_state != CSOLScenarioMapState::RoundActive) return false;
    // Called from a synchronous death nested in a frame callback. Busy is
    // expected here; only future controller entries observe the changed gate.
    m_waveDispatchStopped = true;
    return true;
}
