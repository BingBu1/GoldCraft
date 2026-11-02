// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/golden_runtime.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
void Require(bool value, const char* label)
{
    Check(value, label);
    if (!value) throw std::runtime_error(label);
}
template<class T> std::vector<T> Copy(std::span<const T> values)
{ return {values.begin(), values.end()}; }

TriggerRule Rule(int group, int trigger, int cooldown, std::vector<std::uint16_t> waves = {})
{
    TriggerRule rule;
    rule.group = TriggerKey(group); rule.trigger = TriggerKey(trigger);
    rule.command = 'w'; rule.cooldown = static_cast<std::int16_t>(cooldown);
    rule.waves = std::move(waves);
    return rule;
}
TriggerRuleTable Rules(std::initializer_list<TriggerRule> rows)
{
    TriggerRuleTable result;
    for (auto row : rows)
    {
        result.groups[row.group].push_back(result.rows.size());
        result.rows.push_back(std::move(row));
    }
    return result;
}
TriggerVolumes Boxes(std::initializer_list<int> ids)
{
    TriggerVolumes result;
    for (const int id : ids) result.Register({{-10, -10, -10}, {10, 10, 10}, id, 0});
    return result;
}
WaveRow Row(int wave, int point = 1, int start = 0, int end = 0, int interval = 0)
{
    WaveRow row;
    row.waveId = wave; row.spawnPoint = point; row.startTime = start; row.endTime = end;
    row.interval = static_cast<std::int16_t>(interval); row.types = {1};
    row.weights = {1}; row.weightTotal = 1;
    return row;
}
WaveTable Waves(std::initializer_list<WaveRow> rows)
{
    WaveTable result;
    result.rows.assign(rows);
    return result;
}

// Only trigger notifications are captured here. Production TriggerProgram owns
// cooldown mutation; separate cases drive the actual controller and row ticks.
struct Actions : TriggerActions
{
    std::vector<std::uint16_t> activated;
    int ended{}, refreshed{}, goals{};
    bool Current() const override { return true; }
    void ActivateWave(std::uint16_t id, float) override { activated.push_back(id); }
    bool EndWave(std::uint16_t, bool) override { ++ended; return false; }
    void RefreshWaves() override { ++refreshed; }
    void GoalReached() override { ++goals; }
};

void UnknownExplicitRecords()
{
    TriggerVolumes volumes;
    volumes.Register({{-30, -10, -10}, {-20, 10, 10}, 40, 1});
    volumes.Register({{10, 10, 10}, {20, 20, 20}, 80, 2});
    const auto rules = Rules({Rule(1, 40, 3, {10}), Rule(2, 80, 0, {20})});
    TriggerProgram triggers; WaveController waves; ConfigError error; Actions actions;
    const auto noActions = [&]
    { return actions.activated.empty() && actions.ended == 0 && actions.refreshed == 0 && actions.goals == 0; };
    Require(triggers.Install(rules, volumes, 1, error), "install known spatial record before explicit missing-ID calls");
    const std::uint16_t preload[]{10};
    Require(waves.Prepare(Waves({Row(10), Row(90)}), preload, error), "initialize wave runtime without global source wave 90");
    GoldenWaveStatus status;
    Require(ReadGoldenWaveStatus(90, triggers, volumes, waves, status, error), "query truly absent trigger ID before explicit call");
    Check(!status.triggerCooldown && !waves.FindWave(90), "missing trigger ID initially has neither metadata nor runtime container");
    const auto first = triggers.ExecuteTrigger(90, 0, 1, actions);
    Check(first.valid && first.waveCommands == 0 && !first.goalReached && triggers.RuntimeCooldown(90) == 0,
        "explicit missing-ID call creates a default zero-cooldown record without executing a command");
    Check(triggers.UsedTriggers().empty() && triggers.Arrivals() == 0 && noActions(),
        "default record creation does not disable the trigger, count a goal or notify wave observers");
    Require(ReadGoldenWaveStatus(90, triggers, volumes, waves, status, error), "Golden observes the newly created default record");
    Check(status.triggerCooldown == 0 && !status.runtimeHasPendingRow && waves.FindWave(90) &&
        waves.FindWave(90)->Rows().empty(), "default metadata makes the later Golden lookup create only Empty runtime");
    Check(waves.ActiveWaves().empty() && waves.MissingWaves().empty(),
        "default-record Golden query leaves activation and source-missing diagnostics unchanged");
    const auto repeat = triggers.ExecuteTrigger(65626, 100, 1, actions);
    Check(repeat.valid && repeat.waveCommands == 0 && !repeat.goalReached && triggers.RuntimeCooldown(90) == 0 &&
        triggers.UsedTriggers().empty() && noActions(), "repeated low16 alias call remains a no-op with cooldown zero");

    const auto geometryOnly = triggers.ExecuteTrigger(80, 0, 1, actions);
    Check(geometryOnly.valid && geometryOnly.waveCommands == 0 && triggers.RuntimeCooldown(80) == 0,
        "geometry-only ID can acquire an unbound default runtime record");
    const auto beforeBinding = triggers.CheckPlayer({15, 15, 15}, 0, 1, actions);
    Check(beforeBinding.valid && beforeBinding.waveCommands == 0 && !beforeBinding.goalReached &&
        triggers.Arrivals() == 0 && triggers.UsedTriggers().empty() && noActions(),
        "unbound default record does not join spatial execution or alter known records");
    Require(triggers.Install(rules, volumes, 2, error), "bind a real rule after explicit default record creation");
    const auto afterBinding = triggers.CheckPlayer({15, 15, 15}, 0, 1, actions);
    Check(afterBinding.valid && afterBinding.waveCommands == 1 && !afterBinding.goalReached &&
        actions.activated == std::vector<std::uint16_t>{20} && actions.refreshed == 1 &&
        actions.ended == 0 && actions.goals == 0, "later rule binding uses the real geometry box instead of stale zero bounds");
    Check(triggers.RuntimeCooldown(80) == -1 && Copy(triggers.UsedTriggers()) == std::vector<std::int16_t>{80},
        "only the newly bound one-shot records its real trigger ID and disables itself");
    triggers.BeginMap(); actions = Actions{};
    const auto afterReset = triggers.CheckPlayer({15, 15, 15}, 100, 1, actions);
    Check(!triggers.RuntimeCooldown(80) && !triggers.RuntimeCooldown(90) && triggers.UsedTriggers().empty() &&
        afterReset.valid && afterReset.waveCommands == 0 && noActions(),
        "BeginMap removes both explicit defaults and later bound spatial records");
}

void TriggerIdentityAndLifecycle()
{
    const auto rules = Rules({Rule(1, 10, 0, {20}), Rule(2, 30, 5, {40}),
        Rule(3, 10, 7, {20}), Rule(4, 10, 2), Rule(4, 10, 9)});
    const auto volumes = Boxes({10});
    TriggerProgram triggers; WaveController waves; ConfigError error; Actions actions;
    const std::uint16_t preload[]{20};
    Require(waves.Prepare(Waves({Row(20)}), preload, error), "preload referenced wave 20");
    Require(triggers.Install(rules, volumes, 1, error), "install one-shot trigger 10 targeting wave 20");
    const auto executed = triggers.ExecuteTrigger(10, 0, 1, actions);
    Require(executed.valid && executed.waveCommands == 1, "real trigger execution consumes one-shot cooldown");
    Check(actions.activated == std::vector<std::uint16_t>{20} && triggers.RuntimeCooldown(10) == -1,
        "trigger 10 activates wave 20 and stores its own exhausted state");
    Check(triggers.RuntimeCooldown(65546) == -1 && !triggers.RuntimeCooldown(20),
        "runtime cooldown uses low16 trigger identity without reverse wave lookup");
    GoldenWaveStatus status;
    Require(ReadGoldenWaveStatus(20, triggers, volumes, waves, status, error), "read referenced wave 20 status");
    Check(status.wave == 20 && !status.triggerCooldown && status.runtimeHasPendingRow,
        "trigger 10 exhaustion cannot become wave 20 metadata");
    Require(ReadGoldenWaveStatus(10, triggers, volumes, waves, status, error), "read actual same-ID trigger status");
    Check(status.triggerCooldown == -1 && !status.runtimeHasPendingRow && waves.FindWave(10) &&
        waves.FindWave(10)->Rows().empty(), "same-ID exhausted record creates only an empty runtime");

    Require(triggers.Install(rules, volumes, 2, error), "install a different trigger group");
    Require(ReadGoldenWaveStatus(10, triggers, volumes, waves, status, error), "read prior group record");
    Check(status.triggerCooldown == -1, "prior group exhaustion survives installing another group");
    triggers.ClearUsedTriggers();
    Check(triggers.UsedTriggers().empty() && triggers.RuntimeCooldown(10) == -1,
        "clearing used-ID diagnostics does not reset runtime cooldown");
    Require(triggers.Install(rules, volumes, 3, error), "reinstall the same trigger with a new rule");
    Require(ReadGoldenWaveStatus(10, triggers, volumes, waves, status, error), "read overwritten runtime cooldown");
    Check(status.triggerCooldown == 7, "reinstallation overwrites exhausted cooldown with the new rule value");
    Require(triggers.Install(rules, volumes, 4, error), "install duplicate trigger records");
    Check(triggers.RuntimeCooldown(10) == 9, "last rule for the same trigger overwrites the runtime record");
    triggers.BeginMap();
    Check(!triggers.RuntimeCooldown(10), "new map removes retained trigger runtime records");
    Require(ReadGoldenWaveStatus(10, triggers, volumes, waves, status, error), "read geometry after runtime reset");
    Check(status.triggerCooldown == 0, "geometry-only record returns its original zero cooldown");
}

void PendingClocks()
{
    TriggerProgram triggers; const auto volumes = Boxes({20, 21, 22, 23});
    WaveController waves; ConfigError error;
    const std::uint16_t preload[]{20, 21, 22, 23};
    Require(waves.Prepare(Waves({Row(20), Row(21, 9999), Row(22, 1, -1),
        Row(22, 2, 7), Row(23, 1, 100)}), preload, error), "preload ordinary, event and mixed row clocks");
    Check(waves.ActiveWaves().empty(), "preloaded runtime has no active wave IDs");
    GoldenWaveStatus status;
    Require(ReadGoldenWaveStatus(20, triggers, volumes, waves, status, error), "query preloaded ordinary row");
    Check(status.triggerCooldown == 0 && status.runtimeHasPendingRow && waves.ActiveWaves().empty(),
        "unactivated next zero is pending without adding an active ID");
    Require(ReadGoldenWaveStatus(21, triggers, volumes, waves, status, error), "query preloaded event row");
    Check(waves.FindWave(21)->Rows()[0].spawnPoint == 9999 && waves.FindWave(21)->Clocks()[0].next == 2 &&
        status.runtimeHasPendingRow, "event point 9999 is pending according to its delayed next clock");
    Require(ReadGoldenWaveStatus(22, triggers, volumes, waves, status, error), "query mixed finished and pending rows");
    Check(waves.FindWave(22)->Clocks()[0].next == -1 && waves.FindWave(22)->Clocks()[1].next == 7 &&
        status.runtimeHasPendingRow, "a later point's pending row survives an earlier finished row");
    Require(ReadGoldenWaveStatus(23, triggers, volumes, waves, status, error), "query future row clock");
    Check(status.runtimeHasPendingRow, "future scheduled rows count as pending before they are due");

    Require(waves.ActivateWave(20, 0, error), "activate one ordinary one-shot row");
    struct Context
    {
        TriggerProgram& triggers; const TriggerVolumes& volumes; WaveController& waves;
        int calls{};
    } context{triggers, volumes, waves};
    const auto dispatch = [](void* raw, const WaveRow& row, RowClock& clock)
    {
        auto& state = *static_cast<Context*>(raw); ++state.calls;
        GoldenWaveStatus current; ConfigError localError;
        Check(row.spawnPoint == 1 && clock.next == -1 && state.waves.IsDispatching(),
            "production tick advances ordinary one-shot clock to finished before dispatch");
        Check(Copy(state.waves.ActiveWaves()) == std::vector<std::uint16_t>{20},
            "wave remains on active list while its dispatch is running");
        Require(ReadGoldenWaveStatus(20, state.triggers, state.volumes, state.waves, current, localError),
            "query current wave from inside dispatch");
        Check(!current.runtimeHasPendingRow, "active wave with every next clock finished is not pending");
    };
    const auto tick = waves.Tick(0, dispatch, &context);
    Check(tick.valid && tick.finished && tick.attempts == 1 && context.calls == 1,
        "ordinary one-shot completes through real controller and runner tick");
    Require(ReadGoldenWaveStatus(20, triggers, volumes, waves, status, error), "read completed cached runtime");
    Check(!status.runtimeHasPendingRow && waves.FindWave(20)->Rows()[0].spawnPoint == 1 &&
        waves.FindWave(20)->Clocks()[0].next == -1, "retained ordinary spawn point cannot revive an ended clock");
}

void EmptyRuntimeAndFailure()
{
    TriggerProgram triggers; const auto volumes = Boxes({10, 90, 65591});
    WaveController waves; ConfigError error;
    const std::uint16_t preload[]{10, 77};
    Require(waves.Prepare(Waves({Row(10, 1, 3, 30, 2), Row(90, 1, 99), Row(91)}), preload, error),
        "preload a timed wave plus an explicitly missing source ID");
    Require(waves.ActivateWave(10, 10.25f, error), "start existing runtime with a fractional offset");
    const auto* original = waves.FindWave(10);
    GoldenWaveStatus status;
    Require(ReadGoldenWaveStatus(90, triggers, volumes, waves, status, error), "geometry-only lookup inserts absent runtime");
    Require(waves.FindWave(90) != nullptr, "geometry-only runtime is present after lookup");
    Check(status.triggerCooldown == 0 && !status.runtimeHasPendingRow && waves.FindWave(90)->Rows().empty() &&
        waves.FindWave(90)->Clocks().empty() && waves.FindWave(90)->StartOffset() == 0,
        "lookup inserts Empty without importing wave 90 from the global source table");
    Check(Copy(waves.ActiveWaves()) == std::vector<std::uint16_t>{10} &&
        Copy(waves.MissingWaves()) == std::vector<std::uint16_t>{77},
        "empty insertion changes neither active IDs nor missing-source diagnostics");
    Require(waves.EnsureWaveRuntime(10, error), "ensure already-present runtime");
    Require(waves.FindWave(10) == original, "ensuring another or existing key preserves runner identity");
    Check(original->StartOffset() == 10.25f && original->Clocks()[0].next == 13 &&
        original->Clocks()[0].end == 40 && original->Clocks()[0].interval == 2,
        "lookup preserves existing offset, next, end and interval");
    Require(ReadGoldenWaveStatus(91, triggers, volumes, waves, status, error), "query absent trigger and geometry record");
    Check(!status.triggerCooldown && !status.runtimeHasPendingRow && !waves.FindWave(91),
        "known absence is nullopt and does not create a runtime despite global source data");
    Require(ReadGoldenWaveStatus(55, triggers, volumes, waves, status, error), "geometry lookup accepts low16 ID alias");
    Check(status.wave == 55 && status.triggerCooldown == 0 && waves.FindWave(55),
        "geometry ID 65591 resolves to the same low16 key 55");

    WaveController uninitialized;
    status = {444, -7, true};
    Check(!ReadGoldenWaveStatus(90, triggers, volumes, uninitialized, status, error) && !error.message.empty(),
        "metadata requiring runtime access rejects an uninitialized controller");
    Check(status.wave == 444 && status.triggerCooldown == -7 && status.runtimeHasPendingRow &&
        !uninitialized.FindWave(90) && uninitialized.ActiveWaves().empty() && uninitialized.MissingWaves().empty(),
        "failed snapshot preserves caller output and uninitialized controller state");
    Check(!uninitialized.EnsureWaveRuntime(10, error) && !uninitialized.FindWave(10),
        "direct ensure also rejects insertion before controller initialization");
}

void SignedTriggerAlias()
{
    TriggerProgram triggers; const auto volumes = Boxes({65535}); WaveController waves; ConfigError error;
    Require(triggers.Install(Rules({Rule(1, -1, 9)}), volumes, 1, error), "install signed trigger key minus one");
    const std::uint16_t preload[]{65535};
    Require(waves.Prepare(Waves({Row(-1)}), preload, error), "preload signed source wave alias");
    Check(triggers.RuntimeCooldown(-1) == 9 && triggers.RuntimeCooldown(65535) == 9 &&
        triggers.RuntimeCooldown(131071) == 9, "all low16 aliases observe the same signed trigger record");
    GoldenWaveStatus status;
    Require(ReadGoldenWaveStatus(65535, triggers, volumes, waves, status, error), "read unsigned wave key against signed trigger key");
    Check(status.wave == 65535 && status.triggerCooldown == 9 && status.runtimeHasPendingRow,
        "high-bit trigger runtime overrides geometry zero and matches the preloaded clock");
}

void InsertDuringDispatch()
{
    TriggerProgram triggers; const auto volumes = Boxes({10, 20, 30, 40});
    WaveController waves; ConfigError error;
    const std::uint16_t active[]{10, 20};
    Require(waves.Activate(Waves({Row(10, 1), Row(10, 2), Row(20, 1)}), active, error),
        "activate two waves and three independent point rows");
    struct Context
    {
        TriggerProgram& triggers; const TriggerVolumes& volumes; WaveController& waves;
        const WaveRunner* first{};
        std::vector<std::pair<int, int>> calls;
    } context{triggers, volumes, waves, waves.FindWave(10), {}};
    const auto dispatch = [](void* raw, const WaveRow& row, RowClock& clock)
    {
        auto& state = *static_cast<Context*>(raw);
        state.calls.emplace_back(row.waveId, row.spawnPoint);
        if (state.calls.size() != 1) return;
        GoldenWaveStatus status; ConfigError localError;
        Check(state.waves.IsDispatching() && clock.next == -1, "new-key lookup begins during a real row dispatch");
        Require(ReadGoldenWaveStatus(30, state.triggers, state.volumes, state.waves, status, localError),
            "first absent runtime can be inserted during dispatch");
        Check(status.triggerCooldown == 0 && !status.runtimeHasPendingRow, "new runtime contains no pending rows");
        Require(ReadGoldenWaveStatus(40, state.triggers, state.volumes, state.waves, status, localError),
            "second absent runtime can be inserted during the same dispatch");
        Check(Copy(state.waves.ActiveWaves()) == std::vector<std::uint16_t>{10, 20} &&
            state.waves.MissingWaves().empty(), "dispatch insertion does not alter current active iteration or diagnostics");
        Check(state.waves.FindWave(10) == state.first && clock.next == -1 &&
            state.first->Clocks()[1].next == 0, "map insertion retains current runner, row reference and later clock");
        Require(ReadGoldenWaveStatus(10, state.triggers, state.volumes, state.waves, status, localError),
            "current wave remains queryable after new-key insertions");
        Check(status.runtimeHasPendingRow, "later unvisited row remains pending during first row dispatch");
        Require(ReadGoldenWaveStatus(20, state.triggers, state.volumes, state.waves, status, localError),
            "next active wave remains queryable before its turn");
        Check(status.runtimeHasPendingRow && state.waves.FindWave(20)->Clocks()[0].next == 0,
            "reading the next wave does not restart or consume its clock");
    };
    const auto tick = waves.Tick(0, dispatch, &context);
    Check(tick.valid && tick.finished && tick.attempts == 3 &&
        context.calls == std::vector<std::pair<int, int>>{{10, 1}, {10, 2}, {20, 1}},
        "both original waves finish in order with no skipped, repeated or inserted-row dispatch");
    Require(waves.FindWave(30) && waves.FindWave(40), "both inserted empty runtimes survive dispatch");
    Check(waves.FindWave(30)->Rows().empty() && waves.FindWave(40)->Rows().empty() &&
        waves.ActiveWaves().empty() && waves.MissingWaves().empty(),
        "empty query runtimes stay cached without activation after original waves finish");
    Check(waves.FindWave(10)->Clocks()[0].next == -1 && waves.FindWave(10)->Clocks()[1].next == -1 &&
        waves.FindWave(20)->Clocks()[0].next == -1, "real row clocks finish normally after map insertion");
}
}

int main()
{
    try
    {
        UnknownExplicitRecords(); TriggerIdentityAndLifecycle(); PendingClocks(); EmptyRuntimeAndFailure();
        SignedTriggerAlias(); InsertDuringDispatch();
    }
    catch (const std::exception& exception) { ++failures; std::cerr << exception.what() << '\n'; }
    std::cout << "Golden runtime " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
