// SPDX-License-Identifier: GPL-3.0-or-later
// Preparation only: complete matched SDK objects, actual root/provider/Golden
// code, controlled engine and typed ability edges. Independent provider cases
// stop at the real clearpoint gate; registered rules also cover Combat assembly.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_death_services.h"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/scenario_drops.h"
#include "../../native/csol/server/scenario_monster_damage.h"
#include "../../native/csol/server/scenario_monster_factory.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/weapons.h"
#include "../../native/csol/server/characters.h"
#include "../csol-scenario-navigation-loader/support.hpp"
#include <rapidjson/document.h>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using csol::scenario::ResourceError;
using csol::scenario::GoldenConfiguration;
constexpr const char* GoldenPath = "maps/ZBS_GoldenZombie_dedi.json";

std::map<std::string, std::string> Resources()
{
    using difficulty_test::Encrypt;
    auto files = session_test::Resources();
    files["maps/zbs_map_session_test.cso"] = Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes\r\n1,1\r\n");
    files["maps/ZBS_ZT_map_session_test.cso"] = Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,7,1\r\n");
    files["maps/ZBS_DropItem.cso"] = Encrypt(
        "id,diff,effect,p,owner,count,remove,reward\r\n7,1,0,100,0,1,5,3000\r\n");
    std::string difficulty = "header\r\n";
    for (int i = 0; i < 64; ++i) { if (i) difficulty += ','; difficulty += '1'; }
    files["maps/ZBS_Difficulty_Default.cso"] = Encrypt(difficulty + "\r\n");
    std::string levels = "level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for (int i = 0; i <= 100; ++i) levels += std::to_string(i) + ",100,0,1,1,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"] = Encrypt(levels);
    files["resource/ZBSVER2/ZBS_class.cso"] = Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    files["resource/buff/BuffWeapon.cso"] = Encrypt("header\r\n");
    files["resource/FloatingDamage/FloatingDamageInfo.jsoo"] = Encrypt(
        R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})");
    files["maps/ZBS.json"] = "{}";
    files["resource/item.csv"] = ";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n"
        "1036,AK47,AK47,Equipment,2,28\r\n";
    files["resource/defaultitems.csv"] = "Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"] = Encrypt(
        "id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"] = Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    csol::scenario::NavigationFile nav;
    nav.areas = {nav_loader_test::Area(1, 0, 0, 0, 4096)};
    files["maps/map_session_test.nav"] = nav_loader_test::Encode(nav);
    return files;
}

void Markers(Engine& engine)
{
    for (const char* name : {"playerspawn", "zombiespawn", "zbstrigger"})
        for (int id : {1, 2, 30})
        {
            if (std::strcmp(name, "zbstrigger") && id != 1) continue;
            auto* ed = CREATE_NAMED_ENTITY(Engine::String(name));
            if (auto* p = dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 2; }
            if (auto* p = dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 1; p->m_iMaxSpawn = 5; }
            if (auto* p = dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))) { p->m_iId = id; ed->v.model = Engine::String("*1"); }
            Require(engine.api.pfnSpawn(ed) == 0, "provider fixture uses real marker DispatchSpawn");
            ed->v.mins = Vector(-8, -8, -8); ed->v.maxs = Vector(8, 8, 8);
        }
}

struct Waves final : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override { throw std::runtime_error("no ready combat rules in this fixture"); }
};
struct UnusedExecutor final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&, const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("narrow provider cases cannot substitute a complete death owner"); }
};

struct Fixture
{
    Engine& engine;
    std::shared_ptr<CCSOLScenarioRuntime> runtime = std::make_shared<CCSOLScenarioRuntime>();
    std::shared_ptr<CCSOLScenarioSession> session = runtime->session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<CCSOLScenarioDrops> drops;
    std::shared_ptr<CCSOLScenarioDeathServices> deaths;
    UnusedExecutor executor;
    CCSOLMonsterDeathRequests requests{executor};
    Waves waves;
    CBasePlayer* player{};
    CCSOLScenarioMonster* monster{};
    explicit Fixture(Engine& e) : engine(e)
    {
        e.Clear(); e.files = Resources(); e.globals.time = 10; e.Prepare("0");
        g_pGameRules = InstallGameRules(); Require(g_pGameRules && !CSOL_RulesRejected(), "native rules host an independent real Runtime");
        e.onTrace = [](edict_t*, TraceResult& trace) { trace.flFraction = 0.5f; trace.vecEndPos = Vector(0, 0, 0); };
        Require(session->Precache("map_session_test", 1, CSOLScenarioResourceScope::Combat), "real Combat resources prepare component cases");
        deaths = runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(runtime);
        ResourceError error;
        Require(deaths->Precache({Engine::Load, Engine::Free}, error), "concrete provider reads actual missing Golden resource");
        Check(!deaths->Players(), "provider resolves Players after activation rather than capturing constructor null");
        Markers(e); Require(session->Activate(), "actual Session activates authored resources");
        players = runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(), "real player authority binds equipment and upgrade services");
        drops = runtime->drops = std::make_shared<CCSOLScenarioDrops>(session, players);
        Require(drops->Activate(), "real Drops owner activates without creating rewards");
        player = Player(1);
        Require(session->BeginRound(0, e.globals.time, 0, waves), "actual map enters round with a valid death lease");
        const auto* type = session->Map().Tables()->monsterTypes.Find(1);
        Require(type != nullptr, "actual table provides ordinary type");
        monster = session->Monsters().Acquire(*type, 1, session->Map().SpawnPoints().Points());
        Require(monster != nullptr, "actual monster pool provides a leased entity");
    }
    CBasePlayer* Player(int index)
    {
        auto* ed = engine.Reserve(index, "player");
        auto* result = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags = FL_CLIENT; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
        result->m_iTeam = CT; result->m_iNumSpawns = 1; result->has_disconnected = false;
        Require(players->SetProfile(*result, {}), "actual player state has local empty loadout");
        return result;
    }
    template<class T, class Wrapper = CCSEntity> T* Entity()
    {
        auto* ed = engine.Reserve(engine.nextEntity++, "death_service_probe");
        return GetClassPtr<Wrapper>(reinterpret_cast<T*>(&ed->v));
    }
    template<class T> T* Weapon(int id)
    {
        auto* weapon = Entity<T, CCSPlayerWeapon>(); weapon->m_iId = id; weapon->m_pPlayer = player; return weapon;
    }
    CSOLMonsterDeathCall Call(CBaseEntity* source = nullptr, bool withAttacker = true)
    {
        CSOLMonsterDeathArguments args; args.attacker = withAttacker ? player : nullptr; args.inflictor = source;
        return CSOLMonsterDeathCall(session->Map(), requests, *monster, args);
    }
    std::uint32_t Kills(int item) const
    {
        const auto state = players->Find(EHANDLE(player));
        if (!state) return 0;
        const auto found = state->weaponKills.find(item);
        return found == state->weaponKills.end() ? 0 : found->second;
    }
    ~Fixture()
    {
        engine.onOffset = {}; engine.onIndexLookup = {}; engine.onFind = {}; engine.onTrace = {};
        engine.onModel = {}; engine.onOrigin = {}; engine.onSize = {}; engine.onSound = {};
        drops->Deactivate(); players->Invalidate(); Check(requests.Reset(), "provider fixture retires death request identity");
        if (!session->Map().EntityAccessAllowed())
            for (auto& ed : engine.edicts) if (ed.pvPrivateData) Engine::FreePrivate(&ed);
        Check(session->Deactivate(), "provider fixture releases real Session resources");
        engine.Clear();
    }
};

struct SkillState
{
    int type{10}, typeCalls{}, readyCalls{}, activates{}, destroyed{};
    bool ready{true}, readySuccess{true}, activateSuccess{true};
    std::string failure;
    std::function<void()> onType, onReady, onActivate, onDestroy;
};
struct SkillProbe final : CSOLScenarioActiveSkill
{
    std::shared_ptr<SkillState> state;
    explicit SkillProbe(std::shared_ptr<SkillState> value) : state(std::move(value)) {}
    int Type() const override
    { const auto s = state; ++s->typeCalls; const auto action = s->onType; if (action) action(); return s->type; }
    bool Ready(CSOLMonsterDeathCall& call, bool& ready, ConfigError& error) override
    {
        const auto s = state; ++s->readyCalls; Require(call.Current() != nullptr, "typed readiness receives current death call");
        const auto action = s->onReady; if (action) action(); ready = s->ready;
        if (!s->readySuccess && !s->failure.empty()) error.message = s->failure;
        return s->readySuccess;
    }
    bool Activate(CSOLMonsterDeathCall& call, ConfigError&) override
    {
        const auto s = state; ++s->activates; Require(call.Current() != nullptr, "typed activation receives current death call");
        const auto action = s->onActivate; if (action) action(); return s->activateSuccess;
    }
    ~SkillProbe() override
    {
        ++state->destroyed;
        try { const auto action = state->onDestroy; if (action) action(); }
        catch (...) { Check(false, "ability destructor callback must not throw"); }
    }
};
void Bind(Fixture& f, const std::shared_ptr<SkillState>& state)
{ Require(f.players->SetActiveSkill(EHANDLE(f.player), std::make_shared<SkillProbe>(state)), "bind explicit typed ability through production registry"); }

struct SourceProbe final : CBaseEntity, CSOLWeaponDamageSource
{
    // Use external callback state: GoldSrc explicitly releases private data.
    struct State { int value{29}, reads{}, playerQueries{}; std::function<void()> onRead; };
    State* state{};
    BOOL IsPlayer() override { ++state->playerQueries; return TRUE; }
    int DamageWeaponId() const override
    { auto& s = *state; ++s.reads; const int value = s.value; const auto action = s.onRead; if (action) action(); return value; }
};

void RootSourceCases(Engine& engine)
{
    for (int kind = 0; kind != 7; ++kind)
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>(); Bind(f, ability);
        auto* knife = f.Weapon<CKnife>(WEAPON_KNIFE); f.player->m_pActiveItem = knife;
        SourceProbe::State sourceState; CBaseEntity* source{};
        if (kind == 1 || kind == 2) source = f.player;
        if (kind == 2) f.player->m_pActiveItem = nullptr;
        if (kind == 3) source = f.Player(2); // Different player does not borrow attacker active ID.
        if (kind == 4) { auto* probe = f.Entity<SourceProbe>(); probe->state = &sourceState; source = probe; }
        if (kind == 5) source = knife;
        if (kind == 6) source = f.Entity<CBaseEntity>();
        auto call = f.Call(source); ConfigError error;
        const bool success = f.players->RootMonsterKilled(call, error);
        const bool selected = kind == 1 || kind == 4 || kind == 5;
        Check(success == (kind != 6) && ability->activates == static_cast<int>(selected),
            "root uses entity-index equality, then direct source adapter, never attribution IsPlayer fallback");
        Check(sourceState.playerQueries == 0 && sourceState.reads == (kind == 4 ? 1 : 0),
            "root source is one direct getter without IsPlayer or double-read attribution semantics");
        Check(f.Kills(161) == (kind == 2 || kind == 6 ? 0u : 1u), "root still records active weapon when ability branch is absent");
        if (kind == 6) Check(!error.message.empty(), "unknown source fails explicitly before current weapon statistics");
    }
    {
        Fixture f(engine); CSOLScenarioPlayerProfile profile; profile.characterFeature = 10;
        Require(f.players->SetProfile(*f.player, std::move(profile)), "trusted profile stores feature byte separately");
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        auto call = f.Call(f.player); ConfigError error;
        Check(f.players->RootMonsterKilled(call, error) && !f.players->Find(EHANDLE(f.player))->activeSkill && f.Kills(161) == 1,
            "feature10 cannot fabricate an active skill and does not suppress weapon statistics");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>(); Bind(f, ability);
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        auto* source = f.Weapon<CKnife>(WEAPON_KNIFE); source->pev->flags |= FL_DORMANT;
        auto call = f.Call(source); ConfigError error;
        Check(f.players->RootMonsterKilled(call, error) && !ability->typeCalls && f.Kills(161) == 1,
            "ineligible source skips ability but preserves current weapon statistics");
        f.player->pev->flags |= FL_DORMANT;
        Check(f.players->RootMonsterKilled(call, error) && f.Kills(161) == 1, "ineligible attacker skips the whole root phase");
    }
}

void RootSkillCases(Engine& engine)
{
    for (int mode = 0; mode != 5; ++mode)
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        if (mode == 0) ability->type = 9;
        if (mode == 1) ability->ready = false;
        if (mode == 2) ability->readySuccess = false;
        if (mode == 3) ability->activateSuccess = false;
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        auto* changed = f.Weapon<CAK47>(WEAPON_AK47);
        if (mode == 4) ability->onActivate = [&] { f.player->m_pActiveItem = changed; };
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        const bool succeeded = f.players->RootMonsterKilled(call, error);
        Check(succeeded == (mode != 2 && mode != 3), "typed readiness and activation failures stop root continuation");
        Check(ability->typeCalls == 1 && ability->readyCalls == (mode ? 1 : 0) && ability->activates == (mode >= 3 ? 1 : 0),
            "root only invokes type10 readiness and activates only when actually ready");
        Check(f.Kills(161) == (mode <= 1 ? 1u : 0u) && f.Kills(1036) == (mode == 4 ? 1u : 0u),
            "current weapon catalog is resolved after successful ability callbacks");
    }
    for (int stage = 0; stage != 3; ++stage)
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        const auto old = f.players->Find(EHANDLE(f.player))->generation;
        auto replace = [&]
        {
            f.players->Remove(EHANDLE(f.player));
            Require(f.players->SetProfile(*f.player, {}), "callback registers a new generation on the same real player");
        };
        if (stage == 0) ability->onType = replace;
        if (stage == 1) ability->onReady = replace;
        if (stage == 2) ability->onActivate = replace;
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(!f.players->RootMonsterKilled(call, error) && f.players->Find(EHANDLE(f.player))->generation > old && !f.Kills(161),
            "each typed callback detects player replacement and leaves the new generation untouched");
    }
    for (int stage = 0; stage != 2; ++stage)
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>(); auto replacement = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        auto replace = [&] { Bind(f, replacement); };
        if (stage == 0) ability->onType = replace; else ability->onReady = replace;
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(!f.players->RootMonsterKilled(call, error) && ability->destroyed == 1 && !replacement->typeCalls && !f.Kills(161),
            "ability identity change during Type or Ready cancels without entering the replacement");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        ability->onActivate = [&] { Require(f.players->SetActiveSkill(EHANDLE(f.player), {}), "ability can retire itself during activation"); };
        ability->onDestroy = [&] { f.players->Invalidate(); };
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(!f.players->RootMonsterKilled(call, error) && ability->destroyed == 1 && !f.players->Active(),
            "release of the pinned self-retired skill rechecks cancellation before root statistics");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>(); auto replacement = std::make_shared<SkillState>();
        ability->onDestroy = [&] { Bind(f, replacement); }; Bind(f, ability);
        Check(!f.players->SetActiveSkill(EHANDLE(f.player), {}) && ability->destroyed == 1 &&
            f.players->Find(EHANDLE(f.player))->activeSkill != nullptr,
            "SetActiveSkill detects destructor reentry without wiping the newly bound ability");
    }
    {
        Fixture f(engine); SourceProbe::State state; auto* source = f.Entity<SourceProbe>(); source->state = &state;
        const EHANDLE sourceHandle(source); auto* sourceEdict = source->edict();
        state.onRead = [&] { Engine::Remove(sourceEdict); };
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        auto call = f.Call(source); ConfigError error;
        Check(!f.players->RootMonsterKilled(call, error) && !sourceHandle.Get<CBaseEntity>() && !f.Kills(161),
            "root direct source callback deletion cancels before touching its removed entity");
    }
}

void ProviderMethods(Engine& engine)
{
    Fixture f(engine); auto call = f.Call(); ConfigError error; bool enabled = true;
    Check(f.deaths->Players() == f.session->Players() && f.deaths->Players() != nullptr,
        "concrete provider dynamically resolves the activated real player registry");
    Check(f.deaths->RulesDeath(call, error) && f.deaths->GoldenDeath(call, error) &&
        f.deaths->Golden().State() == GoldenConfiguration::Missing, "verified rules no-op and actual missing Golden plan accept a current call");
    Check(f.deaths->MoneyExtractor(call, EHANDLE(f.player), enabled, error) && !enabled,
        "empty authoritative profile does not invent the extractor item");
    CSOLScenarioPlayerProfile profile; profile.moneyExtractor = true;
    Require(f.players->SetProfile(*f.player, profile), "trusted profile installs extractor flag");
    Check(f.deaths->MoneyExtractor(call, EHANDLE(f.player), enabled, error) && enabled,
        "provider reads actual profile flag independently of character feature");
    int option = -99; error = {};
    Check(!f.deaths->RoomOption(call, option, error) && option == -99 && !error.message.empty(),
        "unconfigured optional room value fails rather than inventing zero");
    for (int value : {0, 800})
    {
        f.runtime->room.moneyOption = value; error = {};
        Check(f.deaths->RoomOption(call, option, error) && option == value, "explicit room zero and 800 are distinct from absent state");
    }
    auto* other = f.Player(2); error = {};
    Check(!f.deaths->MoneyExtractor(call, EHANDLE(other), enabled, error) && !enabled && !error.message.empty(),
        "extractor request cannot use another registered player's state");
    auto environment = f.Call(nullptr, false); error = {};
    Check(f.deaths->RulesDeath(environment, error) && f.deaths->GoldenDeath(environment, error),
        "environmental death still reaches verified rules and empty Golden services");
    Check(!f.deaths->PlayerKilledMonster(environment, error), "player kill method rejects environmental misuse");
    f.runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(f.runtime); error = {};
    Check(!f.deaths->Players() && !f.deaths->RulesDeath(call, error) && !f.deaths->GoldenDeath(call, error),
        "retained old provider cannot consume a replacement Runtime owner");
    f.runtime->deathServices = f.deaths;
    Check(f.deaths->RulesDeath(call, error), "restoring the same loaded owner restores its matching identity");
    Require(f.requests.Reset(), "actual death request generation reset");
    Check(!f.deaths->RulesDeath(call, error) && !f.deaths->GoldenDeath(call, error), "old request generation cannot use provider's valid resource state");
}

void ProviderGateAndLifecycle(Engine& engine)
{
    for (int boundary = 0; boundary != 3; ++boundary)
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        const auto previous = f.players->Find(EHANDLE(f.player))->generation;
        const auto replace = [&]
        {
            f.players->Remove(EHANDLE(f.player));
            Require(f.players->SetProfile(*f.player, {}), "provider callback re-registers a fresh same-entity player");
        };
        if (boundary == 0) ability->onType = replace;
        if (boundary == 1) ability->onReady = replace;
        if (boundary == 2) ability->onActivate = replace;
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(f.deaths->PlayerKilledMonster(call, error) && call.Current() == f.monster &&
            !call.Attacker() && error.message.empty() && f.requests.Error().empty(),
            "provider revokes only a replaced attacker and preserves the monster transaction without poisoning its request owner");
        Check(f.players->Find(EHANDLE(f.player))->generation > previous && !f.Kills(161) &&
            f.deaths->Records().empty() && f.players->KillPositions().empty(),
            "cancelled attacker work is neither resumed nor credited to the fresh player generation");
        Check(f.deaths->GoldenDeath(call, error), "actual empty Golden service remains reachable for the preserved monster tail");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        ability->onReady = [&] { f.runtime->RequestClose(); }; Bind(f, ability);
        auto call = f.Call(f.player); ConfigError error;
        Check(!f.deaths->PlayerKilledMonster(call, error) && !call.Current(),
            "actual Runtime retirement is not swallowed as mere attacker cancellation");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        ability->onReady = [&] { f.players->Remove(EHANDLE(f.player)); f.drops->Deactivate(); };
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(!f.deaths->PlayerKilledMonster(call, error) && call.Current() && call.Attacker() == f.player && !f.drops->Active(),
            "simultaneous player removal and real Drops shutdown is an owner failure rather than successful attacker cancellation");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>(); ability->readySuccess = false;
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE); Bind(f, ability);
        auto call = f.Call(f.player); ConfigError error;
        Check(!f.deaths->PlayerKilledMonster(call, error) && call.Attacker() == f.player && call.Current(),
            "actual ability failure with an unchanged player is not converted into recipient cancellation");
    }
    {
        Fixture f(engine); auto ability = std::make_shared<SkillState>();
        ability->readySuccess = false; ability->failure = "explicit ability provider failure";
        f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        ability->onReady = [&]
        {
            f.players->Remove(EHANDLE(f.player));
            Require(f.players->SetProfile(*f.player, {}), "failed provider also changes player generation");
        };
        Bind(f, ability); auto call = f.Call(f.player); ConfigError error;
        Check(!f.deaths->PlayerKilledMonster(call, error) && error.message == ability->failure && call.Attacker() == f.player,
            "a real provider diagnostic is not swallowed merely because its callback also replaced the player");
    }
    {
        Fixture f(engine); auto call = f.Call(f.player); ConfigError error;
        f.players->Remove(EHANDLE(f.player));
        Check(!f.deaths->PlayerKilledMonster(call, error) && call.Attacker() == f.player,
            "missing player state at entry is not treated as a later recipient cancellation");
    }
    {
        Fixture f(engine); f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
        f.player->pev->frags = 9; f.runtime->ctCounts = {1, 1, true};
        auto call = f.Call(f.player); ConfigError error;
        const auto firstEvent = engine.events.size();
        Check(!f.deaths->PlayerKilledMonster(call, error), "independent Runtime cannot bypass the actual registered clearpoint Ready gate");
        Check(f.Kills(161) == 1 && f.player->pev->frags == 9 && f.deaths->Records().empty() &&
            f.players->KillPositions().empty() && !f.drops->RegisteredCount() && f.drops->Rewards().empty(),
            "failed clearpoint retains the real root prefix but performs no frags, statistics, drop, classification or Notify tail");
        Check(std::none_of(engine.events.begin() + static_cast<std::ptrdiff_t>(firstEvent), engine.events.end(),
            [](const auto& event) { return event.starts_with("message:"); }), "no later death packet is manufactured after completion rejection");
        Check(!f.deaths->Acknowledge(0) && !f.deaths->Acknowledge(1), "empty journal cannot acknowledge an invented saved prefix");
    }
    {
        Fixture f(engine); auto call = f.Call(); ConfigError error;
        const auto generation = f.runtime->Generation(); f.runtime->RequestClose();
        Check(f.runtime->Closing() && f.runtime->Generation() > generation && !call.Current() &&
            !f.deaths->Players() && !f.deaths->RulesDeath(call, error) && !f.deaths->GoldenDeath(call, error),
            "Runtime close revokes both map access and all narrow death service calls immediately");
        Check(!f.runtime->TryFinalize(), "real registered pool entities keep finalization pending until engine free");
    }
    {
        Fixture f(engine); auto call = f.Call(); ConfigError error;
        f.runtime.reset();
        Check(call.Current() && !f.deaths->Players() && !f.deaths->RulesDeath(call, error),
            "provider's weak Runtime does not prolong or fabricate an expired authority");
    }
    {
        Fixture f(engine); CCSOLScenarioMap foreign;
        Require(foreign.BeginMap("map_session_test"), "foreign actual Map reads authored input");
        Markers(engine); Require(foreign.CollectEntities(), "foreign actual Map collects markers");
        Require(foreign.BeginRound(0, engine.globals.time, 0, f.waves), "foreign actual Map owns a live round");
        CSOLMonsterDeathArguments args; args.attacker = f.player;
        CSOLMonsterDeathCall call(foreign, f.requests, *f.monster, args); ConfigError error;
        Require(call.Current() != nullptr, "foreign call remains independently current");
        Check(!f.deaths->RulesDeath(call, error) && !f.deaths->GoldenDeath(call, error) &&
            !f.players->RootMonsterKilled(call, error), "valid foreign Map cannot consume this Runtime's provider or root state");
        Check(foreign.EndMap(), "foreign map retires without replacing primary resources");
    }
}

struct GoldenFile
{
    inline static GoldenFile* current{};
    std::optional<std::string> content;
    std::optional<int> reportedLength;
    int loads{}, releases{};
    std::unique_ptr<unsigned char[]> allocation;
    std::function<void()> onLoad, onRelease;
    explicit GoldenFile(std::optional<std::string> bytes) : content(std::move(bytes))
    { Require(!current, "one controlled Golden file boundary"); current = this; }
    ~GoldenFile() { Check(!allocation, "every returned non-null Golden allocation was released"); current = nullptr; }
    static unsigned char* Load(const char* path, int* length)
    {
        auto& f = *current; ++f.loads; Require(std::strcmp(path, GoldenPath) == 0, "real loader requests exact dedicated Golden path");
        *length = f.reportedLength.value_or(f.content ? static_cast<int>(f.content->size()) : 0);
        if (f.content)
        {
            Require(!f.allocation, "no recursive or duplicate Golden allocation");
            f.allocation = std::make_unique<unsigned char[]>(f.content->size() + 1);
            std::memcpy(f.allocation.get(), f.content->c_str(), f.content->size() + 1);
        }
        const auto action = f.onLoad; if (action) action();
        return f.allocation.get();
    }
    static void Release(void* pointer)
    {
        auto& f = *current; Require(pointer && pointer == f.allocation.get(), "release only the actual non-null file allocation");
        ++f.releases; f.allocation.reset(); const auto action = f.onRelease; if (action) action();
    }
    static csol::scenario::ResourceFileApi Api() { return {Load, Release}; }
};

void GoldenCases()
{
    struct Row { const char* bytes; GoldenConfiguration state; bool accepted; };
    const Row rows[] = {
        {nullptr, GoldenConfiguration::Missing, true}, {"", GoldenConfiguration::Empty, true},
        {"null", GoldenConfiguration::Empty, true}, {"{}", GoldenConfiguration::Empty, true},
        {R"({"other":1})", GoldenConfiguration::Empty, true},
        {R"({"ZBS_GoldenZombie":{}})", GoldenConfiguration::Empty, true},
        {R"({"ZBS_GoldenZombie":{"difficulty":2}})", GoldenConfiguration::Empty, true},
        {"{", GoldenConfiguration::ParseError, true},
        {R"({"ZBS_GoldenZombie":{"monster_info":[{"id":1}]}})", GoldenConfiguration::Unsupported, false},
        {R"({"ZBS_GoldenZombie":[]})", GoldenConfiguration::Unsupported, false}
    };
    for (const auto& row : rows)
    {
        GoldenFile file(row.bytes ? std::optional<std::string>(row.bytes) : std::nullopt);
        csol::scenario::GoldenResources golden; ResourceError error;
        Check(golden.Load(GoldenFile::Api(), error) == row.accepted && golden.State() == row.state &&
            golden.EmptyPlan() == row.accepted, "actual Golden loader distinguishes missing, empty, parse error and unsupported configured plans");
        Check(file.loads == 1 && file.releases == (row.bytes ? 1 : 0), "null allocation is not released and even zero-length allocation is released once");
        if (row.state == GoldenConfiguration::ParseError) Check(!golden.Diagnostic().empty(), "parse error stays distinguishable from successful empty parse");
        if (!row.accepted) Check(!error.detail.message.empty(), "nonempty unimplemented Golden plan is explicit failure");
        ResourceError repeated;
        Check(!golden.Load(GoldenFile::Api(), repeated) && file.loads == 1 && golden.State() == row.state,
            "fresh-only Golden owner cannot silently reload even after missing or parse-error input");
    }
    for (bool duringRelease : {false, true})
    {
        GoldenFile file(std::string("{}")); csol::scenario::GoldenResources golden; ResourceError error;
        bool nestedAccepted = true;
        auto nested = [&] { ResourceError nestedError; nestedAccepted = golden.Load(GoldenFile::Api(), nestedError); Check(!nestedError.detail.message.empty(), "reentry explains fresh loading rejection"); };
        if (duringRelease) file.onRelease = nested; else file.onLoad = nested;
        Check(golden.Load(GoldenFile::Api(), error) && !nestedAccepted && golden.EmptyPlan() && file.loads == 1 && file.releases == 1,
            "engine load and release reentry cannot reset or partially replace the in-progress Golden owner");
    }
    {
        GoldenFile file(std::string("{}")); file.reportedLength = -1;
        csol::scenario::GoldenResources golden; ResourceError error;
        Check(!golden.Load(GoldenFile::Api(), error) && !golden.EmptyPlan() && file.releases == 1 && !error.detail.message.empty(),
            "invalid engine length still releases its actual allocation without inventing an empty plan");
    }
}

void ProviderPrecacheCancellation(Engine& engine)
{
    for (bool replace : {false, true})
    {
        engine.Clear(); engine.files = Resources(); engine.Prepare("0"); g_pGameRules = InstallGameRules();
        auto runtime = std::make_shared<CCSOLScenarioRuntime>();
        Require(runtime->session->Precache("map_session_test", 1, CSOLScenarioResourceScope::Combat), "actual precached Combat session owns provider resource load");
        auto deaths = runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(runtime);
        GoldenFile file(std::string("{}")); ResourceError error;
        file.onRelease = [&]
        {
            if (replace) runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(runtime);
            else runtime->RequestClose();
        };
        Check(!deaths->Precache(GoldenFile::Api(), error) && file.loads == 1 && file.releases == 1,
            "provider Precache detects Runtime close or service replacement from actual file release");
        Check(!deaths->Players(), "cancelled provider cannot expose a different owner's player registry");
        if (!runtime->session->Map().EntityAccessAllowed())
            for (auto& ed : engine.edicts) if (ed.pvPrivateData) Engine::FreePrivate(&ed);
        Check(runtime->session->Deactivate(), "cancelled precache releases owned resources"); engine.Clear();
    }
}

// The archive tests use actual OS files only when this SDK suite is executed.
// Keep them below the checkout's build directory, independently owned, and
// reject reparse points before either creating or removing test contents.
struct ArchiveDirectory
{
    std::filesystem::path path, parent;
    static bool Plain(const std::filesystem::path& value)
    {
        const auto attributes = GetFileAttributesW(value.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    }
    ArchiveDirectory()
    {
        auto root = std::filesystem::absolute(__FILE__).parent_path();
        while (!root.empty() && !std::filesystem::is_regular_file(root / "AGENTS.md"))
        {
            const auto next = root.parent_path();
            if (next == root) { root.clear(); break; }
            root = next;
        }
        Require(!root.empty() && std::filesystem::is_regular_file(root / "native/csol/server/scenario_runtime.h"),
            "archive tests must resolve their own workspace source path");
        auto part = root.root_path();
        for (const auto& component : root.relative_path())
        {
            part /= component;
            Require(Plain(part), "archive test workspace has no reparse ancestor");
        }
        parent = root / "build" / "csol-round-finalization-20261029";
        Require(Plain(root / "build"), "archive test build directory is independent");
        if (!std::filesystem::exists(parent)) std::filesystem::create_directory(parent);
        Require(Plain(parent) && std::filesystem::is_directory(parent), "archive test evidence parent is a plain directory");
        static unsigned sequence{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 32 && path.empty(); ++attempt)
        {
            const auto candidate = parent / ("io-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(stamp) + "-" + std::to_string(++sequence));
            if (std::filesystem::create_directory(candidate)) path = candidate;
        }
        Require(!path.empty(), "archive test owns a newly created directory");
    }
    ~ArchiveDirectory()
    {
        if (path.empty()) return;
        try
        {
            bool safe = path.parent_path() == parent && Plain(parent) && Plain(path);
            auto ancestor = path.root_path();
            for (const auto& component : path.relative_path())
            {
                ancestor /= component;
                if (!Plain(ancestor)) { safe = false; break; }
            }
            if (!safe) { Check(false, "archive cleanup refuses a replaced ancestor"); return; }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
            {
                if (!Plain(entry.path())) { safe = false; break; }
            }
            Check(safe, "archive test cleanup refuses unexpected reparse paths");
            if (safe) std::filesystem::remove_all(path);
        }
        catch (...) { Check(false, "archive test directory cleanup failed"); }
    }
};

std::string ReadArchiveBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(static_cast<bool>(input), "committed archive is readable through real OS I/O");
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Require(!input.bad(), "archive read completes without I/O error");
    return bytes;
}

std::vector<rapidjson::Document> ReadArchiveRows(const std::string& bytes)
{
    Require(!bytes.empty() && bytes.back() == '\n', "archive contains complete newline-delimited JSON records");
    std::vector<rapidjson::Document> rows;
    std::istringstream input(bytes);
    std::string line;
    while (std::getline(input, line))
    {
        rapidjson::Document value;
        value.Parse(line.data(), line.size());
        Require(!value.HasParseError() && value.IsObject(), "every actual archive line parses as one JSON object");
        rows.push_back(std::move(value));
    }
    return rows;
}

const rapidjson::Value& ArchiveRow(const std::vector<rapidjson::Document>& rows, const char* channel)
{
    const rapidjson::Value* found{};
    for (const auto& row : rows)
    {
        if (!row.HasMember("channel") || !row["channel"].IsString() ||
            std::strcmp(row["channel"].GetString(), channel)) continue;
        Require(!found, "authored archive fixture has exactly one row in this channel");
        found = &row;
    }
    Require(found != nullptr, "committed archive includes the expected channel");
    return *found;
}

// Restore every engine ABI pointer and globals even on an assertion/exception.
// Unsupported<> has exact matched signatures and throws on any attempted use.
struct NoArchiveEngineCalls
{
    enginefuncs_t saved{g_engfuncs};
    globalvars_t* globals{gpGlobals};
    void* edicts{};
    std::size_t length{};
    DWORD protection{};
    NoArchiveEngineCalls()
    {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        const auto first = reinterpret_cast<std::uintptr_t>(Engine::self->edicts.data());
        const auto last = first + sizeof(Engine::self->edicts);
        // Protect only pages wholly within the edict array. Its first/last
        // partial pages may contain another Engine member or heap metadata.
        const auto begin = (first + info.dwPageSize - 1) / info.dwPageSize * info.dwPageSize;
        const auto end = last - last % info.dwPageSize;
        Require(begin < end, "fixture owns at least one complete interior edict page");
        const auto here = reinterpret_cast<std::uintptr_t>(this);
        Require(here + sizeof(*this) <= begin || here >= end, "archive guard does not share an edict page");
        edicts = reinterpret_cast<void*>(begin); length = end - begin;
        Require(VirtualProtect(edicts, length, PAGE_NOACCESS, &protection) != FALSE,
            "complete interior edict pages become inaccessible during shutdown serialization");
#include "../csol-scenario-rules-session/engine_edge_defaults.inc"
        gpGlobals = nullptr;
    }
    ~NoArchiveEngineCalls()
    {
        DWORD ignored{};
        if (!VirtualProtect(edicts, length, protection, &ignored)) std::abort();
        g_engfuncs = saved; gpGlobals = globals;
    }
};

std::uint64_t RecordArchiveKill(Fixture& f)
{
    f.player->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
    auto call = f.Call(f.player); ConfigError error;
    Require(f.players->RootMonsterKilled(call, error), "real root records an active knife kill before shutdown");
    Require(f.Kills(161) == 1, "authored player owns exactly one recorded knife kill");
    return f.players->Find(EHANDLE(f.player))->generation;
}

void RecordArchiveChannels(Fixture& f)
{
    RecordArchiveKill(f);
    auto call = f.Call(f.player); ConfigError error;
    f.player->pev->origin = Vector(1.5f, -0.0f, -2.25f);
    Require(f.players->NotifyMonsterKill(call, WEAPON_KNIFE, error), "real weapon tail appends a kill position");
    csol::scenario::AnnounceCandidates candidates;
    Require(f.players->ClassifyKill(call, 1, candidates, error), "real ordinary-kill classifier records its counters");
    f.monster->pev->health = 100; f.monster->pev->deadflag = DEAD_NO;
    f.monster->pev->takedamage = DAMAGE_YES; f.monster->pev->effects &= ~EF_NODRAW;
    bool active = true;
    CSOLMonsterSpawnCall spawn(f.session->Map(), f.session->Monsters(), f.requests, *f.monster);
    CSOLMonsterDamageCall hit(spawn, EHANDLE(f.player), EHANDLE(f.player), 12.5f, DMG_BULLET, {}, active,
        [](const void* owner) { return *static_cast<const bool*>(owner); }, &active);
    Require(hit.Current() == f.monster, "archive statistics use a real leased monster damage call");
    Require(f.players->RecordWeaponDamage(hit, WEAPON_KNIFE, f.runtime, error) &&
        f.players->RecordMonsterHeadHit(hit, HITGROUP_HEAD, f.runtime, error) &&
        f.players->RecordMonsterDamage(hit, f.runtime, false, 12.5f, error) &&
        f.players->RecordMonsterDamage(hit, f.runtime, true, 3.25f, error),
        "actual damage consumers populate weapon, head, ordinary and Boss statistics");
    // Seed only the numeric boundary; the actual consumer produces the C6
    // journal and advances chain/best/time. No invented death-provider success.
    auto state = std::const_pointer_cast<CSOLScenarioPlayerRuntimeState>(f.players->Find(EHANDLE(f.player)));
    state->monsterKillChain = 49; state->lastMonsterKillAt = 9;
    f.monster->pev->health = 0;
    Require(f.players->RecordMonsterKillChain(hit, f.runtime, 1, 1, error), "real fiftieth-kill consumer appends C6");
    Require(f.players->KillChainRecords().size() == 1 && f.players->KillPositions().size() == 1,
        "archive fixture owns one actual record in each player journal");
}

void DrainArchiveEntities(Fixture& f, bool invalidate)
{
    f.runtime->RequestClose();
    Require(!f.session->Map().EntityAccessAllowed(), "shutdown revokes entity access before archive preparation");
    if (invalidate) f.players->Invalidate();
    for (auto& edict : f.engine.edicts) if (edict.pvPrivateData) Engine::FreePrivate(&edict);
    Require(f.engine.privateBlocks.empty() && !f.session->Monsters().RegisteredCount() && !f.drops->RegisteredCount(),
        "actual OnFree callbacks and engine releases drain every registered private object");
}

void ShutdownStatisticsLifecycle(Engine& engine)
{
    for (bool disconnectFirst : {false, true})
    {
        Fixture f(engine);
        Check(f.players->StatisticsSnapshot().empty(), "a connected but unmodified player does not create statistics");
        const auto first = RecordArchiveKill(f);
        const EHANDLE old(f.player);
        if (disconnectFirst) f.players->Remove(old);
        Engine::Remove(&engine.edicts[1]);
        f.player = f.Player(1);
        const auto second = RecordArchiveKill(f);
        auto statistics = f.players->StatisticsSnapshot();
        Require(statistics.size() == 2, "disconnect or direct serial reuse retains exactly two statistics generations");
        Check(second > first && statistics[0].generation == first && statistics[1].generation == second &&
            statistics[0].weaponKills.at(161) == 1 && statistics[1].weaponKills.at(161) == 1,
            "reused player slot cannot inherit, merge or discard the former player's kills");
        f.players->Invalidate(); f.players->Invalidate();
        statistics = f.players->StatisticsSnapshot();
        Check(statistics.size() == 2 && statistics[0].generation == first && statistics[1].generation == second,
            "repeated invalidation retains each old generation exactly once");
        Require(f.players->Activate(), "same Session can reactivate the actual player registry");
        Check(f.players->StatisticsSnapshot().empty(), "explicit new activation clears retired statistics");
    }
    {
        Fixture f(engine); RecordArchiveChannels(f);
        const auto position = f.players->KillPositions().front();
        const auto chain = f.players->KillChainRecords().front();
        f.players->Invalidate(); f.players->Invalidate();
        Check(f.players->KillPositions().size() == 1 && f.players->KillPositions().front().sequence == position.sequence &&
            f.players->KillChainRecords().size() == 1 && f.players->KillChainRecords().front().sequence == chain.sequence,
            "invalidation retains position and kill-chain journals for shutdown persistence");
        Require(f.players->Activate(), "journal owner can explicitly start another activation");
        Check(f.players->KillPositions().empty() && f.players->KillChainRecords().empty() &&
            f.players->StatisticsSnapshot().empty(), "new activation clears all retained prior-lifecycle values");
    }
}

void ShutdownArchiveAfterRevocation(Engine& engine)
{
    for (bool invalidateFirst : {false, true})
    {
        ArchiveDirectory temporary; Fixture f(engine); RecordArchiveChannels(f);
        const auto generation = f.players->StatisticsSnapshot().front().generation;
        const auto position = f.players->KillPositions().front();
        const auto chain = f.players->KillChainRecords().front();
        auto call = f.Call(f.player); ConfigError configError;
        Require(f.drops->MonsterKilled(call, 1, 1, configError) && f.drops->PendingRewardCount() == 1 &&
            f.drops->RegisteredCount() == 1, "actual drop entity issues one unsubmitted reward ticket");
        DrainArchiveEntities(f, invalidateFirst);
        Require(f.drops->PendingRewardCount() == 0 && f.drops->Rewards().size() == 1,
            "actual post-revocation drop OnDestroy submits its already-issued ticket");
        const auto reward = f.drops->Rewards().front();
        std::string directory, error;
        const auto root = temporary.path / "game";
        Require(std::filesystem::create_directory(root), "archive fixture supplies an existing server game directory");
        {
            NoArchiveEngineCalls forbidden;
            Require(CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error),
                "actual archive succeeds with freed private data, absent globals and forbidden engine callbacks");
        }
        const auto destination = std::filesystem::path(directory) / "shutdown.jsonl";
        const auto bytes = ReadArchiveBytes(destination);
        const auto rows = ReadArchiveRows(bytes);
        Require(rows.size() == 5, "one header and four actual channels are committed without invented death records");
        const auto& header = rows.front();
        Check(header["format"] == "goldcraft.scenario.shutdown" && header["version"].GetInt() == 1 &&
            header["map"] == "map_session_test" && header["map_id"].GetInt() == 1 &&
            header["identity_scope"] == "archive-directory/player-generation" &&
            header["float_encoding"] == "ieee754-binary32-unsigned-bits",
            "parsed archive identifies local generation scope, actual map and lossless float encoding");
        const auto& savedReward = ArchiveRow(rows, "reward");
        Check(savedReward["sequence"].GetUint64() == reward.sequence && savedReward["player_generation"].GetUint64() == generation &&
            savedReward["reward_id"].GetInt() == 3000 && savedReward["submitted"].GetBool() &&
            savedReward["status"] == "awaiting-handler" && f.drops->Rewards().front().status == CSOLScenarioDropReward::Status::AwaitingHandler,
            "OS persistence retains an unknown account reward without granting or acknowledging it");
        const auto& savedPosition = ArchiveRow(rows, "kill-position");
        Check(savedPosition["sequence"].GetUint64() == position.sequence && savedPosition["player_generation"].GetUint64() == generation &&
            savedPosition["map_generation"].GetUint64() == position.mapGeneration &&
            savedPosition["round_generation"].GetUint64() == position.roundGeneration &&
            savedPosition["local_identity"].GetUint() == position.localIdentity && savedPosition["team"].GetUint() == CT &&
            savedPosition["source"].GetUint() == WEAPON_KNIFE && savedPosition["player"].GetBool() && savedPosition["killed"].GetBool() &&
            savedPosition["origin_bits"][0].GetUint() == std::bit_cast<std::uint32_t>(1.5f) &&
            savedPosition["origin_bits"][1].GetUint() == 0x80000000u &&
            savedPosition["origin_bits"][2].GetUint() == std::bit_cast<std::uint32_t>(-2.25f),
            "position serialization preserves generation and every float bit including negative zero");
        const auto& savedChain = ArchiveRow(rows, "kill-chain");
        Check(savedChain["sequence"].GetUint64() == chain.sequence && savedChain["player_generation"].GetUint64() == generation &&
            savedChain["local_identity"].GetUint() == chain.localIdentity &&
            savedChain["map_generation"].GetUint64() == chain.mapGeneration &&
            savedChain["round_generation"].GetUint64() == chain.roundGeneration &&
            savedChain["kind"].GetInt() == static_cast<int>(CSOLScenarioKillChainRecord::Kind::EventC6),
            "typed C6 journal survives serialization after access revocation");
        const auto& stats = ArchiveRow(rows, "player-statistics");
        Check(stats["player_generation"].GetUint64() == generation && !stats["cap_event_pending"].GetBool() &&
            stats["class_id"].GetInt() == -1 && stats["classification"].Size() == 5 &&
            stats["classification"][0].GetUint() == 0 && stats["classification"][1].GetUint() == 1 &&
            stats["classification"][2].GetUint() == 1 && stats["classification"][3].GetUint() == 0 &&
            !stats["classification"][4].GetBool() &&
            stats["cap_event_sequence"].GetUint64() == 0 && stats["head_hits"].GetUint() == 1 &&
            stats["damage_hits"].GetUint() == 2 && stats["kill_chain"].GetUint() == 50 && stats["best_kill_chain"].GetUint() == 50 &&
            stats["boss_damage_bits"].GetUint() == std::bit_cast<std::uint32_t>(3.25f) &&
            stats["ordinary_damage_bits"].GetUint() == std::bit_cast<std::uint32_t>(12.5f) &&
            stats["last_kill_at_bits"].GetUint() == std::bit_cast<std::uint32_t>(10.0f) &&
            stats["weapon_kills"].Size() == 1 && stats["weapon_kills"][0][0].GetInt() == 161 && stats["weapon_kills"][0][1].GetUint() == 1 &&
            stats["weapon_damage_bits"].Size() == 1 && stats["weapon_damage_bits"][0][0].GetInt() == 161 &&
            stats["weapon_damage_bits"][0][1].GetUint() == std::bit_cast<std::uint32_t>(12.5f),
            "actual player-statistics JSON retains every populated accumulator and catalog identity");
        const auto originalDirectory = directory;
        const auto writtenAt = std::filesystem::last_write_time(destination);
        {
            NoArchiveEngineCalls forbidden;
            Check(CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && error.empty(),
                "identical shutdown retry succeeds without entity access");
        }
        Check(directory == originalDirectory && ReadArchiveBytes(destination) == bytes &&
            std::filesystem::last_write_time(destination) == writtenAt &&
            std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 1,
            "identical retry reuses unchanged committed bytes and creates no extra file");
        Require(f.drops->Acknowledge(reward.sequence, reward.playerGeneration, CSOLScenarioDropReward::Status::Rejected),
            "explicit local rejection changes the in-memory snapshot after the original write");
        {
            NoArchiveEngineCalls forbidden;
            Check(!CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && !error.empty(),
                "retry with different records rejects an already-committed archive");
        }
        Check(directory == originalDirectory && ReadArchiveBytes(destination) == bytes &&
            f.drops->Rewards().front().status == CSOLScenarioDropReward::Status::Rejected,
            "conflict preserves both immutable prior bytes and the caller's changed memory");
    }
}

void ShutdownArchiveFailuresAndEmpty(Engine& engine)
{
    {
        ArchiveDirectory temporary; Fixture f(engine); RecordArchiveKill(f);
        auto call = f.Call(f.player); ConfigError configError;
        Require(f.drops->MonsterKilled(call, 1, 1, configError), "failure case owns an actual pending reward");
        const auto root = temporary.path / "game";
        std::string directory, error;
        Check(!CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && !std::filesystem::exists(root),
            "live Runtime cannot publish a premature shutdown snapshot");
        f.runtime->RequestClose();
        Check(!CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && !std::filesystem::exists(root),
            "registered old monster and drop objects prevent archive creation");
        DrainArchiveEntities(f, false);
        const auto generation = f.players->StatisticsSnapshot().front().generation;
        const auto obstacle = temporary.path / "file-not-directory";
        { std::ofstream output(obstacle, std::ios::binary); output << "preserve"; Require(static_cast<bool>(output), "OS failure obstacle is written"); }
        {
            NoArchiveEngineCalls forbidden;
            Check(!CSOL_ArchiveScenarioShutdown(*f.runtime, obstacle.string(), directory, error) && !error.empty() && directory.empty(),
                "real non-directory I/O failure reports failure before choosing an archive directory");
            Check(!CSOL_ArchiveScenarioShutdown(*f.runtime, "", directory, error) && !error.empty(),
                "nonempty shutdown with no cached game directory remains unsaved");
        }
        Check(ReadArchiveBytes(obstacle) == "preserve" && f.players->StatisticsSnapshot().size() == 1 &&
            f.players->StatisticsSnapshot().front().generation == generation &&
            f.players->StatisticsSnapshot().front().weaponKills.at(161) == 1 && f.drops->Rewards().size() == 1 &&
            f.drops->Rewards().front().status == CSOLScenarioDropReward::Status::AwaitingHandler,
            "I/O failure preserves existing disk contents, local statistics and unacknowledged reward memory");
        Require(std::filesystem::create_directory(root), "corrected server game root exists before archive retry");
        {
            NoArchiveEngineCalls forbidden;
            Check(CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && error.empty(),
                "unchanged retained snapshot can be saved after correcting the failed root");
        }
    }
    {
        ArchiveDirectory temporary; Fixture f(engine);
        DrainArchiveEntities(f, true);
        const auto root = temporary.path / "empty-session-must-not-exist";
        std::string directory, error = "stale";
        {
            NoArchiveEngineCalls forbidden;
            Check(CSOL_ArchiveScenarioShutdown(*f.runtime, root.string(), directory, error) && error.empty() && directory.empty(),
                "empty retired Session requires no archive destination");
            Check(CSOL_ArchiveScenarioShutdown(*f.runtime, "", directory, error),
                "empty retired Session does not require a game-directory lookup");
        }
        Check(!std::filesystem::exists(root) && std::filesystem::is_empty(temporary.path),
            "empty Session performs no filesystem creation");
    }
}

void ShutdownRuntimeFinalization(Engine& engine)
{
    for (bool failArchive : {false, true})
    {
        ArchiveDirectory temporary; Fixture f(engine); RecordArchiveChannels(f);
        auto call = f.Call(f.player); ConfigError configError;
        Require(f.drops->MonsterKilled(call, 1, 1, configError), "finalization owns a real queued drop request");
        const auto root = temporary.path / "game";
        f.runtime->SetArchiveRoot(failArchive ? "" : root.string());
        f.runtime->RequestClose();
        Check(!f.runtime->TryFinalize() && f.runtime->players == f.players && !std::filesystem::exists(root),
            "actual finalizer cannot release owners or save before SDK private objects drain");
        DrainArchiveEntities(f, false);
        const auto generation = f.players->StatisticsSnapshot().front().generation;
        if (failArchive)
        {
            const auto before = engine.events.size();
            Check(!f.runtime->TryFinalize(), "actual finalizer retains records when persistence cannot commit");
            const auto after = engine.events.size();
            Check(f.runtime->players == f.players && f.runtime->drops == f.drops && f.runtime->deathServices == f.deaths &&
                f.session->State() == CSOLScenarioSessionState::Empty && f.players->StatisticsSnapshot().size() == 1 &&
                f.players->StatisticsSnapshot().front().generation == generation && f.players->KillPositions().size() == 1 &&
                f.players->KillChainRecords().size() == 1 && f.drops->Rewards().size() == 1 &&
                f.drops->Rewards().front().status == CSOLScenarioDropReward::Status::AwaitingHandler && after > before,
                "failed archive retains value owners and statistics after callback/map retirement, plus the undelivered reward and diagnostic");
            Check(!f.runtime->TryFinalize() && engine.events.size() == after,
                "immediate failed-finalizer retry is throttled without duplicate diagnostic output");
            continue;
        }
        Require(std::filesystem::create_directory(root), "finalizer uses an existing server game directory");
        {
            NoArchiveEngineCalls forbidden;
            Require(f.runtime->TryFinalize(), "actual finalizer commits and drains after all entity access is revoked");
            Check(f.runtime->TryFinalize(), "completed finalizer is idempotent");
        }
        Check(!f.runtime->players && !f.runtime->drops && !f.runtime->deathServices &&
            f.session->State() == CSOLScenarioSessionState::Empty,
            "successful persistence lets the actual Runtime release services and map resources");
        Check(f.players->StatisticsSnapshot().size() == 1 && f.players->StatisticsSnapshot().front().generation == generation &&
            f.drops->Rewards().size() == 1 && f.drops->Rewards().front().status == CSOLScenarioDropReward::Status::AwaitingHandler,
            "external diagnostic references still observe retained values without fabricated account delivery");
        const auto outbox = root / "csol" / "scenario" / "outbox";
        Require(std::filesystem::is_directory(outbox) &&
            std::distance(std::filesystem::directory_iterator(outbox), std::filesystem::directory_iterator{}) == 1,
            "real finalizer publishes exactly one local archive directory");
        const auto archive = std::filesystem::directory_iterator(outbox)->path() / "shutdown.jsonl";
        const auto rows = ReadArchiveRows(ReadArchiveBytes(archive));
        Check(ArchiveRow(rows, "player-statistics")["player_generation"].GetUint64() == generation &&
            ArchiveRow(rows, "reward")["status"] == "awaiting-handler",
            "finalizer's actual OS archive precedes owner release and preserves pending reward semantics");
    }
}

void ShutdownArchiveDuringPrecache(Engine& engine)
{
    ArchiveDirectory temporary;
    engine.Clear(); engine.files = Resources(); engine.Prepare("0"); g_pGameRules = InstallGameRules();
    auto runtime = std::make_shared<CCSOLScenarioRuntime>();
    struct LoadGuard
    {
        std::shared_ptr<CCSOLScenarioRuntime> runtime;
        std::string root, directory, error;
        bool entered{};
        decltype(g_engfuncs.pfnLoadFileForMe) saved{g_engfuncs.pfnLoadFileForMe};
        ~LoadGuard() { g_engfuncs.pfnLoadFileForMe = saved; }
    } guard{runtime, (temporary.path / "busy-session").string(), {}, {}, false, g_engfuncs.pfnLoadFileForMe};
    static LoadGuard* current{};
    struct ResetCurrent { LoadGuard*& value; ~ResetCurrent() { value = nullptr; } } reset{current};
    current = &guard;
    g_engfuncs.pfnLoadFileForMe = [](const char* path, int* length) -> unsigned char*
    {
        auto* data = Engine::Load(path, length);
        auto& value = *current;
        if (!value.entered && std::strcmp(path, "maps/ZBS_DropItem.cso") == 0)
        {
            value.entered = true;
            value.runtime->RequestClose();
            Check(value.runtime->session->Busy() && !value.runtime->session->Monsters().RegisteredCount(),
                "real resource-load callback owns Session busy without registered monsters");
            Check(!value.runtime->ReadyForArchive() &&
                !CSOL_ArchiveScenarioShutdown(*value.runtime, value.root, value.directory, value.error) &&
                !value.runtime->TryFinalize() && !value.error.empty() && value.directory.empty(),
                "direct archive and finalizer both reject a closed but still-executing Session operation");
        }
        return data;
    };
    Check(!runtime->session->Precache("map_session_test", 1, CSOLScenarioResourceScope::Combat) && guard.entered,
        "actual Precache unwinds the request-close callback instead of claiming a complete map");
    Check(runtime->ReadyForArchive() && !runtime->session->Busy() && engine.buffers.empty() &&
        !std::filesystem::exists(guard.root), "busy archive attempt does not write disk or abandon its file allocation");
    Check(runtime->TryFinalize(), "empty partial Session finalizes after its resource operation returns");
    engine.Clear();
}

void RegisteredRulesPrecache(Engine& engine)
{
    const auto originalMap = engine.globals.mapname;
    struct RestoreMap { globalvars_t& globals; string_t map; ~RestoreMap() { globals.mapname = map; } } restore{engine.globals, originalMap};
    // Actual rules now install the concrete damage service before choosing
    // resources. Use the currently supported real map identity, not the old
    // Selection-only map_session_test policy or an injected success provider.
    for (int variant = 0; variant < 3; ++variant)
    {
        engine.Clear();
        engine.files.clear();
        for (const auto& entry : Resources())
        {
            auto path = entry.first;
            auto bytes = entry.second;
            const auto position = path.find("map_session_test");
            if (position != std::string::npos) path.replace(position, std::strlen("map_session_test"), "zs_deadend");
            if (path == "resource/maplist.csv")
            {
                const auto name = bytes.find("map_session_test");
                Require(name != std::string::npos, "authored catalog identifies its map row");
                bytes.replace(name, std::strlen("map_session_test"), "zs_deadend");
                const auto row = bytes.find("\r\n1,fixture,");
                Require(row != std::string::npos, "authored catalog exposes its explicit map identity");
                bytes.replace(row + 2, 1, "98");
            }
            engine.files.emplace(std::move(path), std::move(bytes));
        }
        engine.files["maps/zbs_zs_deadend.cso"] = difficulty_test::Encrypt(
            "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,0,0,0,0,0,0,0,0,0,0,1\r\n");
        engine.globals.mapname = Engine::String("zs_deadend");
        engine.Set("csol_scenario_difficulty", "1"); engine.Prepare("2");
        if (variant == 1) engine.files[GoldenPath] = R"({"ZBS_GoldenZombie":{"monster_info":[{"id":1}]}})";
        if (variant == 2) engine.files.erase("maps/zs_deadend.nav");
        const auto firstEvent = engine.events.size(); g_pGameRules = InstallGameRules();
        Require(CSOL_UsesScenarioRules() && !CSOL_RulesRejected(), "actual registered scenario rules install before resource preparation");
        CSOL_PrecacheGameRules();
        const auto events = engine.events.begin() + static_cast<std::ptrdiff_t>(firstEvent);
        Check(std::count(events, engine.events.end(), std::string("load:maps/zs_deadend.nav")) == 1,
            "registered concrete damage service selects real Combat navigation loading");
        Check(std::count(events, engine.events.end(), std::string("load:") + GoldenPath) == (variant == 2 ? 0 : 1),
            "Golden loads after Combat resources, never before a failed navigation load");
        if (variant != 0)
        {
            Check(CSOL_RulesRejected() && !CSOL_ActivateGameRules() && !CSOL_ScenarioSelecting(),
                variant == 1 ? "nonempty unsupported Golden rejects Combat instead of pretending it is Selection" :
                "missing navigation rejects Combat instead of silently continuing without AI geometry");
            engine.Clear();
            continue;
        }
        Require(!CSOL_RulesRejected(), "actual concrete damage/death/monster services assemble after loaded map resources");
        Markers(engine); Require(CSOL_ActivateGameRules(), "registered Combat rules activate players, drops and actual gameplay");
        auto* rules = dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
        Check(rules && !CSOL_ScenarioSelecting() && !rules->IsGameStarted() && !rules->IsGameOver() &&
            !rules->m_bRoundTerminating && !rules->TeamFull(CT),
            "assembled Combat waits for players without claiming a played wave or completed map");
        {
            CSOLScenarioSdkScope scope;
            Check(scope.Current() && CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get()) != nullptr,
                "registered Combat resolves the actual current player authority after activation");
        }
        engine.Clear();
    }
}

// Engine boundaries observe actual registered Rules. The authored map has two
// g-only groups, so these cases do not claim monster combat or normal login.
struct RoundWire
{
    struct Field
    {
        char type{}; int value{}; std::string text;
        bool operator==(const Field&) const = default;
    };
    struct Packet
    {
        int destination{}, id{};
        const float* origin{};
        edict_t* recipient{};
        std::vector<Field> fields;
        bool ended{};
    };
    inline static RoundWire* active{};
    enginefuncs_t saved{g_engfuncs};
    std::vector<Packet> packets;
    std::function<void(char)> edge;
    int cleanups{};
    RoundWire()
    {
        Require(!active, "round message capture has one engine owner"); active = this;
        g_engfuncs.pfnMessageBegin = [](int destination, int id, const float* origin, edict_t* recipient)
        {
            active->packets.push_back({destination,id,origin,recipient,{},false});
            active->Step('m');
        };
        g_engfuncs.pfnWriteByte = [](int value) { active->Write('b',value); };
        g_engfuncs.pfnWriteShort = [](int value) { active->Write('s',value); };
        g_engfuncs.pfnWriteLong = [](int value) { active->Write('l',value); };
        g_engfuncs.pfnWriteString = [](const char* value)
        { active->packets.back().fields.push_back({'t',0,value}); active->Step('t'); };
        g_engfuncs.pfnMessageEnd = [] { active->packets.back().ended = true; active->Step('e'); };
        g_engfuncs.pfnPlaybackEvent = [](int flags, const edict_t*, unsigned short, float,
            float*, float*, float, float, int, int, int, int)
        {
            Check(flags == (FEV_GLOBAL | FEV_RELIABLE), "real round cleanup resets decals reliably for all clients");
            ++active->cleanups;
        };
    }
    void Step(char point) { const auto action = edge; if (action) action(point); }
    void Write(char type, int value) { packets.back().fields.push_back({type,value,{}}); Step(type); }
    ~RoundWire() { g_engfuncs = saved; active = nullptr; }
    bool PacketIs(std::size_t index, int destination, int id, std::vector<Field> fields) const
    {
        if (index >= packets.size()) return false;
        const auto& packet = packets[index];
        return packet.destination == destination && packet.id == id && !packet.origin && !packet.recipient &&
            packet.ended && packet.fields == fields;
    }
    bool Completion(int mp, int score, int wins, bool reward, int finalText = 0) const
    {
        const std::size_t shift = finalText ? 1 : 0;
        return packets.size() == 5+shift && PacketIs(0,MSG_BROADCAST,9,{{'t',0,"spk \"RoundClear\"\n"}}) &&
            (!finalText || PacketIs(1,MSG_ALL,finalText,{{'b',4,{}},{'t',0,"恭喜！！您已经成功通关！"}})) &&
            PacketIs(1+shift,MSG_ALL,score,{{'t',0,"CT"},{'s',wins,{}}}) &&
            PacketIs(2+shift,MSG_ALL,score,{{'t',0,"TERRORIST"},{'s',0,{}}}) &&
            (reward ? PacketIs(3+shift,MSG_ALL,mp,{{'b',27,{}},{'b',7,{}},{'s',131,{}}}) :
                PacketIs(3+shift,MSG_ALL,mp,{{'b',25,{}},{'b',8,{}}})) &&
            PacketIs(4+shift,MSG_ALL,mp,{{'b',36,{}},{'b',reward ? 1 : 0,{}}});
    }
};

struct RoundRulesFixture
{
    Engine& engine;
    const string_t oldMap;
    const float oldTime, oldFrameTime;
    const std::map<std::string,std::string> oldFiles;
    const std::string oldFreeze, oldDifficulty, oldDelay, oldFinishAction, oldNextMap;
    bool characters{};
    RoundWire wire;
    CHalfLifeMultiplay* rules{};
    csol::scenario::TriggerWaveEvents* goal{};
    CBasePlayer* player{};
    explicit RoundRulesFixture(Engine& value, bool reward = false, bool shortDelay = false, bool withCharacters = false)
        : engine(value), oldMap(value.globals.mapname), oldTime(value.globals.time), oldFrameTime(value.globals.frametime),
          oldFiles(value.files), oldFreeze(value.Var("mp_freezetime").text),
          oldDifficulty(value.Var("csol_scenario_difficulty").text), oldDelay(value.Var("csol_scenario_short_final_delay").text),
          oldFinishAction(value.Var("csol_scenario_finish_action").text), oldNextMap(value.Var("csol_scenario_nextmap").text),
          characters(withCharacters)
    {
        engine.Clear(); engine.files.clear();
        for (const auto& entry : Resources())
        {
            auto path = entry.first, bytes = entry.second;
            const auto position = path.find("map_session_test");
            if (position != std::string::npos) path.replace(position,std::strlen("map_session_test"),"zs_deadend");
            if (path == "resource/maplist.csv")
            {
                const auto name = bytes.find("map_session_test"), row = bytes.find("\r\n1,fixture,");
                Require(name != std::string::npos && row != std::string::npos, "round fixture supplies a supported real map identity");
                bytes.replace(name,std::strlen("map_session_test"),"zs_deadend"); bytes.replace(row+2,1,"98");
            }
            engine.files.emplace(std::move(path),std::move(bytes));
        }
        const std::string metadata = reward ? "RewardRound,1\r\n" : "RewardRound,0\r\n";
        engine.files["maps/zbs_zs_deadend.cso"] = difficulty_test::Encrypt(
            "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\n" + metadata +
            "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
            "0,1,0,0,0,0,0,0,0,0,0,1\r\n1,2,0,0,0,0,0,0,0,0,0,1\r\n");
        engine.files["maps/ZBS_TRG_zs_deadend.cso"] = difficulty_test::Encrypt(
            "id,type,map trigger id,wave id,cool time,dest ratio,end wave\r\n"
            "1,g,1,,0,100,\r\n2,g,2,,0,100,\r\n");
        engine.globals.mapname = Engine::String("zs_deadend");
        engine.globals.time = 10; engine.globals.frametime = 0;
        if (characters)
        {
            engine.files["resource/ZBSVER2/ZBS_class.cso"] = difficulty_test::Encrypt(
                "id,hp,damage,speed,armor,ammo,order\r\n49,20,20,20,20,20,hdsam\r\n");
            engine.files["models/csol/characters.json"] = R"({"version":1,"characters":[
                {"id":149,"team":2,"skin":0,"male":true,"allowPlayerStyle":false,"handStyle":0,
                 "scenarioClass":49,"name":"Fixture CT","model":"fixture_ct","hand":"","portrait":"gfx/csol/p/fixture_ct.tga"}]})";
            engine.files["shader/csol/characters.txt"] = "models/player/fixture_ct/fixture_ct.mdl\r\ngfx/csol/p/fixture_ct.tga\r\n";
            // The real directory/manifest/selection logic loads these bytes;
            // the controlled engine does not claim to render an actual MDL.
            engine.files["models/player/fixture_ct/fixture_ct.mdl"] = "fixture";
            engine.files["gfx/csol/p/fixture_ct.tga"] = "fixture";
            CSOL_PrecacheCharacters();
        }
        engine.Set("mp_freezetime","0"); engine.Set("csol_scenario_difficulty","1");
        engine.Set("csol_scenario_finish_action","0"); engine.Set("csol_scenario_nextmap","zs_deadend");
        engine.Set("csol_scenario_short_final_delay",shortDelay ? "1" : "0"); engine.Prepare("2");
        g_pGameRules = InstallGameRules(); CSOL_PrecacheGameRules();
        Require(CSOL_UsesScenarioRules() && !CSOL_RulesRejected(), "round fixture installs real Combat rules and concrete services");
        Markers(engine);
        for (auto& ed : engine.edicts)
            if (auto* trigger = ed.pvPrivateData ? dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(&ed)) : nullptr)
                if (trigger->m_iId == 2) { ed.v.mins = Vector(120,-8,-8); ed.v.maxs = Vector(136,8,8); }
        Require(CSOL_ActivateGameRules(), "round fixture collects actual independent trigger boxes");
        LinkUserMessages();
        rules = dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
        goal = dynamic_cast<csol::scenario::TriggerWaveEvents*>(g_pGameRules);
        Require(rules && goal && gmsgTeamScore >= 64 && gmsgTextMsg >= 64 && engine.messages.contains("MPToCL") &&
            engine.messages.contains("TextMsg") && engine.messages.at("TextMsg") == gmsgTextMsg,
            "registered rules expose their real trigger consumer and SDK message channels");
        player = Player(1,Vector(32,0,0));
        wire.packets.clear(); goal->GoalReached();
        Check(!rules->IsGameStarted() && !rules->m_bRoundTerminating && wire.packets.empty(),
            "a goal request before the first round cannot fabricate pending success");
        rules->RestartRound();
        Require(!CSOL_RulesRejected() && rules->IsGameStarted() && rules->IsFreezePeriod() && wire.cleanups == 1,
            "public RestartRound enters actual Combat and performs real map cleanup once");
        rules->Think();
        Require(!CSOL_RulesRejected() && !rules->IsFreezePeriod() && !rules->m_bRoundTerminating,
            "actual Think thaws the zero-duration freeze after its manager update");
        wire.packets.clear();
    }
    CBasePlayer* Player(int index, const Vector& origin)
    {
        auto* ed = engine.Reserve(index,"player");
        auto* result = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        AddEntityHashValue(&ed->v,"player",CLASSNAME);
        result->m_iTeam = CT; result->m_iJoiningState = JOINED; result->m_bJustConnected = false;
        // This skips real character preparation; the case observes Rules/TRG,
        // not ordinary login, three-second automatic start or respawn fidelity.
        result->m_iMenu = Menu_ChooseAppearance;
        ed->v.flags = FL_CLIENT; ed->v.deadflag = DEAD_NO; ed->v.health = 100; ed->v.origin = origin;
        return result;
    }
    void Tick(float time)
    {
        Require(g_pGameRules == rules, "round tick never enters a retired rules pointer");
        engine.globals.time = time; rules->Think();
    }
    void Scans(int count)
    { for (int i=0;i<count;++i) Tick(engine.globals.time+0.25f); }
    void Reach(const Vector& origin)
    {
        player->pev->origin = origin;
        for (int i=0;i<engine.globals.maxClients && g_pGameRules == rules && !rules->m_bRoundTerminating;++i)
            Tick(engine.globals.time+0.25f);
    }
    ~RoundRulesFixture()
    {
        wire.edge = {};
        engine.Clear();
        engine.files = oldFiles; engine.globals.mapname = oldMap; engine.globals.time = oldTime;
        engine.globals.frametime = oldFrameTime; engine.Set("mp_freezetime",oldFreeze.c_str());
        engine.Set("csol_scenario_difficulty",oldDifficulty.c_str()); engine.Set("csol_scenario_short_final_delay",oldDelay.c_str());
        engine.Set("csol_scenario_finish_action",oldFinishAction.c_str()); engine.Set("csol_scenario_nextmap",oldNextMap.c_str());
        if (characters) CSOL_PrecacheCharacters();
    }
};

struct RoundApache final : CBaseEntity
{
    struct State { int calls{}, gib{}; entvars_t* attacker{}; bool remove{}; };
    State* state{};
    void Killed(entvars_t* attacker, int gib) override
    {
        auto* observation = state;
        ++observation->calls; observation->gib = gib; observation->attacker = attacker;
        if (observation->remove) Engine::Remove(edict());
        else pev->health = 0;
    }
};

void RegisteredRulesRoundSuccess(Engine& engine)
{
    for (bool reward : {false,true}) for (bool shortDelay : {false,true})
    {
        RoundRulesFixture f(engine,reward,shortDelay);
        f.player->pev->origin = Vector(8,0,0); f.Scans(engine.globals.maxClients);
        Check(!f.rules->m_bRoundTerminating && f.wire.packets.empty(), "open goal-box boundary does not complete a stage");
        RoundApache::State first{}, second{}; first.remove = true;
        for (auto* state : {&first,&second})
        {
            auto* ed = engine.Reserve(engine.nextEntity++,"monster_apache");
            auto* target = GetClassPtr<CCSEntity>(reinterpret_cast<RoundApache*>(&ed->v));
            target->state = state; ed->v.health = 100;
        }
        f.Reach(Vector(0,0,0));
        Require(!CSOL_RulesRejected() && f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 1 &&
            f.rules->m_iRoundWinStatus == WINSTATUS_CTS && !f.rules->IsGameOver(),
            "actual Think-to-g chain accepts one intermediate stage without the old unsupported FinishRound error");
        const float deadline = engine.globals.time+7.0f;
        Check(f.rules->m_flRestartRoundTime == deadline && f.wire.cleanups == 1 &&
            f.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,1,reward),
            "stage success sends exact sound, CT/T score, outcome or reward event, and reward state before a seven-second deadline");
        Check(first.calls == 1 && second.calls == 1 && first.gib == GIB_NORMAL && second.gib == GIB_NORMAL &&
            !first.attacker && !second.attacker, "actual apache enumeration continues after the first controlled Killed callback frees itself");
        f.wire.packets.clear(); f.goal->GoalReached(); f.Tick(deadline-0.25f);
        Check(f.rules->m_iNumCTWins == 1 && f.wire.packets.empty() && f.wire.cleanups == 1,
            "repeated completion and pre-deadline manager ticks do not duplicate success or rebuild the loaded group");
        // The requested option is latched in real Precache; a mid-map change
        // cannot shorten/lengthen the final deadline of the current map.
        engine.Set("csol_scenario_short_final_delay",shortDelay ? "0" : "1");
        f.Tick(deadline);
        Require(!CSOL_RulesRejected() && !f.rules->m_bRoundTerminating && f.rules->IsFreezePeriod() &&
            f.rules->m_flRestartRoundTime == 0 && f.wire.cleanups == 2,
            "deadline runs the actual pending RestartRound once and clears its old deadline");
        f.Tick(deadline+0.25f); f.wire.packets.clear(); f.Scans(engine.globals.maxClients);
        Check(!f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 1 && f.wire.packets.empty(),
            "old spatial group is inactive after the pending target commits");
        f.Reach(Vector(128,0,0));
        const float finalDeadline = engine.globals.time+(shortDelay ? 5.0f : 30.0f);
        Require(!CSOL_RulesRejected() && f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 2 &&
            f.rules->m_flRestartRoundTime == finalDeadline &&
            f.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,2,reward,gmsgTextMsg),
            "new group completes the final row using the map-latched five/thirty-second policy");
        f.wire.packets.clear(); f.rules->RestartRound(); f.goal->GoalReached(); f.Tick(finalDeadline-0.25f);
        Check(f.wire.cleanups == 2 && f.wire.packets.empty() && !f.rules->IsGameOver() && f.rules->m_iNumCTWins == 2,
            "final-result wait rejects restart/repeated goal and remains incomplete until its deadline");
        f.Tick(finalDeadline);
        Check(!CSOL_RulesRejected() && !f.rules->IsGameOver() && f.rules->IsGameStarted() && !f.rules->IsFreezePeriod() &&
            f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0 && f.wire.cleanups == 2 && f.wire.packets.empty(),
            "default final deadline keeps native controls in free play while retaining the old wave/trigger stop gate");
        f.rules->RestartRound(); f.goal->GoalReached(); f.Scans(2);
        Check(!f.rules->IsGameOver() && f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 2 &&
            f.wire.cleanups == 2 && f.wire.packets.empty(), "free-play rules do not consume a stale pending target or score another goal");
    }
}

void RegisteredRulesRoundThreshold(Engine& engine)
{
    RoundRulesFixture f(engine);
    auto* second = f.Player(2,Vector(32,0,0));
    f.player->pev->origin = Vector(0,0,0); f.Scans(engine.globals.maxClients*2);
    Check(!f.rules->m_bRoundTerminating && f.wire.packets.empty() && f.rules->m_iNumCT == 2,
        "one arriving CT cannot accumulate across scan cycles to meet a two-CT hundred-percent goal");
    second->pev->origin = Vector(0,0,0); f.Scans(1);
    Check(!f.rules->m_bRoundTerminating && f.wire.packets.empty(), "first slot alone still leaves the current scan short of its goal");
    f.Scans(1);
    Check(!CSOL_RulesRejected() && f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 1 &&
        f.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,1,false),
        "second CT arrival through the real scanner reaches exactly one stage completion");
}

void RegisteredRulesRoundReentry(Engine& engine)
{
    // Three fields of the first packet, then all five packet-end boundaries.
    for (int variant=0;variant<8;++variant)
    {
        RoundRulesFixture f(engine); int steps{}, ends{}, attempted{};
        f.wire.edge = [&](char edge)
        {
            const bool selected = variant < 3 ? steps++ == variant : edge == 'e' && ends++ == variant-3;
            if (!selected || attempted) return;
            ++attempted; const auto packets = f.wire.packets.size();
            f.goal->GoalReached(); f.rules->RestartRound();
            Check(f.wire.packets.size() == packets && f.wire.cleanups == 1,
                "actual public restart and goal reentry cannot start nested completion or cleanup inside an open packet");
        };
        f.Reach(Vector(0,0,0)); f.wire.edge = {};
        Require(attempted == 1 && !CSOL_RulesRejected() && f.rules->m_iNumCTWins == 1 &&
            f.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,1,false),
            "all completion packet boundaries preserve exactly one committed success");
        const auto oldDeadline = f.rules->m_flRestartRoundTime;
        f.rules->RestartRound();
        Require(!CSOL_RulesRejected() && !f.rules->m_bRoundTerminating && f.wire.cleanups == 2,
            "explicit external restart consumes the already committed next stage outside its callback");
        f.Tick(engine.globals.time+0.25f); f.wire.packets.clear(); f.Tick(oldDeadline);
        Check(f.wire.cleanups == 2 && !f.rules->m_bRoundTerminating && f.wire.packets.empty(),
            "old automatic deadline cannot advance a second time after an explicit restart");
        f.Reach(Vector(128,0,0));
        Check(!CSOL_RulesRejected() && f.rules->m_iNumCTWins == 2 && f.rules->m_bRoundTerminating,
            "external restart commits group two rather than replaying or skipping the pending target");
    }
}

void RegisteredRulesRoundCancellation(Engine& engine)
{
    for (int packetIndex=0;packetIndex<5;++packetIndex)
    {
        RoundRulesFixture f(engine); bool cancelled{};
        f.wire.edge = [&](char edge)
        {
            if (edge != 'e' || cancelled || f.wire.packets.size() != static_cast<std::size_t>(packetIndex+1)) return;
            cancelled = true; FreeGameRules(&g_pGameRules);
        };
        f.Reach(Vector(0,0,0)); f.wire.edge = {};
        Check(cancelled && !g_pGameRules && !CSOL_RulesRejected() &&
            f.wire.packets.size() == static_cast<std::size_t>(packetIndex+1) && f.wire.packets.back().ended &&
            f.wire.cleanups == 1, "actual FreeGameRules at a completed packet cancels all remaining old-rule completion stages");
        // No raw rules/player dereference after cancellation. Fixture cleanup
        // releases private entities then runs the actual retirement collector.
    }
    RoundRulesFixture fresh(engine); fresh.Reach(Vector(0,0,0));
    Check(!CSOL_RulesRejected() && fresh.rules->m_iNumCTWins == 1 &&
        fresh.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,1,false),
        "a fresh map after cancellation owns its independent success and packet guard");
}

void RegisteredRulesRoundMessageFailure(Engine& engine)
{
    RoundRulesFixture f(engine);
    const auto write = g_engfuncs.pfnWriteString; g_engfuncs.pfnWriteString = nullptr;
    struct RestoreString { decltype(write) value; ~RestoreString() { g_engfuncs.pfnWriteString = value; } } restore{write};
    f.Reach(Vector(0,0,0));
    Check(CSOL_RulesRejected() && f.wire.packets.empty() && f.rules->m_iNumCTWins == 0 &&
        !f.rules->m_bRoundTerminating && f.wire.cleanups == 1,
        "missing required round-clear writer rejects before a packet, score mutation, pending transition or cleanup");
    g_engfuncs.pfnWriteString = write;
    f.goal->GoalReached(); f.rules->RestartRound(); f.Scans(2);
    Check(f.wire.packets.empty() && f.wire.cleanups == 1 && f.rules->m_iNumCTWins == 0,
        "restoring a writer does not silently resume rejected map rules or repeat a partial completion");
}

struct FinishPolicyEdges
{
    inline static FinishPolicyEdges* active{};
    enginefuncs_t saved{g_engfuncs};
    std::function<void(char)> edge;
    int mapChecks{}, changes{}, warnings{}, catalogLoads{}, catalogFrees{};
    bool valid{true};
    std::string checkedMap, changedMap;
    bool nullLandmark{};
    void* catalogBuffer{};
    explicit FinishPolicyEdges()
    {
        Require(!active,"one finish-policy engine capture"); active = this;
        g_engfuncs.pfnLoadFileForMe = [](const char* name, int* size)
        {
            auto* result = active->saved.pfnLoadFileForMe(name,size);
            if (std::strcmp(name,"resource/maplist.csv") == 0)
            { ++active->catalogLoads; active->catalogBuffer = result; active->Step('l'); }
            return result;
        };
        g_engfuncs.pfnFreeFile = [](void* bytes)
        {
            const bool catalog = bytes && bytes == active->catalogBuffer;
            active->saved.pfnFreeFile(bytes);
            if (catalog) { active->catalogBuffer = nullptr; ++active->catalogFrees; active->Step('f'); }
        };
        g_engfuncs.pfnIsMapValid = [](const char* name)
        { ++active->mapChecks; active->checkedMap = name; active->Step('v'); return active->valid ? TRUE : FALSE; };
        g_engfuncs.pfnChangeLevel = [](const char* name, const char* landmark)
        {
            ++active->changes; active->changedMap = name; active->nullLandmark = landmark == nullptr;
            active->Step('c');
        };
        g_engfuncs.pfnAlertMessage = Alert;
    }
    static void Alert(ALERT_TYPE type, const char* format, ...)
    {
        char text[2048]; va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
        Engine::self->Record(std::string("alert:")+text);
        if (std::strstr(text,"retaining") && std::strstr(text,"free play"))
        { Check(type == at_console,"finish fallback is one console warning"); ++active->warnings; active->Step('a'); }
    }
    void Step(char point) { const auto action = edge; if (action) action(point); }
    ~FinishPolicyEdges() { g_engfuncs = saved; active = nullptr; }
};

float ReachFinalWait(RoundRulesFixture& f)
{
    f.Reach(Vector(0,0,0));
    Require(!CSOL_RulesRejected() && f.rules->m_iNumCTWins == 1 && f.rules->m_bRoundTerminating,
        "finish-policy setup reaches its intermediate stage through real spatial g");
    const float deadline = f.rules->m_flRestartRoundTime;
    f.Tick(deadline); f.Tick(deadline+0.25f);
    f.Reach(Vector(128,0,0));
    Require(!CSOL_RulesRejected() && f.rules->m_iNumCTWins == 2 && f.rules->m_bRoundTerminating &&
        f.rules->m_flRestartRoundTime > f.engine.globals.time && !f.rules->IsGameOver(),
        "finish-policy setup reaches actual final wait without replacing Rules or trigger consumers");
    f.wire.packets.clear();
    return f.rules->m_flRestartRoundTime;
}

bool FreePlayState(const RoundRulesFixture& f)
{
    return !CSOL_RulesRejected() && !f.rules->IsGameOver() && f.rules->IsGameStarted() &&
        !f.rules->IsFreezePeriod() && f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0 &&
        f.rules->m_iNumCTWins == 2 && f.wire.cleanups == 2 && f.wire.packets.empty();
}

void RegisteredRulesFinishChoice(Engine& engine)
{
    for (bool change : {false,true})
    {
        RoundRulesFixture f(engine); FinishPolicyEdges edges;
        // Change while waiting, rather than mutating a captured policy or
        // replacing a production getter, proves the actual read boundary.
        engine.Set("csol_scenario_finish_action",change ? "0" : "1");
        const auto deadline = ReachFinalWait(f);
        Check(edges.mapChecks == 0 && edges.changes == 0 && edges.catalogLoads == 0,
            "intermediate restart and entering final wait do not apply the final map policy");
        engine.Set("csol_scenario_finish_action",change ? "1" : "0");
        if (!change) engine.Set("csol_scenario_nextmap","../unused;quit");
        f.Tick(deadline-0.25f);
        Check(edges.mapChecks == 0 && edges.changes == 0 && edges.catalogLoads == 0,
            "final policy does not touch catalog or engine validity before the deadline");
        f.Tick(deadline);
        if (change)
        {
            Check(!CSOL_RulesRejected() && f.rules->IsGameOver() && f.rules->m_flRestartRoundTime == 0 &&
                edges.catalogLoads == 1 && edges.catalogFrees == 1 && edges.mapChecks == 1 && edges.changes == 1 &&
                edges.checkedMap == "zs_deadend" && edges.changedMap == "zs_deadend" && edges.nullLandmark && !edges.warnings,
                "supported catalog and native map validity permit one exact ChangeLevel request at the final deadline");
        }
        else Check(FreePlayState(f) && !edges.catalogLoads && !edges.mapChecks && !edges.changes && !edges.warnings,
            "current action zero retains free play without evaluating an unused invalid map or issuing a warning");
        f.goal->GoalReached(); f.rules->RestartRound(); f.Scans(8);
        Check(edges.changes == (change ? 1 : 0) && edges.mapChecks == (change ? 1 : 0) &&
            f.rules->m_iNumCTWins == 2 && f.wire.cleanups == 2 && f.wire.packets.empty() && !edges.warnings,
            "repeated final ticks, goal and restart do not retry a change request or resume old stage triggers");
        if (!change)
        {
            f.player->pev->health = 0; f.player->pev->deadflag = DEAD_DEAD; f.Scans(4);
            Check(FreePlayState(f), "all CTs dying in free play cannot restart the old failure/waiting stage");
        }
    }
}

void RegisteredRulesFinishFallback(Engine& engine)
{
    for (int variant=0;variant<13;++variant)
    {
        RoundRulesFixture f(engine); FinishPolicyEdges edges; const auto deadline = ReachFinalWait(f);
        engine.Set("csol_scenario_finish_action","1");
        if (variant == 0) engine.Set("csol_scenario_finish_action","01");
        if (variant == 1) engine.Set("csol_scenario_nextmap","../zs_deadend");
        if (variant == 2) engine.Set("csol_game_mode","1");
        if (variant == 3) engine.files.erase("resource/maplist.csv");
        if (variant == 4) engine.files["resource/maplist.csv"] = "malformed\r\n";
        if (variant == 5)
        {
            auto& catalog = engine.files.at("resource/maplist.csv");
            auto row = catalog.substr(catalog.find('\n')+1); row.replace(0,2,"99"); catalog += row;
        }
        if (variant == 6)
        {
            auto& catalog = engine.files.at("resource/maplist.csv");
            const auto subtype = catalog.rfind(",1\r\n"); Require(subtype != std::string::npos,"fixture catalog exposes subtype");
            catalog.replace(subtype+1,1,"2");
        }
        if (variant == 7)
        {
            engine.Set("csol_scenario_nextmap","cs_assault");
            auto& catalog = engine.files.at("resource/maplist.csv"); const auto name = catalog.find("zs_deadend");
            Require(name != std::string::npos,"fixture catalog exposes map name"); catalog.replace(name,10,"cs_assault");
        }
        if (variant == 8) edges.valid = false;
        if (variant == 9) g_engfuncs.pfnIsMapValid = nullptr;
        if (variant == 10) g_engfuncs.pfnChangeLevel = nullptr;
        if (variant == 11) g_engfuncs.pfnLoadFileForMe = nullptr;
        if (variant == 12) g_engfuncs.pfnFreeFile = nullptr;
        f.Tick(deadline);
        Check(FreePlayState(f) && edges.warnings == 1 && edges.changes == 0 && edges.mapChecks == (variant == 8 ? 1 : 0),
            "invalid policy, catalog identity/subtype/support, engine refusal or missing edge falls back once without rejecting current gameplay");
        engine.Set("csol_scenario_finish_action","0"); f.goal->GoalReached(); f.rules->RestartRound(); f.Scans(5);
        Check(FreePlayState(f) && edges.warnings == 1 && edges.changes == 0 && edges.mapChecks == (variant == 8 ? 1 : 0),
            "corrected configuration cannot repeatedly reapply an already consumed final policy or duplicate its warning");
    }
}

void RegisteredRulesFinishReentry(Engine& engine)
{
    for (char boundary : {'l','f','v','c','a'})
    {
        RoundRulesFixture f(engine); FinishPolicyEdges edges; const auto deadline = ReachFinalWait(f);
        engine.Set("csol_scenario_finish_action",boundary == 'a' ? "invalid" : "1"); int attempted{};
        edges.edge = [&](char point)
        {
            if (point != boundary || attempted) return;
            ++attempted; const auto checks = edges.mapChecks, changes = edges.changes;
            f.rules->Think(); f.rules->RestartRound(); f.goal->GoalReached();
            Check(checks == edges.mapChecks && changes == edges.changes && f.wire.cleanups == 2 && f.wire.packets.empty(),
                "actual rule reentry at finish-policy engine boundaries cannot initiate nested final actions");
            engine.Set("csol_scenario_finish_action","0"); engine.Set("csol_scenario_nextmap","changed_after_snapshot");
        };
        f.Tick(deadline); edges.edge = {};
        Check(attempted == 1 && !CSOL_RulesRejected() && edges.changes == (boundary == 'a' ? 0 : 1) &&
            edges.warnings == (boundary == 'a' ? 1 : 0) && (boundary == 'a' || edges.changedMap == "zs_deadend"),
            "finish policy retains its captured target through callback mutation and publishes only its original action once");
        f.Scans(3);
        Check(edges.changes == (boundary == 'a' ? 0 : 1) && edges.warnings == (boundary == 'a' ? 1 : 0),
            "later frames do not reapply a callback-mutated policy");
    }
}

void RegisteredRulesFinishCancellation(Engine& engine)
{
    for (char boundary : {'l','f','v','c','a'})
    {
        RoundRulesFixture f(engine); FinishPolicyEdges edges; const auto deadline = ReachFinalWait(f);
        engine.Set("csol_scenario_finish_action",boundary == 'a' ? "invalid" : "1"); bool cancelled{};
        edges.edge = [&](char point)
        {
            if (point != boundary || cancelled) return;
            cancelled = true; FreeGameRules(&g_pGameRules);
        };
        f.Tick(deadline); edges.edge = {};
        Check(cancelled && !g_pGameRules && !CSOL_RulesRejected() && engine.buffers.empty() &&
            edges.mapChecks == (boundary == 'v' || boundary == 'c' ? 1 : 0) &&
            edges.catalogLoads == (boundary == 'a' ? 0 : 1) && edges.catalogFrees == (boundary == 'a' ? 0 : 1) &&
            edges.changes == (boundary == 'c' ? 1 : 0) && edges.warnings == (boundary == 'a' ? 1 : 0) &&
            f.wire.cleanups == 2 && f.wire.packets.empty(),
            "retiring actual rules at a catalog, validity, change or log edge releases loaded bytes and cancels later old-owner actions");
    }
    RoundRulesFixture fresh(engine); FinishPolicyEdges edges; fresh.Tick(ReachFinalWait(fresh));
    Check(FreePlayState(fresh) && !edges.changes && !edges.warnings,
        "a later map owns an independent default final policy after cancelled old finish operations");
}

void RegisteredRulesFreePlayEligibility(Engine& engine)
{
    RoundRulesFixture f(engine,false,false,true); FinishPolicyEdges edges;
    f.Tick(ReachFinalWait(f));
    Require(FreePlayState(f) && !f.rules->FPlayerCanRespawn(f.player),
        "default free play does not permit an unfinished character to spawn");
    f.player->m_iMenu = Menu_OFF;
    Check(!f.rules->FPlayerCanRespawn(f.player), "closing the menu without an actual character selection cannot grant spawn permission");
    f.player->m_iMenu = Menu_ChooseAppearance;
    f.player->pev->flags |= FL_FAKECLIENT;
    CSOLScenarioSdkScope scope;
    Require(CSOL_EnsureScenarioBotCharacter(f.player,scope.EntityAccess()), "real catalog selection maps the controlled CT to its actual scenario class");
    int classId = -1;
    Require(CSOL_CharacterScenarioClass(f.player,classId,scope.EntityAccess()) && classId == 49,
        "production character getter resolves the authored class rather than a replacement getter or private test state");
    f.player->m_iMenu = Menu_OFF; f.player->pev->health = 0; f.player->pev->deadflag = DEAD_DEAD;
    Check(f.rules->FPlayerCanRespawn(f.player), "actual FreePlay rule permits a selected CT even with the old end-round gate retained");
    for (int denied=0;denied<7;++denied)
    {
        if (denied == 0) f.player->m_iMenu = Menu_ChooseAppearance;
        if (denied == 1) f.player->m_iTeam = TERRORIST;
        if (denied == 2) f.player->has_disconnected = true;
        if (denied == 3) f.player->pev->flags |= FL_KILLME;
        if (denied == 4) f.player->pev->flags |= FL_DORMANT;
        if (denied == 5) f.player->m_bJustConnected = true;
        if (denied == 6) f.player->m_iJoiningState = GETINTOGAME;
        Check(!f.rules->FPlayerCanRespawn(f.player), "free-play spawn permission retains menu, team, connection and entity lifetime gates");
        f.player->m_iMenu = Menu_OFF; f.player->m_iTeam = CT; f.player->has_disconnected = false;
        f.player->pev->flags = FL_CLIENT | FL_FAKECLIENT; f.player->m_bJustConnected = false;
        f.player->m_iJoiningState = JOINED;
    }
    Check(f.rules->FPlayerCanRespawn(f.player) && FreePlayState(f) && !edges.changes && !edges.warnings,
        "restored legitimate character remains eligible without starting a new scenario stage");
    // This is the actual permission gate only. Full player Spawn, equipment,
    // input processing, rendered models and a human selection remain separate.
    f.player->m_iMenu = Menu_ChooseAppearance;
}

void ReachFinalPlaying(RoundRulesFixture& f)
{
    f.Reach(Vector(0,0,0));
    Require(!CSOL_RulesRejected() && f.wire.Completion(f.engine.messages.at("MPToCL"),gmsgTeamScore,1,false),
        "intermediate completion has exactly five packets and no final-clear prompt");
    const auto deadline = f.rules->m_flRestartRoundTime;
    f.Tick(deadline); f.Tick(deadline+0.25f); f.wire.packets.clear();
    Require(!f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0 && f.rules->m_iNumCTWins == 1,
        "actual pending restart reaches second stage before testing final text callbacks");
}

void RegisteredRulesFinalTextReentry(Engine& engine)
{
    for (char boundary : {'m','b','t','e'})
    {
        RoundRulesFixture f(engine); ReachFinalPlaying(f); int attempted{};
        f.wire.edge = [&](char point)
        {
            if (point != boundary || attempted || f.wire.packets.size() != 2 || f.wire.packets.back().id != gmsgTextMsg) return;
            ++attempted;
            Check(!f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0,
                "final-clear prompt is sent before the actual final TerminateRound commit");
            const auto packets = f.wire.packets.size();
            f.rules->Think(); f.goal->GoalReached(); f.rules->RestartRound();
            Check(f.wire.packets.size() == packets && f.rules->m_iNumCTWins == 2 && f.wire.cleanups == 2 &&
                !f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0,
                "real reentry at each final TextMsg boundary cannot duplicate final clear or commit a nested deadline");
        };
        f.Reach(Vector(128,0,0)); f.wire.edge = {};
        Require(attempted == 1 && !CSOL_RulesRejected() && f.rules->m_bRoundTerminating &&
            f.rules->m_flRestartRoundTime == engine.globals.time+30.0f &&
            f.wire.Completion(engine.messages.at("MPToCL"),gmsgTeamScore,2,false,gmsgTextMsg),
            "final completion sends one Chinese prompt between RoundClear and both team scores then commits its deadline");
        f.wire.packets.clear(); f.goal->GoalReached(); f.rules->RestartRound(); f.Scans(2);
        Check(f.wire.packets.empty() && f.wire.cleanups == 2 && f.rules->m_iNumCTWins == 2,
            "final prompt is not repeated after the completion stack has returned");
    }
}

void RegisteredRulesFinalTextCancellation(Engine& engine)
{
    for (char boundary : {'m','b','t','e'})
    {
        RoundRulesFixture f(engine); ReachFinalPlaying(f); bool cancelled{};
        // Explicit SDK pin keeps the retired rules storage alive solely to
        // observe that its old final deadline was not written after the hook.
        CSOLScenarioSdkScope retained(f.rules);
        f.wire.edge = [&](char point)
        {
            if (point != boundary || cancelled || f.wire.packets.size() != 2 || f.wire.packets.back().id != gmsgTextMsg) return;
            cancelled = true; FreeGameRules(&g_pGameRules);
        };
        f.Reach(Vector(128,0,0)); f.wire.edge = {};
        Check(cancelled && !g_pGameRules && !retained.Current() && !CSOL_RulesRejected() &&
            !f.rules->m_bRoundTerminating && f.rules->m_flRestartRoundTime == 0 && f.rules->m_iNumCTWins == 2 &&
            f.wire.cleanups == 2 && f.wire.packets.size() == 2 &&
            f.wire.PacketIs(0,MSG_BROADCAST,9,{{'t',0,"spk \"RoundClear\"\n"}}) &&
            f.wire.PacketIs(1,MSG_ALL,gmsgTextMsg,{{'b',4,{}},{'t',0,"恭喜！！您已经成功通关！"}}),
            "actual retirement at final prompt Begin/byte/string/End completes only that packet and stops old TerminateRound and later scores/reward packets");
    }
}

void RegisteredRulesFinalTextFailure(Engine& engine)
{
    // Missing/invalid native channel and a writer needed only after the
    // already completed RoundClear packet all fail at the actual Rules edge.
    for (int variant=0;variant<4;++variant)
    {
        RoundRulesFixture f(engine); ReachFinalPlaying(f);
        const int originalId = gmsgTextMsg;
        const auto originalByte = g_engfuncs.pfnWriteByte;
        struct Restore
        {
            int id; decltype(originalByte) byte;
            ~Restore() { gmsgTextMsg = id; g_engfuncs.pfnWriteByte = byte; }
        } restore{originalId,originalByte};
        if (variant < 3) gmsgTextMsg = std::array{0,63,256}[variant];
        else g_engfuncs.pfnWriteByte = nullptr;
        f.Reach(Vector(128,0,0));
        Check(CSOL_RulesRejected() && !f.rules->m_bRoundTerminating &&
            f.rules->m_flRestartRoundTime == 0 && f.rules->m_iNumCTWins == 2 && f.wire.cleanups == 2 &&
            f.wire.packets.size() == 1 &&
            f.wire.PacketIs(0,MSG_BROADCAST,9,{{'t',0,"spk \"RoundClear\"\n"}}),
            "final TextMsg channel/writer failure rejects after RoundClear but before final deadline and score/reward suffix");
        gmsgTextMsg = originalId; g_engfuncs.pfnWriteByte = originalByte;
        f.wire.packets.clear(); f.goal->GoalReached(); f.rules->RestartRound(); f.Scans(2);
        Check(CSOL_RulesRejected() && f.wire.packets.empty() && f.wire.cleanups == 2 &&
            f.rules->m_flRestartRoundTime == 0 && !f.rules->m_bRoundTerminating && f.rules->m_iNumCTWins == 2,
            "restoring final text facilities does not silently resume the rejected completion");
    }
}

constexpr const char* BossSkillPath = "maps/zbs_sk_map_session_test.cso";
constexpr const char* BossSkillCsv =
    "temp,id,type,ratio,cooltime,aniTime,aniSeq,damageDelay,damage,damageDegree,damageHeight,damageDist,forceAttackTime,startDistMin,startDistMax,startHealthMin,startHealthMax,approachDist,startMsg,kickSide,kickUp,rushPower\r\n"
    "authored,1002,3,3,3,2.5,zbs_attack1,0.5,100,0,2,2,4,0,1000,0,100,0,-1,400,250,30\r\n";

struct SessionResourceFixture
{
    Engine& engine;
    CCSOLScenarioSession session;
    explicit SessionResourceFixture(Engine& value) : engine(value)
    {
        engine.Clear(); engine.files = Resources(); engine.Prepare("0");
        g_pGameRules = InstallGameRules();
        Require(g_pGameRules && !CSOL_RulesRejected(), "native rules host an independent Session resource test");
    }
    ~SessionResourceFixture()
    {
        Check(session.Deactivate(), "resource Session fixture performs real orderly teardown");
        engine.Clear();
    }
};

struct ModelFileSessionEdges
{
    inline static ModelFileSessionEdges* active{};
    enginefuncs_t saved{g_engfuncs};
    std::vector<std::string> order, precaches;
    ModelFileSessionEdges()
    {
        Require(!active, "one model-file engine callback wrapper"); active = this;
        g_engfuncs.pfnGetFileSize = [](const char* path)
        {
            auto& self = *active; self.order.push_back(std::string("size:") + path);
            return self.saved.pfnGetFileSize(path);
        };
        g_engfuncs.pfnPrecacheModel = [](const char* path)
        {
            auto& self = *active; self.order.push_back(std::string("model:") + path);
            self.precaches.emplace_back(path);
            return self.saved.pfnPrecacheModel(path);
        };
    }
    ~ModelFileSessionEdges() { g_engfuncs = saved; active = nullptr; }
};

void SessionModelFiles(Engine& engine)
{
    constexpr const char* host = "models/player/zombi_host/zombi_host.mdl";
    constexpr const char* origin = "models/player/zombi_origin/zombi_origin.mdl";
    constexpr const char* speed = "models/player/speed_zombi_host/speed_zombi_host.mdl";
    // A valid earlier path must not reach the fatal engine precache boundary
    // when a later required model has no usable file. Neither authored catalog
    // uses origin: the pool still requires that initial model independently.
    // Test missing/empty fixed paths and catalog paths, then a positive control.
    for (int variant = 0; variant < 5; ++variant)
    {
        SessionResourceFixture f(engine); ModelFileSessionEdges edges;
        const bool fixedPath = variant < 2;
        const bool accepted = variant == 4;
        const char* rejectedPath = fixedPath ? origin : host;
        std::string types =
            "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
            "1,zombi_host,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,7,1\r\n";
        if (!fixedPath) types += "2,speed_zombi_host,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,7,1\r\n";
        engine.files["maps/ZBS_ZT_map_session_test.cso"] = difficulty_test::Encrypt(types);
        engine.files[speed] = "fixture-speed-zombi-host-model";
        Require(!engine.files.at(host).empty() && !engine.files.at(origin).empty() && !engine.files.at(speed).empty(),
            "model-file cases begin with explicit nonempty filesystem fixtures");
        if (!accepted)
        {
            if (variant % 2 == 0) engine.files.erase(rejectedPath);
            else engine.files.at(rejectedPath).clear();
        }
        const int allocations = engine.allocations;
        const auto privateBlocks = engine.privateBlocks.size();
        const bool loaded = f.session.Precache("map_session_test", 1, CSOLScenarioResourceScope::Combat);
        Check(engine.buffers.empty(), "model-file gate releases all preceding resource buffers");
        std::vector<std::string> expectedOrder = fixedPath ?
            std::vector<std::string>{std::string("size:") + host, std::string("size:") + origin} :
            std::vector<std::string>{std::string("size:") + speed, std::string("size:") + host};
        if (accepted) expectedOrder.push_back(std::string("size:") + origin);
        Check(edges.order.size() >= expectedOrder.size() &&
            std::equal(expectedOrder.begin(), expectedOrder.end(), edges.order.begin()),
            "Session checks the required fixed and catalog model paths before the first model precache");
        if (!accepted)
        {
            Check(!loaded && f.session.State() == CSOLScenarioSessionState::Failed &&
                f.session.Error().path == rejectedPath && f.session.Error().detail.message ==
                    std::string("required scenario monster model is missing or empty: ") + rejectedPath,
                "missing and empty model files fail Combat with the exact offending path");
            Check(edges.precaches.empty() && !f.session.Models().Ready() &&
                f.session.Monsters().RegisteredCount() == 0 && !f.session.ScenarioNavigation() &&
                engine.allocations == allocations && engine.privateBlocks.size() == privateBlocks,
                "a later bad model prevents every model precache, model publication, NAV and monster allocation");
            Check(!f.session.Activate() && f.session.State() == CSOLScenarioSessionState::Failed &&
                f.session.Error().path == rejectedPath && edges.precaches.empty(),
                "a rejected model file cannot continue into Session activation");
        }
        else
        {
            Check(loaded && f.session.State() == CSOLScenarioSessionState::Precached &&
                f.session.Models().Ready() && f.session.Monsters().RegisteredCount() == 45 &&
                f.session.ScenarioNavigation(),
                "nonempty fixed and catalog model fixtures allow the actual Combat model, NAV and pool stages");
            // Pool Spawn legitimately precaches its initial origin model again.
            Check(edges.precaches.size() >= 2 && edges.precaches[0] == host && edges.precaches[1] == speed,
                "the successful Session precaches both authored catalog models before pool creation");
        }
    }
}

struct BossSkillSessionEdges
{
    inline static BossSkillSessionEdges* active{};
    enginefuncs_t saved{g_engfuncs};
    std::function<void(char)> edge;
    void* buffer{};
    int loads{}, frees{};
    std::vector<char> order;
    BossSkillSessionEdges()
    {
        Require(!active,"one SK engine callback wrapper"); active = this;
        g_engfuncs.pfnLoadFileForMe = [](const char* path,int* length)
        {
            auto& self = *active;
            auto* bytes = self.saved.pfnLoadFileForMe(path,length);
            if (std::strcmp(path,BossSkillPath) == 0)
            {
                ++self.loads; self.buffer = bytes; self.order.push_back('l');
                const auto callback = self.edge; if (callback) callback('l');
            }
            return bytes;
        };
        g_engfuncs.pfnFreeFile = [](void* data)
        {
            auto& self = *active;
            const bool skills = self.buffer && data == self.buffer;
            if (skills)
            {
                ++self.frees; self.order.push_back('f');
                const auto callback = self.edge; if (callback) callback('f');
                self.buffer = nullptr;
            }
            self.saved.pfnFreeFile(data);
        };
    }
    ~BossSkillSessionEdges() { g_engfuncs = saved; active = nullptr; }
};

void SessionBossSkills(Engine& engine)
{
    // Selection does not request SK; Combat distinguishes missing/zero from
    // a nonempty malformed file and publishes only after its release edge.
    for (int variant=0;variant<5;++variant)
    {
        SessionResourceFixture f(engine); BossSkillSessionEdges edges;
        const bool selection = variant == 0;
        if (variant == 0 || variant == 4) engine.files[BossSkillPath] = "bad";
        if (variant == 2) engine.files[BossSkillPath] = "";
        if (variant == 3) engine.files[BossSkillPath] = difficulty_test::Encrypt(BossSkillCsv);
        bool busyObserved{};
        edges.edge = [&](char)
        {
            const auto state = f.session.State();
            Check(!f.session.BossSkills() && f.session.Busy() &&
                !f.session.Precache("map_session_test") && !f.session.Activate() && !f.session.Deactivate() &&
                f.session.State() == state, "real Session remains busy and exposes no SK snapshot through load/release callbacks");
            busyObserved = true;
        };
        const bool loaded = f.session.Precache("map_session_test",1,
            selection ? CSOLScenarioResourceScope::Selection : CSOLScenarioResourceScope::Combat);
        edges.edge = {};
        Check(loaded == (variant != 4) && edges.loads == (selection ? 0 : 1) &&
            edges.frees == (selection || variant == 1 ? 0 : 1) && engine.buffers.empty(),
            "actual Selection/Combat scope preserves missing/zero/valid/malformed SK distinctions and buffer ownership");
        if (selection)
            Check(!f.session.BossSkills() && !busyObserved && f.session.Monsters().RegisteredCount() == 0,
                "Selection skips even a malformed SK and does not create the combat pool");
        else if (variant == 4)
            Check(f.session.State() == CSOLScenarioSessionState::Failed && !f.session.BossSkills() &&
                f.session.Error().path == BossSkillPath && f.session.Monsters().RegisteredCount() == 0,
                "nonempty bad SK fails Combat before monster pool creation");
        else
        {
            const auto skills = f.session.BossSkills();
            Require(skills && skills->Table().rows.size() == (variant == 3 ? 1u : 0u),
                "successful Combat owns the current parsed or explicitly empty shared SK table");
            Check(f.session.Monsters().RegisteredCount() == 45 && busyObserved,
                "successful SK stage precedes the actual 45-slot combat pool");
            if (variant == 3)
            {
                csol::scenario::BossSkillClocks clocks;
                Require(skills->CommitSelection(0,10,clocks) && skills->CacheSequence(0,7),
                    "Session borrower owns shared cooldown and animation cache");
                Check(f.session.BossSkills() == skills && f.session.Monsters().RegisteredCount() == 45 &&
                    skills->State(0)->cooldownDeadline == 13 && skills->State(0)->sequence == 7,
                    "before Deactivate the Session owns SK state beside its registered pool");
                // UTIL_Remove marks FL_KILLME; it does not synchronously free
                // engine private data. Observe only this API's visible state.
                const bool cleared = f.session.Deactivate();
                Check(cleared && !f.session.BossSkills() && skills->State(0)->cooldownDeadline == 13 &&
                    skills->State(0)->sequence == 7,
                    "Deactivate clears SK publication while explicit old snapshot remains accessible");
                Require(f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Combat),
                    "same Session can load a fresh SK map generation after orderly teardown");
                Check(f.session.BossSkills() != skills && f.session.BossSkills()->State(0)->sequence == -1 &&
                    f.session.BossSkills()->State(0)->cooldownDeadline == 0,
                    "reload creates independent row state without resetting the retained old generation");
            }
        }
    }
    for (const char boundary : {'l','f'})
    {
        SessionResourceFixture f(engine); BossSkillSessionEdges edges;
        engine.files[BossSkillPath] = difficulty_test::Encrypt(BossSkillCsv);
        int cancellations{};
        edges.edge = [&](char at)
        {
            if (at != boundary || cancellations) return;
            ++cancellations; f.session.RevokeEntityAccess();
            Check(!f.session.Map().EntityAccessAllowed() && f.session.Busy() && !f.session.Deactivate(),
                "actual SK callback revokes Session access immediately while orderly cleanup stays busy");
        };
        Check(!f.session.Precache("map_session_test",1,CSOLScenarioResourceScope::Combat) &&
            cancellations == 1 && edges.loads == 1 && edges.frees == 1 && engine.buffers.empty() &&
            !f.session.BossSkills() && f.session.Monsters().RegisteredCount() == 0 &&
            f.session.Error().detail.message.empty(),
            "SK load/release revocation cancels actual Combat before publication and pool creation without a fabricated resource error");
        edges.edge = {};
    }
}
}

void ScenarioDeathServicesCases(sdk_test::Engine& engine)
{
    GoldenCases(); RootSourceCases(engine); RootSkillCases(engine); ProviderMethods(engine);
    ProviderGateAndLifecycle(engine); ProviderPrecacheCancellation(engine); RegisteredRulesPrecache(engine);
    ShutdownStatisticsLifecycle(engine); ShutdownArchiveAfterRevocation(engine); ShutdownArchiveFailuresAndEmpty(engine);
    ShutdownRuntimeFinalization(engine);
    ShutdownArchiveDuringPrecache(engine);
    RegisteredRulesRoundSuccess(engine); RegisteredRulesRoundThreshold(engine);
    RegisteredRulesRoundReentry(engine); RegisteredRulesRoundCancellation(engine); RegisteredRulesRoundMessageFailure(engine);
    RegisteredRulesFinishChoice(engine); RegisteredRulesFinishFallback(engine); RegisteredRulesFinishReentry(engine);
    RegisteredRulesFinishCancellation(engine); RegisteredRulesFreePlayEligibility(engine);
    RegisteredRulesFinalTextReentry(engine); RegisteredRulesFinalTextCancellation(engine);
    RegisteredRulesFinalTextFailure(engine);
    SessionModelFiles(engine); SessionBossSkills(engine);
}
