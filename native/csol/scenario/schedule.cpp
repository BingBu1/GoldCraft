// SPDX-License-Identifier: GPL-3.0-or-later
#include "schedule.hpp"

#include <cmath>

namespace csol::scenario
{
bool BeginWaveRound(WaveSchedule& clock, float now, float freezeTime)
{
    if (!std::isfinite(now) || !std::isfinite(freezeTime)) return false;
    const float start = now + freezeTime;
    if (!std::isfinite(start)) return false;
    clock.start = start;
    return true;
}

ScheduleStep CheckWaveSchedule(WaveSchedule& clock, float thinkTime, float engineTime)
{
    if (!std::isfinite(thinkTime) || !std::isfinite(engineTime) ||
        !std::isfinite(clock.start) || !std::isfinite(clock.nextThink))
        return ScheduleStep::Invalid;
    const float elapsed = thinkTime - clock.start;
    if (!std::isfinite(elapsed)) return ScheduleStep::Invalid;
    if (elapsed < 0) return ScheduleStep::Frozen;
    clock.elapsed = elapsed;
    return engineTime < clock.nextThink ? ScheduleStep::Waiting : ScheduleStep::Dispatch;
}

bool FinishWaveSchedule(WaveSchedule& clock, float engineTime)
{
    if (!std::isfinite(engineTime)) return false;
    const float next = engineTime + 0.5f;
    if (!std::isfinite(next)) return false;
    clock.nextThink = next;
    return true;
}
}
