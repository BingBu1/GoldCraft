// SPDX-License-Identifier: GPL-3.0-or-later
// Real component methods; the synchronous executor is a strict call spy only.
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../csol-scenario-frame/frame_fixture.hpp"
#include "../../native/csol/server/scenario_death_rules.h"
#include "death_fixtures.hpp"
#include <climits>
#include <deque>
#include <iostream>
#include <limits>

using namespace csol::scenario;
namespace {
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
void Require(bool value, const char* label)
{ Check(value, label); if (!value) throw std::runtime_error(label); }

struct MixedEngine : PoolEngine
{
    inline static MixedEngine* mixed{};
    std::map<std::string, std::string> files;
    std::map<void*, std::size_t> buffers;
    std::vector<int> scans;
    std::vector<CBasePlayer*> clients;
    int summaryQueries{};
    MixedEngine()
    {
        mixed = this;
        g_engfuncs.pfnCreateNamedEntity = CreateMixed;
        g_engfuncs.pfnLoadFileForMe = Load;
        g_engfuncs.pfnFreeFile = FreeFile;
        g_engfuncs.pfnPEntityOfEntIndex = ClientIndex;
        g_engfuncs.pfnIsDedicatedServer = Dedicated;
        std::vector<std::string> header(59), row(59, "0");
        header[0] = ";map_id"; header[1] = "map_name";
        header[2] = "map_filename"; header[58] = "SubType";
        row[0] = "1"; row[1] = "test"; row[2] = "map_session_test"; row[58] = "1";
        for (const auto& fields : {header, row}) {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) files["resource/maplist.csv"] += ',';
                files["resource/maplist.csv"] += fields[i];
            }
            files["resource/maplist.csv"] += "\r\n";
        }
        files["maps/zbs_wave_map_session_test.cso"] = FrameEngine::Hex(fixtures::waves);
        files["maps/zbs_map_session_test.cso"] = FrameEngine::Hex(fixtures::rounds);
        files["maps/ZBS_ZT_map_session_test.cso"] = FrameEngine::Hex(fixtures::types);
        files["maps/ZBS_TRG_map_session_test.cso"] = FrameEngine::Hex(fixtures::triggers);
        files["maps/ZBS_DIE_EVENT_map_session_test.cso"] = FrameEngine::Hex(death_fixtures::good);
    }
    ~MixedEngine()
    {
        if (!buffers.empty()) std::abort();
        for (auto* player : clients) {
            auto* ed = player->edict(); player->~CBasePlayer(); std::free(player); ed->pvPrivateData = nullptr;
        }
        mixed = nullptr;
    }
    static edict_t* CreateMixed(int name)
    { return std::strcmp(STRING(name), "monster_entity") == 0 ? CreatePoolEntity(name) : Engine::Create(name); }
    static unsigned char* Load(const char* name, int* length)
    {
        Require(*length == 0, "resource length initialized before engine callback");
        const auto found = mixed->files.find(name);
        if (found == mixed->files.end()) return nullptr;
        auto* data = new unsigned char[found->second.size() + 1];
        std::memcpy(data, found->second.data(), found->second.size()); data[found->second.size()] = 0;
        *length = static_cast<int>(found->second.size()); mixed->buffers[data] = found->second.size(); return data;
    }
    static void FreeFile(void* data)
    { Require(mixed->buffers.erase(data) == 1, "owned engine buffer released once"); delete[] static_cast<unsigned char*>(data); }
    static edict_t* ClientIndex(int slot) { mixed->scans.push_back(slot); return Engine::FromOffset(slot); }
    static int Dedicated() { ++mixed->summaryQueries; return 0; }
    edict_t* Raw(const char* name, int index)
    {
        auto ed = std::make_unique<edict_t>(); ed->serialnumber = 1; ed->v.pContainingEntity = ed.get();
        auto* raw = ed.get(); indices[raw] = index; edicts.push_back(std::move(ed));
        raw->v.classname = AllocString(name); return raw;
    }
    void Markers()
    {
        Player(101, 1); Monster(201, 7, 1000);
        for (const int id : {1, 2, 30}) {
            auto* ed = Raw("zbstrigger", 300 + id); ed->v.model = AllocString("*1"); zbstrigger(&ed->v);
            auto* trigger = static_cast<CCSOLScenarioTrigger*>(ed->pvPrivateData);
            trigger->m_iId = id; trigger->Spawn();
            const float x = id == 1 ? 0.0f : id == 2 ? 100.0f : 200.0f;
            ed->v.mins = Vector(x - 5, -5, -5); ed->v.maxs = Vector(x + 5, 5, 5);
        }
    }
    CBasePlayer& Client(int index, Vector origin)
    {
        auto* ed = Raw("player", index); auto* player = new(&ed->v) CBasePlayer;
        player->pev = &ed->v; player->m_pEntity = nullptr;
        ed->v.health = 100; ed->v.deadflag = DEAD_NO; ed->v.origin = origin;
        clients.push_back(player); return *player;
    }
};

struct Call
{
    std::size_t slot{}; std::uint64_t lease{};
    CSOLMonsterDeathArguments arguments;
};
struct ExecutorSpy final : CCSOLMonsterDeathExecutor
{
    struct Expected { CCSOLScenarioMonster* entity; std::uint64_t lease; CSOLMonsterDeathArguments arguments;
        std::function<bool(CCSOLScenarioMonster&)> callback; };
    std::vector<Expected> expected;
    std::vector<Call> calls;
    std::size_t next{};
    void Expect(CCSOLScenarioMonster& entity, const CSOLMonsterDeathArguments& args,
        std::function<bool(CCSOLScenarioMonster&)> callback)
    { expected.push_back({&entity, entity.Pool()->Lease(entity), args, std::move(callback)}); }
    bool ExecuteDeath(CCSOLScenarioMonster& entity, const CSOLMonsterDeathArguments& args) override
    {
        Require(next < expected.size(), "executor has an explicit expected call");
        const auto want = expected[next++];
        Require(want.entity == &entity && want.lease == entity.Pool()->Lease(entity), "executor entity and lease match sequence");
        Check(want.arguments.inflictor.Get() == args.inflictor.Get() && want.arguments.attacker.Get() == args.attacker.Get(),
            "executor preserves inflictor and attacker handles");
        Check(want.arguments.suppressSound == args.suppressSound && want.arguments.context.bytes == args.context.bytes,
            "executor preserves sound flag and all context bytes");
        calls.push_back({entity.Slot(), entity.Pool()->Lease(entity), args});
        Require(bool(want.callback), "executor callback result is explicitly configured");
        return want.callback(entity);
    }
    void Done() const { Check(next == expected.size(), "all expected executor calls consumed exactly once"); }
};
bool Acknowledge(CCSOLScenarioMonster&) { return true; } // Call-spy acknowledgement; no death/owner side effects.

struct Fixture
{
    MixedEngine engine;
    BehaviorStats behavior;
    std::deque<MonsterType> types;
    CCSOLMonsterPool pool;
    CCSOLScenarioMap map;
    FrameEvents events;
    FrameRules rules;
    ExecutorSpy spy;
    CCSOLMonsterDeathRequests requests{spy};
    CCSOLScenarioDeathRules drain{map, pool, requests};
    std::int8_t team{};
    int dispatches{};
    std::function<void(const WaveRow&, RowClock&)> dispatch;
    Fixture()
    {
        engine.Markers();
        Require(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 0, 0, events), "real map and round initialize");
        Require(pool.Initialize(), "real 45-slot pool initializes"); events.Clear();
    }
    ~Fixture()
    {
        requests.Reset(); pool.Shutdown(); map.EndMap();
        // PoolEngine's destruction remains responsible for private data; the
        // pool owner stays alive through destruction and EHANDLE detachment.
        for (auto& ed : engine.edicts) {
            auto* base = static_cast<CBaseEntity*>(ed->pvPrivateData);
            if (dynamic_cast<CCSOLScenarioMonster*>(base)) engine.DestroyPrivate(ed.get());
        }
        pool.ForgetMap();
    }
    CCSOLScenarioMonster& Get(int priority = 0, int skill = 0, int teamId = 1, bool install = true)
    {
        types.emplace_back(); auto& type = types.back(); type.id = static_cast<int>(types.size());
        type.priority = static_cast<std::int8_t>(priority); type.skill = static_cast<std::int16_t>(skill);
        auto* monster = pool.Acquire(type, 7, map.SpawnPoints().Points());
        Require(monster != nullptr, "real pool acquire succeeds"); monster->pev->team = teamId;
        if (install) Require(monster->InstallBehavior(std::make_unique<BehaviorProbe>(behavior)), "explicit behavior probe installed");
        return *monster;
    }
    CSOLMonsterDeathArguments Args(int tag = 0, bool silent = false)
    { CSOLMonsterDeathArguments args; args.context.bytes[0] = static_cast<std::uint8_t>(tag); args.suppressSound = silent; return args; }
    void Expect(CCSOLScenarioMonster& entity, int tag = 0, bool silent = false,
        std::function<bool(CCSOLScenarioMonster&)> callback = Acknowledge)
    { spy.Expect(entity, Args(tag, silent), std::move(callback)); }
    void Activate(int id = 7, float offset = 0)
    { ConfigError error; Require(map.Round().Waves().ActivateWave(static_cast<std::uint16_t>(id), offset, error), "wave activation succeeds"); }
    static void Dispatch(void* context, const WaveRow& row, RowClock& clock)
    { auto& self = *static_cast<Fixture*>(context); ++self.dispatches; if (self.dispatch) self.dispatch(row, clock); }
    CSOLScenarioFrame Frame(float think, float now)
    { engine.globals.time = now; return map.UpdateFrame(rules, think, 4, Dispatch, this, events); }
};

void Requests()
{
    {
        Fixture f; auto& a = f.Get(); auto& b = f.Get();
        const auto global = f.requests.BeginTimedEvents(); const auto local = f.requests.BeginBehavior(a);
        Require(global && local, "both real gates entered");
        Check(f.requests.Request(a, f.Args(1)) == CSOLDeathRequestResult::Deferred &&
            f.requests.Request(a, f.Args(2)) == CSOLDeathRequestResult::Deferred &&
            f.requests.Request(b, f.Args(3)) == CSOLDeathRequestResult::Deferred, "local replacement and global append defer");
        f.Expect(b, 3); f.Expect(a, 2);
        Check(f.requests.EndBehavior(*local) && f.spy.calls.empty(), "local last request reenters active global gate");
        Check(f.requests.EndTimedEvents(*global), "global snapshot drains FIFO"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(); auto& b = f.Get(); auto& c = f.Get();
        const auto global = f.requests.BeginTimedEvents(); Require(bool(global), "outer global scope");
        f.requests.Request(a, f.Args(1)); f.requests.Request(b, f.Args(2));
        f.Expect(a, 1, false, [&](auto&) {
            const auto nested = f.requests.BeginTimedEvents(); Require(bool(nested), "global gate lowered before replay");
            Check(f.requests.Request(c, f.Args(3)) == CSOLDeathRequestResult::Deferred, "nested snapshot receives separate request");
            return f.requests.EndTimedEvents(*nested);
        });
        f.Expect(c, 3); f.Expect(b, 2);
        Check(f.requests.EndTimedEvents(*global), "outer snapshot resumes after nested replay"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(); const auto local = f.requests.BeginBehavior(a);
        Require(bool(local), "local replay scope entered");
        f.requests.Request(a, f.Args(12));
        f.Expect(a, 12, false, [&](auto&) {
            const auto next = f.requests.BeginBehavior(a); Require(bool(next), "local gate lowered before replay callback");
            f.requests.Request(a, f.Args(13)); return f.requests.EndBehavior(*next);
        });
        f.Expect(a, 13);
        Check(f.requests.EndBehavior(*local), "same entity can enter fresh local scope during replay"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(); auto& inflictor = f.engine.Client(1, Vector(0,0,0));
        auto& attacker = f.engine.Client(2, Vector(0,0,0));
        auto args = f.Args(14, true); args.inflictor = &inflictor; args.attacker = &attacker;
        const auto global = f.requests.BeginTimedEvents(); Require(bool(global), "argument copy scope entered");
        f.spy.Expect(a, args, Acknowledge); f.requests.Request(a, args);
        args.context.bytes.fill(0xdd); args.inflictor = nullptr; args.attacker = nullptr; args.suppressSound = false;
        Check(f.requests.EndTimedEvents(*global), "deferred arguments are owned copies"); f.spy.Done();
    }
    for (int gate = 0; gate < 3; ++gate) {
        Fixture f; auto& a = f.Get();
        const auto local = gate == 1 ? f.requests.BeginBehavior(a) : std::optional<CCSOLMonsterDeathRequests::BehaviorScope>{};
        const auto global = gate == 2 ? f.requests.BeginTimedEvents() : std::optional<CCSOLMonsterDeathRequests::TimedScope>{};
        f.Expect(a, 7, false, [](auto&) { return false; });
        const auto result = f.requests.Request(a, f.Args(7));
        Check(gate ? result == CSOLDeathRequestResult::Deferred : result == CSOLDeathRequestResult::Failed, "executor failure routes through selected gate");
        if (local) Check(!f.requests.EndBehavior(*local), "local replay propagates executor failure");
        if (global) Check(!f.requests.EndTimedEvents(*global), "global replay propagates executor failure");
        Check(!f.requests.Error().empty(), "same-session failure retained"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get();
        f.Expect(a, 4, false, [&](auto&) { Require(bool(f.requests.BeginTimedEvents()), "nested gate entered");
            Check(!f.requests.BeginTimedEvents(), "nested misuse creates same-generation error"); return true; });
        Check(f.requests.Request(a, f.Args(4)) == CSOLDeathRequestResult::Failed && !f.requests.Error().empty(),
            "executor true cannot hide nested same-session error"); f.spy.Done();
    }
    for (int gate = 0; gate < 3; ++gate) {
        Fixture f; auto& a = f.Get(); const auto generation = f.requests.Generation();
        const auto local = gate == 1 ? f.requests.BeginBehavior(a) : std::optional<CCSOLMonsterDeathRequests::BehaviorScope>{};
        const auto global = gate == 2 ? f.requests.BeginTimedEvents() : std::optional<CCSOLMonsterDeathRequests::TimedScope>{};
        f.Expect(a, 5, false, [&](auto&) { Require(f.requests.Reset(), "callback resets request owner"); return false; });
        const auto result = f.requests.Request(a, f.Args(5));
        if (!gate) Check(result == CSOLDeathRequestResult::Ignored, "stale direct result ignored after Reset");
        if (local) Check(!f.requests.EndBehavior(*local), "old local scope does not report completion after Reset");
        if (global) Check(!f.requests.EndTimedEvents(*global), "old global scope does not report completion after Reset");
        Check(f.requests.Generation() != generation && f.requests.Error().empty(), "old false result cannot poison new session"); f.spy.Done();
    }
    {
        Fixture f; auto& noBehavior = f.Get(0, 0, 1, false); auto& dead = f.Get(); dead.pev->deadflag = DEAD_DEAD;
        Check(f.requests.Request(noBehavior, f.Args()) == CSOLDeathRequestResult::Ignored &&
            f.requests.Request(dead, f.Args()) == CSOLDeathRequestResult::Ignored && f.spy.calls.empty(), "unready or already dead monster ignored");
        auto& live = f.Get(); const auto local = f.requests.BeginBehavior(live); Require(bool(local), "local scope entered before Reset");
        f.requests.Request(live, f.Args(9)); f.requests.Reset();
        Check(!f.requests.EndBehavior(*local) && f.requests.Error().empty(), "stale scope cannot consume or poison new session");
    }
    {
        Fixture f; std::vector<CCSOLScenarioMonster*> monsters;
        for (int i = 0; i < 45; ++i) monsters.push_back(&f.Get(i < 39 ? 0 : 1));
        auto& a = *monsters[0]; const auto oldLease = f.pool.Lease(a);
        const auto global = f.requests.BeginTimedEvents(); Require(bool(global), "queued lease scope"); f.requests.Request(a, f.Args(11));
        Require(f.pool.Free(a), "return old lease"); auto& replacement = f.Get();
        Require(&replacement == &a && f.pool.Lease(replacement) != oldLease, "same entity reused with a different real lease");
        Check(f.requests.EndTimedEvents(*global) && f.spy.calls.empty(), "queued old lease never reaches executor");
    }
    {
        Fixture f; auto& a = f.Get(); const auto global = f.requests.BeginTimedEvents();
        Require(bool(global), "queued serial scope"); f.requests.Request(a, f.Args(15));
        auto* ed = a.edict(); const auto serial = ed->serialnumber; ++ed->serialnumber;
        Check(f.requests.EndTimedEvents(*global) && f.spy.calls.empty(), "expired SDK EHANDLE serial blocks replay");
        ed->serialnumber = serial;
    }
    {
        Fixture f; auto& a = f.Get(); auto& b = f.Get(); const auto global = f.requests.BeginTimedEvents();
        Require(bool(global), "reset interrupts a nonempty FIFO"); f.requests.Request(a, f.Args(16)); f.requests.Request(b, f.Args(17));
        f.Expect(a, 16, false, [&](auto&) { return f.requests.Reset(); });
        Check(!f.requests.EndTimedEvents(*global) && f.spy.calls.size() == 1 && f.requests.Error().empty(),
            "Reset during first replay abandons later old FIFO requests"); f.spy.Done();
    }
}

void Drain()
{
    const std::array<std::uint8_t, 32> defaultContext{0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0};
    Check(CSOLMonsterDeathArguments{}.context.bytes == defaultContext, "default context matches independently listed original writes");
    for (int limit : {1, 2, 0, -1, INT_MIN, 45}) for (bool bosses : {false, true}) {
        Fixture f; auto& boss = f.Get(0, 1000); auto& ordinary = f.Get(1, 999); auto& tail = f.Get(0, -1);
        auto& hidden = f.Get(); hidden.pev->solid = SOLID_NOT;
        const int selected = limit == 1 ? 1 : limit == 2 ? 2 : 3;
        std::srand(641); const bool firstSilent = std::rand() % 2 != 0;
        const bool tailSilent = selected == 3 ? std::rand() % 2 != 0 : false;
        const auto next = std::rand(); std::srand(641);
        if (bosses) f.Expect(boss, 0, firstSilent);
        if (selected >= 2) f.Expect(ordinary, 0, true);
        if (selected == 3) f.Expect(tail, 0, tailSilent);
        const auto result = f.drain.Drain(limit, bosses, f.team);
        Check(result.valid && result.selected == static_cast<std::size_t>(selected) &&
            result.requested == static_cast<std::size_t>(selected - (bosses ? 0 : 1)), "limit counts selected slots including skipped Boss");
        Check(std::rand() == next, "RNG occurs before Boss gate and only for priority zero"); f.spy.Done();
        for (const auto& call : f.spy.calls) Check(!call.arguments.attacker && !call.arguments.inflictor &&
            call.arguments.context.bytes == defaultContext, "drain uses null owners and exact default context");
    }
    {
        Fixture f; auto& a = f.Get(1, 0, 1); f.Get(1, 0, 2); auto& c = f.Get(1, 0, -1); f.team = 1;
        f.Expect(a, 0, true, [&](auto&) { f.team = -1; return true; }); f.Expect(c, 0, true);
        const auto result = f.drain.Drain(45, true, f.team);
        Check(result.valid && result.selected == 2 && result.requested == 2, "signed team reference reread after callback"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(1); const auto local = f.requests.BeginBehavior(a); Require(bool(local), "local drain gate");
        const auto result = f.drain.Drain(1, true, f.team);
        Check(result.valid && result.selected == 1 && result.requested == 1 && f.spy.calls.empty(), "drain counts deferred request without claiming a kill");
        f.Expect(a, 0, true); Check(f.requests.EndBehavior(*local), "deferred drain reaches executor on behavior exit"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(1); f.Expect(a, 0, true, [](auto&) { return false; });
        const auto result = f.drain.Drain(1, true, f.team);
        Check(!result.valid && result.error.message == f.requests.Error() && !result.error.message.empty(), "drain propagates executor failure"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(1); auto& b = f.Get(1); ConfigError error;
        f.Expect(a, 0, true, [&](auto&) { Check(!f.map.WaveDispatchStopped(), "first nested death occurs before table stop"); return true; });
        f.Expect(b, 0, true, [&](auto&) { Check(!f.map.WaveDispatchStopped(), "last nested death occurs before table stop"); return true; });
        Check(f.drain.ApplyRule(500, f.team, error) && f.map.WaveDispatchStopped(), "actual table drains all then sets stop"); f.spy.Done();
    }
    {
        Fixture f; ConfigError error;
        Check(f.drain.ApplyRule(501, f.team, error) && error.message.empty() && !f.map.WaveDispatchStopped(),
            "missing actualType does not consume another death-table row");
        const auto empty = f.drain.Drain(45, false, f.team);
        Check(empty.valid && empty.selected == 0 && empty.requested == 0 && f.spy.calls.empty(),
            "empty drain returns zero without inventing manager pending-state writes");
    }
    {
        Fixture f; auto& a = f.Get(1); auto& b = f.Get(1); ConfigError error; int depth{};
        f.Expect(a, 0, true, [&](auto& monster) {
            monster.pev->solid = SOLID_NOT; ++depth;
            const auto nested = f.drain.ApplyRule(500, f.team, error); --depth;
            Check(nested && f.map.WaveDispatchStopped(), "nested table tail sets stop before outer scan resumes");
            return nested;
        });
        f.Expect(b, 0, true, [&](auto& monster) { Check(depth == 1 && !f.map.WaveDispatchStopped(),
            "nested clear visits remaining monster before its own stop"); monster.pev->solid = SOLID_NOT; return true; });
        Check(f.drain.ApplyRule(500, f.team, error), "outer clear completes after nested table processing"); f.spy.Done();
    }
    {
        Fixture f; auto& a = f.Get(1); ConfigError error; const auto generation = f.map.RoundGeneration();
        f.Expect(a, 0, true, [&](auto&) { Require(f.map.BeginRound(0, 3, 0, f.events), "same-index formal restart succeeds");
            Check(f.map.RoundGeneration() != generation, "same-index restart publishes new generation"); return true; });
        Check(!f.drain.ApplyRule(500, f.team, error) && !error.message.empty() && !f.map.WaveDispatchStopped(),
            "old recursive table tail cannot stop restarted round"); f.spy.Done();
    }
    for (int reset = 0; reset < 2; ++reset) {
        Fixture f; auto& a = f.Get(1); f.Expect(a, 0, true, [&](auto&) {
            if (reset == 0) Require(f.requests.Reset(), "request generation changes inside drain");
            else Require(f.map.EndMap(), "map ends inside drain");
            return true;
        });
        const auto result = f.drain.Drain(45, true, f.team);
        Check(!result.valid && !result.error.message.empty(), "drain stops after map or request generation change"); f.spy.Done();
    }
}

void Frame()
{
    {
        Fixture f; const auto first = f.map.RoundGeneration(); Require(f.map.StopWaveDispatch(), "active map accepts stop");
        int notifications{}; f.events.callback = [&](char tag) { if (tag == 's') { ++notifications;
            Check(f.map.RoundGeneration() == first + 1 && !f.map.WaveDispatchStopped(), "round observer sees new generation and cleared stop"); } };
        Check(f.map.BeginRound(0, 2, 0, f.events) && notifications == 1, "same-index formal BeginRound succeeds"); f.events.callback = {};
        Require(f.map.StopWaveDispatch(), "stop before invalid restart"); const auto generation = f.map.RoundGeneration();
        Check(!f.map.BeginRound(0, std::numeric_limits<float>::quiet_NaN(), 0, f.events) &&
            f.map.RoundGeneration() == generation && f.map.WaveDispatchStopped(), "pre-callback validation failure restores generation and stop");
        Check(!f.map.BeginRound(std::numeric_limits<std::size_t>::max(), 0, 0, f.events) &&
            f.map.RoundGeneration() == generation && f.map.WaveDispatchStopped(), "invalid round index preserves existing identity");
        Check(f.map.EndMap() && f.map.RoundGeneration() == generation && !f.map.WaveDispatchStopped() &&
            !f.map.StopWaveDispatch(), "EndMap retains monotonic identity and clears stop while inactive");
    }
    {
        Fixture f; f.Activate(); Require(f.Frame(0, 0).valid, "seed actual wave row and summary");
        const auto active = std::vector<std::uint16_t>(f.map.Round().Waves().ActiveWaves().begin(), f.map.Round().Waves().ActiveWaves().end());
        const auto cached = std::vector<CSOLActiveWave>(f.map.WaveSummary().begin(), f.map.WaveSummary().end());
        Require(!active.empty() && !cached.empty(), "seed leaves active wave and local cache");
        const auto dispatches = f.dispatches; const auto queries = f.engine.summaryQueries; const auto scan = f.map.PlayerScan().NextPlayerSlot();
        Require(f.map.StopWaveDispatch(), "stop running controller");
        const auto frame = f.Frame(5, 10);
        Check(frame.valid && frame.waves.valid && frame.waves.attempts == 0 && f.dispatches == dispatches,
            "stopped entry never advances or dispatches wave rows");
        Check(f.map.Round().Schedule().elapsed == 5 && f.map.Round().Schedule().nextThink == 10.5f &&
            f.map.PlayerScan().NextPlayerSlot() != scan, "stopped frame still advances elapsed deadline and player cursor");
        Check(std::equal(active.begin(), active.end(), f.map.Round().Waves().ActiveWaves().begin(), f.map.Round().Waves().ActiveWaves().end()) &&
            f.engine.summaryQueries == queries && f.map.WaveSummary().size() == cached.size(), "stop retains active programs and skips local summary refresh");
        Check(f.events.tags.empty() && !f.rules.IsGameOver(), "stop emits no goal/summary notification or GameOver");
        f.engine.globals.time = 11;
        Check(f.map.UpdateFrame(f.rules, 6, 4, nullptr, nullptr, f.events).valid,
            "stopped entry needs no dispatcher but still runs remaining manager work");
    }
    {
        Fixture f; WaveTable table;
        for (int id : {71, 72}) { WaveRow row; row.waveId = id; row.spawnPoint = id; row.types = {1};
            row.weights = {1}; row.weightTotal = 1; row.startTime = 0; row.interval = 1; table.rows.push_back(row); }
        ConfigError error; const std::array<std::uint16_t, 2> ids{71,72};
        Require(f.map.Round().Waves().Activate(table, ids, error), "two wave programs installed in real controller");
        f.dispatch = [&](const WaveRow&, RowClock&) { if (f.dispatches == 1) Require(f.map.StopWaveDispatch(), "row callback may set stop while frame busy"); };
        const auto first = f.Frame(0, 0);
        Check(first.valid && first.waves.attempts == 2 && f.dispatches == 2 && f.map.WaveDispatchStopped(), "stop from first row does not interrupt entered Tick");
        const auto second = f.Frame(1, 1);
        Check(second.valid && second.waves.attempts == 0 && f.dispatches == 2, "next controller entry observes stop");
    }
    {
        Fixture f; f.engine.Client(1, Vector(0,0,0)); Require(f.map.StopWaveDispatch(), "stop before spatial scan");
        const auto frame = f.Frame(0.25f, 0);
        Check(frame.valid && f.dispatches == 0 && f.events.tags == std::vector<char>({'a','s'}) &&
            !f.map.Round().Waves().ActiveWaves().empty(), "spatial scanner can still activate wave after controller stop");
    }
}
}
int main(int argc, char** argv)
{
    try {
        Require(argc == 2, "choose one bounded group");
        if (std::strcmp(argv[1], "--requests") == 0) Requests();
        else if (std::strcmp(argv[1], "--drain") == 0) Drain();
        else if (std::strcmp(argv[1], "--frame") == 0) Frame();
        else throw std::runtime_error("unknown group");
    } catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "death-flow " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
