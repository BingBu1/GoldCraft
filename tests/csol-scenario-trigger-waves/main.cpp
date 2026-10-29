// SPDX-License-Identifier: GPL-3.0-or-later
// Real program -> adapter -> controller/schedule; observers only are captures.
#include "trigger_wave_actions.hpp"
#include "resources.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <tuple>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
template<class T> std::vector<T> Copy(std::span<const T> values) { return {values.begin(), values.end()}; }
struct Event
{
    char kind{}; std::uint16_t id{}; std::vector<std::uint16_t> active; float nextThink{};
};
struct Events final : TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    WaveController& waves; WaveSchedule& schedule; std::vector<Event> log;
    std::function<void(char)> callback;
    Events(WaveController& w, WaveSchedule& s) : waves(w), schedule(s) {}
    void Add(char kind, std::uint16_t id = 0)
    { log.push_back({kind, id, Copy(waves.ActiveWaves()), schedule.nextThink}); if (callback) callback(kind); }
    void WaveActivated(std::uint16_t id) override { Add('a', id); }
    void BeforeSpatialWaveEnd(std::uint16_t id) override { Add('e', id); }
    void WavesChanged(std::span<const std::uint16_t> active) override
    { ::Check(Copy(active) == Copy(waves.ActiveWaves()), "refresh observer receives current actual controller list"); Add('r'); }
    void GoalReached() override { Add('g'); }
    std::vector<std::pair<char, std::uint16_t>> Tags() const
    { std::vector<std::pair<char, std::uint16_t>> result; for (const auto& event : log) result.emplace_back(event.kind, event.id); return result; }
};
WaveRow Wave(int id, int start = 3, int end = 15, int interval = 2, int point = 1)
{
    WaveRow row; row.waveId = id; row.spawnPoint = point; row.startTime = start; row.endTime = end;
    row.interval = static_cast<std::int16_t>(interval); row.types = {40}; row.weights = {1}; row.weightTotal = 1; return row;
}
WaveTable Waves()
{
    WaveTable table; table.rows = {Wave(7), Wave(8, 0, 0, 0, 9999), Wave(9, 0, 0, 1)}; return table;
}
TriggerRule Rule(int id, char command, int cooldown, std::vector<std::uint16_t> waves = {}, std::vector<std::uint16_t> ends = {})
{
    TriggerRule row; row.group = 1; row.trigger = static_cast<std::int16_t>(id); row.command = command;
    row.cooldown = static_cast<std::int16_t>(cooldown); row.waves = std::move(waves); row.endWaves = std::move(ends); row.destinationRatio = 50; return row;
}
void Install(TriggerProgram& program, std::initializer_list<TriggerRule> rows)
{
    TriggerRuleTable table; TriggerVolumes volumes; ConfigError error;
    for (const auto& row : rows)
    {
        table.groups[1].push_back(table.rows.size()); table.rows.push_back(row);
        volumes.Register({{-10, -10, -10}, {10, 10, 10}, row.trigger, 1});
    }
    Check(program.Install(table, volumes, 1, error) && error.message.empty(), "real trigger program installed");
}
void Initialize(WaveController& controller, const WaveTable& waves, std::vector<std::uint16_t> ids = {})
{ ConfigError error; Check(controller.Activate(waves, ids, error), "real controller initialized"); }
void Prepare(WaveController& controller, const WaveTable& waves, std::vector<std::uint16_t> ids)
{ ConfigError error; Check(controller.Prepare(waves, ids, error) && controller.ActiveWaves().empty(), "real controller preloads only selected runtime programs with no active waves"); }
void Noop(void*, const WaveRow&, RowClock&) {}
void RepeatAndOrder()
{
    const auto waves = Waves(); WaveController controller; Prepare(controller, waves, {7,8});
    WaveSchedule schedule{10, 5.25f, 999}; Events events(controller, schedule); TriggerProgram program;
    Install(program, {Rule(1, 'w', 2, {7, 7, 8}, {7, 99})});
    WaveTriggerActions actions(controller, schedule, events);
    auto result = program.CheckPlayer({0, 0, 0}, 5.25f, 4, actions);
    Check(result.valid && result.waveCommands == 1 && actions.Valid(), "spatial real program completes adapter dispatch");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}, {'a', 7}, {'a', 8}, {'e', 7}, {'r', 0}}, "activation order then existing spatial end then final refresh");
    Check(events.log[3].active == std::vector<std::uint16_t>{7, 7, 8} && events.log.back().active == std::vector<std::uint16_t>{7, 8}, "spatial observer sees list before first-match removal");
    Check(schedule.start == 10 && schedule.elapsed == 5.25f && schedule.nextThink == 0 && events.log[0].nextThink == 0, "successful activation wakes manager before activation observer");
    const auto* shared = controller.FindWave(7);
    Check(shared && shared->StartOffset() == 5.25f && shared->Clocks()[0].next == 8 && shared->Clocks()[0].end == 20, "adapter resets actual shared monster schedule");
    Check(controller.FindWave(8)->Clocks()[0].next == 5, "adapter resets actual event without load plus-two");
    events.log.clear(); schedule.nextThink = 200;
    result = program.ExecuteTrigger(1, 7.24f, 4, actions);
    Check(result.waveCommands == 0 && events.log.empty() && schedule.nextThink == 200, "cooldown waiting does not touch actual waves or gate");
    result = program.ExecuteTrigger(1, 7.25f, 4, actions);
    Check(result.waveCommands == 1 && actions.Valid() && controller.FindWave(7) == shared && shared->Clocks()[0].next == 10, "cooldown equality resets existing shared program");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}, {'a', 7}, {'a', 8}, {'r', 0}}, "explicit-ID end sends no spatial observer");
    Check(Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{8, 7, 7, 8}, "explicit end removes only earliest matching duplicate");
}
void EndsAndMissing()
{
    const auto waves = Waves(); WaveController controller; Initialize(controller, waves, {7, 7});
    WaveSchedule schedule{10, 3, 500}; Events events(controller, schedule); TriggerProgram program;
    Install(program, {Rule(1, 'w', 1, {}, {7})}); WaveTriggerActions actions(controller, schedule, events);
    auto result = program.CheckPlayer({0, 0, 0}, 2, 4, actions);
    Check(result.waveCommands == 1 && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'e', 7}, {'r', 0}}, "end-only spatial rule emits observer and refresh");
    Check(events.log[0].active == std::vector<std::uint16_t>{7, 7} && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7} && schedule.nextThink == 500, "end-only removes first duplicate without resetting schedule gate");
    events.log.clear(); result = program.ExecuteTrigger(1, 3, 4, actions);
    Check(result.waveCommands == 1 && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'r', 0}} && controller.ActiveWaves().empty(), "end-only explicit removes final duplicate with refresh only");
    events.log.clear(); result = program.CheckPlayer({0, 0, 0}, 4, 4, actions);
    Check(result.waveCommands == 1 && events.log.empty() && schedule.nextThink == 500, "absent end target emits neither observer nor refresh");
    TriggerProgram missing; Install(missing, {Rule(2, 'w', 0, {404})});
    result = missing.ExecuteTrigger(2, 10, 4, actions);
    Check(result.valid && actions.Valid() && controller.FindWave(404) && controller.FindWave(404)->Rows().empty() && controller.FindWave(404)->StartOffset()==10 && schedule.nextThink == 0, "absent wave stores empty runtime identity and offset and wakes the manager");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 404}, {'r', 0}}, "missing-wave observer and final summary are delivered");
    const auto tick = controller.Tick(10, Noop, nullptr);
    Check(tick.valid && tick.finished && controller.ActiveWaves().empty(), "actual controller removes missing work on next tick");
}
void Goals()
{
    const auto waves = Waves(); WaveController controller; Prepare(controller, waves, {7});
    WaveSchedule schedule{}; Events events(controller, schedule); WaveTriggerActions actions(controller, schedule, events);
    TriggerProgram program; Install(program, {Rule(2, 'g', 0), Rule(1, 'w', 0, {7})});
    const auto result = program.CheckPlayer({0, 0, 0}, 1.25f, 2, actions);
    Check(result.valid && result.waveCommands == 1 && result.goalReached && actions.Valid(), "real overlapping wave then goal accepted");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}, {'g', 0}, {'r', 0}}, "goal notification precedes pending final wave refresh");
    Check(events.log[1].active == std::vector<std::uint16_t>{7}, "goal observer sees already activated actual wave");
    TriggerProgram early; Install(early, {Rule(1, 'w', 0, {8}), Rule(2, 'g', 0)}); events.log.clear();
    const auto earlyResult = early.CheckPlayer({0, 0, 0}, 2, 2, actions);
    Check(earlyResult.goalReached && earlyResult.waveCommands == 0 && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'g', 0}} && !controller.FindWave(8), "goal-first exits before later wave and produces no refresh");
}
void Failures()
{
    auto waves = Waves(); WaveController controller; Prepare(controller, waves, {7,8});ConfigError setupError;
    Check(controller.ActivateWave(7,0,setupError)&&controller.ActivateWave(7,0,setupError),"failure fixture seeds duplicate active cached wave");
    WaveSchedule schedule{10, 7, 999}; Events events(controller, schedule); TriggerProgram program;
    Install(program, {Rule(2, 'g', 0), Rule(1, 'w', 0, {7, 8}, {7})});
    WaveTriggerActions failed(controller, schedule, events);
    const auto before = controller.FindWave(7)->Clocks()[0];
    const auto result = program.CheckPlayer({0, 0, 0}, std::numeric_limits<float>::max(), 2, failed);
    Check(!result.valid && !failed.Valid() && !failed.Error().message.empty(), "adapter failure stops the rule program and retains its diagnostic");
    Check(events.log.empty() && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7, 7} && controller.FindWave(8)->StartOffset()==0 && controller.FindWave(8)->Clocks()[0].next==2, "first invalid activation suppresses later cached wave activation ends goal and refresh");
    Check(controller.FindWave(7)->Clocks()[0].next == before.next && controller.FindWave(7)->Clocks()[0].end == before.end && schedule.nextThink == 999 && schedule.start == 10 && schedule.elapsed == 7, "invalid wave activation retains prior clock and all schedule fields");
    const auto error = failed.Error().message;
    failed.ActivateWave(404, 0); failed.EndWave(7, true); failed.RefreshWaves(); failed.GoalReached();
    Check(!failed.Valid() && failed.Error().message == error && events.log.empty() && Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7, 7}, "failed adapter remains failed and all subsequent public actions stay suppressed");
    Check(program.UsedTriggers().empty(), "failed activation does not commit the later once-only used state");
    WaveTriggerActions fresh(controller, schedule, events); fresh.ActivateWave(8, 3.5f);
    Check(fresh.Valid() && controller.FindWave(8)->Clocks()[0].next == 3 && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 8}}, "new dispatch adapter can recover after invalid input is corrected");
    WaveController unloaded; WaveSchedule clock{1, 2, 3}; Events unloadedEvents(unloaded, clock); WaveTriggerActions notReady(unloaded, clock, unloadedEvents);
    notReady.ActivateWave(7, 0);
    Check(!notReady.Valid() && unloadedEvents.log.empty() && clock.nextThink == 3, "uninitialized controller failure suppresses observer and gate changes");
    waves.rows.push_back(Wave(10, INT32_MAX, 0)); WaveController partial; Prepare(partial, waves, {7,10,8});
    WaveSchedule partialClock{0, 0, 50}; Events partialEvents(partial, partialClock); WaveTriggerActions partialActions(partial, partialClock, partialEvents);
    TriggerProgram partialProgram; Install(partialProgram, {Rule(4, 'w', 0, {7, 10, 8}, {7})});
    partialProgram.ExecuteTrigger(4, 0, 2, partialActions);
    Check(!partialActions.Valid() && Copy(partial.ActiveWaves()) == std::vector<std::uint16_t>{7} && partialEvents.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}}, "earlier valid action remains, later failed activation suppresses remaining effects");
    Check(partial.FindWave(10)->Clocks()[0].next==INT32_MAX && partial.FindWave(8)->Clocks()[0].next==2 && partialClock.nextThink == 0, "failure leaves later preloaded clocks untouched and does not roll back earlier accepted wake");
}
void ObserverFailures()
{
    for (bool diagnostic : {false, true})
    {
        WaveController controller; Prepare(controller, Waves(), {7, 8, 9});
        WaveSchedule schedule{10, 4, 900}; Events events(controller, schedule);
        ConfigError unchanged{7, "existing caller diagnostic"};
        Check(events.Check(unchanged) && unchanged.byteOffset == 7 && unchanged.message == "existing caller diagnostic",
            "successful observer check leaves caller error untouched");
        events.observerValid = false;
        if (diagnostic) events.observerError = {5, "observer failed before dispatch"};
        Check(!events.Check(unchanged) && unchanged.message == (diagnostic ? "observer failed before dispatch" : "") &&
            unchanged.byteOffset == (diagnostic ? 5u : 0u),
            "failure publishes its own diagnostic while cancellation clears stale caller error");
        WaveTriggerActions actions(controller, schedule, events);
        Check(!actions.Current() && !actions.Valid() &&
            actions.Error().message == (diagnostic ? "observer failed before dispatch" : ""),
            "pre-dispatch cancellation stays distinct from observer failure");
        events.observerValid = true; events.observerError = {};
        actions.ActivateWave(7, 4); actions.EndWave(7, true); actions.RefreshWaves(); actions.GoalReached();
        Check(!actions.Current() && events.log.empty() && controller.ActiveWaves().empty() && schedule.nextThink == 900,
            "same adapter latches failed or cancelled observer even after its observation fields recover");
    }
    for (bool spatial : {false, true}) for (bool diagnostic : {false, true})
    {
        WaveController controller; Prepare(controller, Waves(), {7, 8, 9}); ConfigError seed;
        Check(controller.ActivateWave(9, 0, seed), "activation failure fixture has a prior active wave");
        WaveSchedule schedule{10, 4, 900}; Events events(controller, schedule); TriggerProgram program;
        Install(program, {Rule(2, 'g', 0), Rule(1, 'w', 0, {7, 8}, {9})});
        events.callback = [&](char kind)
        {
            if (kind == 'a')
            {
                events.observerValid = false;
                if (diagnostic) events.observerError = {3, "activation observer failed"};
            }
        };
        WaveTriggerActions actions(controller, schedule, events);
        const auto result = spatial ? program.CheckPlayer({0, 0, 0}, 4, 2, actions) : program.ExecuteTrigger(1, 4, 2, actions);
        Check(!result.valid && !result.goalReached && !result.waveCommands && !actions.Valid() &&
            actions.Error().message == (diagnostic ? "activation observer failed" : ""),
            "activation callback failure or cancellation propagates through either trigger entry");
        Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}} &&
            Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{9, 7} && schedule.nextThink == 0,
            "accepted first activation and wake remain but later activation, end, goal and summary never run");
        Check(program.UsedTriggers().empty() && program.Arrivals() == 0 && controller.FindWave(8)->StartOffset() == 0,
            "failed activation does not disable its one-shot or modify the later preloaded wave");
        events.observerValid = true; events.observerError = {}; events.callback = {};
        actions.RefreshWaves(); actions.GoalReached();
        Check(!actions.Valid() && events.log.size() == 1, "callback cancellation remains latched for this dispatch");
    }
    for (bool diagnostic : {false, true})
    {
        WaveController controller; Initialize(controller, Waves(), {7, 7, 8});
        WaveSchedule schedule{10, 4, 900}; Events events(controller, schedule); TriggerProgram program;
        Install(program, {Rule(1, 'w', 0, {}, {7, 8})});
        events.callback = [&](char kind)
        {
            if (kind == 'e')
            {
                events.observerValid = false;
                if (diagnostic) events.observerError = {4, "spatial end observer failed"};
            }
        };
        WaveTriggerActions actions(controller, schedule, events);
        const auto result = program.CheckPlayer({0, 0, 0}, 4, 2, actions);
        Check(!result.valid && !result.waveCommands && program.UsedTriggers().empty() &&
            events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'e', 7}},
            "failed before-end observer suppresses later end, one-shot commit and summary");
        Check(Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7, 7, 8} && schedule.nextThink == 900 &&
            !actions.Valid() && actions.Error().message == (diagnostic ? "spatial end observer failed" : ""),
            "before-end failure leaves both duplicate active entries present");
    }
    {
        WaveController controller; Prepare(controller, Waves(), {7, 8});
        WaveSchedule schedule{}; Events events(controller, schedule); TriggerProgram program;
        Install(program, {Rule(3, 'w', 0, {8}), Rule(2, 'g', 0), Rule(1, 'w', 0, {7})});
        events.callback = [&](char kind) { if (kind == 'g') events.observerValid = false; };
        WaveTriggerActions actions(controller, schedule, events);
        const auto result = program.CheckPlayer({0, 0, 0}, 4, 2, actions);
        Check(!result.valid && !result.goalReached && result.waveCommands == 1 && program.Arrivals() == 1 &&
            events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}, {'g', 0}},
            "cancelled goal preserves its arrival prefix but does not report completion or publish pending summary");
        Check(Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7} &&
            Copy(program.UsedTriggers()) == std::vector<std::int16_t>{1} && actions.Error().message.empty(),
            "goal cancellation preserves the previously completed wave command without executing the following one");
    }
    {
        WaveController controller; Prepare(controller, Waves(), {7});
        WaveSchedule schedule{}; Events events(controller, schedule); TriggerProgram program;
        Install(program, {Rule(1, 'w', 0, {7})});
        events.callback = [&](char kind) { if (kind == 'r') events.observerError = {8, "summary failed"}; };
        WaveTriggerActions actions(controller, schedule, events);
        const auto result = program.ExecuteTrigger(1, 4, 2, actions);
        Check(!result.valid && result.waveCommands == 1 && !actions.Valid() && actions.Error().message == "summary failed" &&
            Copy(program.UsedTriggers()) == std::vector<std::int16_t>{1} &&
            Copy(controller.ActiveWaves()) == std::vector<std::uint16_t>{7},
            "summary failure is reported after preserving the already committed one-shot and active wave");
    }
}
void RuntimeAbsentIntegration()
{
    const auto global=Waves();WaveController controller;Prepare(controller,global,{7});
    WaveSchedule schedule{0,0,100};Events events(controller,schedule);WaveTriggerActions actions(controller,schedule,events);
    TriggerProgram program;Install(program,{Rule(5,'w',-2,{9})});
    auto result=program.ExecuteTrigger(5,4.5f,2,actions);
    Check(result.valid&&actions.Valid()&&controller.FindWave(9)&&controller.FindWave(9)->Rows().empty()&&controller.FindWave(9)->StartOffset()==4.5f,"adapter cannot pull global source wave9 that current runtime did not preload");
    Check(events.Tags()==std::vector<std::pair<char,std::uint16_t>>{{'a',9},{'r',0}}&&schedule.nextThink==0,"empty runtime activation retains observer order and wake semantics");
    int calls{};const auto record=[](void* context,const WaveRow&,RowClock&){++*static_cast<int*>(context);};
    auto tick=controller.Tick(4.5f,record,&calls);Check(tick.valid&&tick.finished&&tick.attempts==0&&calls==0,"global-present runtime-absent wave never dispatches through real adapter");
    Prepare(controller,global,{9});events.log.clear();WaveTriggerActions cached(controller,schedule,events);
    result=program.ExecuteTrigger(5,8.5f,2,cached);tick=controller.Tick(8.5f,record,&calls);
    Check(result.valid&&cached.Valid()&&tick.valid&&tick.attempts==1&&calls==1,"same rule executes after wave9 explicitly preloaded into new runtime");
    Check(controller.EndWave(9),"inactive cache fixture ends active wave");
    result=program.ExecuteTrigger(5,12.5f,2,cached);tick=controller.Tick(12.5f,record,&calls);
    Check(result.valid&&cached.Valid()&&tick.valid&&tick.attempts==1&&calls==2,"inactive preloaded cache restores execution without any source-table argument");
}
struct Reader
{
    std::ifstream file;
    explicit Reader(const char* path) : file(path, std::ios::binary) { if (!file) throw std::runtime_error("cannot open independent TRUL"); }
    std::uint32_t U32() { unsigned char bytes[4]; file.read(reinterpret_cast<char*>(bytes), 4); if (!file) throw std::runtime_error("truncated TRUL"); return bytes[0] | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24); }
    std::int32_t I32() { return std::bit_cast<std::int32_t>(U32()); }
    std::string String() { const auto n = U32(); if (n > 1024 * 1024) throw std::runtime_error("oversized string"); std::string s(n, '\0'); file.read(s.data(), n); if (!file) throw std::runtime_error("truncated string"); return s; }
    std::vector<std::uint16_t> List() { const auto n = U32(); if (n > 10000) throw std::runtime_error("oversized list"); std::vector<std::uint16_t> result; for (std::uint32_t i = 0; i < n; ++i) result.push_back(static_cast<std::uint16_t>(U32())); return result; }
};
std::string Read(const char* path)
{ std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("cannot open actual resource"); return {std::istreambuf_iterator<char>(file), {}}; }
bool SameRule(const TriggerRule& a, const TriggerRule& b)
{
    return std::tie(a.recordIndex, a.group, a.command, a.suffix, a.trigger, a.waves, a.cooldown, a.destinationRatio, a.endWaves) ==
           std::tie(b.recordIndex, b.group, b.command, b.suffix, b.trigger, b.waves, b.cooldown, b.destinationRatio, b.endWaves);
}
bool SameRules(const TriggerRuleTable& a, const TriggerRuleTable& b)
{
    if (a.source.sourceBytes != b.source.sourceBytes || a.source.records.size() != b.source.records.size() || a.groups != b.groups || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i) if (!SameRule(a.rows[i], b.rows[i])) return false;
    for (std::size_t i = 0; i < a.source.records.size(); ++i) if (a.source.records[i].byteOffset != b.source.records[i].byteOffset || a.source.records[i].fields != b.source.records[i].fields) return false;
    return true;
}
void Actual(const char* encryptedPath, const char* csvPath, const char* oraclePath, const char* wavePath)
{
    auto encrypted = Read(encryptedPath); const auto ciphertext = encrypted; const auto csv = Read(csvPath); const auto waveBytes = Read(wavePath);
    ConfigError error{999, "stale"}; TriggerRuleTable rules; WaveTable waves;
    Check(ReadTriggerRuleResource(std::as_bytes(std::span(encrypted.data(), encrypted.size())), rules, error), "actual encrypted TRG uses new production resource entry");
    Check(encrypted == ciphertext && rules.source.sourceBytes == csv && error.byteOffset == 0 && error.message.empty(), "encrypted entry preserves caller bytes, owns original decoded CP949 bytes and clears error");
    Check(ReadWaveResource(std::as_bytes(std::span(waveBytes.data(), waveBytes.size())), waves, error) && waves.rows.size() == 288, "actual 288 wave program rows available for integration");
    Reader reader(oraclePath); Check(reader.U32() == 0x4c555254, "independent TRUL signature"); const auto count = reader.U32();
    Check(count == 29 && rules.rows.size() == count, "actual encrypted source yields exactly 29 rules");
    std::vector<TriggerRule> expected; std::map<std::int16_t, std::vector<std::size_t>> groups;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        TriggerRule row; row.recordIndex = reader.U32(); const auto offset = reader.U32(); row.group = static_cast<std::int16_t>(reader.I32()); row.command = static_cast<char>(reader.U32());
        row.suffix = reader.String(); row.trigger = static_cast<std::int16_t>(reader.I32()); row.waves = reader.List(); row.cooldown = static_cast<std::int16_t>(reader.I32()); row.destinationRatio = static_cast<std::int8_t>(reader.I32()); row.endWaves = reader.List();
        const auto n = reader.U32(); std::vector<std::string> fields; for (std::uint32_t j = 0; j < n; ++j) fields.push_back(reader.String());
        Check(i < rules.rows.size() && SameRule(rules.rows[i], row), "every encrypted rule typed field agrees with independent original fixture");
        Check(row.recordIndex < rules.source.records.size() && rules.source.records[row.recordIndex].byteOffset == offset && rules.source.records[row.recordIndex].fields == fields, "every original column byte and record offset agrees including CP949 comments");
        groups[row.group].push_back(expected.size()); expected.push_back(row);
    }
    Check(reader.file.peek() == std::char_traits<char>::eof() && rules.groups == groups, "independent fixture consumed exactly and complete group index agrees");
    const auto before = rules;
    for (const std::size_t n : {0u, 7u, 9u})
    { const std::vector<std::byte> bad(n); Check(!ReadTriggerRuleResource(bad, rules, error) && SameRules(rules, before), "bad encrypted length retains complete rule source rows and groups"); }
    Check(!ReadTriggerRuleResource(std::as_bytes(std::span(waveBytes.data(), waveBytes.size())), rules, error) && SameRules(rules, before), "decoded wrong header preserves complete prior trigger table");
    encrypted.assign(encrypted.size(), 'x'); Check(rules.source.sourceBytes == csv, "decoded rule table owns bytes after input buffer overwrite");
    for (const auto& wanted : expected)
    {
        std::vector<std::uint16_t> preload;
        for(const auto& row:expected)if(row.group==wanted.group&&row.command=='w')preload.insert(preload.end(),row.waves.begin(),row.waves.end());
        WaveController controller;Prepare(controller,waves,preload);
        for(auto id:wanted.endWaves)Check(controller.ActivateWave(id,0,error),"actual end-wave fixture seeds prior activity from runtime only");
        WaveSchedule schedule{100, 12.25f, 999}; Events events(controller, schedule); WaveTriggerActions actions(controller, schedule, events);
        TriggerProgram program; TriggerVolumes volumes; Check(program.Install(rules, volumes, wanted.group, error), "actual group installs for explicit-ID integration");
        const auto result = program.ExecuteTrigger(wanted.trigger, 12.25f, 1, actions);
        Check(result.valid && actions.Valid() && result.goalReached == (wanted.command == 'g'), "actual rule dispatch succeeds through real wave adapter");
        auto active = wanted.endWaves; std::vector<std::pair<char, std::uint16_t>> tags;
        if (wanted.command == 'g') tags.emplace_back('g', 0);
        else
        {
            for (auto id : wanted.waves) { active.push_back(id); tags.emplace_back('a', id); }
            bool changed = !wanted.waves.empty();
            for (auto id : wanted.endWaves) { const auto at = std::find(active.begin(), active.end(), id); if (at != active.end()) { active.erase(at); changed = true; } }
            if (changed) tags.emplace_back('r', 0);
        }
        Check(Copy(controller.ActiveWaves()) == active && events.Tags() == tags, "actual IDs activate and end with ordered observer and final summary");
        Check(schedule.start == 100 && schedule.elapsed == 12.25f && schedule.nextThink == (wanted.waves.empty() ? 999.f : 0.f), "actual rule gate refresh follows successful activation only");
        for (auto id : wanted.waves)
        {
            const auto* runner = controller.FindWave(id); const auto countRows = std::count_if(waves.rows.begin(), waves.rows.end(), [id](const auto& row) { return WaveKey(row.waveId) == id; });
            Check(runner != nullptr, "every actual trigger reference was explicitly preloaded or represented as empty runtime");
            if (!runner) continue;
            Check(runner->Rows().size() == static_cast<std::size_t>(countRows) && runner->StartOffset() == 12.25f, "actual shared runner retains complete referenced row set and activation offset");
            for (std::size_t i = 0; i < runner->Rows().size(); ++i)
            {
                const auto& row = runner->Rows()[i]; const auto& clock = runner->Clocks()[i];
                const int next = static_cast<int>(static_cast<float>(row.startTime) + 12.25f);
                const int end = row.endTime ? static_cast<int>(static_cast<float>(row.endTime) + 12.25f) : 0;
                Check(clock.next == next && clock.end == end && clock.interval == row.interval, "actual referenced row has shifted original times and restored interval");
            }
        }
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { RepeatAndOrder(); EndsAndMissing(); Goals(); Failures(); ObserverFailures(); RuntimeAbsentIntegration(); }
        else if (argc == 6 && std::string_view(argv[1]) == "--actual") Actual(argv[2], argv[3], argv[4], argv[5]);
        else throw std::runtime_error("usage: trigger_waves_tests [--actual encryptedTRG decodedTRG independentTRUL encryptedWave]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "trigger_waves checks=" << checks << " failures=" << failures << '\n'; return failures ? 1 : 0;
}
