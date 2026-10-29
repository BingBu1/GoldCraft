// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_round.hpp"

#include <utility>

namespace csol::scenario
{
bool TriggerRound::BeginMap()
{
    if (starting_ || waves_.IsDispatching() || triggers_.IsExecuting()) return false;
    waves_ = {};
    triggers_ = {};
    schedule_ = {};
    group_.reset();
    return true;
}

bool TriggerRound::Begin(const WaveTable& table, const TriggerRuleTable& rules,
    const TriggerVolumes& volumes, const RoundRow& round, float now, float freezeTime,
    SpawnPoints& points, TriggerWaveEvents& events, ConfigError& error, bool* committed)
{
    error = {};
    if (committed) *committed = false;
    if (!events.Check(error)) return false;
    if (starting_ || waves_.IsDispatching() || triggers_.IsExecuting())
    {
        error.message = "cannot begin a scenario round during dispatch";
        return false;
    }
    if (round.waves.empty())
    {
        error.message = "triggered round requires a trigger group in its first waveTypes value";
        return false;
    }
    const auto group = TriggerKey(round.waves.front());
    std::vector<std::uint16_t> waveIds;
    if (const auto found = rules.groups.find(group); found != rules.groups.end())
    {
        for (const auto index : found->second)
        {
            if (index >= rules.rows.size())
            {
                error.message = "invalid triggered-round rule index";
                return false;
            }
            const auto& rule = rules.rows[index];
            if (rule.command == 'w') waveIds.insert(waveIds.end(), rule.waves.begin(), rule.waves.end());
        }
    }

    // Validate candidate data before changing the running session. This avoids
    // partial map-state corruption from invalid local configurations; it does
    // not claim original malformed-input behavior is transactional.
    auto schedule = schedule_;
    if (!BeginWaveRound(schedule, now, freezeTime))
    {
        error.message = "invalid scenario round time origin";
        return false;
    }
    WaveController waves;
    if (!waves.Prepare(table, waveIds, error)) return false;
    auto triggers = triggers_;
    triggers.ClearUsedTriggers();
    if (!triggers.Install(rules, volumes, group, error)) return false;

    struct StartScope
    {
        bool& starting;
        explicit StartScope(bool& value) : starting(value) { starting = true; }
        ~StartScope() { starting = false; }
    } scope(starting_);
    schedule_ = schedule;
    waves_ = std::move(waves);
    triggers_ = std::move(triggers);
    group_ = group;
    if (committed) *committed = true;
    // MP uses active[0] temporarily to transfer the TRG group. It clears that
    // temporary value before this notification; do not expose it as a wave.
    events.WavesChanged(waves_.ActiveWaves());
    if (!events.Check(error)) return false;
    points.ResetMonsterQuotas();
    // Retain nextThink, elapsed and scan-cycle arrivals across ordinary rounds.
    // The separate player scanner retains its cursor until BeginMap.
    return true;
}
}
