// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_map.h"

CSOLScenarioFrame CCSOLScenarioMap::UpdateFrame(CHalfLifeMultiplay& rules,
    float thinkTime, int aliveCtCount, csol::scenario::WaveRunner::Dispatch dispatch,
    void* context, csol::scenario::TriggerWaveEvents& events)
{
    using namespace csol::scenario;
    if (Busy() || m_state != CSOLScenarioMapState::RoundActive || !gpGlobals)
        return {};
    // These are the host's actual GameRules fields/accessors, not offsets from
    // CSO. A pending continuation's completion lock does not stop this path.
    CSOLScenarioFrame result{true};
    if (!rules.IsGameStarted() || rules.IsFreezePeriod() || rules.m_bRoundTerminating)
        return result;
    if (!dispatch && !m_waveDispatchStopped)
    {
        Fail("scenario wave dispatch is unavailable");
        result.valid = false;
        return result;
    }
    struct FrameScope
    {
        bool& busy;
        explicit FrameScope(bool& value) : busy(value) { busy = true; }
        ~FrameScope() { busy = false; }
    } scope(m_busy);

    auto& clock = m_round.Schedule();
    result.schedule = CheckWaveSchedule(clock, thinkTime, gpGlobals->time);
    if (result.schedule == ScheduleStep::Invalid)
    {
        Fail("invalid scenario frame time");
        result.valid = false;
        return result;
    }
    if (result.schedule == ScheduleStep::Frozen) return result;
    if (result.schedule == ScheduleStep::Dispatch)
    {
        // MP checks its death-table stop once at controller entry. A stop
        // raised during a row callback does not cancel that same traversal.
        if (m_waveDispatchStopped)
            result.waves = {true, m_round.Waves().ActiveWaves().empty(), 0};
        else
        {
            result.waves = m_round.Waves().Tick(clock.elapsed, dispatch, context);
            // The original controller refreshes its local summary at the end
            // of a normal tick, even after a terminating row callback. The
            // entry stop gate skips it; it is not a WavesChanged notification.
            if (result.waves.valid) RefreshWaveSummary();
        }
        // Re-read engine time after callbacks. The original manager advances
        // its half-second deadline even when the active list becomes empty.
        if (!result.waves.valid || !FinishWaveSchedule(clock, gpGlobals->time))
        {
            Fail("scenario wave update failed");
            result.valid = false;
            return result;
        }
    }
    // A row callback can change any rule gate. Re-read all of them before
    // spatial scanning; skipped scans must not advance the player cursor.
    if (!rules.IsGameStarted() || rules.IsFreezePeriod() || rules.m_bRoundTerminating) return result;
    WaveEvents notifications(*this, events);
    WaveTriggerActions actions(m_round.Waves(), clock, notifications);
    result.triggers = m_scan.Update(true, m_round.Triggers(), clock.elapsed,
        aliveCtCount, actions);
    if (!result.triggers.valid || !actions.Valid())
    {
        result.valid = false;
        if (actions.Valid()) Fail("scenario player scan failed");
        else
        {
            m_error = {"", actions.Error()};
            m_state = CSOLScenarioMapState::Failed;
        }
        // Earlier row/trigger side effects are not rolled back. Failed maps
        // remain stopped until an explicit new map session repairs their data.
    }
    return result;
}
