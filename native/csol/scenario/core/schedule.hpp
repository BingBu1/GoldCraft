// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::scenario
{
struct WaveSchedule
{
    float start{};
    float elapsed{};
    float nextThink{};
};

enum class ScheduleStep { Invalid, Frozen, Waiting, Dispatch };

// A round changes its time origin, but does not reset the manager's nextThink.
// A new map/session constructs a new schedule with nextThink == 0.
bool BeginWaveRound(WaveSchedule& clock, float now, float freezeTime);

// MP uses the Think argument for elapsed time and the current engine clock for
// the half-second gate. During freeze it leaves the last elapsed value intact.
ScheduleStep CheckWaveSchedule(WaveSchedule& clock, float thinkTime, float engineTime);

// Call after dispatch, with a fresh engine clock sample. Missed manager ticks
// are not replayed, and this is not an increment of the previous nextThink.
bool FinishWaveSchedule(WaveSchedule& clock, float engineTime);

// Trigger activation invalidates the half-second gate; it does not change the
// round time origin or elapsed value. Call after a successful ActivateWave.
inline void RequestWaveUpdate(WaveSchedule& clock) { clock.nextThink = 0; }
}
