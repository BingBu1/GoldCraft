// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_program.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace csol::scenario
{
namespace
{
struct ExecutionScope
{
    bool& executing;
    explicit ExecutionScope(bool& value) : executing(value) { executing = true; }
    ~ExecutionScope() { executing = false; }
};
}

bool TriggerProgram::Install(const TriggerRuleTable& table, const TriggerVolumes& volumes,
    int group, ConfigError& error)
{
    error = {};
    if (executing_)
    {
        error.message = "cannot install trigger rules during command execution";
        return false;
    }
    // Installing a group clears its spatial index, not the runtime records.
    // A record from an earlier group can still be executed explicitly by ID.
    auto states = states_;
    std::vector<std::int16_t> order;
    const auto found = table.groups.find(TriggerKey(group));
    if (found != table.groups.end())
    {
        for (const auto index : found->second)
        {
            if (index >= table.rows.size())
            {
                error.message = "invalid trigger rule source index";
                return false;
            }
            const auto& rule = table.rows[index];
            if (rule.command != 'w' && rule.command != 'g')
            {
                if (rule.recordIndex < table.source.records.size())
                    error.byteOffset = table.source.records[rule.recordIndex].byteOffset;
                error.message = "unsupported scenario trigger command";
                return false;
            }
            State state;
            if (const auto old = states.find(rule.trigger); old != states.end())
                state.volume = old->second.volume;
            else if (const auto* volume = volumes.Find(rule.trigger)) state.volume = *volume;
            else state.volume.id = rule.trigger; // Original missing key creates a zero box.
            state.rule = rule;
            state.ratio = static_cast<float>(rule.destinationRatio) / 100.0f;
            states[rule.trigger] = std::move(state);
            if (std::find(used_.begin(), used_.end(), rule.trigger) == used_.end())
                order.push_back(rule.trigger);
        }
    }
    // Original cells use head insertion. Duplicate IDs keep multiple nodes
    // pointing at the one final overwritten state. Linear scanning preserves
    // that order and tests the exact box; it is bounded by configured rule rows.
    std::reverse(order.begin(), order.end());
    matches_.reserve(order.size());
    states_ = std::move(states);
    order_ = std::move(order);
    return true;
}

bool TriggerProgram::CountGoal(const State& state, int aliveCtCount, TriggerActions& actions)
{
    if (!actions.Current()) return false;
    ++arrivals_;
    const auto count = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(aliveCtCount));
    if (static_cast<float>(std::bit_cast<std::int32_t>(arrivals_)) <
        static_cast<float>(count) * state.ratio) return false;
    actions.GoalReached();
    return actions.Current();
}

void TriggerProgram::BeginMap()
{
    if (executing_) return;
    states_.clear();
    order_.clear();
    matches_.clear();
    used_.clear();
    arrivals_ = 0;
}

void TriggerProgram::ClearUsedTriggers()
{
    if (!executing_) used_.clear();
}

bool TriggerProgram::Execute(State& state, float elapsed, bool spatial,
    TriggerActions& actions, bool& changed)
{
    auto& rule = state.rule;
    if (!actions.Current()) return false;
    if (rule.cooldown == -1 || state.nextTime > elapsed) return false;
    state.nextTime = elapsed + static_cast<float>(rule.cooldown);
    for (const auto wave : rule.waves)
    {
        actions.ActivateWave(wave, elapsed);
        if (!actions.Current()) return false;
    }
    changed |= !rule.waves.empty();
    for (const auto wave : rule.endWaves)
    {
        changed |= actions.EndWave(wave, spatial);
        if (!actions.Current()) return false;
    }
    if (rule.cooldown == 0)
    {
        if (used_.size() < 30) used_.push_back(rule.trigger);
        rule.cooldown = -1; // A full used-ID list does not make this repeatable.
    }
    return true;
}

TriggerResult TriggerProgram::CheckPlayer(const std::array<float, 3>& origin, float elapsed,
    int aliveCtCount, TriggerActions& actions)
{
    if (executing_ || !std::isfinite(elapsed) || !actions.Current()) return {};
    ExecutionScope scope(executing_);
    // Collect matches before dispatch, as MP does. No map entity or client
    // pointer survives here; the game adapter owns player/gate validation.
    matches_.clear();
    for (const auto id : order_)
        if (states_.at(id).volume.Contains(origin)) matches_.push_back(id);
    TriggerResult result{true, 0, false};
    bool changed = false;
    for (const auto id : matches_)
    {
        if (!actions.Current()) { result.valid = false; return result; }
        auto& state = states_.at(id);
        if (state.rule.command == 'w')
        {
            if (Execute(state, elapsed, true, actions, changed)) ++result.waveCommands;
        }
        else
        {
            if (CountGoal(state, aliveCtCount, actions))
            {
                result.goalReached = true;
                break;
            }
        }
        if (!actions.Current()) { result.valid = false; return result; }
    }
    if (changed) actions.RefreshWaves();
    result.valid = actions.Current();
    return result;
}

TriggerResult TriggerProgram::ExecuteTrigger(int id, float elapsed, int aliveCtCount,
    TriggerActions& actions)
{
    if (executing_ || !std::isfinite(elapsed) || !actions.Current()) return {};
    const auto found = states_.find(TriggerKey(id));
    if (found == states_.end()) return {true, 0, false};
    ExecutionScope scope(executing_);
    if (found->second.rule.command == 'g')
    {
        const bool reached = CountGoal(found->second, aliveCtCount, actions);
        return {actions.Current(), 0, reached};
    }
    bool changed = false;
    const bool executed = Execute(found->second, elapsed, false, actions, changed);
    if (!actions.Current()) return {};
    if (changed) actions.RefreshWaves();
    return {actions.Current(), executed ? 1u : 0u, false};
}
}
