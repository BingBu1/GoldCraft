// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "trigger_rules.hpp"

#include <span>

namespace csol::scenario
{
// The game adapter performs these actions synchronously. EndWave removes only
// the first active match, and spatial=true preserves the additional scenario
// observer notification that the explicit-ID command path does not send.
class TriggerActions
{
public:
    virtual ~TriggerActions() = default;
    // Identity/status only, with no engine or observer callbacks. False stops
    // the current command prefix without consuming its remaining side effects.
    virtual bool Current() const = 0;
    virtual void ActivateWave(std::uint16_t id, float elapsed) = 0;
    virtual bool EndWave(std::uint16_t id, bool spatial) = 0;
    virtual void RefreshWaves() = 0;
    virtual void GoalReached() = 0;
};

struct TriggerResult
{
    bool valid{};
    std::size_t waveCommands{};
    bool goalReached{};
};

// Implements the w/g rules used by gametype1. Mode4 target ordering and other
// commands remain separate; Install rejects unsupported commands in its group.
class TriggerProgram
{
public:
    bool Install(const TriggerRuleTable& table, const TriggerVolumes& volumes,
        int group, ConfigError& error);
    void BeginMap();
    void ClearUsedTriggers();
    // Called exactly once when the manager starts a complete client-slot scan,
    // including when that cycle's first slot is empty, dead or otherwise skipped.
    void BeginPlayerScanCycle() { arrivals_ = 0; }
    TriggerResult CheckPlayer(const std::array<float, 3>& origin, float elapsed,
        int aliveCtCount, TriggerActions& actions);
    TriggerResult ExecuteTrigger(int id, float elapsed, int aliveCtCount, TriggerActions& actions);
    std::span<const std::int16_t> UsedTriggers() const { return used_; }
    std::uint32_t Arrivals() const { return arrivals_; }
    bool IsExecuting() const { return executing_; }

private:
    struct State
    {
        TriggerVolume volume;
        TriggerRule rule;
        float nextTime{};
        float ratio{};
    };
    bool Execute(State& state, float elapsed, bool spatial, TriggerActions& actions,
        bool& changed);
    bool CountGoal(const State& state, int aliveCtCount, TriggerActions& actions);
    std::map<std::int16_t, State> states_;
    std::vector<std::int16_t> order_;
    std::vector<std::int16_t> matches_;
    std::vector<std::int16_t> used_;
    std::uint32_t arrivals_{};
    bool executing_{};
};
}
