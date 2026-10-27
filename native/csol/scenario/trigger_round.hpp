// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "trigger_wave_actions.hpp"

#include <optional>

namespace csol::scenario
{
// The shared wave/trigger portion of subtype 1 round initialization. Selecting
// the round, spawning players, map restart, termination and progression belong
// to GameRules; a finished/empty active-wave list is not a victory condition.
class TriggerRound
{
public:
    bool BeginMap();
    bool Begin(const WaveTable& table, const TriggerRuleTable& rules,
        const TriggerVolumes& volumes, const RoundRow& round,
        float now, float freezeTime, SpawnPoints& points,
        TriggerWaveEvents& events, ConfigError& error);

    WaveController& Waves() { return waves_; }
    const WaveController& Waves() const { return waves_; }
    TriggerProgram& Triggers() { return triggers_; }
    const TriggerProgram& Triggers() const { return triggers_; }
    WaveSchedule& Schedule() { return schedule_; }
    const WaveSchedule& Schedule() const { return schedule_; }
    std::optional<std::int16_t> Group() const { return group_; }
    bool IsBusy() const { return starting_ || waves_.IsDispatching() || triggers_.IsExecuting(); }

private:
    WaveController waves_;
    TriggerProgram triggers_;
    WaveSchedule schedule_;
    std::optional<std::int16_t> group_;
    bool starting_{};
};
}
