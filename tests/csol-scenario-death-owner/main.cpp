// SPDX-License-Identifier: GPL-3.0-or-later
// Real synchronous owner, player registry, money modifier and SDK AddAccount.
// Behavior, item/room inputs and Golden remain explicit observers. Death
// effects use actual installed source records and their notification/lifetime.
// Execute with the complete matched SDK batch, not the old slices.
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_death_owner.h"
#include "../../native/csol/server/scenario_player_upgrades.h"
#include "../../native/csol/server/scenario_monster_effects.h"
#include "../../native/csol/scenario/monster_rewards.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

namespace owner_test
{
using death_events_test::Check;
using death_events_test::Require;
using csol::scenario::ConfigError;
using Result = CSOLDeathRequestResult;
std::function<void(CBaseAnimating&)> afterReset;
std::function<void()> recordReset;
void ResetHook(IReGameHook_CBaseAnimating_ResetSequenceInfo* chain, CBaseAnimating* monster)
{
    if (recordReset) recordReset();
    chain->callNext(monster);
    if (afterReset) afterReset(*monster);
}

// Controlled authoritative ability input; the real production upgrade object
// evaluates its percent and lifetime rules. No money calculation lives here.
struct MoneyEffects final : CCSOLScenarioUpgradeServices
{
    std::vector<std::string>& log;
    std::optional<float> percent;
    std::function<void()> onMoney;
    bool valid{true}, moneyAvailable{true};
    int moneyQueries{};
    explicit MoneyEffects(std::vector<std::string>& value) : log(value) {}
    bool Current() const override { return valid; }
    bool Initialize(const EHANDLE&, int id, std::array<csol::scenario::UpgradeAttribute, 5>&) override
    { Require(id == 0, "money fixture configures explicit class zero"); return true; }
    bool MoneyAddPercent(const EHANDLE&, std::optional<float>& output) override
    {
        ++moneyQueries; log.emplace_back("modifier");
        const auto callback = onMoney; if (callback) callback();
        output = percent; return moneyAvailable;
    }
    bool ReadBonus(const EHANDLE&, csol::scenario::UpgradeStat, int&) override
    { throw std::runtime_error("bonus update outside death money case"); }
    bool ModifyExperience(const EHANDLE&, int&) override
    { throw std::runtime_error("XP outside death money case"); }
    bool ModifyJump(const EHANDLE&, float&) override
    { throw std::runtime_error("jump outside death money case"); }
    bool ReachedEffectiveCap(const EHANDLE&) override
    { throw std::runtime_error("cap reward outside death money case"); }
};

struct ProbeState
{
    using Callback = std::function<bool(CSOLMonsterDeathCall&, ConfigError&)>;
    std::string name;
    std::vector<std::string>* log{};
    int sequence{7}, destroyed{}, beforeCalls{}, sequenceCalls{}, durationCalls{}, postCalls{};
    float seconds{3};
    CSOLMonsterDeathParameters parameters{17, 0, false};
    Callback before, select, duration, post;
    std::function<void()> onDestroy;
};

struct Behavior final : CCSOLMonsterDeathBehavior
{
    std::shared_ptr<ProbeState> state;
    explicit Behavior(std::shared_ptr<ProbeState> value) : state(std::move(value)) {}
    ~Behavior() override
    {
        ++state->destroyed;
        state->log->push_back("destroy:" + state->name);
        try { if (state->onDestroy) state->onDestroy(); }
        catch (...) { Check(false, "test destructor action must not throw"); }
    }
    void Think(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected AI in death owner test"); }
    BOOL TakeDamage(CCSOLScenarioMonster&, entvars_t*, entvars_t*, float, int) override
    { throw std::runtime_error("unexpected damage in death owner test"); }
    void Killed(CCSOLScenarioMonster&, entvars_t*, int) override
    { throw std::runtime_error("owner must be entered through Requests"); }
    CSOLMonsterDeathParameters DeathParameters() const override { return state->parameters; }
    bool BeforeDeath(CSOLMonsterDeathCall& call, ConfigError& error) override
    {
        ++state->beforeCalls; state->log->push_back("before:" + state->name);
        Require(call.Current() != nullptr, "before receives current lease");
        return state->before ? state->before(call, error) : true;
    }
    bool SelectDeathSequence(CSOLMonsterDeathCall& call, int& sequence, ConfigError& error) override
    {
        ++state->sequenceCalls; state->log->push_back("sequence:" + state->name);
        auto* monster = call.Current(); Require(monster != nullptr, "sequence current lease");
        Check(monster->pev->health == 0 && monster->pev->movetype == MOVETYPE_TOSS &&
            !(monster->pev->flags & FL_ONGROUND), "health and movement change before sequence callback");
        sequence = state->sequence;
        return state->select ? state->select(call, error) : true;
    }
    bool AdjustDeathDuration(CSOLMonsterDeathCall& call, float& seconds, ConfigError& error) override
    {
        ++state->durationCalls; state->log->push_back("duration:" + state->name);
        auto* monster = call.Current(); Require(monster != nullptr, "duration current lease");
        Check(monster->pev->deadflag == DEAD_DEAD && monster->pev->solid == SOLID_NOT &&
            monster->pev->takedamage == DAMAGE_NO, "dead state published before duration callback");
        seconds = state->seconds;
        return state->duration ? state->duration(call, error) : true;
    }
    bool PostDeath(CSOLMonsterDeathCall& call, ConfigError& error) override
    {
        ++state->postCalls; state->log->push_back("post:" + state->name);
        Require(call.Current() != nullptr, "post receives current lease");
        return state->post ? state->post(call, error) : true;
    }
};

struct Services final : CCSOLMonsterDeathServices
{
    using Callback = ProbeState::Callback;
    std::vector<std::string>& log;
    std::vector<std::string> expected;
    std::map<std::string, Callback> actions;
    std::size_t next{};
    std::shared_ptr<CCSOLScenarioPlayers> players = std::make_shared<CCSOLScenarioPlayers>();
    bool extractor{};
    int roomOption{}, extractorQueries{}, roomQueries{};
    explicit Services(std::vector<std::string>& value) : log(value) {}
    bool Invoke(const char* name, CSOLMonsterDeathCall& call, ConfigError& error)
    {
        Require(next < expected.size() && expected[next] == name, "service call matches explicit expectation");
        ++next; log.emplace_back(name); Require(call.Current() != nullptr, "service sees current lease");
        const auto found = actions.find(name);
        return found == actions.end() ? true : found->second(call, error);
    }
    std::shared_ptr<CCSOLScenarioPlayers> Players() const override { return players; }
    bool MoneyExtractor(CSOLMonsterDeathCall& call, const EHANDLE& player, bool& output, ConfigError& error) override
    {
        Require(player.Get<CBaseEntity>() == call.Attacker(), "extractor reads captured attacker");
        ++extractorQueries; const bool result = Invoke("extractor", call, error); output = extractor; return result;
    }
    bool RoomOption(CSOLMonsterDeathCall& call, int& output, ConfigError& error) override
    { ++roomQueries; const bool result = Invoke("room", call, error); output = roomOption; return result; }
    bool RulesDeath(CSOLMonsterDeathCall& call, ConfigError& error) override { return Invoke("rules", call, error); }
    bool PlayerKilledMonster(CSOLMonsterDeathCall& call, ConfigError& error) override { return Invoke("player", call, error); }
    bool GoldenDeath(CSOLMonsterDeathCall& call, ConfigError& error) override { return Invoke("golden", call, error); }
};

struct EffectState
{
    std::vector<std::string>* log{};
    int notified{}, destroyed{};
    std::function<void()> onNotify, onDestroy;
};
struct EffectLifetime
{
    std::shared_ptr<EffectState> state;
    explicit EffectLifetime(std::shared_ptr<EffectState> value) : state(std::move(value)) {}
    ~EffectLifetime()
    {
        ++state->destroyed; state->log->emplace_back("effects-destroy");
        try { const auto action = state->onDestroy; if (action) action(); }
        catch (...) { Check(false, "effect destructor observation must not throw"); }
    }
};

struct Fixture
{
    inline static Fixture* active{};
    death_events_test::EventEngine engine;
    death_events_test::Events events;
    std::deque<csol::scenario::MonsterType> types;
    std::vector<std::string> log;
    std::vector<std::shared_ptr<ProbeState>> states;
    std::vector<std::shared_ptr<EffectState>> effectStates;
    CCSOLMonsterPool pool;
    CCSOLScenarioMap map;
    CCSOLMonsterModels models;
    CCSOLScenarioMessages messages;
    Services services{log};
    std::shared_ptr<MoneyEffects> moneyEffects = std::make_shared<MoneyEffects>(log);
    std::shared_ptr<const csol::scenario::UpgradeTables> upgradeTables;
    std::vector<int> accountAmounts;
    std::function<void()> afterAccount;
    std::uint64_t playerGeneration{};
    int previousMoneyId{};
    float previousMaxMoney{};
    std::unique_ptr<CCSOLMonsterDeathOwner> owner;
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 32> sequences{}; } model;
    int sounds{};
    std::function<void()> onSound;

    explicit Fixture(bool stopRule = false, bool drainRule = false)
    {
        active = this;
        engine.globals.time = 20;
        previousMoneyId = gmsgMoney; previousMaxMoney = maxmoney.value;
        gmsgMoney = 77; maxmoney.value = 16000;
        g_engfuncs.pfnRegUserMsg = [](const char* name, int size)
        {
            Require(size == -1, "all scenario registrations remain variable length");
            const int id = std::strcmp(name, "MPToCL") == 0 ? 96 : std::strcmp(name, "ZBSVer2") == 0 ? 97 :
                std::strcmp(name, "MPToCL2") == 0 ? 98 : 0;
            Require(id != 0, "only declared scenario messages register in owner fixture");
            active->engine.trace.push_back({'r', size, id, name}); return id;
        };
        g_engfuncs.pfnWriteLong = [](int value)
        {
            auto& engine = active->engine; Require(engine.packetOpen, "long belongs to open packet");
            for (unsigned shift = 0; shift < 32; shift += 8)
                engine.packets.back().bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) >> shift));
            engine.trace.push_back({'l', value}); engine.Hook('l');
        };
        g_engfuncs.pfnPrecacheSound = [](const char*) { return 1; };
        g_engfuncs.pfnEmitSound = [](edict_t*, int channel, const char*, float volume, float attenuation, int, int pitch)
        {
            ++active->sounds; active->log.emplace_back("sound");
            Check(channel == CHAN_BODY && volume == 1 && attenuation == ATTN_NORM && pitch == PITCH_NORM,
                "actual death sound wrapper passes original channel and levels");
            const auto callback = active->onSound; if (callback) callback();
        };
        g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { return &active->model; };
        model.header.numseq = static_cast<int>(model.sequences.size());
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequences.data()) - reinterpret_cast<char*>(&model));
        for (auto& sequence : model.sequences) { sequence.fps = 10; sequence.numframes = 11; }
        // Synthetic table encoded from "id,drain,stop\r\n40,0,1\r\n" by the
        // documented TEA configuration codec. This suite does not test TEA.
        if (stopRule) engine.files["maps/ZBS_DIE_EVENT_map_session_test.cso"] =
            death_events_test::EventEngine::Hex("b83f42ecd05cc4e1b3c89ac08c59c28e7f988e528cbbd5a5");
        // Saved death-rule fixture: type500 drains ordinary monsters and then
        // stops waves. Load it through the real map resource decoder.
        if (drainRule) engine.files["maps/ZBS_DIE_EVENT_map_session_test.cso"] =
            death_events_test::EventEngine::Hex("a0442f2bc137e6f410ee49cd6223a87f846930e12cf37f4b438904d7e8cd199f09ef547f546be6313976e95009f378af6a97499d031ea2f8");
        engine.Markers();
        Require(map.BeginMap("map_session_test") && map.CollectEntities() &&
            map.BeginRound(0, 0, 0, events), "owner fixture initializes real map");
        Require(pool.Initialize() && messages.Register(), "owner fixture initializes real pool and messages");
        Check(engine.trace.size() >= 3 && engine.trace[engine.trace.size() - 3].name == "MPToCL" &&
            engine.trace[engine.trace.size() - 2].name == "ZBSVer2" && engine.trace.back().name == "MPToCL2",
            "owner registers all three independent message IDs");
        // Valid synthetic progression data, not a resource-decoder oracle.
        // Money uses the configured production state but does not award XP.
        auto upgrades = std::make_shared<csol::scenario::UpgradeTables>();
        upgrades->levels.rows.resize(3);
        auto& character = upgrades->classes.byId[0]; character.increases.fill(1);
        character.order = {csol::scenario::UpgradeStat::Health, csol::scenario::UpgradeStat::Damage,
            csol::scenario::UpgradeStat::Speed, csol::scenario::UpgradeStat::Armor, csol::scenario::UpgradeStat::Ammo};
        upgradeTables = std::move(upgrades);
        csol::scenario::MonsterTypeTable table;
        table.rows.emplace_back(); table.rows[0].id = 40; table.rows[0].appearance = "zombi_host";
        table.byId.emplace(40, 0); ConfigError error;
        Require(models.Precache(table, error), "owner fixture precaches actual appearance/sound catalog");
        owner = std::make_unique<CCSOLMonsterDeathOwner>(map, pool, models, messages, services, 98);
        g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(AccountHook, 150);
        g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.registerHook(ResetHook, 150);
        recordReset = [&] { log.emplace_back("reset"); };
        engine.onMessage = [&](char stage)
        {
            if (stage == 'e' && engine.packets.back().message == 96 &&
                engine.packets.back().bytes.size() == 3 && engine.packets.back().bytes[0] == 200)
                log.emplace_back("notice");
        };
        engine.ClearCapture();
    }
    ~Fixture()
    {
        // Captured test locals can end before Fixture. Detach all callbacks
        // before releasing owned behaviors; no destructor uses expired locals.
        afterReset = {}; recordReset = {}; onSound = {}; afterAccount = {}; moneyEffects->onMoney = {};
        g_ReGameHookchains.m_CBasePlayer_AddAccount.unregisterHook(AccountHook);
        g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.unregisterHook(ResetHook);
        engine.onMessage = {}; engine.onQuery = {}; engine.onRemove = {}; engine.findOverride = {};
        services.actions.clear();
        for (auto& state : effectStates) { state->onNotify = {}; state->onDestroy = {}; }
        for (auto& state : states) { state->onDestroy = {}; state->before = {}; state->select = {}; state->duration = {}; state->post = {}; }
        owner->Reset(); if (services.players) services.players->Clear(); moneyEffects->valid = false;
        pool.Shutdown(); map.EndMap(); engine.DisposeAll(); engine.DestroyWorld(); pool.ForgetMap();
        gmsgMoney = previousMoneyId; maxmoney.value = previousMaxMoney;
        active = nullptr;
    }
    static void AccountHook(IReGameHook_CBasePlayer_AddAccount* chain, CBasePlayer* player,
        int amount, RewardType type, bool tracked)
    {
        auto& f = *active;
        Check(type == RT_NONE && !tracked, "real kill money reaches native AddAccount with untracked HUD delta");
        f.accountAmounts.push_back(amount); f.log.emplace_back("account");
        chain->callNext(player, amount, type, tracked);
        const auto callback = f.afterAccount; if (callback) callback();
    }
    std::shared_ptr<CCSOLScenarioPlayerUpgrades> UpgradeState(CBasePlayer& player)
    {
        auto state = CCSOLScenarioPlayerUpgrades::Create(player, upgradeTables, moneyEffects,
            messages, 1.0f, ++playerGeneration);
        Require(state && state->Configure(0, 0, false), "real player upgrade state configured for money consumer");
        return state;
    }
    CBasePlayer& Client(int index)
    {
        auto& player = engine.Client(index);
        Require(services.players->Bind(UpgradeState(player)), "death reward player bound to actual registry");
        engine.ClearCapture(); log.clear(); return player;
    }
    std::size_t MoneyPackets() const
    { return static_cast<std::size_t>(std::count_if(engine.packets.begin(), engine.packets.end(), [](const auto& packet) { return packet.message == 77; })); }
    bool MoneyPacket(int balance, int slot = 1) const
    {
        const auto packet = std::find_if(engine.packets.begin(), engine.packets.end(), [](const auto& value) { return value.message == 77; });
        const auto value = static_cast<std::uint32_t>(balance);
        return packet != engine.packets.end() && packet->complete && packet->destination == MSG_ONE &&
            packet->slot == slot && packet->origin == nullptr && packet->bytes == std::vector<std::uint8_t>{
                static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24), 0};
    }
    std::shared_ptr<ProbeState> State(std::string name)
    {
        auto state = std::make_shared<ProbeState>(); state->name = std::move(name); state->log = &log;
        states.push_back(state); return state;
    }
    CCSOLScenarioMonster& Monster(const std::shared_ptr<ProbeState>& state, int priority = 0, int id = 40, int suffix = -1)
    {
        auto& type = types.emplace_back(); type.id = id; type.appearance = "zombi_host";
        type.priority = static_cast<std::int8_t>(priority); type.column19 = suffix;
        auto* monster = pool.Acquire(type, 7, map.SpawnPoints().Points());
        Require(monster && monster->InstallBehavior(std::make_unique<Behavior>(state)), "real monster and behavior installed");
        monster->pev->health = 100; monster->pev->takedamage = DAMAGE_AIM;
        monster->pev->flags |= FL_ONGROUND; monster->pev->frame = 72; monster->pev->nextthink = 77;
        monster->pev->sequence = 1; monster->Animation().SynchronizeSequence(1);
        if (priority != 2) Require(map.SpawnPoints().Points().ConsumePointQuota(1, 7), "attempted spawn quota consumed");
        return *monster;
    }
    std::uint32_t Quota() const { return map.SpawnPoints().Points().FindMonsterPoint(1, 7)->consumed; }
    std::shared_ptr<EffectState> Effect(CCSOLScenarioMonster& monster, CBaseEntity& source)
    {
        auto state = std::make_shared<EffectState>(); state->log = &log; effectStates.push_back(state);
        auto lifetime = std::make_shared<EffectLifetime>(state);
        CSOLMonsterDeathCall install(map, owner->Requests(), monster, {});
        const CSOLMonsterEffectGuard guard{[](const void* context)
            { return static_cast<const CSOLMonsterDeathCall*>(context)->Current(); }, &install};
        Require(monster.DamageEffects()->Install(guard, source.pev, 6, 3, 0, 725, -1, {}, [state, lifetime]
        {
            ++state->notified; state->log->emplace_back("effects");
            const auto action = state->onNotify; if (action) action();
        }), "real death effect source record installed before the death transaction");
        return state;
    }
    void Expect(bool attacker, bool grenade = false)
    {
        services.expected.clear(); services.next = 0;
        if (attacker && !grenade)
        {
            services.expected.emplace_back("extractor");
            if (services.extractor) services.expected.emplace_back("room");
        }
        services.expected.emplace_back("rules");
        if (attacker) services.expected.emplace_back("player");
        services.expected.emplace_back("golden");
    }
    void Complete() { Check(services.next == services.expected.size(), "every explicitly expected service consumed"); }
    void Reborrow(CCSOLScenarioMonster& old)
    {
        while (pool.AvailableCount())
        {
            const int priority = pool.CategoryCount(0) < 39 ? 0 : pool.CategoryCount(1) < 4 ? 1 : 2;
            Monster(State("filler"), priority, static_cast<int>(types.size()) + 100);
        }
        const auto* type = old.Type(); const auto lease = pool.Lease(old);
        Require(pool.Free(old), "return exact slot for nested acquisition");
        auto* next = pool.Acquire(*type, 7, map.SpawnPoints().Points());
        Require(next == &old && pool.Lease(old) != lease, "same address receives different lease");
        Require(next->InstallBehavior(std::make_unique<Behavior>(State("replacement"))), "replacement owns real behavior");
    }
    void ReleaseMap()
    {
        Require(owner->Reset(), "reset death dispatcher before storage release");
        if (services.players) services.players->Clear(); moneyEffects->valid = false;
        Require(map.EndMap() && pool.Shutdown(), "detach map and pool before releasing storage");
        engine.DisposeAll(); engine.DestroyWorld(); Require(pool.ForgetMap(), "forget old edict storage");
    }
};

void KillMoneyNumbers()
{
    using csol::scenario::ResolveMonsterKillMoney;
    const struct Case { std::int32_t input; bool extractor; int room; std::int32_t output; } cases[]{
        {16777217,false,0,16777217},{16777217,true,800,16777217},{16777217,true,0,25165824},
        {-16777217,false,0,-16777217},{17,true,0,25},{-17,true,0,-25},
        {0,true,0,0},{1,true,0,1},{-1,true,0,-1},
        {INT32_MAX,false,0,INT32_MAX},{INT32_MAX,true,800,INT32_MAX},
        {INT32_MAX,true,0,INT32_MIN},{INT32_MIN,true,0,INT32_MIN}
    };
    for (const auto& test : cases)
        Check(ResolveMonsterKillMoney(test.input,test.extractor,test.room)==test.output,
            "kill money preserves integer bypass or original float32 multiply and truncation");
    std::int32_t hit=123;
    Check(csol::scenario::ResolveMonsterHitMoney(17.9f,true,0,hit)&&hit==25,
        "hit reward truncates RNG before shared extractor multiplier");
    Check(csol::scenario::ResolveMonsterHitMoney(-17.9f,true,0,hit)&&hit==-25,
        "negative hit RNG also truncates before extractor multiplier");
    Check(!csol::scenario::ResolveMonsterHitMoney(std::numeric_limits<float>::quiet_NaN(),true,0,hit)&&hit==-25,
        "shared helper retains hit nonfinite rejection and output preservation");
}

void KillMoneyConsumption()
{
    const struct Case
    {
        int base, initial, room; bool extractor, dead; std::optional<float> percent;
        int amount, balance;
    } cases[]{
        {17,100,0,false,false,{},17,117}, {17,100,0,true,false,{},25,125},
        {17,100,800,true,false,{},17,117}, {17,100,0,true,false,50,37,137},
        {0,100,0,false,false,{},0,100}, {-17,100,0,false,false,{},-17,83},
        {-17,100,0,true,false,50,-37,63}, {17,100,0,false,true,{},17,117},
        {16777217,0,0,false,false,{},16777217,16777217},
        {16777217,0,800,true,false,{},16777217,16777217},
        {16777217,0,0,false,false,0,16777216,16777216},
        {17,15990,0,false,false,{},17,16000}, {-17,5,0,false,false,{},-17,0}
    };
    for (const auto& test : cases)
    {
        Fixture f; auto state=f.State("A"); state->parameters.bonusMoney=test.base;
        auto& monster=f.Monster(state); auto& player=f.Client(1); f.engine.globals.maxClients=1;
        player.m_iAccount=test.initial;
        if (test.base>16000) maxmoney.value=100000000;
        if (test.dead) { player.pev->health=0; player.pev->deadflag=DEAD_DEAD; }
        f.services.extractor=test.extractor; f.services.roomOption=test.room; f.moneyEffects->percent=test.percent;
        f.services.actions["extractor"]=[&](auto& call,auto&)
        { Check(call.Current()->pev->deadflag!=DEAD_DEAD,"money input is queried before MarkDead");return true; };
        f.moneyEffects->onMoney=[&]
        {
            Check(f.services.roomQueries==(test.extractor?1:0)&&player.m_iAccount==test.initial,
                "extractor and lazy room query precede actual percent consumer and account write");
        };
        f.afterAccount=[&]
        { Check(monster.pev->deadflag!=DEAD_DEAD&&player.m_iAccount==test.balance,"native account commits before monster MarkDead"); };
        CSOLMonsterDeathArguments args; args.attacker=&player;
        f.Expect(true);
        Require(f.owner->Requests().Request(monster,args)==Result::Executed,"real death owner completes native money consumption");
        Check(f.accountAmounts==std::vector<int>{test.amount}&&player.m_iAccount==test.balance&&
            f.MoneyPackets()==1&&f.MoneyPacket(test.balance),"real modifier AddAccount clamp and Money wire match independent cases");
        Check(f.services.extractorQueries==1&&f.services.roomQueries==(test.extractor?1:0)&&
            f.moneyEffects->moneyQueries==1,"room query is lazy and zero negative dead-player awards are not skipped");
        f.Complete();
    }
    {
        Fixture f; auto state=f.State("A"); auto& monster=f.Monster(state);
        auto& player=f.Client(1); auto& inflictor=f.engine.Construct("test_projectile",700);
        f.engine.globals.maxClients=1; CSOLMonsterDeathArguments args;args.attacker=&player;args.inflictor=&inflictor;
        f.Expect(true);int ignored{};
        f.afterAccount=[&]{if(f.owner->Requests().Request(monster,args)==Result::Ignored)++ignored;};
        Check(f.owner->Requests().Request(monster,args)==Result::Executed&&ignored==1&&
            f.accountAmounts==std::vector<int>{17}&&player.m_iAccount==17,
            "non-grenade inflictor awards and recursive same-monster death cannot pay twice");
        f.Complete();
    }
    // Grenades bypass money even if there is no authoritative money registry.
    // The later player-kill callback remains part of the death order.
    {
        Fixture f; auto state=f.State("A"); auto& monster=f.Monster(state);auto& player=f.Client(1);
        f.services.players->Clear();f.services.players.reset();
        auto& grenade=f.engine.Construct("grenade",700);
        CSOLMonsterDeathArguments args;args.attacker=&player;args.inflictor=&grenade;
        f.Expect(true,true);
        Check(f.owner->Requests().Request(monster,args)==Result::Executed&&f.accountAmounts.empty()&&
            f.services.extractorQueries==0&&f.services.roomQueries==0&&f.moneyEffects->moneyQueries==0,
            "grenade skips whole money resolution without requiring player registry");
        f.Complete();
    }
}

void KillMoneyCancellation()
{
    for (const std::string_view edge : {"extractor","room","modifier","account"})
    {
        Fixture f;auto state=f.State("A");auto& monster=f.Monster(state);auto& player=f.Client(1);
        f.services.extractor=true;f.Expect(true);bool reset{};
        const auto cancel=[&]{reset=true;Require(f.owner->Reset(),"money callback resets actual dispatcher generation");};
        if(edge=="extractor"||edge=="room")
            f.services.actions[std::string(edge)]=[&](auto&,auto&){cancel();return true;};
        else if(edge=="modifier")f.moneyEffects->onMoney=cancel;
        else f.afterAccount=cancel;
        CSOLMonsterDeathArguments args;args.attacker=&player;
        Check(f.owner->Requests().Request(monster,args)==Result::Ignored&&reset&&state->postCalls==0&&
            monster.pev->deadflag!=DEAD_DEAD,"money callback generation change cancels old death before MarkDead");
        const bool paid=edge=="account";
        Check(f.accountAmounts.size()==(paid?1u:0u)&&player.m_iAccount==(paid?25:0)&&
            f.MoneyPackets()==(paid?1u:0u),"cancellation retains only account side effects completed before callback");
        Check(f.owner->Error().message.empty()&&f.owner->Requests().Error().empty(),
            "old money callback cannot poison replacement dispatcher state");
    }
    for (int change : {0,1,2})
    {
        Fixture f;auto state=f.State("A");auto& monster=f.Monster(state);auto& player=f.Client(1);
        const auto old=f.services.players->Upgrades(EHANDLE(&player));
        const auto replacement=change<2?f.UpgradeState(player):nullptr;
        f.engine.ClearCapture();f.log.clear();f.Expect(true);
        if(change!=0)
        {
            f.services.expected={"extractor","rules","golden"};
            f.services.actions["rules"]=[](auto& call,auto&)
            { Check(!call.Attacker(),"retired money recipient is revoked before rules and player stages");return true; };
            f.services.actions["golden"]=[](auto& call,auto&)
            { Check(!call.Attacker(),"monster cleanup cannot credit a replacement through Golden");return true; };
        }
        std::shared_ptr<CCSOLScenarioPlayers> replacementRegistry;
        if(change==0)
        {
            replacementRegistry=std::make_shared<CCSOLScenarioPlayers>();
            Require(replacementRegistry->Bind(replacement),"replacement registry owns configured actual player state");
        }
        f.moneyEffects->onMoney=[&]
        {
            if(change==0)f.services.players=replacementRegistry;
            else if(change==1)Require(f.services.players->Bind(replacement),"same registry replaces player generation during money modifier");
            else {f.services.players->Remove(EHANDLE(&player));f.engine.Dispose(player.edict());}
        };
        CSOLMonsterDeathArguments args;args.attacker=&player;
        const auto result=f.owner->Requests().Request(monster,args);
        Check(f.accountAmounts.empty()&&f.MoneyPackets()==0,"retired or replaced money recipient receives no stale account write");
        if(change==0)
            Check(result==Result::Failed&&state->postCalls==0&&monster.pev->deadflag!=DEAD_DEAD,
                "whole registry replacement still fails rather than disguising an owner change as player retirement");
        else
        {
            Check(result==Result::Executed&&state->postCalls==1&&monster.pev->deadflag==DEAD_DEAD&&
                f.owner->Error().message.empty()&&f.owner->Requests().Error().empty()&&f.Quota()==0,
                "same registry player replacement or disconnect skips old credit and completes the actual monster death tail");
            f.Complete();
        }
        if(replacement)Check(replacement->Current()&&replacement->State()&&replacement->State()->experience==0,
            "stale reward never changes replacement upgrade progress");
        if(change!=0)Check(!old->Current(),"replace or disconnect invalidates retained previous player owner");
        // The isolated old registry can remain alive on the callback stack.
        // Invalidate its state before the fixture releases engine edicts.
        if(change==0)old->Invalidate();
    }
    {
        Fixture f;auto state=f.State("A");auto& monster=f.Monster(state);auto& player=f.Client(1);
        f.engine.globals.maxClients=1;f.services.expected={"extractor","rules","golden"};
        f.services.actions["rules"]=[](auto& call,auto&)
        {Check(!call.Attacker(),"post-account disconnect revokes subsequent player credit");return true;};
        f.services.actions["golden"]=[](auto& call,auto&)
        {Check(!call.Attacker(),"Golden receives no disconnected recipient");return true;};
        f.afterAccount=[&]{player.has_disconnected=true;};
        CSOLMonsterDeathArguments args;args.attacker=&player;
        Check(f.owner->Requests().Request(monster,args)==Result::Executed&&
            f.accountAmounts==std::vector<int>{17}&&player.m_iAccount==17&&f.MoneyPackets()==1&&f.MoneyPacket(17)&&
            monster.pev->deadflag==DEAD_DEAD&&state->postCalls==1&&f.Quota()==0&&
            f.owner->Error().message.empty()&&f.owner->Requests().Error().empty(),
            "disconnect after native AddAccount retains the single completed payment and still completes monster cleanup");
        Check(f.owner->Requests().Request(monster,args)==Result::Ignored&&f.accountAmounts.size()==1,
            "repeating the same dead lease cannot repay the disconnected recipient");
        f.Complete();
    }
    for (int failure : {0,1,2,3,4,5})
    {
        Fixture f;auto state=f.State("A");auto& monster=f.Monster(state);auto& player=f.Client(1);
        f.services.extractor=failure==5;
        f.Expect(true);
        if(failure==0)f.services.players.reset();
        if(failure==1)f.services.players->Remove(EHANDLE(&player));
        if(failure==2)f.services.actions["extractor"]=[](auto&,auto& error){error.message="explicit unavailable extractor";return false;};
        if(failure==3)f.moneyEffects->moneyAvailable=false;
        if(failure==4){state->parameters.bonusMoney=INT32_MAX;player.m_iAccount=100;}
        if(failure==5)f.services.actions["room"]=[](auto&,auto& error){error.message="explicit unavailable room option";return false;};
        CSOLMonsterDeathArguments args;args.attacker=&player;
        Check(f.owner->Requests().Request(monster,args)==Result::Failed&&f.accountAmounts.empty()&&
            f.MoneyPackets()==0&&!f.owner->Error().message.empty()&&state->postCalls==0&&
            monster.pev->deadflag!=DEAD_DEAD,
            "missing registry state extractor modifier room or overflowing account fails before payment and MarkDead");
    }
    {
        Fixture f;auto state=f.State("A");auto& monster=f.Monster(state);
        auto& attacker=f.engine.Construct("not_a_player",700);
        CSOLMonsterDeathArguments args;args.attacker=&attacker;
        Check(f.owner->Requests().Request(monster,args)==Result::Failed&&
            f.owner->Error().message=="scenario kill money attacker is not a native player"&&
            f.services.extractorQueries==0&&f.services.roomQueries==0&&f.moneyEffects->moneyQueries==0&&
            f.accountAmounts.empty()&&f.MoneyPackets()==0&&state->postCalls==0,
            "non-player attacker cannot acquire player reward services or native money");
    }
}

void OrderAndReplacement()
{
    for (bool attacker : {false, true}) for (bool silent : {false, true}) for (bool grenade : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state, 0, 40, 40);
        auto& client = f.Client(1); f.engine.globals.maxClients = 1;
        const auto effect = f.Effect(monster, client);
        auto& host = monster.HostState(); host.nonlethalDamage = 42; host.damageScaleCount = 9; host.damageScale = 3;
        const auto observeReset = [&]
        {
            Check(host.nonlethalDamage == 0 && host.damageScaleCount == 0 && host.damageScale == 1,
                "actual source notification and destruction see reset host damage modifiers");
        };
        effect->onNotify = observeReset; effect->onDestroy = observeReset;
        auto& target = f.engine.AddTarget(500, "mm_zb_die_40"); target.action = [&] { f.log.emplace_back("target"); };
        CSOLMonsterDeathArguments args; args.suppressSound = silent;
        if (attacker) args.attacker = &client;
        if (grenade) args.inflictor = &f.engine.Construct("grenade", 700);
        f.Expect(attacker, grenade);
        Check(f.owner->Requests().Request(monster, args) == Result::Executed, "whole owner completes exact ordinary path");
        std::vector<std::string> expected{"notice", "before:A", "sequence:A", "reset"};
        if (!silent) expected.emplace_back("sound");
        if (attacker && !grenade) expected.insert(expected.end(), {"extractor", "modifier", "account"});
        expected.insert(expected.end(), {"duration:A", "rules"});
        if (attacker) expected.emplace_back("player");
        expected.insert(expected.end(), {"effects", "effects-destroy", "target", "golden", "post:A"});
        Check(f.log == expected, "notice behavior stages rewards targets Golden post exact order");
        Check(effect->notified == 1 && effect->destroyed == 1 && !monster.DamageEffects()->At(1).notify &&
            monster.DamageEffects()->At(1).count == 0, "death owner notifies and destroys its actual source record once");
        Check(f.Quota() == 0 && monster.pev->nextthink == 23 && monster.Animation().CachedSequence() == 7,
            "single quota return deadline and per-entity sequence cache");
        Check(f.engine.packets.size() == (attacker && !grenade ? 2u : 1u) &&
            death_events_test::DeathPacket(f.engine.packets[0], 1, Engine::Index(monster.edict())),
            "whole owner emits exact death packet before any actual money packet");
        Check(client.m_iAccount == (attacker && !grenade ? 17 : 0) &&
            f.MoneyPackets() == (attacker && !grenade ? 1u : 0u),
            "null inflictor awards real account while grenade or null attacker skips money");
        if (attacker && !grenade) Check(f.MoneyPacket(17), "native account sends exact five-byte Money body");
        f.Complete();
    }
    {
        Fixture f; auto a = f.State("A"), b = f.State("B"), c = f.State("C"); auto& monster = f.Monster(a);
        auto& client = f.Client(1); f.engine.globals.maxClients = 1;
        b->parameters.bonusMoney = 73; b->sequence = 9; b->seconds = 6; c->parameters.shortDeath = true;
        a->before = [&](auto& call, auto&) { return call.Current()->InstallBehavior(std::make_unique<Behavior>(b)); };
        b->duration = [&](auto& call, auto&) { return call.Current()->InstallBehavior(std::make_unique<Behavior>(c)); };
        a->post = [&](auto&, auto&) { Check(a->destroyed == 0 && b->destroyed == 0, "initial and intermediate behavior pinned through original post"); return true; };
        f.Expect(true); CSOLMonsterDeathArguments args; args.attacker = &client;
        Check(f.owner->Requests().Request(monster, args) == Result::Executed, "behavior replacement completes owner");
        Check(a->beforeCalls == 1 && a->postCalls == 1 && b->sequenceCalls == 1 && b->durationCalls == 1 &&
            b->beforeCalls == 0 && b->postCalls == 0 && c->durationCalls == 0 && c->postCalls == 0,
            "entry behavior before/post and current behavior sequence/duration are distinct");
        Check(f.accountAmounts == std::vector<int>{73} && client.m_iAccount == 73 &&
            monster.pev->sequence == 9 && monster.pev->nextthink == 23,
            "money after replacement and timer flags after duration use current attributes");
        Check(a->destroyed == 1 && b->destroyed == 1 && c->destroyed == 0, "retained obsolete behaviors release only after post");
        f.Complete();
    }
    {
        Fixture f(true); auto state = f.State("A"); auto& monster = f.Monster(state, 0, 40, 40); f.Expect(false);
        auto& target = f.engine.AddTarget(500, "mm_zb_die_40");
        target.action = [&]
        {
            Check(!f.map.WaveDispatchStopped(), "target runs before rule stop");
            // Legal fixture-owned table edit distinguishes current type.id
            // from the separate actual identity; it does NOT change TypeId.
            const_cast<csol::scenario::MonsterType*>(monster.Type())->id = 41;
        };
        f.services.actions["golden"] = [&](auto&, auto&) { Check(f.map.WaveDispatchStopped(), "actualType40 rule stops waves before Golden"); return true; };
        Check(f.owner->Requests().Request(monster, {}) == Result::Executed && monster.TypeId() == 40,
            "real rule uses actual identity rather than mutated type.id"); f.Complete();
    }
}

void EarlyParkAndReentry()
{
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state);
        monster.SetThink(&CCSOLScenarioMonster::BehaviorThink); const auto think = monster.m_pfnThink;
        const auto lease = f.pool.Lease(monster); const auto idle = f.pool.AvailableCount();
        state->before = [&](auto& call, auto&)
        {
            auto* current = call.Current(); Require(current != nullptr, "frozen before current");
            Check(f.pool.ParkForDeath(*current), "frozen early park succeeds");
            Check(current->m_pfnThink == think && current->pev->nextthink == 77 && current->pev->takedamage == DAMAGE_AIM &&
                f.pool.Lease(*current) == lease && f.pool.AvailableCount() == idle + 1,
                "early park preserves callbacks nextthink damage and lease");
            Check(f.pool.ParkForDeath(*current) && f.pool.AvailableCount() == idle + 1, "repeated park does not duplicate idle entry");
            return true;
        };
        f.Expect(false); Check(f.owner->Requests().Request(monster, {}) == Result::Executed, "early parked lease finishes full owner");
        Check(state->postCalls == 1 && f.Quota() == 0 && monster.pev->movetype == MOVETYPE_TOSS && monster.pev->nextthink == 23,
            "frozen outer path continues through post with its late movement and timer");
        monster.FreeThink(); Check(f.pool.AvailableCount() == idle + 1 && monster.m_pfnThink == nullptr &&
            monster.pev->movetype == MOVETYPE_TOSS && monster.pev->nextthink == 23, "late Free only clears callbacks for already parked slot");
        f.Complete();
    }
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state); f.Client(1); f.engine.globals.maxClients = 1;
        int ignored{};
        f.engine.onMessage = [&](char stage)
        { if (stage == 'e') { f.log.emplace_back("notice"); if (f.owner->Requests().Request(monster, {}) == Result::Ignored) ++ignored; } };
        state->before = [&](auto&, auto&) { if (f.owner->Requests().Request(monster, {}) == Result::Ignored) ++ignored; return true; };
        f.Expect(false); Check(f.owner->Requests().Request(monster, {}) == Result::Executed, "recursive same lease does not repeat death");
        Check(ignored == 2 && state->beforeCalls == 1 && state->postCalls == 1 && f.Quota() == 0 && f.engine.packets.size() == 1,
            "notice and before recursion produce single side effects"); f.Complete();
    }
    {
        Fixture f; auto a = f.State("A"), b = f.State("B"); auto& monster = f.Monster(a);
        const auto scope = f.owner->Requests().BeginBehavior(monster); Require(scope.has_value(), "actual behavior deferral scope");
        Check(f.owner->Requests().Request(monster, {}) == Result::Deferred, "death queued before replacement");
        Require(monster.InstallBehavior(std::make_unique<Behavior>(b)), "replace before deferred execution");
        f.Expect(false); Check(f.owner->Requests().EndBehavior(*scope), "scope drains deferred owner");
        Check(a->beforeCalls == 0 && a->postCalls == 0 && b->beforeCalls == 1 && b->postCalls == 1,
            "deferred request captures behavior at execution rather than queue time"); f.Complete();
    }
    {
        Fixture f; auto a = f.State("A"), b = f.State("B"); auto& first = f.Monster(a, 0, 40, 40); auto& second = f.Monster(b);
        auto& source = f.engine.Client(1);
        const auto firstEffect = f.Effect(first, source), secondEffect = f.Effect(second, source);
        f.services.expected = {"rules", "rules", "golden", "golden"};
        auto& target = f.engine.AddTarget(500, "mm_zb_die_40");
        target.action = [&]
        {
            Check(firstEffect->notified == 1 && firstEffect->destroyed == 1 && secondEffect->notified == 0,
                "first real effect cleanup completes before target begins nested death");
            Check(f.owner->Requests().Request(second, {}) == Result::Executed, "different slot nested full owner executes");
        };
        Check(f.owner->Requests().Request(first, {}) == Result::Executed && a->postCalls == 1 && b->postCalls == 1 && f.Quota() == 0,
            "different monster nested death preserves both transactions"); f.Complete();
        Check(secondEffect->notified == 1 && secondEffect->destroyed == 1,
            "nested transaction independently cleans its actual source record once");
    }
}

// This group enters the real owner/Drain/Requests/Stages/Pool composition.
// It does not synthesize Gameplay::Update or prove its frame/restart ordering.
void RoundDrain()
{
    {
        Fixture f;
        std::array<std::shared_ptr<ProbeState>, 4> states;
        std::array<CCSOLScenarioMonster*, 4> monsters{};
        for (std::size_t i = 0; i < monsters.size(); ++i)
        {
            states[i] = f.State("batch" + std::to_string(i));
            monsters[i] = &f.Monster(states[i]);
            if (i) Require(monsters[i - 1]->Slot() < monsters[i]->Slot(), "fresh pool leases are in drain traversal order");
            f.services.expected.insert(f.services.expected.end(), {"rules", "golden"});
        }
        // Fixture-owned type input crosses the actual Drain Boss threshold;
        // its explicit death behavior is still an observer, not Boss AI.
        f.types[1].skill = 1000;
        Require(f.map.ScheduleMonsterDrain(20.0f, 0), "ending batch uses the real map-owned team field");
        const auto first = f.owner->Drain(3, true);
        Check(first.valid && first.selected == 3 && first.requested == 3,
            "round drain sends only three selected candidates through the real owner including Boss skill1000");
        for (std::size_t i = 0; i < monsters.size(); ++i)
            Check(states[i]->beforeCalls == (i < 3 ? 1 : 0) && states[i]->postCalls == (i < 3 ? 1 : 0) &&
                monsters[i]->pev->deadflag == (i < 3 ? DEAD_DEAD : DEAD_NO),
                "first batch finishes the actual death tail for its three slots and leaves the fourth live");
        Check(f.services.next == 6 && f.Quota() == 1 && f.accountAmounts.empty() && f.services.extractorQueries == 0,
            "environmental ending deaths return three quotas without player money attribution");
        const auto second = f.owner->Drain(3, true);
        Check(second.valid && second.selected == 1 && second.requested == 1 && states[3]->postCalls == 1 && f.Quota() == 0,
            "next explicit batch skips dead slots and processes only the remaining live monster");
        const auto before = f.log;
        const auto empty = f.owner->Drain(3, true);
        Check(empty.valid && empty.selected == 0 && empty.requested == 0 && f.log == before,
            "empty real pool scan returns no candidates without repeating any death service");
        Check(f.map.MonsterDrainAt() == 20.0f,
            "owner reports candidates but does not impersonate Gameplay clock advancement or cancellation");
        f.Complete();
    }
    {
        Fixture f;
        auto a = f.State("first"), b = f.State("old-team"), c = f.State("new-team"), d = f.State("nested");
        auto& first = f.Monster(a); auto& oldTeam = f.Monster(b);
        auto& newTeam = f.Monster(c); auto& nested = f.Monster(d);
        Require(first.Slot() < oldTeam.Slot() && oldTeam.Slot() < newTeam.Slot() && newTeam.Slot() < nested.Slot(),
            "nested-filter case uses the real pool's ordered slots");
        // Supply a real second-team point and transfer the fixture's already
        // consumed quotas, so each ordinary death returns its actual team.
        auto& points = f.map.SpawnPoints().Points();
        auto secondPoint = points.FindMonsterPoint(1, 7)->point;
        secondPoint.team = 2;
        points.RegisterMonsterPoint(secondPoint, 1000);
        for (auto* monster : {&newTeam, &nested})
        {
            points.ReturnPointQuota(1, 7);
            Require(points.ConsumePointQuota(2, 7), "second-team fixture quota transferred through production points");
            monster->pev->team = 2;
        }
        Require(f.map.ScheduleMonsterDrain(20.0f, 1), "nested-filter batch initially selects team one");
        const auto& team = f.map.MonsterDrainTeam();
        const auto* teamAddress = &team;
        f.services.expected = {"rules", "rules", "golden", "golden", "rules", "golden"};
        f.services.actions["rules"] = [&](auto& call, auto&)
        {
            if (call.Current() == &first)
                Check(f.owner->Requests().Request(nested, {}) == Result::Executed,
                    "first selected death runs a different slot through the same real dispatcher");
            else if (call.Current() == &nested)
                Require(f.map.ScheduleMonsterDrain(21.0f, 2), "nested death changes the stable manager team field");
            return true;
        };
        const auto result = f.owner->Drain(3, true);
        Check(result.valid && result.selected == 2 && result.requested == 2 &&
            teamAddress == &f.map.MonsterDrainTeam() && team == 2,
            "outer drain re-reads the same map field after a nested callback rather than caching team one");
        Check(a->postCalls == 1 && b->beforeCalls == 0 && c->postCalls == 1 && d->postCalls == 1 &&
            oldTeam.pev->deadflag == DEAD_NO && newTeam.pev->deadflag == DEAD_DEAD,
            "changed filter skips later team one selects later team two and does not repeat the nested dead slot");
        Check(f.Quota() == 1 && points.FindMonsterPoint(2, 7)->consumed == 0 && f.map.MonsterDrainAt() == 21.0f,
            "nested and outer deaths each return their own quotas without overwriting the new clock");
        f.Complete();
    }
    {
        Fixture f; auto state = f.State("skipped-boss"); auto& monster = f.Monster(state);
        f.types.back().skill = 1000;
        Require(f.map.ScheduleMonsterDrain(20.0f, 2), "nonmatching team filter installed");
        const auto noMatch = f.owner->Drain(3, false);
        Check(noMatch.valid && noMatch.selected == 0 && noMatch.requested == 0 && monster.pev->deadflag == DEAD_NO,
            "no selected candidate can mean a live monster is excluded by the current team");
        Require(f.map.ScheduleMonsterDrain(20.0f, 0), "all-team filter installed in the same stable field");
        const auto skipped = f.owner->Drain(3, false);
        Check(skipped.valid && skipped.selected == 1 && skipped.requested == 0 &&
            state->beforeCalls == 0 && f.services.next == 0 && f.Quota() == 1,
            "Boss exclusion has a selected slot but no death request and must not be confused with an empty scan");
        f.Expect(false);
        const auto included = f.owner->Drain(3, true);
        Check(included.valid && included.selected == 1 && included.requested == 1 && state->postCalls == 1 && f.Quota() == 0,
            "the same Boss-threshold entity is killed by the actual ending includeBosses path");
        f.Complete();
    }
    for (bool throwing : {false, true})
    {
        Fixture f; auto a = f.State("cancelled"), b = f.State("replacement-request");
        auto& first = f.Monster(a); auto& second = f.Monster(b);
        Require(first.Slot() < second.Slot(), "cancellation occurs before the later candidate is scanned");
        Require(f.map.ScheduleMonsterDrain(20.0f, 0), "cancelled batch scheduled on the real map");
        const auto& team = f.map.MonsterDrainTeam();
        const auto generation = f.owner->Requests().Generation();
        f.services.expected = {"rules"};
        f.services.actions["rules"] = [&](auto&, auto& error) -> bool
        {
            Require(f.owner->Reset(), "nested callback resets the actual owner and request generation");
            Require(f.map.ScheduleMonsterDrain(31.0f, 1), "replacement drain state installed before old callback returns");
            error.message = "old cancelled round drain failure";
            if (throwing) throw std::runtime_error("old cancelled round drain exception");
            return false;
        };
        const auto cancelled = f.owner->Drain(3, true);
        Check(!cancelled.valid && cancelled.selected == 1 && cancelled.requested == 1 && !cancelled.error.message.empty(),
            "old batch reports cancellation after its first real request without pretending it completed");
        Check(f.owner->Requests().Generation() != generation && f.owner->Requests().Error().empty() &&
            f.owner->Error().message.empty() && f.map.MonsterDrainAt() == 31.0f && team == 1,
            "old callback return or exception does not poison the replacement owner dispatcher or drain fields");
        Check(a->postCalls == 0 && b->beforeCalls == 0 && first.pev->deadflag == DEAD_DEAD && second.pev->deadflag == DEAD_NO,
            "cancelled batch leaves its completed dead-state transition and never visits a later live candidate");
        f.Complete();
        f.services.actions.clear(); f.Expect(false);
        const auto replacement = f.owner->Drain(3, true);
        Check(replacement.valid && replacement.error.message.empty() && replacement.selected == 1 && replacement.requested == 1 &&
            b->postCalls == 1 && f.Quota() == 0 && f.owner->Error().message.empty() && f.owner->Requests().Error().empty(),
            "new-generation owner drain completes a real later death despite the cancelled batch's local error");
        f.Complete();
    }
}

// The real owner commits the manager counters; the root player-kill service
// remains an explicit observer, not a substitute for its ability/clearpoint tail.
void ManagerKills()
{
    const struct Case { TeamName initial, counted; bool changeInRules; } cases[]{
        {TERRORIST, TERRORIST, false}, {CT, CT, false}, {CT, TERRORIST, true}
    };
    for (const auto& test : cases)
    {
        Fixture f; auto state = f.State("team-count"); auto& monster = f.Monster(state, 2);
        auto& player = f.Client(1); f.engine.globals.maxClients = 1;
        const auto other = test.counted == TERRORIST ? CT : TERRORIST;
        player.m_iTeam = test.initial; player.pev->team = other;
        // The CT case deliberately kills a same-team monster. Native player
        // team, entity team and monster team are independent inputs here.
        monster.pev->team = CT;
        f.Expect(true);
        f.services.actions["rules"] = [&](auto& call, auto&)
        {
            Check(call.Current() == &monster && call.Attacker() == &player &&
                f.map.TeamMonsterKills(1) == 0 && f.map.TeamMonsterKills(2) == 0,
                "rules death observes counters before the current kill is committed");
            if (test.changeInRules) player.m_iTeam = test.counted;
            return true;
        };
        f.services.actions["player"] = [&](auto&, auto&)
        {
            Check(f.map.TeamMonsterKills(test.counted) == 1 && f.map.TeamMonsterKills(other) == 0 &&
                player.pev->team == other,
                "player-kill callback sees exactly one count from the current native team after rules death");
            player.m_iTeam = other;
            return true;
        };
        f.services.actions["golden"] = [&](auto&, auto&)
        {
            Check(player.m_iTeam == other && f.map.TeamMonsterKills(test.counted) == 1 &&
                f.map.TeamMonsterKills(other) == 0,
                "later player callback team change does not move the already committed count");
            return true;
        };
        CSOLMonsterDeathArguments arguments; arguments.attacker = &player;
        Check(f.owner->Requests().Request(monster, arguments) == Result::Executed && state->postCalls == 1,
            "real attributed death reaches the tail with the native-team count");
        const auto completedLog = f.log;
        Check(f.owner->Requests().Request(monster, arguments) == Result::Ignored && f.log == completedLog &&
            f.map.TeamMonsterKills(test.counted) == 1 && f.map.TeamMonsterKills(other) == 0,
            "repeated dead-lease request does not count a second kill under the changed team");
        f.Complete();
    }
    {
        Fixture f; auto state = f.State("environment"); auto& monster = f.Monster(state);
        f.Expect(false);
        const auto unchanged = [&](auto& call, auto&)
        {
            Check(!call.Attacker() && f.map.TeamMonsterKills(1) == 0 && f.map.TeamMonsterKills(2) == 0,
                "environmental rules and golden callbacks see no attributed team count");
            return true;
        };
        f.services.actions["rules"] = unchanged; f.services.actions["golden"] = unchanged;
        Check(f.owner->Requests().Request(monster, {}) == Result::Executed && state->postCalls == 1 &&
            f.map.TeamMonsterKills(1) == 0 && f.map.TeamMonsterKills(2) == 0,
            "null attacker completes the real death path without player-kill callback or team increment");
        f.Complete();
    }
    {
        Fixture f; auto firstState = f.State("outer"), nestedState = f.State("inner");
        auto& first = f.Monster(firstState); auto& nested = f.Monster(nestedState);
        auto& player = f.Client(1); f.engine.globals.maxClients = 1;
        player.m_iTeam = CT; player.pev->team = CT;
        CSOLMonsterDeathArguments arguments; arguments.attacker = &player;
        f.services.expected = {"extractor", "rules", "player", "extractor", "rules", "player", "golden", "golden"};
        f.services.actions["rules"] = [&](auto& call, auto&)
        {
            if (call.Current() == &first)
                Check(f.map.TeamMonsterKills(CT) == 0 && f.map.TeamMonsterKills(TERRORIST) == 0,
                    "outer rules callback precedes both real death increments");
            else
                Check(call.Current() == &nested && f.map.TeamMonsterKills(CT) == 1 &&
                    f.map.TeamMonsterKills(TERRORIST) == 0,
                    "nested rules callback observes the committed outer count but not its own");
            return true;
        };
        f.services.actions["player"] = [&](auto& call, auto&)
        {
            if (call.Current() == &first)
            {
                Check(f.map.TeamMonsterKills(CT) == 1 && f.map.TeamMonsterKills(TERRORIST) == 0,
                    "outer player-kill callback starts with exactly its own committed count");
                player.m_iTeam = TERRORIST;
                Check(f.owner->Requests().Request(nested, arguments) == Result::Executed,
                    "outer player callback executes a second real death with its changed native team");
                Check(f.map.TeamMonsterKills(CT) == 1 && f.map.TeamMonsterKills(TERRORIST) == 1,
                    "nested return retains both independently attributed counts");
            }
            else
            {
                Check(call.Current() == &nested && f.map.TeamMonsterKills(CT) == 1 &&
                    f.map.TeamMonsterKills(TERRORIST) == 1 && player.pev->team == CT,
                    "nested player callback sees the changed native team despite unchanged entity team");
                player.m_iTeam = SPECTATOR;
            }
            return true;
        };
        f.services.actions["golden"] = [&](auto&, auto&)
        {
            Check(f.map.TeamMonsterKills(CT) == 1 && f.map.TeamMonsterKills(TERRORIST) == 1,
                "both golden callbacks retain outer and nested counts after another team change");
            return true;
        };
        Check(f.owner->Requests().Request(first, arguments) == Result::Executed &&
            firstState->postCalls == 1 && nestedState->postCalls == 1 && f.Quota() == 0 &&
            player.m_iTeam == SPECTATOR && f.map.TeamMonsterKills(CT) == 1 &&
            f.map.TeamMonsterKills(TERRORIST) == 1,
            "nested attributed deaths each complete once and return their actual point quota");
        f.Complete();
    }
    {
        Fixture f(false, true);
        const auto* rule = f.map.DeathRules().Find(500);
        Require(rule && rule->drainMonsters && rule->stopWaves,
            "real map loader installs the saved type500 drain-and-stop rule");
        auto outerState = f.State("rule-source"), firstState = f.State("old-filter-first"),
            skippedState = f.State("old-filter-later"), changedState = f.State("new-filter"), bossState = f.State("boss");
        auto& outer = f.Monster(outerState, 2, 500); auto& first = f.Monster(firstState, 2);
        auto& skipped = f.Monster(skippedState, 2); auto& changed = f.Monster(changedState, 2);
        auto& boss = f.Monster(bossState, 2); f.types.back().skill = 1000;
        first.pev->team = 2; skipped.pev->team = 2; changed.pev->team = 1; boss.pev->team = 1;
        Require(outer.Slot() < first.Slot() && first.Slot() < skipped.Slot() &&
            skipped.Slot() < changed.Slot() && changed.Slot() < boss.Slot(),
            "real death-rule drain traverses the explicitly ordered pool candidates");
        Require(f.map.ScheduleMonsterDrain(20, 2), "death-table recursion starts with the map's team-two filter");
        const auto* filter = &f.map.MonsterDrainTeam();
        f.services.expected = {"rules", "rules", "golden", "rules", "golden", "golden"};
        f.services.actions["rules"] = [&](auto& call, auto&)
        {
            Check(!f.map.WaveDispatchStopped(), "death-table stop waits until every selected nested death completes");
            if (call.Current() == &first)
                Require(f.map.ScheduleMonsterDrain(21, 1), "nested rule death changes the same map-owned filter");
            return true;
        };
        f.services.actions["golden"] = [&](auto& call, auto&)
        {
            Check(f.map.WaveDispatchStopped() == (call.Current() == &outer),
                "only the outer golden callback observes the stop committed after the actual recursive drain");
            return true;
        };
        Check(f.owner->Requests().Request(outer, {}) == Result::Executed &&
            outerState->postCalls == 1 && firstState->postCalls == 1 && changedState->postCalls == 1 &&
            skippedState->beforeCalls == 0 && bossState->beforeCalls == 0,
            "real ApplyRule re-reads the changed filter and excludes the later old team and Boss skill1000");
        Check(skipped.pev->deadflag == DEAD_NO && boss.pev->deadflag == DEAD_NO &&
            changed.pev->deadflag == DEAD_DEAD && filter == &f.map.MonsterDrainTeam() &&
            *filter == 1 && f.map.MonsterDrainAt() == 21 && f.map.WaveDispatchStopped(),
            "recursive death table preserves shared filter identity and the nested schedule after stopping waves");
        Check(f.map.TeamMonsterKills(1) == 0 && f.map.TeamMonsterKills(2) == 0,
            "environmental death-table recursion does not manufacture player attribution");
        f.Complete();
    }
}

void FailuresAndCancellation()
{
    for (bool missingGlobals : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state);
        auto* globals = gpGlobals; const auto strings = globals->pStringBase;
        if (missingGlobals) gpGlobals = nullptr; else globals->pStringBase = nullptr;
        const bool unavailable = f.pool.At(monster.Slot()) == nullptr;
        const auto result = f.owner->Requests().Request(monster, {});
        gpGlobals = globals; globals->pStringBase = strings;
        Check(unavailable && result == Result::Ignored && state->beforeCalls == 0 && f.services.next == 0,
            "missing globals or string table refuses capture before classname access");
    }
    for (bool reset : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state); auto& player = f.Client(1);
        f.engine.globals.maxClients = 1; f.Expect(true); CSOLMonsterDeathArguments args; args.attacker = &player;
        f.services.actions["extractor"] = [&](auto&, auto&) -> bool
        { if (reset) Require(f.owner->Reset(), "reset before throwing service"); throw std::runtime_error("explicit service exception"); };
        Check(f.owner->Requests().Request(monster, args) == (reset ? Result::Ignored : Result::Failed), "exception mapped to failure or stale-session cancellation");
        Check(f.Quota() == 0 && state->postCalls == 0 && f.services.next == 1 && f.accountAmounts.empty(),
            "extractor exception stops after single quota before real modifier or account");
        if (!reset)
        {
            const auto before = f.log;
            Check(f.owner->Requests().Request(monster, args) == Result::Failed && f.log == before && f.Quota() == 0,
                "partial-death exception locks retries and prevents quota underflow");
        }
        else
        {
            Check(f.owner->Requests().Error().empty() && f.owner->Error().message.empty(), "old exception leaves replacement errors clear");
            f.services.actions.clear(); f.Expect(false); auto next = f.State("new-session"); auto& replacement = f.Monster(next);
            Check(f.owner->Requests().Request(replacement, {}) == Result::Executed, "new request runs after cancelled exception"); f.Complete();
        }
    }
    for (bool sound : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state); f.Expect(false);
        bool released{};
        const auto release = [&] { released = true; f.ReleaseMap(); };
        if (sound) f.onSound = release; else afterReset = [&](auto&) { release(); };
        Check(f.owner->Requests().Request(monster, {}) == Result::Ignored && released,
            "stages reject old session before EHANDLE read after actual storage release");
        Check(state->postCalls == 0 && f.services.next == 0 && f.owner->Requests().Error().empty(),
            "reset in ResetSequenceInfo or EmitSound stops later owner services cleanly");
    }
    for (const char* missing : {"rules", "golden"})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state); f.Expect(false);
        f.services.actions[missing] = [](auto&, auto& error) { error.message = "required service explicitly unavailable"; return false; };
        Check(f.owner->Requests().Request(monster, {}) == Result::Failed && state->postCalls == 0 &&
            f.owner->Error().message == "required service explicitly unavailable", "missing concrete service cannot become successful no-op");
    }
    for (bool cancelInDestructor : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state, 0, 40, 40); f.Expect(false);
        auto& source = f.engine.Client(1); auto& last = f.engine.Client(32);
        const auto firstEffect = f.Effect(monster, source), lastEffect = f.Effect(monster, last);
        const auto effects = monster.DamageEffects();
        auto& host = monster.HostState(); host.nonlethalDamage = 42; host.damageScaleCount = 9; host.damageScale = 3;
        bool cancelled{};
        const auto cancel = [&]
        {
            Check(host.nonlethalDamage == 0 && host.damageScaleCount == 0 && host.damageScale == 1,
                "effect cancellation occurs after real host modifier reset");
            cancelled = true; Check(f.owner->Reset(), "effect callback cancels actual death dispatcher");
            host.nonlethalDamage = 91; host.damageScaleCount = 13; host.damageScale = 8;
        };
        if (cancelInDestructor) firstEffect->onDestroy = cancel; else firstEffect->onNotify = cancel;
        int targets{}; auto& target = f.engine.AddTarget(500, "mm_zb_die_40"); target.action = [&] { ++targets; };
        Check(f.owner->Requests().Request(monster, {}) == Result::Ignored && cancelled,
            "real effect notification or destruction cancellation stops the complete death owner");
        Check(firstEffect->notified == 1 && firstEffect->destroyed == (cancelInDestructor ? 1 : 0) &&
            lastEffect->notified == 0 && lastEffect->destroyed == 0 && effects->At(32).count == 3 && effects->At(32).notify,
            "cancelled cleanup preserves the later source32 record and its callback ownership");
        Check(cancelInDestructor ? !effects->At(1).notify && effects->At(1).count == 0 :
            bool(effects->At(1).notify) && effects->At(1).count == 3,
            "notification cancellation precedes slot reset while destructor cancellation follows it");
        Check(f.services.next == 1 && targets == 0 && state->postCalls == 0 &&
            f.owner->Requests().Error().empty() && f.owner->Error().message.empty(),
            "cancelled actual effects skip targets Golden and post without poisoning replacement errors");
        Check(host.nonlethalDamage == 91 && host.damageScaleCount == 13 && host.damageScale == 8,
            "old owner tail does not reset replacement damage modifiers again");
    }
    for (bool reset : {false, true})
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state, 0, 40, 40); f.Expect(false);
        auto& source = f.engine.Client(1); auto& last = f.engine.Client(32);
        const auto firstEffect = f.Effect(monster, source), lastEffect = f.Effect(monster, last);
        firstEffect->onNotify = [&]
        {
            if (reset) Require(f.owner->Reset(), "reset before throwing actual effect notification");
            throw std::runtime_error("explicit source effect exception");
        };
        int targets{}; auto& target = f.engine.AddTarget(500, "mm_zb_die_40"); target.action = [&] { ++targets; };
        const auto result = f.owner->Requests().Request(monster, {});
        Check(result == (reset ? Result::Ignored : Result::Failed) && firstEffect->notified == 1 &&
            firstEffect->destroyed == 0 && lastEffect->notified == 0 && f.services.next == 1 && targets == 0 && state->postCalls == 0,
            "actual effect exception stops before later records targets Golden and post");
        if (reset)
            Check(f.owner->Requests().Error().empty() && f.owner->Error().message.empty(),
                "cancelled source notification exception leaves replacement error state clear");
        else
        {
            Check(f.owner->Requests().Error() == "scenario synchronous death execution threw an exception",
                "actual effect exception is retained by the production request dispatcher");
            const auto before = f.log;
            Check(f.owner->Requests().Request(monster, {}) == Result::Failed && f.log == before && firstEffect->notified == 1,
                "failed source cleanup locks retries without repeating notification or earlier quota effects");
        }
    }
    {
        BehaviorStats wrong; Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state);
        Require(monster.InstallBehavior(std::make_unique<BehaviorProbe>(wrong)), "install non-death behavior for explicit missing capability");
        Check(f.owner->Requests().Request(monster, {}) == Result::Failed && f.engine.packets.empty() && f.Quota() == 1,
            "missing concrete death behavior fails before notification and quota");
    }
    for (bool resetDestructor : {false, true})
    {
        Fixture f; auto a = f.State("A"), b = f.State("B"); auto& monster = f.Monster(a); f.Expect(false);
        a->before = [&](auto& call, auto&) { return call.Current()->InstallBehavior(std::make_unique<Behavior>(b)); };
        bool ran{}; a->onDestroy = [&] { ran = true; if (resetDestructor) f.owner->Reset(); else f.Reborrow(monster); };
        Check(f.owner->Requests().Request(monster, {}) == (resetDestructor ? Result::Ignored : Result::Failed) && ran,
            "destructor reset or same-slot replacement rechecked after retained behaviors release");
        Check(a->postCalls == 1, "destructor cancellation occurs after original post");
    }
    {
        Fixture f; auto state = f.State("A"); auto& monster = f.Monster(state, 0, 40, 40); f.Expect(false);
        auto& source = f.engine.Client(1); const auto effect = f.Effect(monster, source);
        auto& target = f.engine.AddTarget(500, "mm_zb_die_40"); target.action = [&] { f.Reborrow(monster); };
        Check(f.owner->Requests().Request(monster, {}) == Result::Failed && state->postCalls == 0,
            "target same-slot reborrow cancels old lease before death rule and Golden");
        Check(f.services.next == 1 && effect->notified == 1 && effect->destroyed == 1,
            "only rules service and actual effect cleanup precede target cancellation");
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try
    {
        const std::string_view mode = argv[1];
        if (mode == "--order")
        { owner_test::KillMoneyNumbers(); owner_test::KillMoneyConsumption(); owner_test::OrderAndReplacement(); }
        else if (mode == "--lifecycle") owner_test::EarlyParkAndReentry();
        else if (mode == "--round-drain") owner_test::RoundDrain();
        else if (mode == "--manager-kills") owner_test::ManagerKills();
        else if (mode == "--failures")
        { owner_test::KillMoneyCancellation(); owner_test::FailuresAndCancellation(); }
        else return 2;
    }
    catch (const std::exception& error) { ++death_events_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "death_owner checks=" << death_events_test::checks << " failures=" << death_events_test::failures << '\n';
    return death_events_test::failures ? 1 : 0;
}
