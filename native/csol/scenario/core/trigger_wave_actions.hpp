// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controller.hpp"
#include "schedule.hpp"
#include "trigger_program.hpp"

namespace csol::scenario
{
// The scenario manager supplies these game-level notifications. In particular,
// the spatial end-wave observer runs before removing the first active match.
class TriggerWaveEvents
{
public:
    virtual ~TriggerWaveEvents() = default;
    // No callbacks. False with an empty error means owner cancellation;
    // a nonempty error reports a failed service. Success leaves error intact.
    virtual bool Check(ConfigError& error) const = 0;
    virtual void WaveActivated(std::uint16_t id) = 0;
    virtual void BeforeSpatialWaveEnd(std::uint16_t id) = 0;
    virtual void WavesChanged(std::span<const std::uint16_t> active) = 0;
    virtual void GoalReached() = 0;
};

// Make one adapter for a trigger dispatch and check Error afterwards. Invalid
// schedules abort subsequent side effects instead of silently advancing the
// level. The owner reports/disables invalid map data; no engine state is hidden.
class WaveTriggerActions final : public TriggerActions
{
public:
    WaveTriggerActions(WaveController& waves,
        WaveSchedule& schedule, TriggerWaveEvents& events)
        : waves_(waves), schedule_(schedule), events_(events) {}
    void ActivateWave(std::uint16_t id, float elapsed) override;
    bool EndWave(std::uint16_t id, bool spatial) override;
    void RefreshWaves() override;
    void GoalReached() override;
    const ConfigError& Error() const { Current(); return error_; }
    bool Current() const override;
    bool Valid() const { return Current(); }

private:
    WaveController& waves_;
    WaveSchedule& schedule_;
    TriggerWaveEvents& events_;
    mutable ConfigError error_;
    mutable bool current_{true};
};
}
