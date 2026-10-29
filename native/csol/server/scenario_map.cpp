// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_map.h"
#include "../scenario/difficulty.hpp"

#include <algorithm>
#include <cmath>
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

void CCSOLScenarioMap::RefreshWaveSummary(csol::scenario::TriggerWaveEvents* events)
{
    const auto access = m_entityAccess;
    const auto round = m_roundGeneration;
    csol::scenario::ConfigError error;
    // This gate belongs to the local cache, never the simulation itself.
    if (!access->Allowed() || (events && !events->Check(error)) || !g_engfuncs.pfnIsDedicatedServer ||
        g_engfuncs.pfnIsDedicatedServer() || !access->Allowed() || m_entityAccess != access ||
        m_roundGeneration != round || (events && !events->Check(error))) return;
    const auto& waves = m_round.Waves();
    const auto active = waves.ActiveWaves();
    m_waveSummaryCount = std::min(active.size(), m_waveSummary.size());
    for (std::size_t i = 0; i < m_waveSummaryCount; ++i)
    {
        const auto* runner = waves.FindWave(active[i]);
        m_waveSummary[i] = {active[i], runner ? runner->StartOffset() : 0.0f};
    }
}

bool CCSOLScenarioMap::WaveEvents::Check(csol::scenario::ConfigError& error) const
{
    if (!m_status.message.empty()) { error = m_status; return false; }
    if (!m_current) { error = {}; return false; }
    m_current = m_access->Allowed() && m_owner.EntityAccess() == m_access &&
        m_owner.RoundGeneration() == m_round && m_owner.State() == CSOLScenarioMapState::RoundActive;
    if (!m_current) { error = {}; return false; }
    m_current = m_events.Check(m_status);
    if (!m_status.message.empty()) { m_current = false; error = m_status; }
    else if (!m_current) error = {};
    return m_current;
}

void CCSOLScenarioMap::WaveEvents::WavesChanged(std::span<const std::uint16_t> active)
{
    csol::scenario::ConfigError error;
    if (!Check(error)) return;
    m_owner.RefreshWaveSummary(this);
    if (Check(error)) m_events.WavesChanged(active);
}

void CCSOLScenarioMap::Clear()
{
    RevokeEntityAccess();
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
    m_clearPoint = -1.0f;
    m_teamMonsterKills = {};
    m_monsterDrainAt = 0;
    m_monsterDrainTeam = 0;
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
    RevokeEntityAccess();
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
    m_entityAccess = std::make_shared<CSOLScenarioEntityAccess>();
    m_scan.BindEntityAccess(m_entityAccess);
    m_points.BindEntityAccess(m_entityAccess);
    m_volumes.BindEntityAccess(m_entityAccess);
    const csol::scenario::ResourceFileApi files{
        g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile};
    if (!m_resources.BeginMapFromCatalog(nextMap, files, m_error))
    {
        m_state = CSOLScenarioMapState::Failed;
        return false;
    }
    if (!EntityAccessAllowed()) return false;
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
    if (!EntityAccessAllowed()) return false;
    m_state = CSOLScenarioMapState::ResourcesLoaded;
    return true;
}

bool CCSOLScenarioMap::CollectEntities()
{
    if (!EntityAccessAllowed() || Busy()) return false;
    if (m_state == CSOLScenarioMapState::EntitiesCollected ||
        m_state == CSOLScenarioMapState::RoundActive) return true;
    if (m_state != CSOLScenarioMapState::ResourcesLoaded) return false;
    MapOperation operation(m_busy);
    const auto points = m_points.CollectMap();
    if (!EntityAccessAllowed()) return false;
    if (points == CSOLPointCollection::ProxyUnavailable)
    {
        // Proxy rejection has not consumed markers; allow a later retry after
        // the host fixes the cause. The round is still unavailable.
        m_error.detail.message = "scenario player spawn proxy could not be created";
        return false;
    }
    if (points != CSOLPointCollection::Ready)
        return Fail("scenario map needs the unsupported SP spawn-file fallback");
    const auto volumes = m_volumes.CollectMap();
    if (!EntityAccessAllowed()) return false;
    if (volumes != CSOLTriggerCollection::Ready)
        return Fail("scenario map needs the unsupported MAP_TRG volume-file fallback");
    m_error = {};
    m_state = CSOLScenarioMapState::EntitiesCollected;
    return true;
}

bool CCSOLScenarioMap::BeginRound(std::size_t roundIndex, float now, float freezeTime,
    csol::scenario::TriggerWaveEvents& events)
{
    if (!EntityAccessAllowed() || Busy()) return false;
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
    const auto previousClearPoint = m_clearPoint;
    const auto previousDrainAt = m_monsterDrainAt;
    // TriggerRound publishes its committed active summary synchronously. The
    // observer must see the same phase in both the owner and its controller.
    // Failed validation emits no callbacks, so restore the previous phase.
    m_roundIndex = roundIndex;
    m_state = CSOLScenarioMapState::RoundActive;
    // The formal restart clears the stop before its round-start callback.
    // Accepted continuation never calls BeginRound and must retain this gate.
    m_waveDispatchStopped = false;
    m_clearPoint = -1.0f;
    m_monsterDrainAt = 0;
    ++m_roundGeneration;
    m_error = {};
    csol::scenario::ConfigError detail;
    WaveEvents notifications(*this, events);
    bool committed{};
    if (!m_round.Begin(tables->waves, *tables->triggerRules, m_volumes.Volumes(),
        tables->rounds.rows[roundIndex], now, freezeTime, m_points.Points(), notifications, detail, &committed))
    {
        if (!EntityAccessAllowed()) return false;
        if (committed)
        {
            // The controller already belongs to this new round. A callback
            // failure must never restore old identities around committed data.
            if (!detail.message.empty())
            { m_error = {"", std::move(detail)}; m_state = CSOLScenarioMapState::Failed; }
            return false;
        }
        m_roundIndex = previousIndex;
        m_state = previousState;
        m_waveDispatchStopped = previousStop;
        m_roundGeneration = previousGeneration;
        m_clearPoint = previousClearPoint;
        m_monsterDrainAt = previousDrainAt;
        m_error = {"", std::move(detail)};
        return false;
    }
    if (!notifications.Check(detail)) return false;
    m_error = {};
    // Neither ordinary rounds nor the separate continuation path reset the
    // player scan cursor. Only BeginMap/EndMap starts a new scan session.
    return true;
}

bool CCSOLScenarioMap::ApplyDifficulty(const csol::scenario::DifficultyResources& difficulty)
{
    if (!EntityAccessAllowed() || Busy() || (m_state != CSOLScenarioMapState::EntitiesCollected &&
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
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive) return false;
    // Called from a synchronous death nested in a frame callback. Busy is
    // expected here; only future controller entries observe the changed gate.
    m_waveDispatchStopped = true;
    return true;
}

bool CCSOLScenarioMap::EnableMonsterClearPoint(float points)
{
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive ||
        !std::isfinite(points)) return false;
    if (m_clearPoint == -1.0f && points > 0.0f) m_clearPoint = 0.0f;
    return true;
}

bool CCSOLScenarioMap::AddMonsterClearPoint(float points, bool& reached)
{
    #pragma clang fp contract(off)
    reached = false;
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive ||
        !std::isfinite(points)) return false;
    // One float addition, then the original upper-only clamp. In particular,
    // disabled progress (-1) plus 100 is 99, not a new completion.
    float next = m_clearPoint + points;
    if (next > 100.0f) next = 100.0f;
    if (!std::isfinite(next)) return false;
    m_clearPoint = next;
    reached = next >= 100.0f;
    return true;
}

bool CCSOLScenarioMap::ConsumeMonsterClearPoint(std::uint64_t roundGeneration)
{
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive ||
        m_roundGeneration != roundGeneration) return false;
    m_clearPoint = -1.0f;
    return true;
}

bool CCSOLScenarioMap::RecordTeamMonsterKill(int team)
{
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive) return false;
    if (team == 1 || team == 2)
    {
        auto& count = m_teamMonsterKills[team - 1];
        // MP 1137C3CF..1137C3EF: compare the old unsigned short, then add.
        // No enemy-team test and no CS team-kill penalty counter is involved.
        if (count < 65000) ++count;
    }
    return true;
}

bool CCSOLScenarioMap::ScheduleMonsterDrain(float now, std::int8_t team)
{
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive ||
        !std::isfinite(now) || team < 0 || team > 2) return false;
    m_monsterDrainAt = now;
    m_monsterDrainTeam = team;
    return true;
}

bool CCSOLScenarioMap::AdvanceMonsterDrain(float now, bool& due)
{
    #pragma clang fp contract(off)
    due = false;
    if (!EntityAccessAllowed() || m_state != CSOLScenarioMapState::RoundActive ||
        !std::isfinite(now)) return false;
    if (m_monsterDrainAt == 0.0f || now < m_monsterDrainAt) return true;
    // MP 1138807E..11388094 publishes old deadline + float32(0.1)
    // before Drain(3, true). No catch-up loop and no now + interval reset.
    const float next = m_monsterDrainAt + 0.1f;
    if (!std::isfinite(next)) return false;
    m_monsterDrainAt = next;
    due = true;
    return true;
}
