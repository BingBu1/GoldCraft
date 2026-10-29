// SPDX-License-Identifier: GPL-3.0-or-later
#include "trigger_round.hpp"
#include "resources.hpp"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool ok, const char* text)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << text << '\n'; }
}
std::vector<std::uint16_t> Active(const TriggerRound& r)
{ return {r.Waves().ActiveWaves().begin(), r.Waves().ActiveWaves().end()}; }
void Count(void* context, const WaveRow&, RowClock&) { if (context) ++*static_cast<int*>(context); }
WaveRow Wave(int id, int point = 1, int start = 0, int end = 100, int interval = 3)
{
    WaveRow row;
    row.waveId = id; row.spawnPoint = point; row.startTime = start; row.endTime = end; row.interval = static_cast<std::int16_t>(interval);
    row.types = {point == 9999 ? 400 : 1}; row.weights = {1}; row.weightTotal = 1;
    return row;
}
TriggerRule Rule(int group, int id, char command, std::vector<std::uint16_t> waves = {},
    int cooldown = 0, std::vector<std::uint16_t> ends = {}, int ratio = 50)
{
    TriggerRule row;
    row.group = TriggerKey(group); row.trigger = TriggerKey(id); row.command = command;
    row.waves = std::move(waves); row.endWaves = std::move(ends); row.cooldown = static_cast<std::int16_t>(cooldown);
    row.destinationRatio = static_cast<std::int8_t>(ratio);
    return row;
}
TriggerRuleTable Rules(std::vector<TriggerRule> rows)
{
    TriggerRuleTable table; table.rows = std::move(rows);
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    { table.rows[i].recordIndex = i; table.groups[table.rows[i].group].push_back(i); }
    return table;
}
RoundRow Round(int group) { RoundRow row; row.round = 1; row.waves = {group}; return row; }
struct Inputs
{
    WaveTable waves;
    TriggerRuleTable rules;
    TriggerVolumes volumes;
    SpawnPoints points;
    Inputs()
    {
        waves.rows = {Wave(1), Wave(7), Wave(7, 9999, 4), Wave(8), Wave(9), Wave(30), Wave(99), Wave(88, 1, 0, 0, -1)};
        rules = Rules({Rule(1, 11, 'w', {7, 8, 7}), Rule(1, 12, 'w', {8}),
            Rule(1, 13, 'g', {99}), Rule(1, 14, 'w', {404}, 0, {9}),
            Rule(0, 30, 'w', {30}), Rule(5, 51, 'x', {9}), Rule(6, 61, 'w', {88}), Rule(-1, 71, 'w', {9})});
        for (const auto& [id, x] : std::vector<std::pair<int, float>>{{11, 0}, {12, 10}, {13, 20}, {14, 30}, {30, 40}, {71, 50}})
            volumes.Register({{x - 1, -1, -1}, {x + 1, 1, 1}, id, static_cast<std::size_t>(id)});
        points.RegisterMonsterPoint({{1, 2, 3}, {4, 5, 6}, 2, 1, 7}, 4);
        points.RegisterMonsterPoint({{4, 5, 6}, {}, 2, 2, 8}, 0);
        points.AppendPlayerPoint({{10, 20, 30}, {}, 2, 1, 9});
    }
    void Consume()
    { Check(points.ConsumePointQuota(2, 1) && points.ConsumePointQuota(2, 1), "consume quota fixture"); }
};
struct Event
{
    char type{}; std::uint16_t id{};
    std::vector<std::uint16_t> active;
    std::optional<std::int16_t> group;
    WaveSchedule schedule;
    std::uint32_t quota{}, arrivals{};
    std::size_t used{};
};
struct Events : TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    TriggerRound& round; SpawnPoints& points; std::vector<Event> log; std::function<void(char)> callback;
    Events(TriggerRound& r, SpawnPoints& p) : round(r), points(p) {}
    void Add(char type, std::uint16_t id = 0)
    {
        const auto* point = points.FindMonsterPoint(2, 1);
        log.push_back({type, id, Active(round), round.Group(), round.Schedule(), point ? point->consumed : 0,
            round.Triggers().Arrivals(), round.Triggers().UsedTriggers().size()});
        if (callback) callback(type);
    }
    void WaveActivated(std::uint16_t id) override { Add('a', id); }
    void BeforeSpatialWaveEnd(std::uint16_t id) override { Add('e', id); }
    void WavesChanged(std::span<const std::uint16_t> active) override
    { ::Check(std::equal(active.begin(), active.end(), round.Waves().ActiveWaves().begin(), round.Waves().ActiveWaves().end()), "summary uses committed active list"); Add('r'); }
    void GoalReached() override { Add('g'); }
    std::vector<std::pair<char, std::uint16_t>> Tags() const
    { std::vector<std::pair<char, std::uint16_t>> out; for (const auto& e : log) out.emplace_back(e.type, e.id); return out; }
};
bool Start(TriggerRound& r, const Inputs& in, SpawnPoints& points, Events& events, int group, ConfigError& error,
    float now = 100, float freeze = 5)
{ return r.Begin(in.waves, in.rules, in.volumes, Round(group), now, freeze, points, events, error); }
void PreparationAndTransition()
{
    Inputs in; TriggerRound r; Events events(r, in.points); ConfigError error{9, "stale"};
    in.Consume(); r.Schedule() = {1, 17, 123};
    auto first = Round(65537); first.waves.push_back(999);
    Check(r.Begin(in.waves, in.rules, in.volumes, first, 100, 5, in.points, events, error), "first low16 waveTypes selects TRG group");
    Check(error.message.empty() && error.byteOffset == 0 && r.Group() == 1, "successful start clears error and commits signed group");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'r', 0}} && events.log[0].active.empty(), "only empty summary is notified, group is never activated");
    Check(events.log[0].group == 1 && events.log[0].schedule.start == 105 && events.log[0].used == 0, "observer sees committed group clock and cleared used state");
    Check(events.log[0].quota == 2 && in.points.FindMonsterPoint(2, 1)->consumed == 0, "empty summary happens before clearing monster attempt quotas");
    Check(r.Schedule().elapsed == 17 && r.Schedule().nextThink == 123, "ordinary round start preserves elapsed and half-second gate");
    Check(in.points.Monsters().size() == 2 && in.points.Players().size() == 1 && in.points.FindPlayerPoints(2, 1)->front().origin[0] == 10, "quota reset retains map monster and player positions");
    Check(r.Waves().FindWave(7) && r.Waves().FindWave(8) && r.Waves().FindWave(404), "only referenced w programs including missing reference preload");
    Check(!r.Waves().FindWave(1) && !r.Waves().FindWave(9) && !r.Waves().FindWave(99) && !r.Waves().FindWave(30) && !r.Waves().FindWave(88), "group number end-only goal-only other-group and malformed unreferenced waves do not preload");
    const auto* seven = r.Waves().FindWave(7);
    Check(seven->Rows().size() == 2 && seven->Clocks()[1].next == 6 && seven->StartOffset() == 0, "prepared source retains event load delay and zero offset");
    Check(r.Waves().FindWave(404)->Rows().empty() && r.Waves().FindWave(404)->StartOffset() == 0, "absent source produces saved empty runtime");
    int attempts = 0;
    auto tick = r.Waves().Tick(0, Count, &attempts);
    Check(tick.valid && tick.finished && !attempts && events.log.size() == 1, "empty active set is idle and never reports scenario victory");
    in.waves.rows[1].startTime = 999; in.waves.rows[1].types[0] = 88;
    WaveTriggerActions actions(r.Waves(), r.Schedule(), events); events.log.clear();
    auto trigger = r.Triggers().ExecuteTrigger(11, 3.5f, 4, actions);
    Check(trigger.valid && trigger.waveCommands == 1 && actions.Valid() && Active(r) == std::vector<std::uint16_t>{7, 8, 7}, "preloaded repeated IDs activate in exact list order");
    Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 7}, {'a', 8}, {'a', 7}, {'r', 0}}, "activation observer order precedes summary");
    Check(r.Waves().FindWave(7) == seven && seven->Rows()[0].types[0] == 1 && seven->Clocks()[0].next == 3 && seven->Clocks()[1].next == 7, "activation uses shared deep-copied runtime and no second event load delay");
    Check(r.Schedule().start == 105 && r.Schedule().elapsed == 17 && r.Schedule().nextThink == 0, "trigger wake preserves round origin and elapsed");
    const auto once = r.Triggers().ExecuteTrigger(11, 9, 4, actions);
    Check(once.waveCommands == 0 && r.Triggers().UsedTriggers().size() == 1, "once-only rule is spent before next round");
    Check(!r.Triggers().ExecuteTrigger(13, 9, 4, actions).goalReached && r.Triggers().Arrivals() == 1, "one arrival of four at fifty percent is retained");

    in.Consume(); r.Schedule().nextThink = 777; r.Schedule().elapsed = 81; events.log.clear();
    Check(Start(r, in, in.points, events, 0, error, 200, 6), "second round selects group zero");
    Check(events.log.size() == 1 && events.log[0].active.empty() && events.log[0].quota == 2 && events.log[0].arrivals == 1 && events.log[0].used == 0, "second start preserves arrivals clears used and publishes before quota reset");
    Check(r.Schedule().start == 206 && r.Schedule().elapsed == 81 && r.Schedule().nextThink == 777, "second start changes only wave time origin");
    Check(r.Waves().FindWave(30) && !r.Waves().FindWave(7) && !r.Waves().FindWave(8), "runtime source map replaces previous group programs");
    WaveTriggerActions secondActions(r.Waves(), r.Schedule(), events); events.log.clear();
    Check(r.Triggers().CheckPlayer({10, 0, 0}, 1, 4, secondActions).waveCommands == 0, "old-group spatial nodes are absent");
    Check(r.Triggers().ExecuteTrigger(11, 1, 4, secondActions).waveCommands == 0, "spent old runtime remains disabled despite global used-list clear");
    Check(r.Triggers().ExecuteTrigger(12, 12.25f, 4, secondActions).waveCommands == 1 && secondActions.Valid(), "unused prior-group runtime remains addressable by explicit ID");
    const auto* old = r.Waves().FindWave(8);
    Check(old && old->Rows().empty() && old->Clocks().empty() && old->StartOffset() == 12.25f, "regression: prior trigger must not recover global wave 8 outside current preload");
    Check(Active(r) == std::vector<std::uint16_t>{8} && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 8}, {'r', 0}} && r.Schedule().nextThink == 0, "empty missing runtime still activates notifies and wakes");
    attempts = 0; tick = r.Waves().Tick(12.25f, Count, &attempts);
    Check(tick.valid && tick.finished && attempts == 0 && Active(r).empty(), "empty runtime finishes on next tick without spawn");
    Check(r.Triggers().ExecuteTrigger(13, 13, 4, secondActions).goalReached && r.Triggers().Arrivals() == 2, "old goal runtime and accumulated arrivals persist across group switch");
    r.Triggers().BeginPlayerScanCycle(); Check(r.Triggers().Arrivals() == 0, "only explicit next scan cycle clears arrival count");

    events.log.clear(); Check(Start(r, in, in.points, events, 1, error), "same map formal restart reinstalls current group");
    WaveTriggerActions restarted(r.Waves(), r.Schedule(), events);
    Check(r.Triggers().UsedTriggers().empty() && r.Triggers().ExecuteTrigger(11, 0, 4, restarted).waveCommands == 1, "reinstalled once-only rule cooldown and nextTime start fresh");
    Check(r.Waves().FindWave(7)->Rows()[0].startTime == 999, "new formal restart deep-copies changed source instead of old runtime");
    events.log.clear(); Check(Start(r, in, in.points, events, 1234, error), "missing group is supported empty group");
    Check(Active(r).empty() && !r.Waves().FindWave(7) && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'r', 0}}, "missing group emits only empty summary");
    Check(r.BeginMap() && !r.Group() && r.Schedule().start == 0 && r.Schedule().elapsed == 0 && r.Schedule().nextThink == 0 && r.Triggers().Arrivals() == 0 && r.Triggers().UsedTriggers().empty(), "new map clears owned round schedule trigger runtime and counters");
    events.log.clear(); WaveTriggerActions cleared(r.Waves(), r.Schedule(), events);
    Check(r.Triggers().ExecuteTrigger(13, 0, 4, cleared).goalReached == false && events.log.empty(), "new map cannot address prior goal runtime");
    Check(!r.Waves().Tick(0, Count, nullptr).valid, "new map controller is unloaded");
    Check(Start(r, in, in.points, events, 65535, error) && r.Group() == -1 && r.Waves().FindWave(9), "signed-short group key is preserved");
}
void RejectionsAndReentry()
{
    Inputs in; TriggerRound r; Events events(r, in.points); ConfigError error;
    Check(Start(r, in, in.points, events, 1, error), "baseline valid round for rejected starts");
    WaveTriggerActions actions(r.Waves(), r.Schedule(), events);
    r.Triggers().ExecuteTrigger(11, 2, 4, actions); r.Triggers().ExecuteTrigger(13, 2, 4, actions);
    r.Schedule() = {33, 44, 55}; in.Consume();
    const auto* saved = r.Waves().FindWave(7); const auto active = Active(r); const auto count = events.log.size();
    auto Reject = [&](const WaveTable& waves, const TriggerRuleTable& rules, const RoundRow& round, float now, float freeze)
    {
        Check(!r.Begin(waves, rules, in.volumes, round, now, freeze, in.points, events, error) && !error.message.empty(), "invalid candidate is rejected");
        Check(r.Group() == 1 && r.Waves().FindWave(7) == saved && Active(r) == active, "rejected candidate retains group runtime identity and active duplicates");
        Check(r.Schedule().start == 33 && r.Schedule().elapsed == 44 && r.Schedule().nextThink == 55 && events.log.size() == count, "rejected candidate retains all clocks and emits no notifications");
        Check(r.Triggers().UsedTriggers().size() == 1 && r.Triggers().Arrivals() == 1 && in.points.FindMonsterPoint(2, 1)->consumed == 2, "rejected candidate does not clear used arrivals or point quotas");
    };
    Reject(in.waves, in.rules, RoundRow{}, 1, 1);
    Reject(in.waves, in.rules, Round(5), 1, 1);
    Reject(in.waves, in.rules, Round(6), 1, 1);
    auto invalid = in.rules; invalid.groups[7] = {invalid.rows.size()}; Reject(in.waves, invalid, Round(7), 1, 1);
    Reject(in.waves, in.rules, Round(1), std::numeric_limits<float>::quiet_NaN(), 1);
    Reject(in.waves, in.rules, Round(1), 1, std::numeric_limits<float>::infinity());
    Reject(in.waves, in.rules, Round(1), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());

    int reentrantChecks = 0;
    events.callback = [&](char type)
    {
        if (type != 'r' && type != 'a') return;
        ConfigError nested;
        Check(!Start(r, in, in.points, events, 0, nested) && !nested.message.empty(), "begin rejects recursion during summary or trigger notification");
        Check(!r.BeginMap(), "map reset rejects recursion during summary or trigger execution");
        ++reentrantChecks;
    };
    Check(Start(r, in, in.points, events, 0, error), "valid start succeeds after all candidate rejections");
    WaveTriggerActions next(r.Waves(), r.Schedule(), events);
    Check(r.Triggers().ExecuteTrigger(30, 0, 1, next).waveCommands == 1 && next.Valid(), "trigger dispatch remains valid with rejected recursive lifecycle");
    Check(reentrantChecks == 3 && r.Group() == 0 && Active(r) == std::vector<std::uint16_t>{30}, "recursion rejection preserves outer summary activation and trigger refresh");
    events.callback = {};
    struct DuringTick { TriggerRound* round; Inputs* in; Events* events; int calls{}; } tickState{&r, &in, &events};
    const auto dispatch = [](void* p, const WaveRow&, RowClock&)
    {
        auto& x = *static_cast<DuringTick*>(p); ConfigError nested; ++x.calls;
        Check(!Start(*x.round, *x.in, x.in->points, *x.events, 1, nested) && !nested.message.empty(), "begin rejects during wave dispatch");
        Check(!x.round->BeginMap(), "new-map reset rejects during wave dispatch");
    };
    auto tick = r.Waves().Tick(0, dispatch, &tickState);
    Check(tick.valid && tickState.calls == 1 && r.Group() == 0, "wave dispatch continues after lifecycle rejection");
    Check(r.BeginMap(), "lifecycle guard releases after dispatch");
}
void CommittedCancellation()
{
    for (bool diagnostic : {false, true})
    {
        Inputs in; TriggerRound round; Events events(round, in.points); ConfigError error;
        Check(Start(round, in, in.points, events, 1, error), "baseline round for committed cancellation");
        WaveTriggerActions actions(round.Waves(), round.Schedule(), events);
        round.Triggers().ExecuteTrigger(11, 0, 2, actions);
        in.Consume(); round.Schedule().elapsed = 44; round.Schedule().nextThink = 55; events.log.clear();
        bool committed{};
        events.callback = [&](char kind)
        {
            if (kind != 'r') return;
            Check(committed && round.Group() == 0 && round.Waves().FindWave(30) && !round.Waves().FindWave(7),
                "summary callback sees committed flag and replacement controller together");
            events.observerValid = false;
            if (diagnostic) events.observerError = {17, "round summary failed"};
        };
        Check(!round.Begin(in.waves, in.rules, in.volumes, Round(0), 200, 3, in.points, events, error, &committed) && committed,
            "failure after synchronous summary reports that the new round was already committed");
        Check(round.Group() == 0 && round.Waves().FindWave(30) && !round.Waves().FindWave(7) && Active(round).empty() &&
            round.Schedule().start == 203 && round.Schedule().elapsed == 44 && round.Schedule().nextThink == 55,
            "committed cancellation preserves replacement controller and schedule instead of restoring old phase");
        Check(in.points.FindMonsterPoint(2, 1)->consumed == 2 && round.Triggers().UsedTriggers().empty() &&
            events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'r', 0}} && !round.IsBusy(),
            "post-commit cancellation stops before quota reset and releases the starting guard");
        Check(error.message == (diagnostic ? "round summary failed" : "") &&
            (!diagnostic || error.byteOffset == 17), "post-commit cancellation remains distinct from diagnostic failure");
        events.callback = {}; events.observerValid = true; events.observerError = {}; committed = false;
        Check(round.Begin(in.waves, in.rules, in.volumes, Round(0), 300, 1, in.points, events, error, &committed) &&
            committed && in.points.FindMonsterPoint(2, 1)->consumed == 0,
            "a later explicitly valid start can commit and reset quotas normally");
    }
    for (bool diagnostic : {false, true})
    {
        Inputs in; TriggerRound round; Events events(round, in.points); ConfigError error;
        Check(Start(round, in, in.points, events, 1, error), "baseline round for pre-commit rejection");
        in.Consume(); events.log.clear(); const auto* original = round.Waves().FindWave(7);
        events.observerValid = false;
        if (diagnostic) events.observerError = {9, "round unavailable"};
        bool committed = true;
        Check(!round.Begin(in.waves, in.rules, in.volumes, Round(0), 200, 3, in.points, events, error, &committed) && !committed,
            "entry rejection resets caller commit flag before any candidate publication");
        Check(round.Group() == 1 && round.Waves().FindWave(7) == original && !round.Waves().FindWave(30) &&
            in.points.FindMonsterPoint(2, 1)->consumed == 2 && events.log.empty(),
            "entry rejection preserves old controller and quota without a summary callback");
        events.observerValid = true; events.observerError = {}; committed = true;
        Check(!round.Begin(in.waves, in.rules, in.volumes, RoundRow{}, 200, 3, in.points, events, error, &committed) &&
            !committed && !error.message.empty(), "invalid candidate also reports an uncommitted failure");
    }
}
void EmptyActivationBoundary()
{
    Inputs in; WaveController controller; ConfigError error;
    const std::vector<std::uint16_t> selected{7, 404, 404};
    Check(controller.Prepare(in.waves, selected, error) && controller.ActiveWaves().empty(), "Prepare keeps selected runtime but active empty");
    const auto* missing = controller.FindWave(404);
    Check(missing && missing->Rows().empty(), "missing preload is a real empty runner");
    Check(controller.ActivateWave(404, 4.5f, error) && controller.ActivateWave(404, 9.25f, error), "empty runner can be repeatedly activated");
    Check(controller.FindWave(404) == missing && missing->StartOffset() == 9.25f && controller.ActiveWaves().size() == 2, "empty duplicate activation shares latest offset without deduplication");
    Check(!controller.FindWave(8) && controller.ActivateWave(8, 1, error) && controller.FindWave(8)->Rows().empty(), "global existing but not preloaded wave stays empty on activation");
    Check(controller.ActivateWave(999, std::numeric_limits<float>::max(), error) && controller.FindWave(999)->Rows().empty() && controller.FindWave(999)->StartOffset() == std::numeric_limits<float>::max(), "finite large offset is accepted for empty runtime without integer row arithmetic");
    const auto before = controller.ActiveWaves().size();
    Check(!controller.ActivateWave(1000, std::numeric_limits<float>::infinity(), error) && !controller.FindWave(1000) && controller.ActiveWaves().size() == before, "nonfinite empty activation is rejected without insertion");
    Check(!controller.ActivateWave(7, std::numeric_limits<float>::max(), error) && controller.FindWave(7)->StartOffset() == 0 && controller.ActiveWaves().size() == before, "invalid populated activation retains untouched prepared runner");
    int dispatched = 0; const auto tick = controller.Tick(10, Count, &dispatched);
    Check(tick.valid && tick.finished && tick.attempts == 0 && dispatched == 0 && controller.ActiveWaves().empty(), "one tick removes every empty activation including duplicate IDs");
    Check(controller.FindWave(404) == missing && missing->StartOffset() == 9.25f, "completed empty runtime remains addressable");
    WaveController unloaded;
    Check(!unloaded.ActivateWave(8, 0, error) && !unloaded.FindWave(8), "unloaded controller still rejects empty activation");
}

struct Reader
{
    std::ifstream file;
    explicit Reader(const char* path) : file(path, std::ios::binary) { if (!file) throw std::runtime_error("cannot open TRND fixture"); }
    std::uint32_t U32()
    {
        unsigned char b[4]; file.read(reinterpret_cast<char*>(b), 4); if (!file) throw std::runtime_error("truncated TRND");
        return b[0] | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    }
    std::int32_t I32() { return std::bit_cast<std::int32_t>(U32()); }
    float F32() { return std::bit_cast<float>(U32()); }
    std::string String()
    {
        const auto n = U32(); if (n > 65536) throw std::runtime_error("oversized TRND string");
        std::string s(n, '\0'); file.read(s.data(), n); if (!file) throw std::runtime_error("truncated TRND string"); return s;
    }
    std::vector<int> List()
    { const auto n = U32(); if (n > 10000) throw std::runtime_error("oversized TRND list"); std::vector<int> out; for (std::uint32_t i = 0; i < n; ++i) out.push_back(I32()); return out; }
};
struct Expected
{
    std::vector<RoundRow> rounds;
    std::vector<WaveRow> waves;
    std::vector<TriggerRule> rules;
    TriggerVolumes volumes;
    explicit Expected(const char* path)
    {
        Reader reader(path); Check(reader.U32() == 0x444e5254, "TRND signature");
        const auto roundsCount = reader.U32(); Check(roundsCount == 2, "independent fixture two rounds");
        for (std::uint32_t i = 0; i < roundsCount; ++i)
        { RoundRow r; r.recordIndex = reader.U32(); r.round = reader.I32(); r.waves = reader.List(); rounds.push_back(r); }
        const auto waveCount = reader.U32(); Check(waveCount == 288, "independent fixture 288 rows");
        for (std::uint32_t i = 0; i < waveCount; ++i)
        {
            WaveRow r; r.recordIndex = reader.U32(); r.waveId = reader.I32(); r.spawnPoint = reader.I32();
            r.startTime = reader.I32(); r.endTime = reader.I32(); r.interval = static_cast<std::int16_t>(reader.I32());
            r.types = {reader.I32()}; r.weightTotal = static_cast<std::int8_t>(reader.I32()); r.weights = {r.weightTotal}; r.eventMessage = reader.String(); waves.push_back(r);
        }
        const auto ruleCount = reader.U32(); Check(ruleCount == 29, "independent fixture 29 trigger rules");
        for (std::uint32_t i = 0; i < ruleCount; ++i)
        {
            TriggerRule r; r.recordIndex = reader.U32(); r.group = static_cast<std::int16_t>(reader.I32()); r.command = static_cast<char>(reader.I32());
            r.trigger = static_cast<std::int16_t>(reader.I32()); r.cooldown = static_cast<std::int16_t>(reader.I32()); r.destinationRatio = static_cast<std::int8_t>(reader.I32());
            auto a = reader.List(); r.waves.assign(a.begin(), a.end()); a = reader.List(); r.endWaves.assign(a.begin(), a.end()); rules.push_back(r);
        }
        const auto volumeCount = reader.U32(); Check(volumeCount == 30, "independent fixture 30 BSP markers");
        for (std::uint32_t i = 0; i < volumeCount; ++i)
        {
            TriggerVolume v; v.id = reader.I32(); v.entityIndex = reader.U32();
            for (auto& x : v.mins) x = reader.F32(); for (auto& x : v.maxs) x = reader.F32(); volumes.Register(v);
        }
        Check(volumes.Volumes().size() == 29 && reader.file.peek() == std::char_traits<char>::eof(), "last duplicate volume wins and exact fixture consumed");
    }
};
std::string Read(const char* path)
{ std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("cannot open actual encrypted resource"); return {std::istreambuf_iterator<char>(file), {}}; }
bool SameWave(const WaveRow& a, const WaveRow& b)
{
    return std::tie(a.recordIndex, a.waveId, a.spawnPoint, a.startTime, a.endTime, a.interval, a.types, a.weights, a.weightTotal, a.eventMessage) ==
        std::tie(b.recordIndex, b.waveId, b.spawnPoint, b.startTime, b.endTime, b.interval, b.types, b.weights, b.weightTotal, b.eventMessage);
}
void Actual(const char* wavePath, const char* roundPath, const char* trgPath, const char* fixturePath)
{
    Expected expected(fixturePath); WaveTable waves; RoundTable rounds; TriggerRuleTable rules; ConfigError error;
    const auto waveBytes = Read(wavePath), roundBytes = Read(roundPath), trgBytes = Read(trgPath);
    Check(ReadWaveResource(std::as_bytes(std::span(waveBytes.data(), waveBytes.size())), waves, error) && waves.rows.size() == 288, "actual encrypted 288-wave table loads");
    Check(ReadRoundResource(std::as_bytes(std::span(roundBytes.data(), roundBytes.size())), rounds, error) && rounds.rows.size() == 2, "actual encrypted two-round table loads");
    Check(ReadTriggerRuleResource(std::as_bytes(std::span(trgBytes.data(), trgBytes.size())), rules, error) && rules.rows.size() == 29, "actual encrypted 29-rule table loads");
    if (rounds.rows.size() != 2 || waves.rows.size() != 288 || rules.rows.size() != 29) throw std::runtime_error("actual table cardinality mismatch");
    for (std::size_t i = 0; i < expected.rounds.size(); ++i)
        Check(rounds.rows[i].recordIndex == expected.rounds[i].recordIndex && rounds.rows[i].round == expected.rounds[i].round && rounds.rows[i].waves == expected.rounds[i].waves, "actual round IDs and group values agree with independently decoded text");
    for (std::size_t i = 0; i < expected.waves.size(); ++i)
        Check(SameWave(waves.rows[i], expected.waves[i]), "actual wave row agrees with independent original CSV fields");
    TriggerRound r; SpawnPoints points; points.RegisterMonsterPoint({{}, {}, 2, 1, 1}, 4); Events events(r, points);
    for (std::size_t stage = 0; stage < 2; ++stage)
    {
        const auto group = expected.rounds[stage].waves.front();
        std::set<std::uint16_t> refs;
        for (const auto& rule : expected.rules) if (rule.group == group && rule.command == 'w') refs.insert(rule.waves.begin(), rule.waves.end());
        points.ConsumePointQuota(2, 1); r.Schedule().nextThink = 500 + static_cast<float>(stage); r.Schedule().elapsed = 31 + static_cast<float>(stage);
        events.log.clear();
        Check(r.Begin(waves, rules, expected.volumes, rounds.rows[stage], 100 + 100 * static_cast<float>(stage), 5, points, events, error), "actual stage begins with real wave rule and BSP fixtures");
        Check(r.Group() == group && Active(r).empty() && events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'r', 0}}, "actual stage publishes only empty active summary");
        Check(events.log[0].quota == 1 && points.FindMonsterPoint(2, 1)->consumed == 0 && r.Triggers().UsedTriggers().empty(), "actual stage resets quotas after summary and clears used");
        Check(r.Schedule().start == 105 + 100 * static_cast<float>(stage) && r.Schedule().nextThink == 500 + static_cast<float>(stage) && r.Schedule().elapsed == 31 + static_cast<float>(stage), "actual stage retains manager gate and elapsed");
        std::size_t totalRows = 0;
        std::set<std::uint16_t> global;
        for (const auto& row : expected.waves) global.insert(WaveKey(row.waveId));
        for (const auto id : global)
        {
            const auto* runner = r.Waves().FindWave(id);
            Check((runner != nullptr) == refs.contains(id), "actual preload membership is only current group w references");
            if (!runner) continue;
            std::vector<WaveRow> selected; for (const auto& row : expected.waves) if (WaveKey(row.waveId) == id) selected.push_back(row);
            Check(runner->Rows().size() == selected.size() && runner->Clocks().size() == selected.size() && runner->StartOffset() == 0, "actual runtime contains complete referenced row set and initial offset");
            totalRows += runner->Rows().size();
            for (std::size_t j = 0; j < selected.size(); ++j)
            {
                Check(SameWave(runner->Rows()[j], selected[j]), "actual preloaded source row identity preserved in order");
                const auto& clock = runner->Clocks()[j]; const auto& row = selected[j];
                Check(clock.next == row.startTime + (row.IsEvent() && row.types.front() != 9999 ? 2 : 0) && clock.end == row.endTime && clock.interval == row.interval, "actual preload clocks agree with original source plus event load rule");
            }
        }
        Check(totalRows == (stage == 0 ? 259u : 29u), "actual stage preloads 259 traversal or 29 boss-wave rows");
        WaveTriggerActions actions(r.Waves(), r.Schedule(), events);
        if (stage == 0)
        {
            for (const auto& rule : expected.rules)
            {
                if (rule.group != 1 || rule.command != 'w' || rule.trigger == 26) continue;
                events.log.clear(); const auto result = r.Triggers().ExecuteTrigger(rule.trigger, 12.25f, 4, actions);
                Check(result.valid && result.waveCommands == 1 && actions.Valid(), "actual stage-one trigger runs against preloaded runtime");
                for (const auto id : rule.waves)
                {
                    const auto* runner = r.Waves().FindWave(id);
                    Check(runner && !runner->Rows().empty() && runner->StartOffset() == 12.25f, "actual activation keeps nonempty preloaded source");
                }
            }
            Check(!r.Triggers().ExecuteTrigger(100, 12.25f, 4, actions).goalReached && r.Triggers().Arrivals() == 1, "actual first arrival does not reach fifty-percent destination of four");
        }
        else
        {
            Check(r.Triggers().Arrivals() == 1, "actual second stage preserves prior scan arrivals");
            events.log.clear(); const auto* box = expected.volumes.Find(30); Check(box != nullptr, "actual boss-start trigger has geometry");
            std::array<float, 3> center{}; for (std::size_t k = 0; k < center.size(); ++k) center[k] = (box->mins[k] + box->maxs[k]) * .5f;
            auto result = r.Triggers().CheckPlayer(center, 6.5f, 4, actions);
            Check(result.valid && result.waveCommands == 1 && Active(r) == std::vector<std::uint16_t>{30} && actions.Valid(), "actual second-stage spatial trigger activates boss wave 30");
            Check(events.Tags() == std::vector<std::pair<char, std::uint16_t>>{{'a', 30}, {'r', 0}} && r.Waves().FindWave(30)->Rows().size() == 29, "actual boss activation uses only its 29 preloaded rows");
            events.log.clear(); result = r.Triggers().ExecuteTrigger(26, 8, 4, actions);
            const auto* old = r.Waves().FindWave(26);
            Check(result.valid && result.waveCommands == 1 && old && old->Rows().empty() && old->StartOffset() == 8, "actual unused old trigger does not load global traversal wave into boss round");
            Check(Active(r) == std::vector<std::uint16_t>{30, 26}, "actual empty old wave is appended beside boss wave");
            r.Waves().EndWave(30); int count = 0; const auto tick = r.Waves().Tick(8, Count, &count);
            Check(tick.valid && tick.finished && count == 0 && Active(r).empty(), "actual missing old runtime completes without running its 24 global rows");
        }
    }
    Check(r.BeginMap() && !r.Group() && r.Triggers().Arrivals() == 0 && r.Triggers().UsedTriggers().empty() && !r.Waves().FindWave(30), "actual new-map reset discards both stage runtime histories");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { PreparationAndTransition(); RejectionsAndReentry(); CommittedCancellation(); EmptyActivationBoundary(); }
        else if (argc == 6 && std::string_view(argv[1]) == "--actual") Actual(argv[2], argv[3], argv[4], argv[5]);
        else throw std::runtime_error("usage: trigger_round_tests [--actual encryptedWave encryptedRound encryptedTRG TRNDfixture]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "trigger_round checks=" << checks << " failures=" << failures << '\n'; return failures ? 1 : 0;
}
