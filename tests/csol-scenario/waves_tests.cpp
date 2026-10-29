// SPDX-License-Identifier: GPL-3.0-or-later
// Original row-clock boundaries are explicit cases, independent of production
// arithmetic. Link the actual library; original-instruction comparison uses stdin.
#include "waves.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>

using namespace csol::scenario;

namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}

// Protocol actions: wait-group=0, continue-without-attempt=1, attempt=2.
struct ClockCase
{
    int next, end, interval;
    float elapsed;
    int action, expectedNext;
    const char* label;
};

const std::vector<ClockCase> ClockCases{
    {-1, 1, 5, 20.0f, 1, -1, "finished rows remain finished"},
    {0, 0, 5, -0.1f, 0, 0, "future row waits before any end check"},
    {0, 170, 5, 0.0f, 2, 5, "start boundary attempts and advances"},
    {0, 170, 5, 1.0f, 2, 5, "exact one-second tolerance still attempts"},
    {0, 170, 5, std::nextafter(1.0f, 2.0f), 1, 5, "past tolerance advances without catch-up attempt"},
    {0, 170, 5, 5.0f, 1, 5, "late frame can advance to exactly elapsed without attempting"},
    {0, 170, 5, 5.125f, 1, 10, "late frame advances beyond elapsed when between intervals"},
    {5, 170, 5, 5.0f, 2, 10, "next call at exact advanced time attempts"},
    {5, 170, 5, 6.0f, 2, 10, "nonzero start includes tolerance endpoint"},
    {5, 170, 5, std::nextafter(6.0f, 7.0f), 1, 10, "nonzero start excludes next float after tolerance"},
    {5, 5, 5, 5.0f, 2, -1, "end boundary is applied after attempt"},
    {10, 5, 5, 6.0f, 0, 10, "future row does not expire merely because end has passed"},
    {10, 5, 5, 10.0f, 2, -1, "due row attempts even when end predates start"},
    {5, 6, 5, 6.0f, 2, -1, "end within tolerance still permits final attempt"},
    {5, 6, 5, std::nextafter(6.0f, 7.0f), 1, -1, "end expires missed row without attempt"},
    {5, 0, 0, 5.0f, 2, -1, "zero interval is a single attempt"},
    {5, 0, 0, 6.0f, 2, -1, "zero interval accepts tolerance endpoint"},
    {5, 0, 0, std::nextafter(6.0f, 7.0f), 1, -1, "missed one-shot is discarded"},
    {0, 0, 32767, 100.0f, 1, 32767, "maximum loaded interval advances without attempt"},
    {100, 0, 2, 10000.0f, 1, 10000, "large ordinary lateness does not replay missed attempts"},
};

bool SameClock(const RowClock& a, const RowClock& b)
{
    return a.next == b.next && a.end == b.end && a.interval == b.interval;
}

WaveRow Row(int wave, int point, int start, int end, int interval, std::size_t record)
{
    WaveRow row;
    row.waveId = wave; row.spawnPoint = point; row.startTime = start;
    row.endTime = end; row.interval = static_cast<std::int16_t>(interval);
    row.recordIndex = record; row.types = {40}; row.weights = {1}; row.weightTotal = 1;
    return row;
}

void ClockTests()
{
    for (const auto& value : ClockCases)
    {
        RowClock clock{value.next, value.end, static_cast<std::int16_t>(value.interval)};
        const auto step = AdvanceRow(clock, value.elapsed);
        if (step != RowStep::WaitForPoint && step != RowStep::Invalid) FinishRow(clock, value.elapsed);
        Check(static_cast<int>(step) == value.action && clock.next == value.expectedNext, value.label);
        Check(clock.end == value.end && clock.interval == value.interval, "clock advance retains end and interval");
    }
    const auto original = Row(1, 0, 3, 17, 2, 1);
    RowClock active{15, 25, 5}; ResetRunningRow(active, original);
    Check(SameClock(active, {3, 17, 2}), "reset restores all active original clock fields");
    RowClock finished{-1, 25, 5}; ResetRunningRow(finished, original);
    Check(SameClock(finished, {-1, 25, 5}), "reset does not touch any ended row field");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
    {
        RowClock clock{0, 170, 5}; const auto saved = clock;
        Check(AdvanceRow(clock, invalid) == RowStep::Invalid && SameClock(clock, saved), "nonfinite row time rejected without mutation");
    }
    RowClock negative{0, 170, -1};
    Check(AdvanceRow(negative, 0) == RowStep::Invalid && SameClock(negative, {0, 170, -1}), "negative interval rejected without mutation");
    RowClock maximum{std::numeric_limits<int>::max(), 0, 1};
    Check(AdvanceRow(maximum, static_cast<float>(std::numeric_limits<int>::max())) == RowStep::Continue && maximum.next == std::numeric_limits<int>::max(), "LEA wrap at INT_MAX does not start catchup when elapsed equals float next");
    RowClock overflow{2147483600, 0, 32767}; const auto saved = overflow;
    Check(AdvanceRow(overflow, 2147483904.0f) == RowStep::Invalid && SameClock(overflow, saved), "unexecutable wrapping catchup rejected without mutation");
    RowClock precision{16777216, 0, 1};
    Check(AdvanceRow(precision, 16777218.0f) == RowStep::Continue && precision.next == 16777218, "catchup compares float32 schedule values above 2^24");
    RowClock rounded{16777216, 0, 1};
    Check(AdvanceRow(rounded, 16777220.0f) == RowStep::Continue && rounded.next == 16777219, "catchup chooses first integer that rounds to elapsed");
    RowClock wrap{2147483520, 0, 32767};
    Check(AdvanceRow(wrap, 2147483520.0f) == RowStep::Dispatch && wrap.next == -2147451009, "due schedule increment preserves original uint32 wrap");
}

struct Capture
{
    std::vector<std::size_t> records;
    std::vector<int> nextAtCall;
    bool changeEnd{};
};
void Record(void* context, const WaveRow& row, RowClock& clock)
{
    auto& capture = *static_cast<Capture*>(context);
    capture.records.push_back(row.recordIndex);
    capture.nextAtCall.push_back(clock.next);
    if (capture.changeEnd) { clock.next = 99; clock.end = -2; }
}

struct Reenter
{
    WaveRunner* runner;
    const WaveTable* replacement;
    bool loadRejected{}, resetRejected{}, tickRejected{};
};
void Reentrant(void* context, const WaveRow&, RowClock&)
{
    auto& state = *static_cast<Reenter*>(context); ConfigError error;
    state.loadRejected = !state.runner->Load(*state.replacement, 8, error) && !error.message.empty();
    state.resetRejected = !state.runner->ResetRunningRows();
    state.tickRejected = !state.runner->Tick(0, Reentrant, context).valid;
}
void Throwing(void*, const WaveRow&, RowClock&) { throw std::runtime_error("test callback"); }

void RunnerTests()
{
    WaveRunner runner; Capture capture; ConfigError error;
    Check(!runner.Tick(0, Record, &capture).valid && !runner.ResetRunningRows(), "unloaded runner rejects tick and reset");
    WaveTable table;
    table.rows = {Row(7, 8, 10, 0, 0, 10), Row(7, 1, 0, 0, 0, 11),
                  Row(7, 8, 0, 0, 0, 12), Row(7, 3, 0, 5, 5, 13), Row(8, 0, 0, 0, 0, 14)};
    Check(runner.Load(table, 7, error) && runner.WaveId() == 7 && runner.Rows().size() == 4, "runner loads only requested wave");
    Check(runner.Rows()[0].recordIndex == 10 && runner.Rows()[2].recordIndex == 12, "runner retains original row order and provenance");
    auto tick = runner.Tick(0, Record, &capture);
    Check(tick.valid && !tick.finished && tick.attempts == 2, "future point does not prevent other points from running");
    Check(capture.records == std::vector<std::size_t>{11, 13}, "future row skips all later rows of its point");
    Check(capture.nextAtCall == std::vector<int>{-1, 5}, "callback sees clock already advanced");
    Check(runner.Clocks()[2].next == 0, "blocked later row retains original clock");
    capture = {}; tick = runner.Tick(10, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 1 && capture.records == std::vector<std::size_t>{10}, "late one-shot and elapsed repeating row finish without catch-up calls");
    Check(runner.ResetRunningRows(), "finished program accepts reset operation");
    Check(runner.Clocks()[0].next == -1 && runner.Clocks()[2].next == -1, "reset does not revive finished program rows");
    capture = {}; tick = runner.Tick(10, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 0 && capture.records.empty(), "finished wave has no additional attempts");

    table.rows[0].startTime = 0;
    Check(runner.Load(table, 7, error), "explicit load starts a fresh wave program");
    capture = {}; tick = runner.Tick(0, Record, &capture);
    Check(capture.records == std::vector<std::size_t>{10, 12, 11, 13}, "point order is first seen; rows retain order within group");
    Check(tick.attempts == 4, "duplicate rows each dispatch independently");
    const auto before = std::vector<RowClock>(runner.Clocks().begin(), runner.Clocks().end());
    Check(!runner.Load(table, 999, error) && runner.WaveId() == 7 && !error.message.empty(), "missing wave load rejected transactionally");
    Check(runner.Clocks().size() == before.size() && std::equal(before.begin(), before.end(), runner.Clocks().begin(), SameClock), "failed load retains active clocks");
    auto malformed = table; malformed.rows[2].types.clear();
    Check(!runner.Load(malformed, 7, error) && std::equal(before.begin(), before.end(), runner.Clocks().begin(), SameClock), "invalid later row does not partially replace loaded program");
    malformed = table; malformed.rows[2].interval = -1;
    Check(!runner.Load(malformed, 7, error), "negative interval cannot enter runner through manually made table");
    Check(!runner.Tick(1, nullptr, &capture).valid, "null callback rejected");
    Check(!runner.Tick(std::numeric_limits<float>::quiet_NaN(), Record, &capture).valid &&
          !runner.Tick(std::numeric_limits<float>::infinity(), Record, &capture).valid, "runner rejects nonfinite time before mutation");
    Check(std::equal(before.begin(), before.end(), runner.Clocks().begin(), SameClock), "invalid tick inputs retain clocks");

    WaveTable single; single.rows = {Row(1, 0, 0, 0, 5, 100)};
    Check(runner.Load(single, 1, error), "callback mutation fixture loads");
    capture = {}; capture.changeEnd = true; tick = runner.Tick(0, Record, &capture);
    Check(capture.nextAtCall == std::vector<int>{5} && runner.Clocks()[0].next == -1 && tick.finished, "end evaluated after callback using callback-modified fields");
    Check(runner.Load(single, 1, error), "unsuccessful external creation fixture loads");
    capture = {}; tick = runner.Tick(0, Record, &capture);
    capture = {}; tick = runner.Tick(0, Record, &capture);
    Check(tick.valid && tick.attempts == 0 && runner.Clocks()[0].next == 5, "handler without created entity does not roll back attempt clock");
    Check(runner.Load(single, 1, error), "reentrant fixture loads");
    Reenter reenter{&runner, &table}; tick = runner.Tick(0, Reentrant, &reenter);
    Check(tick.valid && tick.attempts == 1 && reenter.loadRejected && reenter.resetRejected && reenter.tickRejected, "dispatch rejects nested load reset and tick");
    Check(runner.WaveId() == 1 && runner.Clocks()[0].next == 5 && runner.ResetRunningRows(), "reentrant rejection leaves owner usable");
    try { runner.Tick(0, Throwing, nullptr); Check(false, "throwing callback must propagate"); }
    catch (const std::runtime_error&) { Check(true, "callback exception propagated"); }
    Check(runner.ResetRunningRows() && runner.Clocks()[0].next == 0, "tick guard clears on callback exception");

    WaveTable events;
    events.rows = {Row(5, 9999, 4, 17, 3, 200), Row(5, 3, 4, 17, 3, 201)};
    Check(runner.Load(events, 5, error) && runner.Clocks()[0].next == 6 && runner.Clocks()[1].next == 4, "ordinary event command shifts next by two only");
    Check(runner.Clocks()[0].end == 17 && runner.Clocks()[0].interval == 3 && runner.Rows()[0].startTime == 4, "event shift preserves end interval and original row");
    Check(runner.ResetRunningRows() && runner.Clocks()[0].next == 4 && runner.ResetRunningRows() && runner.Clocks()[0].next == 4, "active reset restores original start without reapplying event delay");
    events.rows[0].types = {9999};
    Check(runner.Load(events, 5, error) && runner.Clocks()[0].next == 4, "event type sentinel 9999 is not delayed");
}

void RunnerCancellation()
{
    WaveRunner runner; ConfigError error; WaveTable table;
    table.rows = {Row(7, 1, 3, 3, 5, 10), Row(7, 1, 3, 3, 7, 11), Row(7, 2, 3, 3, 9, 12)};
    Check(runner.Load(table, 7, error), "owner-cancel runner has due rows in two real point groups");
    struct State { bool current{true}; std::vector<std::size_t> records; int advanced{}; } state;
    const auto current = [](const void* value) { return static_cast<const State*>(value)->current; };
    const auto dispatch = [](void* value, const WaveRow& row, RowClock& clock)
    {
        auto& capture = *static_cast<State*>(value);
        capture.records.push_back(row.recordIndex); capture.advanced = clock.next; capture.current = false;
    };
    const auto result = runner.Tick(3, dispatch, &state, current, &state);
    Check(!result.valid && !result.finished && result.attempts == 1 && state.records == std::vector<std::size_t>{10} &&
        state.advanced == 8, "first dispatch revokes owner only after actual row advancement and counts its one attempt");
    Check(runner.Clocks()[0].next == 8 && runner.Clocks()[0].end == 3 &&
        runner.Clocks()[1].next == 3 && runner.Clocks()[2].next == 3,
        "cancelled runner skips FinishRow at its reached end and leaves later same/other-point rows untouched");
    const auto again = runner.Tick(3, dispatch, &state, current, &state);
    Check(!again.valid && again.attempts == 0 && state.records.size() == 1 && runner.Clocks()[0].next == 8,
        "already-cancelled runner entry performs no clock or callback work");
    Check(runner.ResetRunningRows() && runner.Clocks()[0].next == 3,
        "cancellation releases the dispatch guard for explicit lifecycle cleanup");
}

void SelectionTests()
{
    auto row = Row(1, 1, 0, 0, 5, 1); row.types = {40, 6, 7}; row.weights = {2, 0, 1}; row.weightTotal = 3;
    const int expected[] = {40, 40, 7, 40, 40, 7};
    for (int value = 0; value < 6; ++value)
    {
        int type = -123; Check(SelectMonsterType(row, value, type) && type == expected[value], "one weighted type selected with modulo and zero-weight skip");
    }
    int type = -123;
    Check(!SelectMonsterType(row, -1, type) && type == -123, "invalid random input preserves output");
    row.weightTotal = 0; Check(!SelectMonsterType(row, 0, type) && type == -123, "zero total rejected without division or output mutation");
    row.weightTotal = -128; Check(!SelectMonsterType(row, 0, type), "negative wrapped total rejected");
    row.weightTotal = 3; row.weights.pop_back(); Check(!SelectMonsterType(row, 0, type), "mismatched weight list rejected");
    row.weights = {-1, 3, 1}; row.weightTotal = 3;
    type = 0; Check(SelectMonsterType(row, 0, type) && type == 6, "signed negative weight affects cumulative selection");
    type = 0; Check(SelectMonsterType(row, 2, type) && type == 7, "signed weights preserve final interval boundary");
    row.weights = {0, 0, 0}; row.weightTotal = 3; type = -123;
    Check(!SelectMonsterType(row, 1, type) && type == -123, "unreachable malformed selection returns without overrun");
}

void ActualWaveTests(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("actual wave input unavailable");
    const std::string bytes(std::istreambuf_iterator<char>(file), {});
    WaveTable table; ConfigError error;
    Check(ReadWaves(bytes, table, error) && table.rows.size() == 218, "actual decoded wave table imported");
    std::size_t totalCalls{};
    for (int id : {2, 3, 19})
    {
        WaveRunner runner; Check(runner.Load(table, id, error) && runner.Rows().size() == 8, "actual first-round present wave loads eight rows");
        Capture capture;
        auto tick = runner.Tick(0, Record, &capture);
        Check(tick.valid && !tick.finished && tick.attempts == 8 && capture.records.size() == 8, "actual first-round present wave attempts each row at zero");
        totalCalls += tick.attempts;
        for (int next : capture.nextAtCall) Check(next == 5, "actual callback sees five-second next time");
        capture = {}; tick = runner.Tick(1.25f, Record, &capture);
        Check(tick.valid && tick.attempts == 0 && capture.records.empty(), "actual rows do not repeat between scheduled times");
        tick = runner.Tick(20, Record, &capture);
        Check(tick.valid && tick.attempts == 0, "actual stalled wave skips missed calls");
        for (const auto& clock : runner.Clocks()) Check(clock.next == 20, "actual missed interval advances to current integer boundary");
        tick = runner.Tick(20, Record, &capture);
        Check(tick.valid && tick.attempts == 8, "actual subsequent tick at advanced boundary dispatches once per row");
        Check(runner.ResetRunningRows(), "actual active row reset succeeds");
        capture = {}; tick = runner.Tick(170, Record, &capture);
        Check(tick.valid && tick.finished && tick.attempts == 0, "actual late end tick ends without catch-up burst");
        Check(runner.ResetRunningRows(), "actual completed row reset call succeeds");
        for (const auto& clock : runner.Clocks()) Check(clock.next == -1, "actual completed rows do not revive after reset");
    }
    Check(totalCalls == 24, "24 present first-round rows dispatched to capture only, no monster creation");
    WaveRunner duplicate; Check(duplicate.Load(table, 1, error), "actual duplicate-point wave loads");
    std::size_t duplicates{};
    for (const auto& row : duplicate.Rows()) if (row.spawnPoint == 13) ++duplicates;
    Check(duplicates == 2, "actual duplicate point 13 retained in runnable program");
    for (int absent : {820, 821, 831, 832, 833, 841})
        Check(!duplicate.Load(table, absent, error) && duplicate.WaveId() == 1, "actual missing wave cannot silently replace active program");
}

int Oracle()
{
    std::string command;
    while (std::cin >> command)
    {
        int next, end, interval;
        if (!(std::cin >> next >> end >> interval)) return 2;
        RowClock clock{next, end, static_cast<std::int16_t>(interval)};
        if (command == "C")
        {
            std::uint32_t bits; if (!(std::cin >> bits)) return 2;
            const float elapsed = std::bit_cast<float>(bits);
            const auto action = AdvanceRow(clock, elapsed);
            if (action != RowStep::WaitForPoint && action != RowStep::Invalid) FinishRow(clock, elapsed);
            std::cout << static_cast<int>(action) << ' ' << clock.next << '\n';
        }
        else if (command == "R")
        {
            int start, originalEnd, originalInterval;
            if (!(std::cin >> start >> originalEnd >> originalInterval)) return 2;
            ResetRunningRow(clock, Row(0, 0, start, originalEnd, originalInterval, 0));
            std::cout << clock.next << ' ' << clock.end << ' ' << clock.interval << '\n';
        }
        else return 2;
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
        if (argc == 3 && std::string_view(argv[1]) == "--actual-wave") ActualWaveTests(argv[2]);
        else if (argc == 1) { ClockTests(); RunnerTests(); RunnerCancellation(); SelectionTests(); }
        else throw std::runtime_error("usage: waves_tests [--oracle | --actual-wave decoded-file]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "wave checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
