// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include "../csol-scenario-point-manager/engine_fixture.h"
#include "../../native/csol/server/scenario_map.h"
#include "fixtures.hpp"

extern "C" void zbstrigger(entvars_t*);
using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool ok, const char* text) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << text << '\n'; } }
std::string Read(const std::filesystem::path& p)
{ std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("cannot read fixture resource"); return {std::istreambuf_iterator<char>(f), {}}; }
std::string Hex(std::string_view x)
{
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string out; for (std::size_t i = 0; i < x.size(); i += 2) out.push_back(static_cast<char>(digit(x[i]) * 16 + digit(x[i + 1]))); return out;
}
std::string Catalog(int subtype = 1)
{
    std::vector<std::string> header(59), row(59, "0");
    header[0] = ";map_id"; header[1] = "map_name"; header[2] = "map_filename"; header[58] = "SubType";
    row[0] = "1"; row[1] = "test"; row[2] = "map_session_test"; row[58] = std::to_string(subtype);
    std::string out;
    for (const auto& fields : {header, row}) { for (std::size_t i = 0; i < fields.size(); ++i) { if (i) out += ','; out += fields[i]; } out += "\r\n"; }
    return out;
}
std::vector<std::string> Paths(std::string_view name)
{ return {"resource/maplist.csv", "maps/zbs_wave_" + std::string(name) + ".cso", "maps/zbs_" + std::string(name) + ".cso", "maps/ZBS_ZT_" + std::string(name) + ".cso", "maps/ZBS_TRG_" + std::string(name) + ".cso"}; }
struct MapEngine : Engine
{
    static MapEngine* current;
    std::map<std::string, std::string> files;
    struct Allocation { std::string original; std::string path; };
    std::map<void*, Allocation> allocations;
    std::vector<std::string> loads, frees;
    std::vector<int> lookups;
    std::function<void()> onLoad, onFree, onIndex;
    std::vector<CBasePlayer*> players;
    MapEngine()
    {
        current = this; globals.maxClients = 4;
        g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
        g_engfuncs.pfnPEntityOfEntIndex = ClientIndex; g_engfuncs.pfnSetModel = SetModel;
    }
    ~MapEngine()
    {
        Check(allocations.empty(), "all engine file allocations released");
        for (auto& [p, unused] : allocations) delete[] static_cast<unsigned char*>(p);
        ClearWorld(); current = nullptr;
    }
    void ClearWorld()
    {
        for (auto* player : players)
        {
            auto* ed = player->edict(); player->~CBasePlayer(); std::free(player); ed->pvPrivateData = nullptr;
        }
        players.clear(); ClearEdicts();
    }
    static unsigned char* Load(const char* name, int* length)
    {
        auto& e = *current; Check(*length == 0, "owner initializes engine file length before callback");
        e.loads.emplace_back(name); if (e.onLoad) e.onLoad();
        const auto found = e.files.find(name); if (found == e.files.end()) return nullptr;
        *length = static_cast<int>(found->second.size());
        auto* p = new unsigned char[found->second.size() + 1];
        std::memcpy(p, found->second.data(), found->second.size()); p[found->second.size()] = 0;
        e.allocations.emplace(p, Allocation{found->second, name}); return p;
    }
    static void Free(void* p)
    {
        auto& e = *current; const auto it = e.allocations.find(p); Check(it != e.allocations.end(), "file release owns exact live allocation");
        if (it == e.allocations.end()) throw std::runtime_error("double file release");
        const auto& saved = it->second.original;
        Check(std::memcmp(p, saved.data(), saved.size()) == 0 && static_cast<unsigned char*>(p)[saved.size()] == 0, "owner readers preserve engine bytes and extra NUL");
        e.frees.push_back(it->second.path); if (e.onFree) e.onFree();
        std::memset(p, 0xcc, saved.size() + 1); delete[] static_cast<unsigned char*>(p); e.allocations.erase(it);
    }
    static edict_t* ClientIndex(int slot)
    {
        auto& e = *current; e.lookups.push_back(slot); if (e.onIndex) e.onIndex();
        return Engine::FromOffset(slot);
    }
    static void SetModel(edict_t*, const char*) {} // Controlled mins/maxs supplied by frozen BSP fixture.
    edict_t* Raw(const char* classname, int index)
    {
        auto ed = std::make_unique<edict_t>(); ed->v.pContainingEntity = ed.get(); ed->serialnumber = 1;
        auto* raw = ed.get(); indices[raw] = index; edicts.push_back(std::move(ed)); raw->v.classname = AllocString(classname); return raw;
    }
    CBasePlayer& Client(int slot, Vector origin)
    {
        auto* ed = Raw("player", slot); auto* p = new(&ed->v) CBasePlayer;
        p->pev = &ed->v; p->m_pEntity = nullptr; p->pev->health = 100; p->pev->deadflag = DEAD_NO; p->pev->origin = origin;
        players.push_back(p); return *p;
    }
    CCSOLScenarioTrigger& Trigger(int index, int id, Vector mins, Vector maxs)
    {
        auto* ed = Raw("zbstrigger", index); ed->v.model = AllocString("*1"); zbstrigger(&ed->v);
        auto& trigger = *static_cast<CCSOLScenarioTrigger*>(ed->pvPrivateData); trigger.m_iId = id;
        trigger.Spawn(); trigger.pev->mins = mins; trigger.pev->maxs = maxs; return trigger;
    }
    void SyntheticFiles()
    {
        auto paths = Paths("map_session_test"); files[paths[0]] = Catalog();
        files[paths[1]] = Hex(fixtures::waves); files[paths[2]] = Hex(fixtures::rounds);
        files[paths[3]] = Hex(fixtures::types); files[paths[4]] = Hex(fixtures::triggers);
    }
    void Markers(bool spawn = true, bool trigger = true)
    {
        if (spawn) { Player(101, 1); Player(102, 1); Player(103, 2); Monster(201, 1, 5); }
        if (trigger) { Trigger(301, 1, Vector(-5, -5, -5), Vector(5, 5, 5)); Trigger(302, 2, Vector(95, -5, -5), Vector(105, 5, 5)); Trigger(330, 30, Vector(195, -5, -5), Vector(205, 5, 5)); }
    }
};
MapEngine* MapEngine::current{};
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
    CCSOLScenarioMap& map; std::vector<char> tags; std::function<void(char)> callback;
    std::vector<std::vector<std::uint16_t>> summaries; std::vector<std::uint32_t> quotas;
    explicit Events(CCSOLScenarioMap& m) : map(m) {}
    void Invoke(char c) { tags.push_back(c); if (callback) callback(c); }
    void WaveActivated(std::uint16_t) override { Invoke('a'); }
    void BeforeSpatialWaveEnd(std::uint16_t) override { Invoke('e'); }
    void WavesChanged(std::span<const std::uint16_t> ids) override
    { summaries.emplace_back(ids.begin(), ids.end()); const auto* p = map.SpawnPoints().Points().FindMonsterPoint(1, 1); quotas.push_back(p ? p->consumed : 0); Invoke('r'); }
    void GoalReached() override { Invoke('g'); }
};
void DenyAll(CCSOLScenarioMap& map, Events& events)
{
    const auto state = map.State(); const auto* tables = map.Tables(); const auto index = map.RoundIndex();
    const auto error = map.Error().detail.message;
    Check(!map.BeginMap("map_session_test") && !map.EndMap() && !map.CollectEntities() && !map.BeginRound(0, 0, 0, events), "busy owner denies all four lifecycle operations");
    Check(map.State() == state && map.Tables() == tables && map.RoundIndex() == index && map.Error().detail.message == error, "rejected nested operations preserve caller state tables index and error");
}
void ResourceAndLifecycle()
{
    MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
    Check(map.State() == CSOLScenarioMapState::Empty && !map.Tables() && !map.RoundIndex(), "owner starts empty");
    Check(!map.CollectEntities() && !map.BeginRound(0, 1, 0, events), "owner cannot collect or start before resource session");
    int fileReentries = 0;
    engine.onLoad = [&] { ++fileReentries; DenyAll(map, events); }; engine.onFree = engine.onLoad;
    Check(map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::ResourcesLoaded, "owner loads subtype1 through real SDK engine function table");
    engine.onLoad = {}; engine.onFree = {};
    Check(engine.loads == Paths("map_session_test") && engine.frees == engine.loads && fileReentries == 10 && engine.allocations.empty(), "exact catalog plus four resource order and each allocation released once");
    Check(map.Tables()->scenarioType == 1 && map.Tables()->rounds.rows.size() == 2 && !map.BeginRound(0, 0, 0, events), "resources alone do not permit round start");
    int removalCallbacks = 0; engine.onRemove = [&](CBaseEntity&) { ++removalCallbacks; DenyAll(map, events); };
    Check(map.CollectEntities() && map.State() == CSOLScenarioMapState::EntitiesCollected, "owner collects real exported marker objects");
    engine.onRemove = {};
    Check(removalCallbacks == 7 && engine.removes == 7 && map.SpawnPoints().Points().Monsters().size() == 1 && map.TriggerVolumes().Volumes().Volumes().size() == 3, "point and brush removals consumed once with snapshots retained");
    const int searches = engine.searches, removes = engine.removes; const auto files = engine.loads.size();
    Check(map.CollectEntities() && engine.searches == searches && engine.removes == removes && engine.loads.size() == files, "repeated collection never rereads files or consumed edicts");
    auto* first = map.SpawnPoints().SelectPlayerSpawn(2, 1, 0); Check(first && first->pev->origin.x == 101, "first spawn proxy selection before round");
    map.SpawnPoints().Points().ConsumePointQuota(1, 1); map.Round().Schedule() = {10, 11, 12};
    events.callback = [&](char) { DenyAll(map, events); };
    Check(map.BeginRound(0, 100, 5, events) && map.State() == CSOLScenarioMapState::RoundActive && map.RoundIndex() == 0, "owner commits formal round after guarded empty summary");
    events.callback = {};
    Check(events.tags == std::vector<char>{'r'} && events.summaries[0].empty() && events.quotas[0] == 1 && map.SpawnPoints().Points().FindMonsterPoint(1, 1)->consumed == 0, "owner notifies before quota clear without exposing group as active");
    Check(map.Round().Group() == 1 && map.Round().Waves().FindWave(7) && !map.Round().Waves().FindWave(30) && map.Round().Schedule().nextThink == 12 && map.Round().Schedule().elapsed == 11, "owner selects group from loaded round and preserves manager clock");
    auto* second = map.SpawnPoints().SelectPlayerSpawn(2, 1, 0); Check(second && second->pev->origin.x == 102, "ordinary round preserves separate player-spawn cursor");
    engine.Client(1, Vector(100, 0, 0)); engine.Client(2, Vector(100, 0, 0));
    WaveTriggerActions actions(map.Round().Waves(), map.Round().Schedule(), events);
    map.Round().Triggers().ExecuteTrigger(1, 0, 4, actions);
    Check(map.PlayerScan().Update(true, map.Round().Triggers(), 0, 4, actions).valid && map.PlayerScan().NextPlayerSlot() == 2 && map.Round().Triggers().Arrivals() == 1, "real scanner performs first slot and accumulates one goal arrival");
    map.Round().Schedule().nextThink = 888; map.SpawnPoints().Points().ConsumePointQuota(1, 1);
    Check(map.BeginRound(0, 200, 5, events) && map.PlayerScan().NextPlayerSlot() == 2 && map.Round().Triggers().Arrivals() == 1 && map.Round().Triggers().UsedTriggers().empty(), "same-round formal restart retains scanner cursor and arrivals while reinstalling rules");
    Check(map.Round().Schedule().nextThink == 888 && map.Round().Triggers().ExecuteTrigger(1, 0, 4, actions).waveCommands == 1, "same-round source reload preserves gate until once-only rule can fire again");
    Check(map.PlayerScan().Update(true, map.Round().Triggers(), 0, 4, actions).goalReached && map.PlayerScan().NextPlayerSlot() == 3 && engine.lookups == std::vector<int>{1, 2}, "next scan resumes at second slot and retained arrival reaches threshold");
    Check(map.BeginRound(1, 300, 6, events) && map.RoundIndex() == 1 && map.Round().Group() == 0 && map.PlayerScan().NextPlayerSlot() == 3 && !map.Round().Waves().FindWave(7), "second stage replaces preloads without resetting player scan cursor");
    const auto* tables = map.Tables(); const auto* boss = map.Round().Waves().FindWave(30); const auto notifications = events.tags.size();
    Check(!map.BeginRound(2, 0, 0, events) && map.State() == CSOLScenarioMapState::RoundActive && map.RoundIndex() == 1 && map.Tables() == tables && map.Round().Waves().FindWave(30) == boss && events.tags.size() == notifications, "out-of-range host round index leaves active round intact");
    Check(!map.BeginRound(0, std::numeric_limits<float>::quiet_NaN(), 0, events) && map.RoundIndex() == 1 && map.Round().Group() == 0 && map.Round().Waves().FindWave(30) == boss, "invalid round time preserves previous active stage");
    engine.onIndex = [&] { Check(map.PlayerScan().IsScanning() && !map.Round().Triggers().IsExecuting(), "INDEXENT callback occurs while scanner busy before trigger execution"); DenyAll(map, events); };
    Check(map.PlayerScan().Update(true, map.Round().Triggers(), 0, 4, actions).valid && map.PlayerScan().NextPlayerSlot() == 4, "INDEXENT callback rejects destructive owner reentry then outer cursor advances"); engine.onIndex = {};

    // External Round() access may bypass the owner operation flag. The round's
    // own starting_ flag must still prevent its owner from clearing it mid-call.
    events.callback = [&](char) { DenyAll(map, events); }; ConfigError detail;
    Check(map.Round().Begin(tables->waves, *tables->triggerRules, map.TriggerVolumes().Volumes(), tables->rounds.rows[1], 400, 5, map.SpawnPoints().Points(), events, detail), "external direct round start is protected by IsBusy during its observer");
    events.callback = [&](char) { Check(map.Round().Triggers().IsExecuting(), "trigger observer runs under program execution guard"); DenyAll(map, events); };
    map.Round().Triggers().ExecuteTrigger(30, 0, 4, actions);
    events.callback = {};
    struct Context { CCSOLScenarioMap* map; Events* events; int calls{}; } context{&map, &events};
    const auto dispatch = [](void* p, const WaveRow&, RowClock&) { auto& c = *static_cast<Context*>(p); ++c.calls; DenyAll(*c.map, *c.events); };
    Check(map.Round().Waves().Tick(0, dispatch, &context).valid && context.calls == 1, "wave callback cannot reset map ownership mid-dispatch");
    const std::string_view aliased = map.Tables()->mapName; const auto removed = engine.removes;
    Check(map.BeginMap(aliased) && map.State() == CSOLScenarioMapState::ResourcesLoaded && !map.RoundIndex() && map.PlayerScan().NextPlayerSlot() == 1 && map.Round().Triggers().Arrivals() == 0, "same-map begin safely copies aliased name and resets map-owned lifecycle");
    Check(map.SpawnPoints().Points().Players().empty() && map.TriggerVolumes().Volumes().Volumes().empty() && engine.removes == removed, "new session clears snapshots without touching former marker edicts");
    Check(!map.CollectEntities() && map.State() == CSOLScenarioMapState::Failed && !map.BeginRound(0, 0, 0, events), "same-map without newly loaded edicts does not falsely become playable");
    engine.ClearWorld(); const auto calls = engine.calls.size();
    Check(map.EndMap() && map.State() == CSOLScenarioMapState::Empty && !map.Tables() && map.Error().detail.message.empty() && engine.calls.size() == calls, "EndMap after engine destroys edicts dereferences no stale handle");
}
void CommittedRoundCancellation()
{
    for (bool diagnostic : {false, true})
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        Check(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 100, 1, events),
            "actual Map begins the first phase before callback cancellation");
        const auto generation = map.RoundGeneration();
        Check(map.SpawnPoints().Points().ConsumePointQuota(1, 1), "actual Map quota is consumed before attempted transition");
        events.tags.clear(); events.summaries.clear(); events.quotas.clear();
        events.callback = [&](char kind)
        {
            if (kind != 'r') return;
            Check(map.RoundIndex() == 1 && map.RoundGeneration() > generation && map.Round().Group() == 0,
                "actual Map callback sees the new phase generation with committed controller");
            events.observerValid = false;
            if (diagnostic) events.observerError = {23, "map round summary failed"};
        };
        Check(!map.BeginRound(1, 200, 2, events), "actual Map propagates post-commit observer rejection");
        Check(map.RoundIndex() == 1 && map.RoundGeneration() > generation && map.Round().Group() == 0 &&
            map.Round().Waves().FindWave(30) && !map.Round().Waves().FindWave(7) && map.Round().Schedule().start == 202,
            "actual Map does not restore old phase or generation around the newly committed controller");
        Check(map.SpawnPoints().Points().FindMonsterPoint(1, 1)->consumed == 1 && events.tags == std::vector<char>{'r'},
            "actual Map post-commit cancellation leaves quota untouched and sends no extra summary");
        Check(map.State() == (diagnostic ? CSOLScenarioMapState::Failed : CSOLScenarioMapState::RoundActive) &&
            map.Error().detail.message == (diagnostic ? "map round summary failed" : ""),
            "actual Map distinguishes a diagnosed failure from cancelled committed work");
        events.callback = {}; engine.ClearWorld(); Check(map.EndMap(), "cancelled actual Map can retire normally");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        Check(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 100, 1, events),
            "actual Map entry-cancel baseline is active");
        const auto generation = map.RoundGeneration(); const auto* runner = map.Round().Waves().FindWave(7);
        events.observerValid = false; events.tags.clear();
        Check(!map.BeginRound(1, 200, 2, events) && map.RoundGeneration() == generation && map.RoundIndex() == 0 &&
            map.Round().Group() == 1 && map.Round().Waves().FindWave(7) == runner && events.tags.empty(),
            "actual Map restores provisional phase only when observer cancellation occurred before commit");
        engine.ClearWorld(); Check(map.EndMap(), "pre-commit cancelled Map retires normally");
    }
}
void Failures()
{
    for (const auto& path : Paths("map_session_test"))
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        Check(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 0, 0, events), "old live session exists before file failure");
        engine.files.erase(path); Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed && !map.Tables(), "missing catalog or required table clears old map atomically");
        Check(!map.RoundIndex() && !map.Round().Group() && map.PlayerScan().NextPlayerSlot() == 1 && map.SpawnPoints().Points().Players().empty() && map.TriggerVolumes().Volumes().Volumes().empty(), "failed new map cannot retain old resources entities round or scanner");
        Check(map.Error().path == path && !map.Error().detail.message.empty() && engine.allocations.empty() && !map.CollectEntities() && !map.BeginRound(0, 0, 0, events), "file failure identifies exact engine path and disables dependent lifecycle");
    }
    for (int subtype : {0, 3, 4, 10})
    {
        MapEngine engine; engine.SyntheticFiles(); engine.files["resource/maplist.csv"] = Catalog(subtype); CCSOLScenarioMap map; Events events(map);
        Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed && !map.CollectEntities() && !map.BeginRound(0, 0, 0, events), "owner explicitly refuses all unsupported subtype profiles");
        Check(!map.Error().detail.message.empty() && map.Round().Group() == std::nullopt, "unsupported subtype never creates a usable round");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); CCSOLScenarioMap map; Events events(map);
        const auto api = g_engfuncs.pfnFreeFile; g_engfuncs.pfnFreeFile = nullptr;
        Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed && engine.loads.empty(), "missing engine release API is rejected before any load"); g_engfuncs.pfnFreeFile = api;
        Check(!map.BeginMap("../zs_deadend") && engine.loads.empty(), "invalid map name is rejected before engine callbacks");
        engine.files[Paths("map_session_test")[2]] = Hex(fixtures::emptyRounds);
        Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed && !map.BeginRound(0, 0, 0, events), "empty round table never becomes an active scenario");
    }
    for (int kind = 0; kind < 2; ++kind)
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(kind != 0, kind != 1); CCSOLScenarioMap map; Events events(map);
        Check(map.BeginMap("map_session_test") && !map.CollectEntities() && map.State() == CSOLScenarioMapState::Failed, "all-empty SP or MAP_TRG fallback is explicit failure");
        Check(map.Error().detail.message.find(kind == 0 ? "SP spawn-file" : "MAP_TRG") != std::string::npos && !map.BeginRound(0, 0, 0, events), "unsupported fallback reason is surfaced instead of Ready");
        const auto searches = engine.searches; Check(!map.CollectEntities() && engine.searches == searches, "failed fallback does not repeatedly consume markers");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); engine.createFailure = Engine::CreateFailure::Null;
        CCSOLScenarioMap map; Events events(map); Check(map.BeginMap("map_session_test"), "proxy retry map loads");
        Check(!map.CollectEntities() && map.State() == CSOLScenarioMapState::ResourcesLoaded && engine.removes == 0 && !map.BeginRound(0, 0, 0, events), "proxy creation failure retains resources and all markers for retry");
        engine.createFailure = Engine::CreateFailure::None;
        Check(map.CollectEntities() && map.State() == CSOLScenarioMapState::EntitiesCollected && map.Error().detail.message.empty() && map.BeginRound(0, 0, 0, events), "corrected proxy service recovers collection and formal start");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.files[Paths("map_session_test")[4]] = Hex(fixtures::unsupported); engine.Markers();
        CCSOLScenarioMap map; Events events(map); Check(map.BeginMap("map_session_test") && map.CollectEntities(), "unknown trigger command resource can be inspected before execution");
        Check(!map.BeginRound(0, 0, 0, events) && map.State() == CSOLScenarioMapState::EntitiesCollected && !map.RoundIndex() && events.tags.empty(), "unsupported command rejects first round without claiming RoundActive");
    }
}
class Reader
{
    std::ifstream file;
public:
    explicit Reader(const char* path) : file(path, std::ios::binary) { if (!file) throw std::runtime_error("missing independent BSP fixture"); }
    std::uint32_t U32() { unsigned char b[4]; file.read(reinterpret_cast<char*>(b), 4); if (!file) throw std::runtime_error("truncated BSP fixture"); return b[0] | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24); }
    int I32() { return std::bit_cast<std::int32_t>(U32()); }
    float F32() { return std::bit_cast<float>(U32()); }
    std::string String() { const auto n = U32(); if (n > 65536) throw std::runtime_error("oversized BSP string"); std::string s(n, '\0'); file.read(s.data(), n); if (!file) throw std::runtime_error("truncated string"); return s; }
    bool Done() { return file.peek() == std::char_traits<char>::eof(); }
};
void Actual(const char* common, const char* pointPath, const char* volumePath)
{
    MapEngine engine; for (const auto& name : Paths("zs_deadend")) engine.files[name] = Read(std::filesystem::path(common) / name);
    Reader sp(pointPath); Check(sp.U32() == 0x544e5053 && sp.U32() == 184, "actual independent 184 point marker fixture");
    std::map<std::int32_t, MonsterPoint> monsters; std::map<std::int32_t, std::vector<SpawnPoint>> players;
    std::vector<int> expectedRemoval, playerRemoval;
    for (unsigned i = 0; i < 184; ++i)
    {
        SpawnPoint expected; expected.entityIndex = sp.U32(); const auto kind = sp.U32(); expected.id = sp.I32(); expected.team = sp.I32(); const auto maximum = sp.I32();
        for (auto& x : expected.origin) x = sp.F32(); for (auto& x : expected.angles) x = sp.F32(); const auto key = sp.I32(); const auto count = sp.U32();
        auto& entity = engine.Add(kind ? "zombiespawn" : "playerspawn", static_cast<int>(expected.entityIndex));
        if (kind) { auto& m = static_cast<CCSOLMonsterSpawn&>(entity); m.m_iId = 0; m.m_iTeam = 3; m.m_iMaxSpawn = 0; }
        else { auto& p = static_cast<CCSOLPlayerSpawn&>(entity); p.m_iId = 0; p.m_iTeam = 0; }
        entity.pev->origin = Vector(expected.origin[0], expected.origin[1], expected.origin[2]); entity.pev->angles = Vector(expected.angles[0], expected.angles[1], expected.angles[2]);
        for (unsigned j = 0; j < count; ++j) { const auto name = sp.String(), value = sp.String(); KeyValueData kv{nullptr, const_cast<char*>(name.c_str()), const_cast<char*>(value.c_str()), 0}; entity.KeyValue(&kv); }
        entity.Spawn();
        if (kind) { monsters[key] = {expected, maximum, 0}; expectedRemoval.push_back(static_cast<int>(expected.entityIndex)); }
        else { players[key].push_back(expected); playerRemoval.push_back(static_cast<int>(expected.entityIndex)); }
    }
    expectedRemoval.insert(expectedRemoval.end(), playerRemoval.begin(), playerRemoval.end()); Check(sp.Done(), "point fixture consumed exactly");
    Reader tr(volumePath); Check(tr.U32() == 0x56475254 && tr.U32() == 30, "actual independent 30 trigger marker fixture");
    std::map<std::int16_t, TriggerVolume> expectedVolumes;
    for (unsigned i = 0; i < 30; ++i)
    {
        TriggerVolume v; v.entityIndex = tr.U32(); v.id = tr.I32(); tr.I32(); std::array<float, 18> values; for (auto& x : values) x = tr.F32(); const auto key = tr.I32();
        std::copy_n(values.begin() + 6, 3, v.mins.begin()); std::copy_n(values.begin() + 9, 3, v.maxs.begin());
        const auto keys = tr.U32(); auto& entity = engine.Trigger(static_cast<int>(v.entityIndex), 0, Vector(v.mins[0], v.mins[1], v.mins[2]), Vector(v.maxs[0], v.maxs[1], v.maxs[2]));
        for (unsigned j = 0; j < keys; ++j) { const auto name = tr.String(), value = tr.String(); KeyValueData kv{nullptr, const_cast<char*>(name.c_str()), const_cast<char*>(value.c_str()), 0}; entity.KeyValue(&kv); }
        expectedVolumes[static_cast<std::int16_t>(key)] = v; expectedRemoval.push_back(static_cast<int>(v.entityIndex));
    }
    Check(tr.Done(), "trigger fixture consumed exactly");
    CCSOLScenarioMap map; Events events(map);
    Check(map.BeginMap("zs_deadend") && map.State() == CSOLScenarioMapState::ResourcesLoaded && engine.loads == Paths("zs_deadend") && engine.frees == engine.loads, "actual owner resolves catalog and reads four encrypted map resources through SDK callbacks");
    Check(map.Tables()->waves.rows.size() == 288 && map.Tables()->rounds.rows.size() == 2 && map.Tables()->monsterTypes.rows.size() == 38 && map.Tables()->triggerRules->rows.size() == 29, "actual owner owns 288/2/38/29 table data after engine frees");
    Check(map.CollectEntities() && engine.removed == expectedRemoval && engine.removes == 214 && engine.creates == 1, "actual owner consumes 184 point and 30 brush markers with one proxy");
    const auto& points = map.SpawnPoints().Points(); Check(points.Monsters().size() == 164 && points.Players().size() == 2 && map.TriggerVolumes().Volumes().Volumes().size() == 29, "actual duplicate IDs and player groups retained");
    for (const auto& [key, m] : monsters)
    { const auto& got = points.Monsters().at(key); Check(got.point.origin == m.point.origin && got.point.angles == m.point.angles && got.point.entityIndex == m.point.entityIndex && got.point.team == m.point.team && got.point.id == m.point.id && got.maxSpawn == m.maxSpawn, "actual owner monster snapshots match independent complete fields"); }
    for (const auto& [key, list] : players)
    { const auto& got = points.Players().at(key); Check(got.size() == list.size(), "actual owner player group count preserved"); for (std::size_t i = 0; i < list.size(); ++i) Check(got[i].origin == list[i].origin && got[i].angles == list[i].angles && got[i].entityIndex == list[i].entityIndex && got[i].team == list[i].team && got[i].id == list[i].id, "actual owner player order and snapshots match original BSP fixture"); }
    for (const auto& [key, v] : expectedVolumes)
    { const auto* got = map.TriggerVolumes().Volumes().Find(key); Check(got && got->id == v.id && got->entityIndex == v.entityIndex && got->mins == v.mins && got->maxs == v.maxs, "actual owner geometry including duplicate50 winner matches independent bounds"); }
    const auto searches = engine.searches, removes = engine.removes;
    Check(map.CollectEntities() && engine.searches == searches && engine.removes == removes, "actual owner collection is exactly once");
    Check(map.BeginRound(0, 100, 5, events) && map.Round().Group() == 1 && map.RoundIndex() == 0 && map.Round().Waves().ActiveWaves().empty(), "actual owner begins traversal stage with empty active waves");
    const auto* goal = map.TriggerVolumes().Volumes().Find(100); const Vector center((goal->mins[0] + goal->maxs[0]) * .5f, (goal->mins[1] + goal->maxs[1]) * .5f, (goal->mins[2] + goal->maxs[2]) * .5f);
    engine.Client(1, center); engine.Client(2, center); WaveTriggerActions actions(map.Round().Waves(), map.Round().Schedule(), events);
    Check(map.PlayerScan().Update(true, map.Round().Triggers(), 10, 4, actions).valid && map.PlayerScan().NextPlayerSlot() == 2 && map.Round().Triggers().Arrivals() == 1, "actual real SDK player contributes one traversal goal arrival");
    map.Round().Schedule().nextThink = 777;
    Check(map.BeginRound(1, 200, 6, events) && map.Round().Group() == 0 && map.RoundIndex() == 1 && map.PlayerScan().NextPlayerSlot() == 2 && map.Round().Triggers().Arrivals() == 1 && map.Round().Schedule().nextThink == 777, "actual owner stage switch preserves scan cursor arrivals and half-second gate");
    Check(!map.Round().Waves().FindWave(1) && map.Round().Waves().FindWave(30)->Rows().size() == 29, "actual owner boss stage replaces traversal preloads");
    const auto* boss = map.TriggerVolumes().Volumes().Find(30); auto& player = *engine.players[1]; player.pev->origin = Vector((boss->mins[0] + boss->maxs[0]) * .5f, (boss->mins[1] + boss->maxs[1]) * .5f, (boss->mins[2] + boss->maxs[2]) * .5f);
    Check(map.PlayerScan().Update(true, map.Round().Triggers(), 2, 4, actions).waveCommands == 1 && map.PlayerScan().NextPlayerSlot() == 3 && map.Round().Waves().ActiveWaves().size() == 1 && map.Round().Waves().ActiveWaves()[0] == 30 && actions.Valid(), "actual next player slot triggers preloaded boss wave via full scanner adapter chain");
    Check(engine.searches == searches && engine.removes == removes && engine.loads.size() == 5, "actual two rounds never reread resources or recollect marker snapshots");
    engine.ClearWorld(); Check(map.EndMap() && !map.Tables() && !map.RoundIndex() && map.PlayerScan().NextPlayerSlot() == 1, "actual map teardown safe after engine edicts freed");
}
void ObserverSequence(CCSOLScenarioMap& map, Events& events)
{
    int notifications = 0; std::size_t expectedIndex{};
    events.callback = [&](char tag)
    {
        Check(tag == 'r', "formal round emits summary notification only");
        ++notifications;
        Check(map.State() == CSOLScenarioMapState::RoundActive, "observer sees RoundActive owner state");
        Check(map.RoundIndex() == expectedIndex, "observer sees new owner index before notification");
        Check(map.Round().Group() == TriggerKey(map.Tables()->rounds.rows[expectedIndex].waves.front()), "observer sees group from same published owner round index");
        Check(map.Error().path.empty() && map.Error().detail.message.empty(), "observer sees cleared previous failure before new successful notification");
        DenyAll(map, events);
    };
    Check(!map.BeginRound(0, std::numeric_limits<float>::quiet_NaN(), 0, events) && map.State() == CSOLScenarioMapState::EntitiesCollected && !map.RoundIndex() && notifications == 0, "failed first candidate restores pre-round owner state without callback");
    expectedIndex = 0;
    Check(map.BeginRound(0, 100, 5, events) && map.RoundIndex() == 0 && map.State() == CSOLScenarioMapState::RoundActive, "successful first stage publishes owner and component consistently");
    const auto* first = map.Round().Waves().FindWave(7); const auto* tables = map.Tables();
    Check(!map.BeginRound(999, 0, 0, events) && map.RoundIndex() == 0 && map.State() == CSOLScenarioMapState::RoundActive && map.Tables() == tables && notifications == 1, "invalid host index preserves old owner and does not notify");
    Check(!map.BeginRound(1, std::numeric_limits<float>::infinity(), 0, events) && map.RoundIndex() == 0 && map.State() == CSOLScenarioMapState::RoundActive && map.Round().Group() == 1 && map.Round().Waves().FindWave(7) == first && notifications == 1, "component time rejection rolls back staged owner index/state");
    expectedIndex = 1;
    Check(map.BeginRound(1, 200, 6, events) && map.RoundIndex() == 1 && map.Round().Group() == 0 && notifications == 2, "second-stage notification commits matching owner index and component");
    Check(!map.BeginRound(0, std::numeric_limits<float>::quiet_NaN(), 0, events) && map.RoundIndex() == 1 && map.State() == CSOLScenarioMapState::RoundActive && map.Round().Group() == 0 && notifications == 2, "later failed first-stage candidate restores second-stage owner");
    expectedIndex = 1;
    Check(map.BeginRound(1, 300, 5, events) && notifications == 3 && map.Error().detail.message.empty(), "successful retry clears error before callback and after return");
    events.callback = {};
}
void Observer()
{
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        Check(map.BeginMap("map_session_test") && map.CollectEntities(), "observer fixture has collected map resources");
        ObserverSequence(map, events);
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.files[Paths("map_session_test")[4]] = Hex(fixtures::unsupported); engine.Markers();
        CCSOLScenarioMap map; Events events(map); Check(map.BeginMap("map_session_test") && map.CollectEntities(), "unsupported-trigger rollback fixture loaded");
        Check(!map.BeginRound(0, 0, 0, events) && map.State() == CSOLScenarioMapState::EntitiesCollected && !map.RoundIndex() && !map.Round().Group() && events.tags.empty(), "unsupported first group restores EntitiesCollected/no index without notification");
        int callbacks = 0;
        events.callback = [&](char)
        {
            ++callbacks;
            Check(map.State() == CSOLScenarioMapState::RoundActive && map.RoundIndex() == 1 && map.Round().Group() == 0 && map.Error().detail.message.empty(), "valid empty group notification after rejection observes new index/state/cleared error");
            DenyAll(map, events);
        };
        Check(map.BeginRound(1, 100, 0, events) && callbacks == 1, "valid second group can recover from first-group validation failure");
        Check(!map.BeginRound(0, 100, 0, events) && map.RoundIndex() == 1 && map.State() == CSOLScenarioMapState::RoundActive && map.Round().Group() == 0 && callbacks == 1, "unsupported group rollback preserves running owner and component");
        Check(map.BeginRound(1, 200, 0, events) && callbacks == 2, "second successful retry publishes clean observer state");
        events.callback = {};
    }
}
void ManagerState()
{
    const auto require = [](bool ok, const char* message)
    { Check(ok, message); if (!ok) throw std::runtime_error(message); };
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        bool due = true;
        Check(map.ClearPoint() == -1 && map.MonsterDrainAt() == 0 && map.MonsterDrainTeam() == 0,
            "new manager starts with disabled progress and idle unfiltered drain");
        Check(!map.EnableMonsterClearPoint(5) && !map.ScheduleMonsterDrain(20, 1) &&
            !map.AdvanceMonsterDrain(20, due) && !due,
            "manager rejects work before a round and clears caller due output");
        require(map.BeginMap("map_session_test"), "manager fixture loads real map resources");
        Check(!map.EnableMonsterClearPoint(5) && !map.ScheduleMonsterDrain(20, 1),
            "loaded resources alone do not enable manager work");
        require(map.CollectEntities(), "manager fixture collects actual marker entities");
        Check(!map.EnableMonsterClearPoint(5) && !map.ScheduleMonsterDrain(20, 1),
            "collected entities alone do not enable manager work");
        int notifications = 0;
        std::size_t expectedIndex{};
        std::int8_t expectedFilter{};
        events.callback = [&](char tag)
        {
            ++notifications;
            Check(tag == 'r' && map.State() == CSOLScenarioMapState::RoundActive &&
                map.RoundIndex() == expectedIndex && map.ClearPoint() == -1 && map.MonsterDrainAt() == 0 &&
                map.MonsterDrainTeam() == expectedFilter && !map.WaveDispatchStopped(),
                "round observer sees reset progress/deadline and retained filter in newly committed phase");
        };
        require(map.BeginRound(0, 10, 0, events), "manager round begins");
        Check(map.EnableMonsterClearPoint(0) && map.EnableMonsterClearPoint(-4) && map.ClearPoint() == -1,
            "nonpositive clear points leave progress disabled");
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
            Check(!map.EnableMonsterClearPoint(invalid) && map.ClearPoint() == -1,
                "nonfinite clear points cannot enable progress");
        require(map.EnableMonsterClearPoint(12), "positive clear points enable progress");
        Check(map.ClearPoint() == 0 && map.EnableMonsterClearPoint(0) && map.EnableMonsterClearPoint(18) &&
            map.ClearPoint() == 0, "spawn enabling is idempotent and does not award the type points");

        const auto* filter = &map.MonsterDrainTeam();
        require(map.ScheduleMonsterDrain(20, 1), "schedule manager drain");
        for (const std::int8_t invalid : {std::int8_t(-1), std::int8_t(3)})
            Check(!map.ScheduleMonsterDrain(90, invalid) && map.MonsterDrainAt() == 20 && *filter == 1,
                "invalid team rejects scheduling without changing existing clock/filter");
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        {
            due = true;
            Check(!map.ScheduleMonsterDrain(invalid, 2) && !map.AdvanceMonsterDrain(invalid, due) &&
                !due && map.MonsterDrainAt() == 20 && *filter == 1,
                "nonfinite time rejects both operations without committing manager state");
        }
        due = true;
        Check(map.AdvanceMonsterDrain(std::nextafter(20.f, 0.f), due) && !due && map.MonsterDrainAt() == 20,
            "immediately before deadline is not due");
        Check(map.AdvanceMonsterDrain(20, due) && due &&
            std::bit_cast<std::uint32_t>(map.MonsterDrainAt()) == 0x41a0cccdu,
            "equality is due and commits float32 deadline 20.1000003814697265625");
        Check(map.AdvanceMonsterDrain(100, due) && due &&
            std::bit_cast<std::uint32_t>(map.MonsterDrainAt()) == 0x41a1999au,
            "late invocation advances old deadline once to 20.200000762939453125");
        map.CompleteMonsterDrain();
        due = true;
        Check(map.MonsterDrainAt() == 0 && *filter == 1 && map.AdvanceMonsterDrain(100, due) && !due,
            "completed drain keeps filter and zero deadline stays idle");
        require(map.ScheduleMonsterDrain(0, 2), "zero deadline may retain a new filter");
        due = true;
        Check(filter == &map.MonsterDrainTeam() && *filter == 2 && map.AdvanceMonsterDrain(100, due) && !due,
            "stable filter reference observes update while zero deadline remains idle");

        require(map.ScheduleMonsterDrain(60, 2) && map.StopWaveDispatch(), "prepare active manager rollback state");
        const auto generation = map.RoundGeneration();
        const auto* wave = map.Round().Waves().FindWave(7);
        const auto unchanged = [&]
        {
            return map.RoundIndex() == 0 && map.RoundGeneration() == generation &&
                map.State() == CSOLScenarioMapState::RoundActive && map.Round().Group() == 1 &&
                map.Round().Waves().FindWave(7) == wave && map.ClearPoint() == 0 &&
                map.MonsterDrainAt() == 60 && *filter == 2 && map.WaveDispatchStopped() && notifications == 1;
        };
        Check(!map.BeginRound(999, 30, 0, events) && unchanged(),
            "invalid host round preserves every active manager field without notification");
        Check(!map.BeginRound(1, std::numeric_limits<float>::quiet_NaN(), 0, events) && unchanged(),
            "component time rejection rolls back staged manager reset and generation");
        expectedIndex = 1; expectedFilter = 2;
        require(map.BeginRound(1, 30, 0, events), "valid restart resets manager before observer");
        Check(notifications == 2 && map.RoundGeneration() == generation + 1 &&
            map.ClearPoint() == -1 && map.MonsterDrainAt() == 0 && *filter == 2,
            "successful restart resets progress/deadline but preserves filter identity/value");
        require(map.EnableMonsterClearPoint(1) && map.ScheduleMonsterDrain(70, 2), "prepare access revocation state");
        map.RevokeEntityAccess(); due = true;
        Check(!map.EnableMonsterClearPoint(1) && !map.ScheduleMonsterDrain(80, 1) &&
            !map.AdvanceMonsterDrain(70, due) && !due && map.ClearPoint() == 0 &&
            map.MonsterDrainAt() == 70 && *filter == 2,
            "revoked entity access rejects manager callbacks without stale state mutation");
        events.callback = {};
        require(map.EndMap(), "manager map teardown succeeds after revocation");
        Check(map.ClearPoint() == -1 && map.MonsterDrainAt() == 0 && *filter == 0 &&
            filter == &map.MonsterDrainTeam(), "map teardown resets all manager fields in stable storage");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.files[Paths("map_session_test")[4]] = Hex(fixtures::unsupported); engine.Markers();
        CCSOLScenarioMap map; Events events(map);
        require(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(1, 10, 0, events),
            "unsupported-group fixture starts its valid phase");
        require(map.EnableMonsterClearPoint(9) && map.ScheduleMonsterDrain(45, 2) && map.StopWaveDispatch(),
            "unsupported-group fixture has active manager state");
        const auto generation = map.RoundGeneration(); const auto notifications = events.tags.size();
        Check(!map.BeginRound(0, 50, 0, events) && map.RoundIndex() == 1 && map.Round().Group() == 0 &&
            map.State() == CSOLScenarioMapState::RoundActive && map.RoundGeneration() == generation &&
            map.ClearPoint() == 0 && map.MonsterDrainAt() == 45 && map.MonsterDrainTeam() == 2 &&
            map.WaveDispatchStopped() && events.tags.size() == notifications,
            "real trigger validation failure restores manager clock/progress/stop without publishing a phase");
        require(map.EndMap(), "unsupported-group manager fixture tears down");
    }
}

void TeamKills()
{
    const auto require = [](bool ok, const char* message)
    { Check(ok, message); if (!ok) throw std::runtime_error(message); };
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        Check(map.TeamMonsterKills(1) == 0 && map.TeamMonsterKills(2) == 0 &&
            !map.RecordTeamMonsterKill(1) && !map.RecordTeamMonsterKill(2),
            "new manager has no kills and rejects attribution before a resource session");
        require(map.BeginMap("map_session_test"), "team-kill fixture loads the real map tables");
        Check(!map.RecordTeamMonsterKill(1) && !map.RecordTeamMonsterKill(2),
            "loaded resources alone do not permit team-kill mutation");
        require(map.CollectEntities(), "team-kill fixture collects actual markers");
        Check(!map.RecordTeamMonsterKill(1) && !map.RecordTeamMonsterKill(2),
            "collected markers alone do not permit team-kill mutation");
        require(map.BeginRound(0, 10, 0, events), "team-kill fixture begins subtype-one formal phase");
        require(map.RecordTeamMonsterKill(1) && map.RecordTeamMonsterKill(2) && map.RecordTeamMonsterKill(2),
            "active manager accepts independent team-one and team-two kills");
        Check(map.TeamMonsterKills(1) == 1 && map.TeamMonsterKills(2) == 2,
            "native team indexes accumulate distinct unsigned-short counts");
        for (int team : {0, 3, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 65537})
            Check(map.RecordTeamMonsterKill(team) && map.TeamMonsterKills(team) == 0 &&
                map.TeamMonsterKills(1) == 1 && map.TeamMonsterKills(2) == 2,
                "other integer teams are a successful no-op without narrowing or indexing either real team");

        int notifications = 0;
        std::uint16_t expectedOne = 1, expectedTwo = 2;
        events.callback = [&](char tag)
        {
            ++notifications;
            Check(tag == 'r' && map.State() == CSOLScenarioMapState::RoundActive && map.RoundIndex() == 1 &&
                map.TeamMonsterKills(1) == expectedOne && map.TeamMonsterKills(2) == expectedTwo,
                "formal-phase observer sees the prior team counts in the newly committed subtype-one phase");
        };
        require(map.BeginRound(1, 20, 0, events), "second formal phase preserves team-kill manager state");
        Check(notifications == 1 && map.TeamMonsterKills(1) == 1 && map.TeamMonsterKills(2) == 2,
            "formal phase replacement keeps both counts after its observer returns");
        Check(!map.BeginRound(999, 30, 0, events) && notifications == 1 &&
            map.TeamMonsterKills(1) == 1 && map.TeamMonsterKills(2) == 2,
            "invalid formal phase neither clears counts nor emits a new observer callback");

        for (int team : {1, 2})
        {
            bool accepted = true;
            for (unsigned count = map.TeamMonsterKills(team); count < 64999; ++count)
            {
                const bool recorded = map.RecordTeamMonsterKill(team);
                accepted = accepted && recorded;
            }
            Check(accepted && map.TeamMonsterKills(team) == 64999,
                "public manager operation reaches the old-count boundary without private state injection");
            Check(map.RecordTeamMonsterKill(team) && map.TeamMonsterKills(team) == 65000,
                "old unsigned-short count64999 increments exactly once to65000");
            for (int i = 0; i < 16; ++i)
            {
                const bool recorded = map.RecordTeamMonsterKill(team);
                accepted = accepted && recorded;
            }
            Check(accepted && map.TeamMonsterKills(team) == 65000,
                "old count65000 remains saturated after further accepted kills without ushort wrap");
        }
        expectedOne = expectedTwo = 65000;
        require(map.BeginRound(1, 40, 0, events), "same-stage formal restart keeps saturated counts");
        Check(notifications == 2 && map.TeamMonsterKills(1) == 65000 && map.TeamMonsterKills(2) == 65000,
            "same-stage restart preserves both saturation values after notification");
        map.RevokeEntityAccess();
        for (int team : {1, 2, 0, 3})
            Check(!map.RecordTeamMonsterKill(team) && map.TeamMonsterKills(1) == 65000 &&
                map.TeamMonsterKills(2) == 65000,
                "revoked entity access rejects every team without mutating existing manager counts");
        events.callback = {};
        require(map.EndMap(), "whole-map teardown succeeds with revoked entity access");
        Check(map.TeamMonsterKills(1) == 0 && map.TeamMonsterKills(2) == 0 &&
            !map.RecordTeamMonsterKill(1) && !map.RecordTeamMonsterKill(2),
            "whole-map teardown clears saturated counters and disables subsequent attribution");
    }
    {
        MapEngine engine; engine.SyntheticFiles(); engine.Markers(); CCSOLScenarioMap map; Events events(map);
        require(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 10, 0, events) &&
            map.RecordTeamMonsterKill(1) && map.RecordTeamMonsterKill(2),
            "map-replacement fixture has real active counts before a new resource session");
        require(map.BeginMap("map_session_test"), "same-map BeginMap replaces the complete manager session");
        Check(map.State() == CSOLScenarioMapState::ResourcesLoaded && map.TeamMonsterKills(1) == 0 &&
            map.TeamMonsterKills(2) == 0 && !map.RecordTeamMonsterKill(1) && !map.RecordTeamMonsterKill(2),
            "new resource session clears both old counters and stays inactive until new marker collection and round start");
        require(map.EndMap(), "resource-only replacement tears down without claiming a playable round");
    }
}

void ActualObserver(const char* common, const char* pointPath, const char* volumePath)
{
    MapEngine engine; for (const auto& path : Paths("zs_deadend")) engine.files[path] = Read(std::filesystem::path(common) / path);
    Reader sp(pointPath); if (sp.U32() != 0x544e5053 || sp.U32() != 184) throw std::runtime_error("bad SPNT");
    for (unsigned i = 0; i < 184; ++i)
    {
        const auto index = sp.U32(), kind = sp.U32(); const auto id = sp.I32(), team = sp.I32(), max = sp.I32();
        std::array<float, 6> pos; for (auto& x : pos) x = sp.F32(); sp.I32(); const auto keys = sp.U32();
        auto& e = engine.Add(kind ? "zombiespawn" : "playerspawn", static_cast<int>(index));
        if (kind) { auto& m = static_cast<CCSOLMonsterSpawn&>(e); m.m_iId = id; m.m_iTeam = team; m.m_iMaxSpawn = max; }
        else { auto& p = static_cast<CCSOLPlayerSpawn&>(e); p.m_iId = id; p.m_iTeam = team; }
        e.pev->origin = Vector(pos[0], pos[1], pos[2]); e.pev->angles = Vector(pos[3], pos[4], pos[5]);
        for (unsigned j = 0; j < keys; ++j) { sp.String(); sp.String(); }
    }
    Reader tr(volumePath); if (tr.U32() != 0x56475254 || tr.U32() != 30) throw std::runtime_error("bad TRGV");
    for (unsigned i = 0; i < 30; ++i)
    {
        const auto index = tr.U32(); const auto id = tr.I32(); tr.I32(); std::array<float, 18> v; for (auto& x : v) x = tr.F32(); tr.I32();
        engine.Trigger(static_cast<int>(index), id, Vector(v[6], v[7], v[8]), Vector(v[9], v[10], v[11]));
        const auto keys = tr.U32(); for (unsigned j = 0; j < keys; ++j) { tr.String(); tr.String(); }
    }
    Check(sp.Done() && tr.Done(), "actual observer consumes prior independent point/volume fixtures exactly");
    CCSOLScenarioMap map; Events events(map);
    Check(map.BeginMap("zs_deadend") && map.CollectEntities() && map.Tables()->rounds.rows.size() == 2, "actual observer fixture resolves real two-stage map");
    ObserverSequence(map, events);
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { ResourceAndLifecycle(); CommittedRoundCancellation(); Failures(); }
        else if (argc == 5 && std::string_view(argv[1]) == "--actual") Actual(argv[2], argv[3], argv[4]);
        else if (argc == 2 && std::string_view(argv[1]) == "--observer") Observer();
        else if (argc == 2 && std::string_view(argv[1]) == "--manager-state") ManagerState();
        else if (argc == 2 && std::string_view(argv[1]) == "--team-kills") TeamKills();
        else if (argc == 5 && std::string_view(argv[1]) == "--actual-observer") ActualObserver(argv[2], argv[3], argv[4]);
        else throw std::runtime_error("usage: map_session_tests [--actual commonRoot independentSPNT independentTRGV]");
    }
    catch (const std::exception& e) { ++failures; std::cerr << "EXCEPTION: " << e.what() << '\n'; }
    std::cout << "map_session checks=" << checks << " failures=" << failures << '\n'; return failures ? 1 : 0;
}
