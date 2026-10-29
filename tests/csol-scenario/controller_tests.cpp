// SPDX-License-Identifier: GPL-3.0-or-later
// Link the production controller/schedule. Original arithmetic is compared by
// the independent x86 runner through the --oracle stdin protocol below.
#include "controller.hpp"
#include "schedule.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
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

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
bool Same(const WaveSchedule& a, const WaveSchedule& b)
{
    return Bits(a.start) == Bits(b.start) && Bits(a.elapsed) == Bits(b.elapsed) && Bits(a.nextThink) == Bits(b.nextThink);
}
template<class T> bool Equal(std::span<const T> a, const std::vector<T>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}
WaveRow Row(int wave, int point, int start, int end, int interval, std::size_t record)
{
    WaveRow result;
    result.waveId = wave; result.spawnPoint = point; result.startTime = start; result.endTime = end;
    result.interval = static_cast<std::int16_t>(interval); result.recordIndex = record;
    result.types = {40}; result.weights = {1}; result.weightTotal = 1;
    return result;
}

struct Capture
{
    std::vector<std::size_t> records;
    std::vector<int> waves;
    std::vector<int> next;
    int finishWave{-123};
    int finishOnCall{};
};
void Record(void* context, const WaveRow& row, RowClock& clock)
{
    auto& log = *static_cast<Capture*>(context);
    log.records.push_back(row.recordIndex); log.waves.push_back(row.waveId); log.next.push_back(clock.next);
    if (row.waveId == log.finishWave && std::count(log.waves.begin(), log.waves.end(), row.waveId) == log.finishOnCall)
        clock.next = -1;
}

void ScheduleTests()
{
    WaveSchedule clock{};
    Check(Same(clock, {}), "new session starts with zero clock fields");
    clock = {10, 8, 100};
    Check(BeginWaveRound(clock, 50, 15) && Same(clock, {65, 8, 100}), "new round sets time origin and preserves elapsed and nextThink");
    auto step = CheckWaveSchedule(clock, 64.5f, 200);
    Check(step == ScheduleStep::Frozen && Same(clock, {65, 8, 100}), "negative elapsed leaves saved elapsed and gate untouched");
    step = CheckWaveSchedule(clock, 65, std::nextafter(100.0f, 0.0f));
    Check(step == ScheduleStep::Waiting && Same(clock, {65, 0, 100}), "elapsed updates even while engine gate waits");
    step = CheckWaveSchedule(clock, 66.25f, 100);
    Check(step == ScheduleStep::Dispatch && Same(clock, {65, 1.25f, 100}), "equal engine gate dispatches with independent think time");
    Check(FinishWaveSchedule(clock, 101.125f) && Same(clock, {65, 1.25f, 101.625f}), "postcall gate uses fresh engine sample plus half second");
    Check(CheckWaveSchedule(clock, 70, 101.5f) == ScheduleStep::Waiting && clock.elapsed == 5, "postcall half-second gate waits while elapsed continues updating");
    Check(CheckWaveSchedule(clock, 70.25f, 101.625f) == ScheduleStep::Dispatch, "postcall exact gate equality dispatches");
    Check(FinishWaveSchedule(clock, 5000) && clock.nextThink == 5000.5f, "long engine stall schedules from now without replaying missed ticks");
    Check(BeginWaveRound(clock, 4000, 0) && clock.nextThink == 5000.5f && clock.elapsed == 5.25f, "round transition retains previous manager gate and elapsed");
    Check(CheckWaveSchedule(clock, 4000, 4000) == ScheduleStep::Waiting && clock.elapsed == 0, "new round waits inherited gate when engine time moved backwards");
    Check(BeginWaveRound(clock, -20, -5) && clock.start == -25, "finite negative origin and freeze fields preserve arithmetic");
    Check(CheckWaveSchedule(clock, -24, -3) == ScheduleStep::Waiting && clock.elapsed == 1, "finite negative clocks are allowed");
    clock = {0, 3, 0};
    Check(CheckWaveSchedule(clock, -0.0f, 0) == ScheduleStep::Dispatch && Bits(clock.elapsed) == 0x80000000u, "negative zero elapsed is not freeze and retains SSE sign");
    Check(BeginWaveRound(clock, 16777216.0f, 1) && clock.start == 16777216.0f, "round origin addition uses float32 rounding");
    Check(FinishWaveSchedule(clock, 16777216.0f) && clock.nextThink == 16777216.0f, "half-second addition follows float32 precision at large clocks");
    const auto saved = clock;
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
    {
        Check(!BeginWaveRound(clock, invalid, 1) && Same(clock, saved), "invalid round time rejected transactionally");
        Check(!BeginWaveRound(clock, 1, invalid) && Same(clock, saved), "invalid freeze time rejected transactionally");
        Check(CheckWaveSchedule(clock, invalid, 1) == ScheduleStep::Invalid && Same(clock, saved), "invalid think time rejected transactionally");
        Check(CheckWaveSchedule(clock, 1, invalid) == ScheduleStep::Invalid && Same(clock, saved), "invalid engine time rejected transactionally");
        Check(!FinishWaveSchedule(clock, invalid) && Same(clock, saved), "invalid postcall time rejected transactionally");
    }
    const auto max = std::numeric_limits<float>::max();
    Check(!BeginWaveRound(clock, max, max) && Same(clock, saved), "overflowing round origin rejected transactionally");
    clock = {-max, 4, 0}; const auto overflow = clock;
    Check(CheckWaveSchedule(clock, max, 1) == ScheduleStep::Invalid && Same(clock, overflow), "overflowing elapsed rejected transactionally");
    clock = {std::numeric_limits<float>::quiet_NaN(), 4, 0}; const auto badStart = clock;
    Check(CheckWaveSchedule(clock, 0, 0) == ScheduleStep::Invalid && Same(clock, badStart), "invalid saved start rejected transactionally");
    clock = {0, 4, std::numeric_limits<float>::infinity()}; const auto badGate = clock;
    Check(CheckWaveSchedule(clock, 0, 0) == ScheduleStep::Invalid && Same(clock, badGate), "invalid saved gate rejected transactionally");
}

struct Nested
{
    WaveController* controller;
    const WaveTable* table;
    bool activateRejected{}, tickRejected{};
};
void Reentrant(void* context, const WaveRow&, RowClock&)
{
    auto& state = *static_cast<Nested*>(context); ConfigError error;
    const std::uint16_t id[]{2};
    state.activateRejected = !state.controller->Activate(*state.table, id, error) && !error.message.empty();
    state.tickRejected = !state.controller->Tick(0, Reentrant, context).valid;
}
void Throwing(void*, const WaveRow&, RowClock&) { throw std::runtime_error("controller callback"); }

void ControllerCancellation()
{
    WaveController controller; ConfigError error; WaveTable table;
    table.rows = {Row(1, 1, 3, 3, 5, 10), Row(1, 1, 3, 3, 7, 11), Row(2, 2, 3, 3, 9, 20)};
    const std::vector<std::uint16_t> ids{1, 1, 2};
    Check(controller.Activate(table, ids, error), "owner-cancel controller retains duplicate first wave and a later wave");
    struct State { bool current{true}; int calls{}, advanced{}; } state;
    const auto current = [](const void* value) { return static_cast<const State*>(value)->current; };
    const auto dispatch = [](void* value, const WaveRow& row, RowClock& clock)
    {
        auto& capture = *static_cast<State*>(value); ++capture.calls; capture.advanced = clock.next;
        Check(row.recordIndex == 10, "cancelled controller invokes only the first actual row");
        capture.current = false;
    };
    const auto result = controller.Tick(3, dispatch, &state, current, &state);
    Check(!result.valid && !result.finished && result.attempts == 1 && state.calls == 1 && state.advanced == 8,
        "controller propagates true owner cancellation and retains its completed attempt count");
    Check(controller.FindWave(1)->Clocks()[0].next == 8 && controller.FindWave(1)->Clocks()[1].next == 3 &&
        controller.FindWave(2)->Clocks()[0].next == 3 && Equal(controller.ActiveWaves(), ids),
        "no FinishRow active-ID removal duplicate traversal or next-wave clock update occurs after cancellation");
    Check(!controller.IsDispatching() && !controller.Tick(3, dispatch, &state, current, &state).valid && state.calls == 1,
        "controller releases its dispatch guard but a retired owner still cannot enter again");
}

void ControllerTests()
{
    WaveController controller; ConfigError error; Capture capture;
    Check(!controller.Tick(0, Record, &capture).valid && !controller.FindWave(1), "unloaded controller rejects tick and has no program");
    Check(WaveKey(-1) == 65535 && WaveKey(65536) == 0 && WaveKey(131071) == 65535, "wave keys use identical low sixteen bits");
    WaveTable table;
    table.rows = {Row(1, 0, 0, 0, 1, 10), Row(2, 0, 2, 0, 5, 20), Row(3, 0, 1, 0, 0, 30)};
    const std::vector<std::uint16_t> ids{1, 2, 1, 3};
    Check(controller.Activate(table, ids, error) && Equal(controller.ActiveWaves(), ids), "activation retains order and duplicate wave IDs");
    Check(controller.MissingWaves().empty() && controller.FindWave(1), "present programs resolved");
    capture.finishWave = 1; capture.finishOnCall = 2;
    auto tick = controller.Tick(1, Record, &capture);
    Check(tick.valid && !tick.finished && tick.attempts == 3, "duplicate shared runner advances twice within same tolerance window");
    Check(capture.waves == std::vector<int>{1, 1, 3} && capture.next == std::vector<int>{1, 2, -1}, "duplicate entries share clock and later wave still dispatches");
    Check(Equal(controller.ActiveWaves(), std::vector<std::uint16_t>{2, 1}), "finishing later duplicate erases first match and keeps loop index");
    Check(controller.FindWave(1)->Clocks()[0].next == -1, "finished program retained with ended state");
    capture = {}; tick = controller.Tick(1, Record, &capture);
    Check(tick.valid && !tick.finished && tick.attempts == 0 && Equal(controller.ActiveWaves(), std::vector<std::uint16_t>{2}), "next tick removes duplicate left before iteration index");

    const std::vector<std::uint16_t> missingIds{404, 2, 404, 405};
    Check(controller.Activate(table, missingIds, error) && Equal(controller.ActiveWaves(), missingIds), "missing wave activation is not a round-level failure");
    Check(Equal(controller.MissingWaves(), std::vector<std::uint16_t>{404, 404, 405}) && controller.FindWave(404) && controller.FindWave(404)->Rows().empty() && controller.FindWave(404)->WaveId()==404, "missing diagnostics retain ordered uses with an identity-bearing empty runtime program");
    const auto oldPointer = controller.FindWave(2); const auto oldClock = oldPointer->Clocks()[0];
    Check(!controller.Tick(0, nullptr, &capture).valid && Equal(controller.ActiveWaves(), missingIds), "invalid null handler does not remove missing entries");
    Check(!controller.Tick(std::numeric_limits<float>::quiet_NaN(), Record, &capture).valid && Equal(controller.ActiveWaves(), missingIds), "nonfinite tick does not consume active list");
    WaveTable malformed = table; malformed.rows[1].types.clear();
    Check(!controller.Activate(malformed, ids, error) && !error.message.empty(), "malformed referenced existing program rejects activation");
    Check(controller.FindWave(2) == oldPointer && controller.FindWave(2)->Clocks()[0].next == oldClock.next &&
          Equal(controller.ActiveWaves(), missingIds) && Equal(controller.MissingWaves(), std::vector<std::uint16_t>{404, 404, 405}), "failed activation preserves all old program state and diagnostics");
    capture = {}; tick = controller.Tick(0, Record, &capture);
    Check(tick.valid && !tick.finished && tick.attempts == 0 && Equal(controller.ActiveWaves(), std::vector<std::uint16_t>{2}), "first valid tick removes all absent IDs and preserves future present wave");
    Check(Equal(controller.MissingWaves(), std::vector<std::uint16_t>{404, 404, 405}), "removed missing IDs remain in diagnostics");
    const std::vector<std::uint16_t> onlyMissing{8, 8, 9};
    Check(controller.Activate(table, onlyMissing, error), "all-missing activation succeeds as empty programs");
    tick = controller.Tick(0, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 0 && controller.ActiveWaves().empty(), "all-missing active list finishes on first valid tick");
    Check(controller.Activate(table, {}, error), "empty round list can activate");
    tick = controller.Tick(0, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 0, "empty round tick finishes without callback");

    WaveTable canonical;
    canonical.rows = {Row(-1, 9, 0, 0, 0, 100), Row(65535, 8, 0, 0, 0, 101),
                      Row(131071, 7, 0, 0, 0, 102), Row(65536, 6, 0, 0, 0, 103)};
    const std::vector<std::uint16_t> canonicalIds{65535, 0, 65535};
    Check(controller.Activate(canonical, canonicalIds, error) && controller.MissingWaves().empty(), "source and active keys canonicalize together");
    Check(controller.FindWave(65535)->Rows().size() == 3 && controller.FindWave(0)->Rows().size() == 1, "source aliases combine into one shared program");
    capture = {}; tick = controller.Tick(0, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 4 && capture.records == std::vector<std::size_t>{100, 101, 102, 103}, "canonical aliases dispatch each source row once and remove duplicate active entry");
    Check(capture.waves == std::vector<int>{-1, 65535, 131071, 65536} && canonical.rows[0].waveId == -1, "canonical lookup preserves original callback IDs and caller source table");

    const std::vector<std::uint16_t> one{1};
    Check(controller.Activate(table, one, error), "reentrancy fixture activates");
    Nested nested{&controller, &table}; tick = controller.Tick(0, Reentrant, &nested);
    Check(tick.valid && nested.activateRejected && nested.tickRejected && Equal(controller.ActiveWaves(), one), "callbacks reject nested activation and tick");
    try { controller.Tick(1, Throwing, nullptr); Check(false, "controller exception propagates"); }
    catch (const std::runtime_error&) { Check(true, "controller callback exception propagates"); }
    Check(controller.Activate(table, one, error) && controller.FindWave(1)->Clocks()[0].next == 0, "controller and inner runner guards clear after exception");
}

void QuotaTests()
{
    SpawnPoints points;
    SpawnPoint a{{1, 2, 3}, {4, 5, 6}, 1, 7, 40};
    SpawnPoint b{{7, 8, 9}, {10, 11, 12}, 2, 7, 41};
    points.RegisterMonsterPoint(a, 2); points.RegisterMonsterPoint(b, 0);
    points.AppendPlayerPoint(a); points.AppendPlayerPoint(b); points.AppendPlayerPoint(a);
    Check(points.ConsumePointQuota(1, 7) && points.ConsumePointQuota(1, 7) && !points.ConsumePointQuota(1, 7), "limited quota exhausted before reset");
    Check(points.ConsumePointQuota(2, 7) && points.FindMonsterPoint(2, 7)->consumed == 0, "unlimited point remains uncounted");
    const auto* first = points.FindMonsterPoint(1, 7); const auto* second = points.FindMonsterPoint(2, 7);
    const auto players = points.Players();
    points.ResetMonsterQuotas();
    Check(points.Monsters().size() == 2 && points.FindMonsterPoint(1, 7) == first && points.FindMonsterPoint(2, 7) == second, "quota reset preserves point records and references");
    Check(first->consumed == 0 && second->consumed == 0 && first->maxSpawn == 2 && second->maxSpawn == 0, "quota reset changes only counters");
    Check(first->point.origin == a.origin && first->point.angles == a.angles && first->point.entityIndex == 40 && second->point.origin == b.origin, "quota reset preserves locations angles and entity indices");
    bool samePlayers = players.size() == points.Players().size();
    for (const auto& [key, values] : players)
    {
        const auto& after = points.Players().at(key);
        samePlayers = samePlayers && values.size() == after.size();
        for (std::size_t i = 0; i < values.size(); ++i)
            samePlayers = samePlayers && values[i].origin == after[i].origin && values[i].angles == after[i].angles && values[i].team == after[i].team && values[i].id == after[i].id && values[i].entityIndex == after[i].entityIndex;
    }
    Check(samePlayers, "quota reset preserves every player point and duplicate");
    Check(points.ConsumePointQuota(1, 7) && first->consumed == 1, "quota can be consumed again after round reset");
    points.ResetMonsterQuotas(); points.ResetMonsterQuotas();
    Check(first->consumed == 0 && points.Monsters().size() == 2, "repeated quota reset is idempotent without clearing map points");
    points.Clear(); points.ResetMonsterQuotas();
    Check(points.Monsters().empty() && points.Players().empty(), "reset safely handles new empty map session");
}

std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read actual fixture");
    return {std::istreambuf_iterator<char>(file), {}};
}
void ActualTests(const char* wavePath, const char* roundPath)
{
    ConfigError error; WaveTable waves; RoundTable rounds;
    Check(ReadWaves(ReadFile(wavePath), waves, error) && waves.rows.size() == 218, "actual old 218-row input loads");
    Check(ReadRounds(ReadFile(roundPath), rounds, error) && rounds.rows.size() == 5, "actual five-round input loads");
    WaveController controller; std::size_t totalMissing{};
    for (const auto& round : rounds.rows)
    {
        std::vector<std::uint16_t> ids, expectedMissing, expectedRemaining;
        for (int id : round.waves)
        {
            ids.push_back(WaveKey(id));
            if (!waves.groups.contains(id)) expectedMissing.push_back(WaveKey(id));
            else expectedRemaining.push_back(WaveKey(id));
        }
        Check(controller.Activate(waves, ids, error) && Equal(controller.ActiveWaves(), ids), "actual round activation preserves supplied order despite missing IDs");
        Check(Equal(controller.MissingWaves(), expectedMissing), "actual missing-wave uses remain diagnosed");
        totalMissing += expectedMissing.size();
        Capture capture; const auto tick = controller.Tick(0, Record, &capture);
        Check(tick.valid && !tick.finished, "actual round starts present programs without declaring gameplay completion");
        Check(Equal(controller.ActiveWaves(), expectedRemaining), "actual first tick removes absent IDs only");
        Check(Equal(controller.MissingWaves(), expectedMissing), "actual removed absent IDs retain diagnostics");
        for (auto id : expectedMissing) Check(controller.FindWave(id) && controller.FindWave(id)->Rows().empty() && controller.FindWave(id)->WaveId()==id, "actual absent source retains empty runtime program with correct ID");
        if (round.round == 1)
        {
            Check(ids == std::vector<std::uint16_t>{2, 3, 19, 820, 821} && expectedMissing == std::vector<std::uint16_t>{820, 821}, "actual first round keeps known missing wave IDs");
            Check(tick.attempts == 24 && capture.records.size() == 24, "first round captures 24 rows; no monster creation or point lookup");
            Check(Equal(controller.ActiveWaves(), std::vector<std::uint16_t>{2, 3, 19}), "first round active list removes two empty programs");
        }
    }
    Check(totalMissing == 6, "actual five rounds retain all six absent-wave uses");
    const auto sourceUnchanged = ReadFile(wavePath);
    Check(waves.source.sourceBytes == sourceUnchanged && waves.rows.size() == 218, "controller activation leaves original source table intact");
}

void PrintClock(int status, const WaveSchedule& clock)
{
    std::cout << status << ' ' << Bits(clock.start) << ' ' << Bits(clock.elapsed) << ' ' << Bits(clock.nextThink) << '\n';
}
int Oracle()
{
    std::string command;
    while (std::cin >> command)
    {
        std::uint32_t start, elapsed, next, a, b;
        if (!(std::cin >> start >> elapsed >> next >> a)) return 2;
        WaveSchedule clock{std::bit_cast<float>(start), std::bit_cast<float>(elapsed), std::bit_cast<float>(next)};
        if (command == "F") PrintClock(FinishWaveSchedule(clock, std::bit_cast<float>(a)), clock);
        else if (command == "B")
        {
            if (!(std::cin >> b)) return 2;
            const bool ok = BeginWaveRound(clock, std::bit_cast<float>(a), std::bit_cast<float>(b));
            PrintClock(ok, clock);
        }
        else if (command == "S")
        {
            if (!(std::cin >> b)) return 2;
            const auto step = CheckWaveSchedule(clock, std::bit_cast<float>(a), std::bit_cast<float>(b));
            PrintClock(static_cast<int>(step), clock);
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
        if (argc == 4 && std::string_view(argv[1]) == "--actual") ActualTests(argv[2], argv[3]);
        else if (argc == 2 && std::string_view(argv[1]) == "--runtime-cache") { ControllerTests(); ControllerCancellation(); }
        else if (argc == 1) { ScheduleTests(); ControllerTests(); ControllerCancellation(); QuotaTests(); }
        else throw std::runtime_error("usage: controller_tests [--runtime-cache | --oracle | --actual wave-file round-file]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "controller checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
