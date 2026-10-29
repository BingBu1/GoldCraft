// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include "frame_fixture.hpp"

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}
bool Active(const FrameFixture& fixture, std::initializer_list<std::uint16_t> expected)
{
    const auto actual = fixture.map.Round().Waves().ActiveWaves();
    return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}
void Gates()
{
    {
        FrameFixture fixture(false);
        Check(!fixture.Frame(0, 0).valid && fixture.engine.slots.empty(), "empty owner rejects frame without client lookup");
        Check(fixture.map.BeginMap("map_session_test"), "resource-only session begins");
        Check(!fixture.Frame(0, 0).valid, "resource-only owner rejects frame");
        Check(fixture.map.CollectEntities() && !fixture.Frame(0, 0).valid, "collected owner still requires formal round");
        Check(fixture.map.BeginRound(0, 0, 0, fixture.events), "formal round enables frame");
        auto* saved = gpGlobals; gpGlobals = nullptr;
        Check(!fixture.map.UpdateFrame(fixture.rules, 0, 4, fixture.Dispatch, &fixture, fixture.events).valid,
            "missing engine globals rejects frame"); gpGlobals = saved;
    }
    for (int gate = 0; gate < 3; ++gate)
    {
        FrameFixture fixture; fixture.Activate();
        fixture.map.Round().Schedule().elapsed = 17;
        if (gate == 0) fixture.rules.Started(false);
        if (gate == 1) fixture.rules.m_bFreezePeriod = TRUE;
        if (gate == 2) fixture.rules.m_bRoundTerminating = true;
        const auto frame = fixture.Frame(0, 10);
        Check(frame.valid && !frame.waves.valid && !frame.triggers.valid, "real GameRules gate is a valid no-op");
        Check(!fixture.dispatches && fixture.engine.slots.empty() && fixture.map.PlayerScan().NextPlayerSlot() == 1,
            "blocked rules neither dispatch nor advance player cursor");
        Check(fixture.map.Round().Schedule().elapsed == 17 && fixture.map.Round().Schedule().nextThink == 0 && Active(fixture, {7}),
            "rules gate preserves clocks and active wave");
        fixture.rules.Started(true); fixture.rules.m_bFreezePeriod = FALSE; fixture.rules.m_bRoundTerminating = false;
        Check(fixture.Frame(0, 10).valid && fixture.dispatches == 1 && fixture.engine.slots == std::vector<int>{1},
            "opening rules gate resumes same pending wave and slot");
    }
    {
        FrameFixture fixture; fixture.Activate(); fixture.map.Round().Schedule().start = 10;
        fixture.map.Round().Schedule().elapsed = 7;
        const auto frame = fixture.Frame(9, 20);
        Check(frame.valid && frame.schedule == ScheduleStep::Frozen, "negative Think elapsed freezes frame");
        Check(!fixture.dispatches && fixture.engine.slots.empty() && fixture.map.Round().Schedule().elapsed == 7,
            "negative elapsed preserves prior elapsed and scanner cursor");
    }
}
void SchedulingAndTriggers()
{
    FrameFixture fixture;
    // Slot 1 is empty; slot 2 is inside the one-shot wave trigger. The wave
    // update occurs before the scanner, so a spatial activation waits one frame.
    fixture.engine.Client(2, Vector(0, 0, 0));
    const auto empty = fixture.Frame(0, 10);
    Check(empty.valid && empty.schedule == ScheduleStep::Dispatch && empty.waves.valid && empty.waves.finished,
        "empty active list still receives a valid scheduled tick");
    Check(fixture.map.Round().Schedule().nextThink == 10.5f && fixture.engine.slots == std::vector<int>{1},
        "empty tick advances half-second deadline and scans empty first slot");
    const auto waiting = fixture.Frame(0.1f, 10.1f);
    Check(waiting.valid && waiting.schedule == ScheduleStep::Waiting && !waiting.waves.valid && waiting.triggers.waveCommands == 1,
        "half-second wait still scans one live player and activates trigger");
    Check(!fixture.dispatches && Active(fixture, {7}) && fixture.map.Round().Schedule().nextThink == 0,
        "spatial activation wakes next tick without dispatching in same scan");
    Check(fixture.engine.slots == std::vector<int>({1, 2}) && fixture.events.tags == std::vector<char>({'a', 's'}),
        "trigger activation and summary occur after one exact slot lookup");
    const auto awake = fixture.Frame(0.2f, 10.2f);
    Check(awake.valid && awake.schedule == ScheduleStep::Dispatch && awake.waves.attempts == 1 && fixture.dispatches == 1,
        "trigger wake bypasses previous deadline on next frame");
    Check(fixture.dispatchClocks.front().next == 3 && fixture.map.Round().Schedule().nextThink == 10.7f,
        "row advances before callback and new deadline follows actual engine sample");
    const auto boundary = fixture.Frame(0.3f, 10.7f);
    Check(boundary.valid && boundary.schedule == ScheduleStep::Dispatch && fixture.dispatches == 1,
        "equal deadline ticks once but future row does not dispatch early");
    Check(fixture.engine.slots == std::vector<int>({1, 2, 3, 4}) && fixture.map.PlayerScan().NextPlayerSlot() == 1,
        "one slot per active frame wraps after maxClients");
    Check(fixture.Frame(0.4f, 10.8f).valid && fixture.map.PlayerScan().NextPlayerSlot() == 2,
        "waiting frame starts next scanner cycle");
    Check(fixture.Frame(0.5f, 10.9f).triggers.waveCommands == 0 && Active(fixture, {7}),
        "used once-only spatial rule cannot reactivate on next scan cycle");

}
void GoalSnapshot()
{
    FrameFixture fixture;
    fixture.engine.Client(1, Vector(100, 0, 0));
    fixture.engine.Client(2, Vector(100, 0, 0));
    const auto first = fixture.Frame(0, 0, 4);
    Check(first.valid && !first.triggers.goalReached && fixture.map.Round().Triggers().Arrivals() == 1,
        "50-percent goal uses alive-CT snapshot four, not current slot one");
    const auto second = fixture.Frame(0.1f, 0.1f, 4);
    Check(second.valid && second.triggers.goalReached && fixture.map.Round().Triggers().Arrivals() == 2,
        "second matching slot reaches two-of-four threshold while wave gate waits");
    Check(std::count(fixture.events.tags.begin(), fixture.events.tags.end(), 'g') == 1,
        "one goal notification occurs at supplied count threshold");
}
void CallbackTimeAndFailure()
{
    {
        FrameFixture fixture; fixture.Activate();
        fixture.onDispatch = [&](const WaveRow&, RowClock&) { fixture.engine.globals.time = 42; };
        const auto frame = fixture.Frame(0, 10);
        Check(frame.valid && fixture.dispatches == 1 && fixture.map.Round().Schedule().nextThink == 42.5f,
            "callback clock change is sampled after wave update");
        Check(fixture.map.Round().Schedule().elapsed == 0 && fixture.engine.slots == std::vector<int>{1},
            "changed engine clock does not replace Think elapsed");
    }
    for (int gate = 0; gate < 3; ++gate)
    {
        FrameFixture fixture; fixture.Activate();
        fixture.onDispatch = [&](const WaveRow&, RowClock&)
        {
            if (gate == 0) fixture.rules.Started(false);
            if (gate == 1) fixture.rules.m_bFreezePeriod = TRUE;
            if (gate == 2) fixture.rules.m_bRoundTerminating = true;
        };
        const auto frame = fixture.Frame(0, 10);
        Check(frame.valid && fixture.dispatches == 1 && fixture.map.Round().Schedule().nextThink == 10.5f,
            "callback closing rules gate retains completed wave side effects and deadline");
        Check(fixture.engine.slots.empty() && fixture.map.PlayerScan().NextPlayerSlot() == 1,
            "callback closing started/freeze/termination gate suppresses subsequent scan");
        Check(fixture.map.WaveSummary().size() == 1 && fixture.map.WaveSummary()[0].id == 7 && fixture.events.tags.empty(),
            "successful tick refreshes local summary after callback gate change without extra observer");
    }
    {
        FrameFixture fixture; fixture.Activate();
        fixture.onDispatch = [&](const WaveRow&, RowClock&) { fixture.engine.globals.time = std::numeric_limits<float>::quiet_NaN(); };
        const auto frame = fixture.Frame(0, 10);
        Check(!frame.valid && frame.waves.attempts == 1 && fixture.map.State() == CSOLScenarioMapState::Failed,
            "invalid post-dispatch engine clock stops map");
        Check(fixture.dispatches == 1 && fixture.map.Round().Waves().FindWave(7)->Clocks()[0].next == 3 && fixture.engine.slots.empty(),
            "post-dispatch clock failure does not roll back row advancement and skips scanner");
        Check(!fixture.Frame(3, 11).valid && fixture.dispatches == 1, "failed map cannot resume by receiving a valid time");
    }
    {
        FrameFixture fixture; fixture.Activate(); fixture.engine.globals.maxClients = 0;
        const auto frame = fixture.Frame(0, 10);
        Check(!frame.valid && fixture.map.State() == CSOLScenarioMapState::Failed && fixture.dispatches == 1,
            "invalid scanner capacity fails after successful wave dispatch");
        Check(fixture.map.Round().Schedule().nextThink == 10.5f && fixture.engine.slots.empty(),
            "scanner failure preserves earlier wave deadline without out-of-range lookup");
    }
    for (int kind = 0; kind < 4; ++kind)
    {
        FrameFixture fixture; fixture.Activate();
        float think = 0, engine = 10;
        if (kind == 0) think = std::numeric_limits<float>::infinity();
        if (kind == 1) engine = std::numeric_limits<float>::quiet_NaN();
        if (kind == 2) fixture.map.Round().Schedule().start = std::numeric_limits<float>::infinity();
        if (kind == 3) fixture.map.Round().Schedule().nextThink = std::numeric_limits<float>::infinity();
        const auto frame = fixture.Frame(think, engine);
        Check(!frame.valid && fixture.map.State() == CSOLScenarioMapState::Failed && !fixture.map.Error().detail.message.empty(),
            "invalid frame time fails map with diagnostic");
        Check(!fixture.dispatches && fixture.engine.slots.empty(), "invalid schedule cannot dispatch or scan");
    }
    {
        FrameFixture fixture;
        const auto frame = fixture.map.UpdateFrame(fixture.rules, 0, 4, nullptr, &fixture, fixture.events);
        Check(!frame.valid && fixture.map.State() == CSOLScenarioMapState::Failed && fixture.engine.slots.empty(),
            "missing wave dispatcher stops map without scanner side effects");
    }
    {
        FrameFixture fixture; fixture.Activate();
        fixture.onDispatch = [](const WaveRow&, RowClock& clock) { clock.interval = -1; };
        Check(fixture.Frame(0, 0).valid && fixture.dispatches == 1, "callback can modify row state after accepted attempt");
        const auto frame = fixture.Frame(3, 1);
        Check(!frame.valid && !frame.waves.valid && fixture.map.State() == CSOLScenarioMapState::Failed,
            "invalid runtime row fails next wave update");
        Check(fixture.dispatches == 1 && fixture.engine.slots == std::vector<int>{1},
            "invalid wave keeps earlier attempt and does not scan another client");
    }
    {
        FrameFixture fixture; fixture.engine.Client(1, Vector(0, 0, 0));
        // Finite elapsed can still exceed cvttss2si's supported trigger-offset range.
        const auto frame = fixture.Frame(2147483648.0f, 1);
        Check(!frame.valid && !frame.triggers.valid && frame.triggers.waveCommands == 0 &&
            fixture.map.State() == CSOLScenarioMapState::Failed, "failed trigger activation is surfaced as an invalid scan");
        Check(Active(fixture, {}) && fixture.events.tags.empty() && fixture.map.PlayerScan().NextPlayerSlot() == 1,
            "failed adapter emits no activation and does not advance the visited player slot");
        Check(fixture.map.Round().Triggers().UsedTriggers().empty() && !fixture.map.Error().detail.message.empty(),
            "failed trigger stops before used commit and reports schedule error");
    }
}
void OwnerCancellation()
{
    for (bool diagnostic : {false, true})
    {
        FrameFixture fixture; fixture.Activate();
        fixture.onDispatch = [&](const WaveRow&, RowClock& clock)
        {
            clock.end = -2; // FinishRow would overwrite the advanced next to -1.
            fixture.events.observerValid = false;
            if (diagnostic) fixture.events.observerError = {5, "frame row owner failed"};
        };
        const auto frame = fixture.Frame(0, 10);
        Check(!frame.valid && !frame.waves.valid && frame.waves.attempts == 1 && fixture.dispatches == 1 &&
            fixture.map.Round().Waves().FindWave(7)->Clocks()[0].next == 3,
            "real Map frame cancels after first dispatch and skips row end finalization");
        Check(fixture.map.Round().Schedule().nextThink == 0 && fixture.map.WaveSummary().empty() &&
            fixture.engine.slots.empty() && fixture.map.PlayerScan().NextPlayerSlot() == 1,
            "cancelled row skips manager deadline local summary and player scan suffix");
        Check(fixture.map.State() == (diagnostic ? CSOLScenarioMapState::Failed : CSOLScenarioMapState::RoundActive) &&
            fixture.map.Error().detail.message == (diagnostic ? "frame row owner failed" : ""),
            "frame distinguishes diagnosed row failure from owner cancellation");
    }
    for (bool diagnostic : {false, true})
    {
        FrameFixture fixture; fixture.engine.Client(1, Vector(0, 0, 0));
        fixture.events.callback = [&](char kind)
        {
            if (kind != 'a') return;
            fixture.events.observerValid = false;
            if (diagnostic) fixture.events.observerError = {6, "frame trigger owner failed"};
        };
        const auto frame = fixture.Frame(0, 10);
        Check(!frame.valid && !frame.triggers.valid && fixture.map.PlayerScan().NextPlayerSlot() == 1 &&
            fixture.engine.slots == std::vector<int>{1} && fixture.events.tags == std::vector<char>{'a'},
            "real Map trigger callback cancellation neither advances cursor nor sends summary");
        Check(Active(fixture, {7}) && fixture.map.Round().Triggers().UsedTriggers().empty() && fixture.map.WaveSummary().empty(),
            "real Map keeps completed activation prefix but does not commit used state or local summary");
    }
    for (bool terminating : {false, true})
    {
        FrameFixture fixture; WaveTable table; ConfigError error;
        WaveRow first; first.waveId = 7; first.spawnPoint = 1; first.startTime = 0;
        first.endTime = 0; first.interval = 3; first.types = {1}; first.weights = {1}; first.weightTotal = 1;
        table.rows = {first, first}; const std::array<std::uint16_t, 1> ids{7};
        Check(fixture.map.Round().Waves().Activate(table, ids, error), "gate distinction fixture owns two due actual rows");
        fixture.onDispatch = [&](const WaveRow&, RowClock& clock)
        {
            clock.end = -2;
            if (terminating) fixture.rules.m_bRoundTerminating = true;
            else Check(fixture.map.StopWaveDispatch(), "row raises original manager wave-stop flag");
        };
        const auto frame = fixture.Frame(0, 10);
        Check(frame.valid && frame.waves.valid && frame.waves.finished && frame.waves.attempts == 2 && fixture.dispatches == 2 &&
            fixture.map.Round().Waves().FindWave(7)->Clocks()[0].next == -1 &&
            fixture.map.Round().Waves().FindWave(7)->Clocks()[1].next == -1,
            "ordinary wave-stop or termination during callback preserves current traversal and FinishRow behavior");
        Check(fixture.map.Round().Schedule().nextThink == 10.5f &&
            fixture.map.PlayerScan().NextPlayerSlot() == (terminating ? 1 : 2),
            "ordinary gate change still advances manager deadline and only termination suppresses subsequent scan");
    }
}
void SummaryCache()
{
    {
        FrameFixture fixture; fixture.Activate();
        Check(fixture.map.WaveSummary().empty(), "direct controller activation does not pretend an owner notification happened");
        const auto queries = fixture.engine.dedicatedQueries;
        Check(fixture.Frame(0, 0).valid && fixture.engine.dedicatedQueries == queries + 1,
            "normal tick tail samples dedicated state exactly once");
        auto summary = fixture.map.WaveSummary();
        Check(summary.size() == 1 && summary[0].id == 7 && summary[0].offset == 0 && fixture.events.tags.empty(),
            "tick local cache contains active ID and runtime offset without observer event");
        Check(fixture.map.Round().Waves().EndWave(7), "remove active wave between scheduled ticks");
        Check(fixture.Frame(0.1f, 0.1f).valid && fixture.map.WaveSummary().size() == 1 && fixture.engine.dedicatedQueries == queries + 1,
            "half-second waiting does not refresh a stale local cache");
        Check(fixture.Frame(0.5f, 0.5f).valid && fixture.map.WaveSummary().empty() && fixture.events.tags.empty(),
            "empty scheduled tick resets valid prefix without synthetic WavesChanged event");
    }
    {
        FrameFixture fixture; ConfigError error;
        const std::array<std::uint16_t, 2> ids{7, 30};
        Check(fixture.map.Round().Waves().Prepare(fixture.map.Tables()->waves, ids, error), "prepare two actual fixture wave programs for summary ordering");
        fixture.Activate(7, 1); fixture.Activate(30, 4); fixture.Activate(7, 2.5f);
        Check(fixture.Frame(0, 0).valid && fixture.dispatches == 0, "future wave rows remain pending at summary refresh");
        const auto summary = fixture.map.WaveSummary();
        Check(summary.size() == 3 && summary[0].id == 7 && summary[1].id == 30 && summary[2].id == 7,
            "summary preserves active order and duplicate IDs");
        Check(summary[0].offset == 2.5f && summary[1].offset == 4 && summary[2].offset == 2.5f,
            "duplicate entries read the latest shared runtime offset");
        bool notified = false;
        fixture.events.callback = [&](char tag)
        {
            if (tag == 's')
            {
                notified = true;
                Check(fixture.map.WaveSummary().empty() && fixture.map.RoundIndex() == 1,
                    "formal stage observer sees refreshed empty summary and new owner phase");
            }
        };
        Check(fixture.map.BeginRound(1, 1, 0, fixture.events) && notified, "formal round routes summary through owner wrapper");
    }
    {
        FrameFixture fixture;
        for (int i = 0; i < 32; ++i) fixture.Activate(7, 2.25f);
        Check(fixture.Frame(0, 0).valid && Active(fixture, {}) == false, "more than thirty duplicate waves remain active");
        auto summary = fixture.map.WaveSummary();
        Check(summary.size() == 30 && std::all_of(summary.begin(), summary.end(), [](const auto& row) { return row.id == 7 && row.offset == 2.25f; }),
            "local summary limits visible prefix to thirty without deduplicating");
        for (int i = 0; i < 31; ++i) Check(fixture.map.Round().Waves().EndWave(7), "remove exactly first active duplicate");
        Check(fixture.Frame(0, 0.5f).valid && fixture.map.WaveSummary().size() == 1,
            "shorter cache exposes one valid element and hides previous tail");
    }
    for (int mode = 0; mode < 2; ++mode)
    {
        FrameFixture fixture; fixture.Activate(); fixture.Frame(0, 0);
        Check(fixture.map.WaveSummary().size() == 1 && fixture.map.WaveSummary()[0].offset == 0, "seed local cache before dedicated or absent API gate");
        if (mode == 0) fixture.engine.dedicated = true;
        else g_engfuncs.pfnIsDedicatedServer = nullptr;
        Check(fixture.map.Round().Waves().EndWave(7), "remove previously cached activation"); fixture.Activate(7, 3);
        const auto frame = fixture.Frame(3, 0.5f);
        Check(frame.valid && frame.waves.attempts == 1 && fixture.dispatches == 2 && fixture.engine.slots == std::vector<int>({1, 2}),
            "dedicated or absent summary API never suppresses wave dispatch and player scan");
        Check(fixture.map.WaveSummary().size() == 1 && fixture.map.WaveSummary()[0].offset == 0,
            "dedicated or absent API skips all summary writes including count");
        Check(fixture.events.tags.empty(), "dedicated tick adds no synthetic observer notification");
        Check(fixture.map.EndMap() && fixture.map.WaveSummary().empty(), "EndMap clears valid summary prefix even if local refresh is disabled");
    }
    {
        FrameFixture fixture; fixture.engine.Client(1, Vector(0, 0, 0));
        std::vector<std::size_t> counts;
        fixture.events.callback = [&](char tag)
        {
            counts.push_back(fixture.map.WaveSummary().size());
            if (tag == 's')
                Check(fixture.map.WaveSummary()[0].id == 7 && fixture.map.WaveSummary()[0].offset == 0.25f,
                    "spatial summary observer sees new ID and exact trigger activation time");
        };
        Check(fixture.Frame(0.25f, 0).valid && fixture.events.tags == std::vector<char>({'a', 's'}) && counts == std::vector<std::size_t>({0, 1}),
            "only original spatial WavesChanged callback refreshes cache before forwarding observer");
    }
}
void ReentryAndReset()
{
    FrameFixture fixture; fixture.Activate();
    auto deny = [&]
    {
        const auto state = fixture.map.State(); const auto slot = fixture.map.PlayerScan().NextPlayerSlot();
        const auto index = fixture.map.RoundIndex();
        Check(!fixture.map.UpdateFrame(fixture.rules, 0, 4, fixture.Dispatch, &fixture, fixture.events).valid,
            "nested frame rejected while owner operation is active");
        Check(!fixture.map.EndMap() && !fixture.map.BeginMap("map_session_test") && !fixture.map.CollectEntities() &&
            !fixture.map.BeginRound(1, 0, 0, fixture.events), "frame callbacks cannot mutate owner lifecycle");
        Check(fixture.map.State() == state && fixture.map.PlayerScan().NextPlayerSlot() == slot && fixture.map.RoundIndex() == index,
            "rejected reentry preserves caller state cursor and phase");
    };
    fixture.onDispatch = [&](const WaveRow&, RowClock&) { deny(); };
    fixture.engine.onIndex = deny;
    Check(fixture.Frame(0, 0).valid && fixture.dispatches == 1 && fixture.engine.slots == std::vector<int>{1},
        "outer dispatch and scanner finish after rejected nested operations");
    fixture.onDispatch = {}; fixture.engine.onIndex = {};
    fixture.engine.Client(2, Vector(0, 0, 0)); fixture.events.callback = [&](char) { deny(); };
    Check(fixture.Frame(0.1f, 0.1f).valid, "spatial activation observers are also protected by owner frame guard");
    fixture.events.callback = {};
    Check(fixture.map.BeginRound(1, 10, 0, fixture.events) && fixture.map.PlayerScan().NextPlayerSlot() == 3,
        "formal next stage retains scan cursor");
    fixture.engine.ClearWorld(); fixture.engine.Markers(); fixture.Start();
    Check(fixture.map.PlayerScan().NextPlayerSlot() == 1 && fixture.map.Round().Schedule().elapsed == 0 &&
        fixture.map.Round().Schedule().nextThink == 0 && Active(fixture, {}), "new map resets scanner active waves and manager clocks");
    Check(fixture.Frame(0, 0).valid && fixture.engine.slots == std::vector<int>{1},
        "new map executes first slot with no former stage state");
}
}
int main()
{
    try
    {
        Gates(); SchedulingAndTriggers(); GoalSnapshot(); CallbackTimeAndFailure(); OwnerCancellation(); SummaryCache(); ReentryAndReset();
    }
    catch (const std::exception& error) { std::cerr << "EXCEPTION: " << error.what() << '\n'; return 2; }
    std::cout << "scenario_frame checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
