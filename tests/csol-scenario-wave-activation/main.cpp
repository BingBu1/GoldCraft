// SPDX-License-Identifier: GPL-3.0-or-later
// Independent trigger-reactivation checks; no old wave/manager suites run here.
#include "controller.hpp"
#include "schedule.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<class T> std::vector<T> Copy(std::span<const T> values) { return {values.begin(), values.end()}; }
bool Same(const RowClock& a, const RowClock& b) { return a.next == b.next && a.end == b.end && a.interval == b.interval; }
bool Same(std::span<const RowClock> a, const std::vector<RowClock>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) { return Same(x, y); });
}
WaveRow Row(int wave, int point, int start, int end, int interval, std::size_t record)
{
    WaveRow row; row.waveId = wave; row.spawnPoint = point; row.startTime = start; row.endTime = end;
    row.interval = static_cast<std::int16_t>(interval); row.recordIndex = record;
    row.types = {40}; row.weights = {1}; row.weightTotal = 1; return row;
}
struct Capture { std::vector<std::size_t> rows; std::vector<RowClock> atDispatch; };
void Record(void* context, const WaveRow& row, RowClock& clock)
{
    auto& capture = *static_cast<Capture*>(context); capture.rows.push_back(row.recordIndex); capture.atDispatch.push_back(clock);
}
void RowTests()
{
    struct Case { int start, end, interval; float offset; int next, shiftedEnd; };
    for (const auto& item : {Case{3, 20, 5, 10.75f, 13, 30}, {3, 0, 0, 10.75f, 13, 0},
        {1, 2, 32767, -1.9f, 0, 0}, {-5, -3, 1, 1.25f, -3, -1}, {-2, 0, 1, 1.f, -1, 0},
        {16777217, 16777219, 2, 1.f, 16777216, 16777220}, {-16777217, -16777219, 2, -1.f, -16777216, -16777220},
        {INT32_MIN, 0, 0, 0.f, INT32_MIN, 0}, {INT32_MAX, 0, 0, -128.f, 2147483520, 0}})
    {
        RowClock clock{-1, 123, 17};
        Check(RestartRowAt(Row(1, 0, item.start, item.end, item.interval, 0), item.offset, clock), "valid original row activation accepted");
        Check(Same(clock, {item.next, item.shiftedEnd, static_cast<std::int16_t>(item.interval)}), "float32 sum then trunc and original end-zero semantics");
    }
    for (const auto& item : {Case{INT32_MAX, 0, 1, 0.f, 0, 0}, {0, INT32_MAX, 1, 0.f, 0, 0},
        {0, 0, -1, 1.f, 0, 0}, {0, 0, 1, std::numeric_limits<float>::infinity(), 0, 0},
        {0, 0, 1, -std::numeric_limits<float>::infinity(), 0, 0}, {0, 0, 1, std::numeric_limits<float>::quiet_NaN(), 0, 0},
        {0, 0, 1, std::numeric_limits<float>::max(), 0, 0}})
    {
        RowClock clock{5, 11, 7}; const auto before = clock;
        Check(!RestartRowAt(Row(1, 0, item.start, item.end, item.interval, 0), item.offset, clock), "unexecutable row schedule rejected");
        Check(Same(clock, before), "row rejection preserves all original output fields");
    }
}
struct RunnerNested { WaveRunner* runner; bool rejected{}; };
void RunnerReentrant(void* context, const WaveRow&, RowClock&)
{
    auto& nested = *static_cast<RunnerNested*>(context); nested.rejected = !nested.runner->RestartAt(100);
}
void RunnerTests()
{
    WaveRunner runner; ConfigError error; Capture capture;
    Check(!runner.RestartAt(0), "unloaded runner cannot restart");
    WaveTable table; table.rows = {Row(1, 9999, 0, 0, 0, 10), Row(1, 7, 0, 0, 0, 11)};
    Check(runner.Load(table, 1, error) && runner.Clocks()[0].next == 2 && runner.Clocks()[1].next == 0, "load has original event plus-two and ordinary time");
    Check(runner.RestartAt(10.75f) && runner.StartOffset() == 10.75f && runner.Clocks()[0].next == 10 && runner.Clocks()[1].next == 10,
        "trigger restart uses original event start without another plus-two");
    auto tick = runner.Tick(10.75f, Record, &capture);
    Check(tick.valid && tick.finished && capture.rows == std::vector<std::size_t>{10, 11}, "reactivated events and monsters dispatch in original point order");
    Check(runner.Clocks()[0].next == -1 && runner.Clocks()[1].next == -1, "one-shot rows ended before restart");
    Check(runner.ResetRunningRows() && runner.Clocks()[0].next == -1, "old running-row reset intentionally leaves finished event ended");
    Check(runner.RestartAt(20.5f) && runner.Clocks()[0].next == 20 && runner.Clocks()[1].next == 20, "trigger restart restores all finished rows");
    capture = {}; tick = runner.Tick(20.5f, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 2, "finished rows execute again after trigger activation");
    WaveTable unsafe; unsafe.rows = {Row(2, 0, 0, 0, 2, 1), Row(2, 1, 100, 2147483520, 7, 2)};
    Check(runner.Load(unsafe, 2, error) && runner.RestartAt(0), "multirow transaction fixture loaded");
    const auto before = Copy(runner.Clocks()); const auto oldOffset = runner.StartOffset();
    Check(!runner.RestartAt(256) && Same(runner.Clocks(), before) && runner.StartOffset() == oldOffset, "late row overflow preserves every earlier clock and offset");
    Check(runner.RestartAt(-20.5f) && runner.Clocks()[0].next == -20 && runner.Clocks()[0].end == 0, "negative finite scenario offset accepted without accumulating previous offset");
    Check(runner.Load(table, 1, error) && runner.RestartAt(0), "runner reentrancy fixture ready");
    RunnerNested nested{&runner}; tick = runner.Tick(0, RunnerReentrant, &nested);
    Check(tick.valid && nested.rejected && runner.StartOffset() == 0, "restart during callback rejected without replacing active clock storage");
}
struct ControllerNested { WaveController* controller; bool activateRejected{}, endRejected{}, tickRejected{}; };
void ControllerReentrant(void* context, const WaveRow&, RowClock&)
{
    auto& nested = *static_cast<ControllerNested*>(context); ConfigError error;
    nested.activateRejected = !nested.controller->ActivateWave(1, 100, error) && !error.message.empty();
    nested.endRejected = !nested.controller->EndWave(1);
    nested.tickRejected = !nested.controller->Tick(0, ControllerReentrant, context).valid;
}
void Throwing(void*, const WaveRow&, RowClock&) { throw std::runtime_error("fixture"); }
void ControllerTests()
{
    WaveController controller; ConfigError error; Capture capture;
    WaveTable table; table.rows = {Row(1, 0, 0, 0, 1, 11), Row(2, 9999, 0, 0, 0, 22), Row(-1, 4, 2, 0, 0, 33)};
    Check(!controller.ActivateWave(1, 0, error) && !controller.EndWave(1), "unloaded controller rejects trigger actions");
    Check(controller.Prepare(table, std::vector<std::uint16_t>{1,2,65535}, error) && controller.ActiveWaves().empty(), "triggered round preloads referenced programs without activating group or waves");
    Check(controller.ActivateWave(1, 10.5f, error) && controller.ActivateWave(1, 10.5f, error), "duplicate trigger activation succeeds");
    Check(Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{1, 1}, "duplicate activations remain ordered in active list");
    const auto* shared = controller.FindWave(1);
    auto tick = controller.Tick(11, Record, &capture);
    Check(tick.valid && tick.attempts == 2 && capture.rows == std::vector<std::size_t>{11, 11} && shared->Clocks()[0].next == 12,
        "duplicate active IDs share mutable clock and can advance twice");
    Check(controller.ActivateWave(1, 100.75f, error) && controller.FindWave(1) == shared && shared->Clocks()[0].next == 100,
        "later activation resets the one shared program for all duplicate entries");
    Check(Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{1, 1, 1} && shared->StartOffset() == 100.75f, "later activation appends and records exact offset");
    const auto clocks = Copy(shared->Clocks());
    Check(controller.EndWave(1) && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{1, 1} && Same(shared->Clocks(), clocks), "EndWave removes first occurrence and preserves shared program");
    Check(!controller.EndWave(999) && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{1, 1}, "EndWave absent id leaves activity untouched");
    Check(controller.EndWave(1) && controller.EndWave(1) && controller.ActiveWaves().empty(), "duplicates require separate end actions");
    Check(controller.ActivateWave(2, 20.25f, error) && controller.FindWave(2)->Clocks()[0].next == 20, "preloaded trigger event omits load-only delay after restart");
    capture = {}; tick = controller.Tick(20.25f, Record, &capture);
    Check(tick.valid && tick.finished && capture.rows == std::vector<std::size_t>{22}, "new event program completes once");
    Check(controller.ActivateWave(2, 30.25f, error) && controller.FindWave(2)->Clocks()[0].next == 30, "inactive finished cached event is restored");
    capture = {}; tick = controller.Tick(30.25f, Record, &capture);
    Check(tick.valid && tick.finished && capture.rows == std::vector<std::size_t>{22}, "cached event executes again after restoration");
    Check(controller.ActivateWave(65535, 4.5f, error) && controller.FindWave(65535)->Clocks()[0].next == 6, "low-sixteen-bit wave identity also applies to trigger activation");
    Check(controller.EndWave(65535), "canonical wave can be ended");
    Check(controller.ActivateWave(404, 1, error) && controller.ActivateWave(404, 2, error), "missing program activation is valid empty work");
    const auto* empty=controller.FindWave(404);
    Check(empty && empty->Rows().empty() && empty->Clocks().empty() && empty->WaveId()==404 && empty->StartOffset()==2 && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{404, 404} && Copy(controller.MissingWaves()) == std::vector<std::uint16_t>{404}, "missing runtime entry stores one empty program identity and latest offset across duplicate activity");
    Check(controller.EndWave(404) && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{404}, "missing duplicate also ends only first occurrence");
    capture = {}; tick = controller.Tick(2, Record, &capture);
    Check(tick.valid && tick.finished && tick.attempts == 0 && capture.rows.empty(), "remaining empty wave disappears on valid tick");
    Check(controller.ActivateWave(1, 1, error), "transaction fixture reactivates prior program");
    const auto beforeActive = Copy(controller.ActiveWaves()), beforeMissing = Copy(controller.MissingWaves());
    const auto beforeClock = Copy(controller.FindWave(1)->Clocks());
    for (const auto invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max()})
    {
        Check(!controller.ActivateWave(1, invalid, error) && !error.message.empty(), "invalid elapsed rejects trigger activation");
        Check(Copy(controller.ActiveWaves()) == beforeActive && Copy(controller.MissingWaves()) == beforeMissing && Same(controller.FindWave(1)->Clocks(), beforeClock), "failed trigger retains active diagnostics and prior shared clocks");
    }
    WaveTable malformed = table; malformed.rows.push_back(Row(9, 1, 0, 0, -1, 99));
    Check(!controller.Prepare(malformed, std::vector<std::uint16_t>{9}, error) && !controller.FindWave(9) && Copy(controller.ActiveWaves()) == beforeActive, "malformed program rejects at preload without replacing previous round");
    Check(controller.Activate(table, std::vector<std::uint16_t>{1}, error), "controller reentrancy fixture ready");
    ControllerNested nested{&controller}; tick = controller.Tick(0, ControllerReentrant, &nested);
    Check(tick.valid && nested.activateRejected && nested.endRejected && nested.tickRejected && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{1}, "callback reentrant trigger mutation and tick are rejected");
    try { controller.Tick(1, Throwing, nullptr); Check(false, "exception propagated"); }
    catch (const std::runtime_error&) { Check(true, "exception propagated"); }
    Check(controller.ActivateWave(1, 100, error) && controller.EndWave(1), "callback exception releases trigger mutation guards");
}
void RuntimeOnlyTests()
{
    WaveTable global;global.rows={Row(1,0,0,0,0,100),Row(2,0,0,0,0,200)};
    WaveController controller;ConfigError error;Capture capture;
    Check(controller.Prepare(global,std::vector<std::uint16_t>{1},error)&&controller.ActiveWaves().empty()&&!controller.FindWave(2),"only referenced source program preloaded; other global source row remains outside runtime");
    Check(controller.ActivateWave(2,7.5f,error),"global-source-present but runtime-absent ID may activate only empty program");
    const auto* empty=controller.FindWave(2);
    Check(empty&&empty->WaveId()==2&&empty->Rows().empty()&&empty->Clocks().empty()&&empty->StartOffset()==7.5f,"absent runtime lookup creates empty ID and offset without consulting global source");
    auto tick=controller.Tick(7.5f,Record,&capture);
    Check(tick.valid&&tick.finished&&tick.attempts==0&&capture.rows.empty()&&controller.FindWave(2)==empty,"empty runtime entry completes without dispatch and remains cached");
    global.rows[1].recordIndex=999;
    Check(controller.ActivateWave(2,9.5f,error)&&controller.FindWave(2)==empty&&empty->Rows().empty()&&empty->StartOffset()==9.5f,"repeat activation remains empty despite updated global source");
    tick=controller.Tick(9.5f,Record,&capture);Check(tick.valid&&tick.attempts==0&&capture.rows.empty(),"repeat missing entry cannot spawn another round's globally defined wave");
    Check(controller.Prepare(global,std::vector<std::uint16_t>{2},error)&&controller.ActiveWaves().empty()&&controller.FindWave(2)->Rows().size()==1,"new explicit preload can replace empty runtime with selected source program");
    Check(controller.ActivateWave(2,10.5f,error),"now-preloaded source activates");tick=controller.Tick(10.5f,Record,&capture);
    Check(tick.valid&&tick.finished&&capture.rows==std::vector<std::size_t>{999},"preloaded program really executes its selected source");
    capture={};Check(controller.ActivateWave(2,20.5f,error),"inactive cached source restored after finishing");tick=controller.Tick(20.5f,Record,&capture);
    Check(tick.valid&&tick.finished&&capture.rows==std::vector<std::size_t>{999},"cached program restoration executes again");
    auto standalone=WaveRunner::Empty(-1);Check(standalone.WaveId()==65535&&standalone.Rows().empty()&&standalone.RestartAt(3.25f)&&standalone.StartOffset()==3.25f,"standalone empty program retains canonical ID and float offset");
}
void GateTests()
{
    WaveSchedule clock{10, 3.25f, 15.5f};
    Check(CheckWaveSchedule(clock, 13.25f, 14) == ScheduleStep::Waiting, "existing half-second gate initially waits");
    RequestWaveUpdate(clock);
    Check(clock.start == 10 && clock.elapsed == 3.25f && clock.nextThink == 0, "trigger update clears only gate");
    Check(CheckWaveSchedule(clock, 13.25f, 14) == ScheduleStep::Dispatch, "trigger update permits next manager pass before old gate");
    Check(FinishWaveSchedule(clock, 14.125f) && clock.nextThink == 14.625f, "ordinary half-second scheduling resumes after trigger update");
    clock = {20, 3.25f, 99}; RequestWaveUpdate(clock);
    Check(CheckWaveSchedule(clock, 19, 30) == ScheduleStep::Frozen && clock.elapsed == 3.25f, "cleared gate does not bypass round freeze origin");
}
int Oracle()
{
    int start{}, end{}, interval{}; std::uint32_t offset{};
    while (std::cin >> start >> end >> interval >> offset)
    {
        RowClock clock{111, 222, 33};
        const bool valid = RestartRowAt(Row(1, 0, start, end, interval, 0), std::bit_cast<float>(offset), clock);
        std::cout << valid << ' ' << clock.next << ' ' << clock.end << ' ' << clock.interval << '\n';
    }
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    try { if(argc==2&&std::string_view(argv[1])=="--runtime-cache"){ControllerTests();RuntimeOnlyTests();}else{RowTests();RunnerTests();ControllerTests();RuntimeOnlyTests();GateTests();} }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "wave_activation checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
