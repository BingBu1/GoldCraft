// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_wave_actions.hpp"

#include <algorithm>

namespace csol::scenario
{
bool WaveTriggerActions::Current() const
{
    if (!current_ || !error_.message.empty()) return false;
    current_ = events_.Check(error_);
    return current_ && error_.message.empty();
}

void WaveTriggerActions::ActivateWave(std::uint16_t id, float elapsed)
{
    if (!Valid() || !waves_.ActivateWave(id, elapsed, error_)) return;
    // This also applies to absent/empty waves: the manager must wake and remove
    // them on its next update rather than waiting for an old half-second gate.
    RequestWaveUpdate(schedule_);
    events_.WaveActivated(id);
}

bool WaveTriggerActions::EndWave(std::uint16_t id, bool spatial)
{
    if (!Valid()) return false;
    const auto active = waves_.ActiveWaves();
    if (std::find(active.begin(), active.end(), id) == active.end()) return false;
    if (spatial) events_.BeforeSpatialWaveEnd(id);
    if (!Current()) return false;
    return waves_.EndWave(id);
}

void WaveTriggerActions::RefreshWaves()
{
    if (Valid()) events_.WavesChanged(waves_.ActiveWaves());
}

void WaveTriggerActions::GoalReached()
{
    if (Valid()) events_.GoalReached();
}
}
