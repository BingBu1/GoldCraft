// SPDX-License-Identifier: GPL-3.0-or-later
// Preparation only. Link the complete matched SDK and actual monster/death
// services. DamageProbe throws on EVERY method: positive cases establish only
// construction/registration and never claim an implemented damage/AI chain.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_monster_services.h"
#include "../../native/csol/server/scenario_death_services.h"
#include "../../native/csol/server/scenario_wave_services.h"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_gameplay.h"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/scenario_monster_effects.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../csol-scenario-navigation-loader/support.hpp"

namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using csol::scenario::ResourceError;
using csol::scenario::GoldenConfiguration;
constexpr const char* GoldenPath = "maps/ZBS_GoldenZombie_dedi.json";

std::map<std::string, std::string> Resources(const std::string& mapName, int mapId)
{
    using difficulty_test::Encrypt;
    std::map<std::string, std::string> files;
    for (const auto& entry : session_test::Resources())
    {
        auto path = entry.first;
        const auto offset = path.find("map_session_test");
        if (offset != std::string::npos) path.replace(offset, std::strlen("map_session_test"), mapName);
        files.emplace(std::move(path), entry.second);
    }
    std::vector<std::string> header(59), row(59, "0");
    header[0] = ";map_id"; header[1] = "map_name"; header[2] = "map_filename"; header[58] = "SubType";
    row[0] = std::to_string(mapId); row[1] = "fixture"; row[2] = mapName; row[58] = "1";
    std::string catalog;
    for (const auto& fields : {header, row})
    {
        for (std::size_t i = 0; i < fields.size(); ++i) { if (i) catalog += ','; catalog += fields[i]; }
        catalog += "\r\n";
    }
    files["resource/maplist.csv"] = std::move(catalog);
    files["maps/zbs_" + mapName + ".cso"] = Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes\r\n1,1\r\n");
    files["maps/ZBS_ZT_" + mapName + ".cso"] = Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,1,0,0,7,1\r\n");
    files["maps/ZBS_DropItem.cso"] = Encrypt("id,diff,effect,p,owner,count,remove,reward\r\n7,1,0,100,0,1,5,3000\r\n");
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
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    files["resource/defaultitems.csv"] = "Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"] = Encrypt("id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"] = Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    csol::scenario::NavigationFile nav;
    nav.areas = {nav_loader_test::Area(1, 0, 0, 0, 4096)};
    files["maps/" + mapName + ".nav"] = nav_loader_test::Encode(nav);
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
            Require(engine.api.pfnSpawn(ed) == 0, "monster service fixture uses actual marker DispatchSpawn");
            ed->v.mins = Vector(-8, -8, -8); ed->v.maxs = Vector(8, 8, 8);
        }
}

struct DamageProbe final : CCSOLMonsterDamageServices
{
    mutable int calls{};
    [[noreturn]] void Unexpected() const
    { ++calls; throw std::runtime_error("construction-only damage probe must never consume a damage stage"); }
    const bool& DamageSuppressed() const override { Unexpected(); }
    bool WeaponEffectsBlocked21F2(CSOLMonsterDamageCall&) const override { Unexpected(); }
    bool WeaponEffectsBlocked34F0(CSOLMonsterDamageCall&) const override { Unexpected(); }
    bool RulesAfterAttackDamage(CSOLMonsterDamageCall&, int, ConfigError&) override { Unexpected(); }
    bool BeforeHitReward(CSOLMonsterDamageCall&, CSOLMonsterHitAccount&, ConfigError&) override { Unexpected(); }
    bool AfterHitReward(CSOLMonsterDamageCall&, ConfigError&) override { Unexpected(); }
    bool AfterHealthDamage(CSOLMonsterDamageCall&, ConfigError&) override { Unexpected(); }
};

// Explicit callback observation for BeginRound only. These are not production
// wave consumers, and no Factory::Spawn/Update/goal/reward path runs here.
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
    int changes{};
    void WaveActivated(std::uint16_t) override { throw std::runtime_error("no wave activation in assembly cases"); }
    void BeforeSpatialWaveEnd(std::uint16_t) override { throw std::runtime_error("no spatial wave completion in assembly cases"); }
    void WavesChanged(std::span<const std::uint16_t>) override { ++changes; }
    void GoalReached() override { throw std::runtime_error("no ready combat rules in assembly cases"); }
};

struct EffectState
{
    int notified{}, destroyed{};
    std::function<void()> onNotify, onDestroy;
};
struct EffectLifetime
{
    std::shared_ptr<EffectState> state;
    explicit EffectLifetime(std::shared_ptr<EffectState> value) : state(std::move(value)) {}
    ~EffectLifetime()
    {
        ++state->destroyed;
        try { const auto action = state->onDestroy; if (action) action(); }
        catch (...) { Check(false, "effect destructor must not throw through actual ClearAll"); }
    }
};

struct Fixture
{
    Engine& engine;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioDeathServices> deaths;
    std::shared_ptr<CCSOLScenarioMonsterServices> services;
    std::shared_ptr<CCSOLScenarioGameplay> gameplay;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<DamageProbe> damage = std::make_shared<DamageProbe>();
    std::vector<std::shared_ptr<EffectState>> effects;
    Waves waves;
    CCSOLScenarioMonster* monster{};

    explicit Fixture(Engine& e, CSOLScenarioResourceScope scope = CSOLScenarioResourceScope::Combat,
        const std::string& mapName = "zs_deadend", int mapId = 98) : engine(e)
    {
        e.Clear(); e.files = Resources(mapName, mapId); e.globals.time = 10;
        e.globals.mapname = Engine::String(mapName.c_str()); e.Prepare("0");
        g_pGameRules = InstallGameRules();
        Require(g_pGameRules && !CSOL_RulesRejected(), "native rules host an independent real Runtime");
        e.onTrace = [](edict_t*, TraceResult& trace) { trace.flFraction = 0.5f; trace.vecEndPos = Vector(0, 0, 0); };
        runtime = std::make_shared<CCSOLScenarioRuntime>(); session = runtime->session;
        Require(session->Precache(mapName, 1, scope), "actual Session prepares authored map resources");
        deaths = runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(runtime);
        services = runtime->monsterServices = std::make_shared<CCSOLScenarioMonsterServices>(runtime, session, deaths);
        runtime->monsterDamageServices = damage;
    }
    void LoadEmptyGolden(const char* bytes = nullptr)
    {
        if (bytes) engine.files[GoldenPath] = bytes;
        else engine.files.erase(GoldenPath);
        ResourceError error;
        Require(deaths->Precache({Engine::Load, Engine::Free}, error), "actual death provider loads an empty Golden plan");
        Require(deaths->Golden().EmptyPlan(), "missing/empty is a loaded real plan, not Unloaded");
    }
    void Assemble()
    {
        ConfigError error;
        Require(services->Assemble(damage, error), "actual monster provider constructs actual Gameplay");
        gameplay = runtime->gameplay;
        Require(gameplay && !gameplay->Active(), "construction does not activate a combat session");
    }
    void ActivatePlayers()
    {
        players = runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(), "real PlayerRuntime installs equipment/upgrade/reward registries");
    }
    void Activate(bool withPlayers = true)
    {
        Require(gameplay != nullptr, "activation follows successful assembly");
        Markers(engine); Require(session->Activate(), "actual Session collects authored markers and navigation");
        if (withPlayers) ActivatePlayers();
        Require(gameplay->Activate(), "actual Gameplay initializes the occupancy and player spawn managers");
        Require(gameplay->BeginRound(0, engine.globals.time, 0, waves), "actual Gameplay/Session establish a round and death identity");
        const auto* type = session->Map().Tables()->monsterTypes.Find(1);
        Require(type != nullptr, "actual table has the fixture ordinary type");
        monster = session->Monsters().Acquire(*type, 1, session->Map().SpawnPoints().Points());
        Require(monster != nullptr, "actual monster pool lends a real entity");
        monster->pev->health = 100; monster->pev->deadflag = DEAD_NO;
    }
    void Ready(bool withPlayers = true) { LoadEmptyGolden(); Assemble(); Activate(withPlayers); }
    CSOLMonsterSpawnCall Call()
    { return CSOLMonsterSpawnCall(session->Map(), session->Monsters(), gameplay->Deaths().Requests(), *monster); }
    CBasePlayer* Player(int index, bool bind = true)
    {
        auto* ed = engine.Reserve(index, "player");
        auto* player = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags = FL_CLIENT; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
        player->m_iTeam = CT; player->m_iNumSpawns = 1; player->has_disconnected = false;
        if (bind) Require(players && players->SetProfile(*player, {}), "real player has an explicit empty profile");
        return player;
    }
    std::shared_ptr<EffectState> Effect(CSOLMonsterSpawnCall& call, CBasePlayer& source)
    {
        auto state = std::make_shared<EffectState>(); effects.push_back(state);
        auto lifetime = std::make_shared<EffectLifetime>(state);
        const CSOLMonsterEffectGuard guard{[](const void* context)
            { return static_cast<const CSOLMonsterSpawnCall*>(context)->Current(); }, &call};
        Require(monster->DamageEffects()->Install(guard, source.pev, 6, 3, 0, 725, -1, {}, [state, lifetime]
        {
            ++state->notified; const auto action = state->onNotify; if (action) action();
        }), "actual effects owner installs a player-indexed source record");
        return state;
    }
    ~Fixture()
    {
        for (const auto& state : effects) { state->onNotify = {}; state->onDestroy = {}; }
        engine.onOffset = {}; engine.onIndexLookup = {}; engine.onFind = {}; engine.onTrace = {};
        engine.onModel = {}; engine.onOrigin = {}; engine.onSize = {}; engine.onSound = {};
        if (players) players->Invalidate();
        if (!session->Map().EntityAccessAllowed())
            for (auto& ed : engine.edicts) if (ed.pvPrivateData) Engine::FreePrivate(&ed);
        if (gameplay) Check(gameplay->Deactivate(), "fixture deactivates actual Gameplay before releasing its borrowed resources");
        Check(session->Deactivate(), "fixture releases the actual Session");
        Check(damage->calls == 0, "construction-only spy never accepted a damage stage");
        engine.Clear();
        engine.globals.mapname = Engine::String("map_session_test");
    }
};

bool ProtectedThrows(const CCSOLScenarioMonsterServices& services)
{
    try { (void)services.ProtectedSlots(); }
    catch (const std::runtime_error&) { return true; }
    return false;
}

void GoldenAndAssemblyCases(Engine& engine)
{
    {
        Fixture f(engine); ConfigError error;
        Check(f.deaths->Golden().State() == GoldenConfiguration::Unloaded && ProtectedThrows(*f.services) &&
            !f.services->Assemble(f.damage, error) && !f.runtime->gameplay,
            "unloaded Golden cannot pretend to be an empty membership or assemble Gameplay");
        f.LoadEmptyGolden();
        Check(f.deaths->Golden().State() == GoldenConfiguration::Missing && ProtectedThrows(*f.services),
            "actually missing Golden becomes a loaded empty plan but still requires Gameplay assembly");
        f.Assemble();
        Check(f.services->ProtectedSlots().empty() && f.services->OwnerCurrent() && !f.damage->calls,
            "actual Missing plan supports empty membership after construction without executing damage");
        auto first = f.runtime->gameplay;
        Check(!f.services->Assemble(f.damage, error) && !error.message.empty() && f.runtime->gameplay == first,
            "second assembly is rejected without replacing the constructed Gameplay");
    }
    {
        Fixture f(engine); f.LoadEmptyGolden("{}"); f.Assemble();
        Check(f.deaths->Golden().State() == GoldenConfiguration::Empty && f.services->ProtectedSlots().empty(),
            "parsed empty Golden configuration uses the same concrete membership boundary");
    }
    {
        Fixture f(engine); engine.files[GoldenPath] = R"({"ZBS_GoldenZombie":{"monster_info":[{"id":1}]}})";
        ResourceError loadError; ConfigError error;
        Check(!f.deaths->Precache({Engine::Load, Engine::Free}, loadError) &&
            f.deaths->Golden().State() == GoldenConfiguration::Unsupported && !loadError.detail.message.empty(),
            "actual nonempty Golden configuration is explicitly unsupported");
        Check(ProtectedThrows(*f.services) && !f.services->Assemble(f.damage, error) && !f.runtime->gameplay,
            "unsupported configured Golden cannot create a successful empty consumer");
    }
    {
        Fixture f(engine, CSOLScenarioResourceScope::Selection); ConfigError error;
        Check(!f.services->Assemble(f.damage, error) && !f.runtime->gameplay,
            "Selection with untouched Golden cannot assemble combat Gameplay");
        // Explicit direct provider call, solely to prove the scope gate even
        // after loading. The rules Selection path must never perform this load.
        f.LoadEmptyGolden(); error = {};
        Check(!f.services->Assemble(f.damage, error) && !error.message.empty() && !f.runtime->gameplay,
            "even loaded empty Golden and a registered damage pointer cannot upgrade Selection to Combat");
    }
    for (int mode = 0; mode != 3; ++mode)
    {
        Fixture f(engine); f.LoadEmptyGolden(); ConfigError error;
        auto otherDamage = std::make_shared<DamageProbe>();
        if (mode == 1) f.runtime->monsterDamageServices.reset();
        std::shared_ptr<CCSOLMonsterDamageServices> requested = f.damage;
        if (mode == 0) requested.reset();
        if (mode == 2) requested = otherDamage;
        Check(!f.services->Assemble(requested, error) && !error.message.empty() && !f.runtime->gameplay && !otherDamage->calls,
            "null, absent registered, and different damage owners all reject assembly");
    }
    for (int owner = 0; owner != 4; ++owner)
    {
        Fixture f(engine); f.LoadEmptyGolden(); ConfigError error{0, "sentinel"};
        if (owner == 0) f.runtime->session = std::make_shared<CCSOLScenarioSession>();
        if (owner == 1) f.runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(f.runtime);
        if (owner == 2) f.runtime->monsterServices = std::make_shared<CCSOLScenarioMonsterServices>(f.runtime, f.session, f.deaths);
        if (owner == 3) f.runtime->RequestClose();
        Check(!f.services->Assemble(f.damage, error) && !f.runtime->gameplay && error.message == "sentinel" &&
            !f.services->OwnerCurrent() && ProtectedThrows(*f.services),
            "wrong session/death/monster owner and closing Runtime cancel without writing replacement diagnostics");
    }
    for (bool wrongMap : {false, true})
    {
        Fixture f(engine, CSOLScenarioResourceScope::Combat, wrongMap ? "map_session_test" : "zs_deadend", wrongMap ? 98 : 0);
        f.LoadEmptyGolden(); ConfigError error;
        Check(!f.services->Assemble(f.damage, error) && !error.message.empty() && !f.runtime->gameplay,
            "unsupported map and absent positive map ID cannot create a ready composition");
    }
    {
        Fixture f(engine); f.LoadEmptyGolden(); Markers(engine);
        Require(f.session->Activate(), "actual Session can activate before a mistaken late assembly");
        ConfigError error;
        Check(!f.services->Assemble(f.damage, error) && !error.message.empty() && !f.runtime->gameplay,
            "Gameplay assembly is limited to the Precached phase");
    }
}

void LateRegistriesAndBossCases(Engine& engine)
{
    Fixture f(engine); f.LoadEmptyGolden();
    Require(!f.session->Players() && !f.session->EquipmentState() && !f.session->HitRewards(),
        "precache has not installed player/equipment/reward registries");
    f.Assemble(); f.Activate(false); auto call = f.Call(); ConfigError error;
    Require(call.Current() == f.monster, "missing-registry rejection uses a current real monster lease");
    Check(!f.services->CreateOrdinary(call, error) && !error.message.empty(),
        "current ordinary creation refuses missing activation-time registries");
    f.ActivatePlayers(); error = {};
    auto ordinary = f.services->CreateOrdinary(call, error);
    Check(dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(ordinary.get()) && error.message.empty() && !f.damage->calls,
        "same provider constructed before activation creates actual ordinary behavior from the later registries");
    Check(!f.services->CreateBoss(call, 183, error) && error.message.find("183") != std::string::npos,
        "unsupported Boss retains its requested record in the diagnostic");
    CCSOLScenarioMap foreign;
    Require(foreign.BeginMap("zs_deadend"), "foreign actual Map reads the same authored tables");
    Markers(engine); // The first Session consumed its own BSP markers.
    Require(foreign.CollectEntities() &&
        foreign.BeginRound(0, engine.globals.time, 0, f.waves), "foreign actual Map can hold its own round");
    CSOLMonsterSpawnCall foreignCall(foreign, f.session->Monsters(), f.gameplay->Deaths().Requests(), *f.monster);
    Require(foreignCall.Current() != nullptr, "foreign call is current before the provider ownership guard");
    error = {0, "foreign sentinel"};
    Check(!f.services->CreateOrdinary(foreignCall, error) && !f.services->CreateBoss(foreignCall, 999, error) &&
        error.message == "foreign sentinel", "foreign map cannot use registries or write Boss diagnostics through this provider");
    Check(foreign.EndMap(), "foreign map releases only its own map resources");
    f.runtime->monsterServices = std::make_shared<CCSOLScenarioMonsterServices>(f.runtime, f.session, f.deaths);
    error = {0, "replacement sentinel"};
    Check(call.Current() && !f.services->OwnerCurrent() && !f.gameplay->Active() && !f.services->CreateOrdinary(call, error) &&
        !f.services->CreateBoss(call, 999, error) && error.message == "replacement sentinel",
        "provider replacement invalidates ordinary creation and Boss diagnosis even while the map lease stays current");
}

void ClearEffectCases(Engine& engine)
{
    for (int mode = 0; mode != 3; ++mode)
    {
        Fixture f(engine); f.Ready(); auto call = f.Call();
        auto* firstPlayer = f.Player(1); auto* secondPlayer = f.Player(2);
        const auto first = f.Effect(call, *firstPlayer), second = f.Effect(call, *secondPlayer);
        const auto effects = f.monster->DamageEffects();
        auto replacement = std::make_shared<CCSOLScenarioMonsterServices>(f.runtime, f.session, f.deaths);
        auto replace = [&] { f.runtime->monsterServices = replacement; };
        if (mode == 1) first->onNotify = replace;
        if (mode == 2) first->onDestroy = replace;
        auto& host = f.monster->HostState();
        host.nonlethalDamage = 5; host.damageScaleCount = 6; host.damageScale = 7;
        ConfigError error; const auto gameplay = f.runtime->gameplay;
        const bool cleared = f.services->ClearSpawnEffects(call, error);
        Check(cleared == (mode == 0) && first->notified == 1 && second->notified == (mode == 0 ? 1 : 0),
            "actual ClearAll stops after notify/destructor replaces the registered owner");
        Check(first->destroyed == (mode == 1 ? 0 : 1) && second->destroyed == (mode == 0 ? 1 : 0) &&
            effects->At(1).count == (mode == 1 ? 3 : 0) && effects->At(2).count == (mode == 0 ? 0 : 3),
            "cancellation preserves the precise unvisited records and never invokes a second source");
        Check(error.message.empty() && call.Current() == f.monster && f.runtime->gameplay == gameplay &&
            host.nonlethalDamage == 5 && host.damageScaleCount == 6 && host.damageScale == 7,
            "effect-only service does not duplicate factory host resets or mutate the replacement Gameplay");
        if (mode)
        {
            Check(f.runtime->monsterServices == replacement && !f.services->OwnerCurrent() && !f.gameplay->Active() && ProtectedThrows(*f.services),
                "retired provider cannot continue through an otherwise-live lease");
        }
        else
        {
            Check(f.services->ClearSpawnEffects(call, error) && first->notified == 1 && second->notified == 1,
                "actual already-empty records are safe to clear without repeating notifications");
        }
    }
}

void PlayerRetirementCases(Engine& engine)
{
    Fixture f(engine); f.Ready(); auto call = f.Call(); auto* player = f.Player(1);
    const EHANDLE handle(player); bool excluded = true; ConfigError error;
    Check(!f.services->PlayerTargetExcluded(call, handle, excluded, error) &&
        error.message == "scenario display ability directory is unavailable",
        "empty profile alone does not claim a known-absent feature3 directory");
    error = {};
    Require(f.players->SetDisplayAbilities(handle, std::make_shared<CSOLScenarioDisplayAbilities>()),
        "trusted fixture explicitly confirms an empty target ability directory");
    Check(f.services->PlayerTargetExcluded(call, handle, excluded, error) && !excluded && error.message.empty(),
        "ordinary target with a trusted empty display ability directory is permitted");
    f.players->Remove(handle);
    Require(!player->has_disconnected && !f.players->Find(handle) && f.players->WasRemoved(handle),
        "real profile removal precedes the SDK has_disconnected write");
    Check(f.services->PlayerTargetExcluded(call, handle, excluded, error) && excluded && error.message.empty(),
        "removed-but-not-yet-disconnected target is excluded without poisoning monster AI");
    Require(f.players->SetProfile(*player, {}), "same live handle registers a fresh real profile");
    Check(!f.services->PlayerTargetExcluded(call, handle, excluded, error) &&
        error.message == "scenario display ability directory is unavailable",
        "same-entity profile regeneration starts with unknown ability ownership");
    error = {};
    Require(f.players->SetDisplayAbilities(handle, std::make_shared<CSOLScenarioDisplayAbilities>()),
        "new player generation receives its own explicitly empty ability owner");
    Check(!f.players->WasRemoved(handle) && f.services->PlayerTargetExcluded(call, handle, excluded, error) && !excluded,
        "successful registration clears the removed marker and permits the new profile");
    auto* unknown = f.Player(2, false); error = {};
    Check(!f.services->PlayerTargetExcluded(call, EHANDLE(unknown), excluded, error) && !error.message.empty(),
        "never-registered live player still reports a missing profile instead of a fabricated safe exclusion");
    f.players->Remove(handle); Engine::Remove(player->edict());
    auto* reused = f.Player(1, false); const EHANDLE newHandle(reused); error = {};
    Check(!handle.Get() && !f.players->WasRemoved(newHandle) &&
        !f.services->PlayerTargetExcluded(call, newHandle, excluded, error) && !error.message.empty(),
        "old removed handle serial cannot exclude a different player reusing the same engine slot");
    error = {};
    Check(f.services->PlayerTargetExcluded(call, handle, excluded, error) && excluded && error.message.empty(),
        "stale entity handle is safely excluded without touching the replacement player");
    Require(f.players->SetProfile(*reused, {}), "slot replacement registers its own profile");
    Check(!f.services->PlayerTargetExcluded(call, newHandle, excluded, error) &&
        error.message == "scenario display ability directory is unavailable",
        "new entity serial does not inherit the old known-empty ability directory");
    error = {};
    Require(f.players->SetDisplayAbilities(newHandle, std::make_shared<CSOLScenarioDisplayAbilities>()),
        "replacement serial receives a separately trusted empty ability owner");
    Check(!f.players->WasRemoved(newHandle) && f.services->PlayerTargetExcluded(call, newHandle, excluded, error) && !excluded,
        "new serial's successful profile registration restores ordinary targeting");
}

struct TargetConditionProbe final : csol::scenario::Feature3Condition
{
    int* clones{};
    std::function<bool(ConfigError&)> action;
    bool Clone(std::unique_ptr<csol::scenario::Feature3Condition>& result, ConfigError& error) const override
    {
        if (clones) ++*clones;
        const auto callback = action;
        result = std::make_unique<TargetConditionProbe>();
        return !callback || callback(error);
    }
};

void PlayerFeature3CancellationCases(Engine& engine)
{
    for (int boundary = 0; boundary != 3; ++boundary)
    {
        Fixture f(engine); f.Ready(); auto call = f.Call(); auto* player = f.Player(1);
        const EHANDLE handle(player); ConfigError error; bool excluded{}; int clones{};
        const auto directory = std::make_shared<CSOLScenarioDisplayAbilities>();
        const auto feature = directory->feature3 = std::make_shared<csol::scenario::Feature3>();
        Require(f.players->SetDisplayAbilities(handle, directory), "AI fixture installs actual trusted Feature3 owner");
        const auto condition = std::make_shared<TargetConditionProbe>(); condition->clones = &clones;
        condition->action = [&](ConfigError& diagnostic)
        {
            if (boundary == 0)
                Require(feature->Put(7, {{0,0,0,9}, {}, {}}), "clone replaces current key with a new ended record");
            if (boundary == 1)
            {
                const auto replacement = std::make_shared<CSOLScenarioDisplayAbilities>();
                replacement->feature3 = std::make_shared<csol::scenario::Feature3>();
                Require(replacement->feature3->Put(7, {{0,0,0,9}, {}, {}}) &&
                    f.players->SetDisplayAbilities(handle, replacement), "clone installs a separate directory with an ended record");
            }
            if (boundary == 2) { diagnostic = {17, "controlled target clone failure"}; return false; }
            return true;
        };
        Require(feature->Put(7, {{0,0,0,9}, condition, {}}), "actual ended node is permitted unless its clone cancels or fails");
        const bool queried = f.services->PlayerTargetExcluded(call, handle, excluded, error);
        if (boundary == 2)
            Check(!queried && clones == 1 && error.byteOffset == 17 && error.message == "controlled target clone failure",
                "real clone diagnostic remains a target-query failure rather than a normal candidate exclusion");
        else
        {
            Check(queried && excluded && clones == 1 && error.message.empty() && call.Current() == f.monster,
                "live monster skips a candidate whose Feature3 node or directory retired without a diagnostic");
            Check(f.services->PlayerTargetExcluded(call, handle, excluded, error) && !excluded &&
                clones == 1 && error.message.empty(),
                "next query reads replacement all3 records and permits the target without cached exclusion");
        }
    }
}

struct WaveObserver final : CCSOLScenarioWaveObserver
{
    std::vector<std::int16_t> ids;
    std::function<bool(std::int16_t, ConfigError&)> action;
    bool WaveActivated(std::int16_t id, ConfigError& error) override
    {
        ids.push_back(id);
        return action ? action(id, error) : true;
    }
};

std::shared_ptr<CCSOLScenarioWaveServices> InstallWaves(Fixture& f)
{
    auto events = std::make_shared<CCSOLScenarioWaveServices>(f.runtime, f.session);
    f.runtime->waveEvents = events;
    return events;
}

void WaveEmptyAndOrderCases(Engine& engine)
{
    Fixture f(engine); f.Ready(); const auto events = InstallWaves(f);
    ConfigError error{23, "unchanged success sentinel"};
    Check(events->Check(error) && error.byteOffset == 23 && error.message == "unchanged success sentinel",
        "current real wave provider leaves a caller's diagnostic unchanged on success");
    const auto engineEvents = engine.events.size();
    const auto generation = f.session->Map().RoundGeneration();
    events->WaveActivated(1);
    const std::uint16_t active[] = {1, 1, 2};
    events->WavesChanged(active);
    error = {};
    Check(events->Check(error) && engine.events.size() == engineEvents &&
        f.session->Map().RoundGeneration() == generation && f.damage->calls == 0,
        "real empty ordinary observer list and dedicated summary require no fabricated packet or damage consumer");
    Check(events->Register({}) == 0 && !events->Unregister(0) && !events->Unregister(999),
        "null observer and unknown subscription cannot create or remove a real registration");
    auto first = std::make_shared<WaveObserver>(), second = std::make_shared<WaveObserver>();
    std::vector<int> order;
    first->action = [&](std::int16_t, ConfigError&) { order.push_back(1); return true; };
    second->action = [&](std::int16_t, ConfigError&) { order.push_back(2); return true; };
    const auto token1 = events->Register(first), token2 = events->Register(second);
    Require(token1 && token2 && token1 != token2, "real provider issues distinct ordered subscription tokens");
    Check(events->Register(first) == token1, "duplicate real observer is registered once");
    events->WaveActivated(0x8001u);
    Check(events->Check(error) && order == std::vector<int>({1, 2}) &&
        first->ids == std::vector<std::int16_t>({-32767}) && first->ids == second->ids,
        "actual provider preserves registration order and the original signed 16-bit wave argument");
    Check(events->Unregister(token1) && !events->Unregister(token1), "subscription is removed exactly once");
    events->WaveActivated(0xffffu);
    Check(order == std::vector<int>({1, 2, 2}) && second->ids.back() == -1 && first->ids.size() == 1,
        "unregistered observer does not receive another wave while later observer preserves signed minus one");
}

void WaveFailureCases(Engine& engine)
{
    // Cancellation, rejected diagnostic, accepted diagnostic, exception and
    // recursive dispatch must all stop before the second real subscription.
    for (int mode = 0; mode != 5; ++mode)
    {
        Fixture f(engine); f.Ready(); const auto events = InstallWaves(f);
        auto first = std::make_shared<WaveObserver>(), later = std::make_shared<WaveObserver>();
        first->action = [&](std::int16_t, ConfigError& error)
        {
            if (mode == 3) throw std::runtime_error("registered observer failure");
            if (mode == 4) { events->WaveActivated(2); return true; }
            if (mode) error = {14, "registered diagnostic"};
            return mode == 2;
        };
        Require(events->Register(first) && events->Register(later), "failure cases use actual provider subscriptions");
        events->WaveActivated(1);
        ConfigError error{91, "caller stale diagnostic"};
        Check(!events->Check(error) && first->ids == std::vector<std::int16_t>({1}) && later->ids.empty(),
            "failed or cancelled real dispatch preserves its first callback and stops every later callback");
        if (mode == 0)
            Check(error.message.empty(), "real observer cancellation clears a caller's stale diagnostic");
        else if (mode < 3)
            Check(error.byteOffset == 14 && error.message == "registered diagnostic",
                "observer diagnostic is retained even when its return value says accepted");
        else if (mode == 3)
            Check(error.message.find("threw an exception") != std::string::npos,
                "real provider catches observer exceptions and reports the service failure");
        else
            Check(error.message.find("reentered") != std::string::npos,
                "recursive dispatch is rejected without recursively invoking subscribers");
        first->action = {}; events->WaveActivated(2);
        Check(first->ids.size() == 1 && later->ids.empty() && events->Register(std::make_shared<WaveObserver>()) == 0,
            "a restored callback cannot revive a failed or cancelled real provider");
    }
}

void WaveSubscriptionMutationCases(Engine& engine)
{
    {
        Fixture f(engine); f.Ready(); const auto events = InstallWaves(f);
        auto first = std::make_shared<WaveObserver>(), removed = std::make_shared<WaveObserver>();
        auto retained = std::make_shared<WaveObserver>(), added = std::make_shared<WaveObserver>();
        std::vector<int> order;
        std::uint64_t removedToken{}, addedToken{};
        bool changed{};
        first->action = [&](std::int16_t, ConfigError&)
        {
            order.push_back(1);
            if (!changed)
            {
                changed = true;
                Check(events->Unregister(removedToken), "active callback unregisters a later snapshot entry");
                addedToken = events->Register(added);
                Check(addedToken != 0, "active callback registers a future subscriber");
            }
            return true;
        };
        removed->action = [&](std::int16_t, ConfigError&) { order.push_back(2); return true; };
        retained->action = [&](std::int16_t, ConfigError&) { order.push_back(3); return true; };
        added->action = [&](std::int16_t, ConfigError&) { order.push_back(4); return true; };
        Require(events->Register(first) != 0, "mutation case installs first subscriber");
        removedToken = events->Register(removed);
        Require(removedToken && events->Register(retained), "mutation case installs later subscribers");
        events->WaveActivated(1); ConfigError error;
        Check(events->Check(error) && order == std::vector<int>({1, 3}) && removed->ids.empty() && added->ids.empty(),
            "real dispatch honors removal before the next slot and defers new subscriptions");
        events->WaveActivated(2);
        Check(events->Check(error) && order == std::vector<int>({1, 3, 1, 3, 4}) &&
            added->ids == std::vector<std::int16_t>({2}) && removed->ids.empty(),
            "next dispatch receives the new subscriber in registration order without reviving removed entries");
    }
    {
        Fixture f(engine); f.Ready(); const auto events = InstallWaves(f);
        auto replacement = std::make_shared<CCSOLScenarioWaveServices>(f.runtime, f.session);
        auto first = std::make_shared<WaveObserver>(), later = std::make_shared<WaveObserver>();
        first->action = [&](std::int16_t, ConfigError& error)
        {
            f.runtime->waveEvents = replacement;
            error = {44, "retired observer diagnostic"};
            return false;
        };
        Require(events->Register(first) && events->Register(later), "replacement case uses the registered old provider");
        events->WaveActivated(1);
        ConfigError oldError{92, "old caller diagnostic"}, newError;
        Check(!events->Check(oldError) && oldError.message.empty() && replacement->Check(newError) &&
            newError.message.empty() && first->ids.size() == 1 && later->ids.empty(),
            "callback owner replacement cancels the old provider and cannot poison the new provider with retired errors");
        events->WaveActivated(2);
        Check(first->ids.size() == 1 && later->ids.empty() && events->Register(later) == 0,
            "strongly held retired provider cannot continue or acquire fresh subscribers");
        Require(replacement->Register(later) != 0, "current replacement accepts its own explicit subscription");
        replacement->WaveActivated(3);
        Check(replacement->Check(newError) && later->ids == std::vector<std::int16_t>({3}),
            "replacement remains usable without inheriting the old dispatch or diagnostic");
    }
}

void WaveBoundaryCases(Engine& engine)
{
    {
        Fixture f(engine); const auto events = InstallWaves(f); ConfigError error;
        Require(f.deaths->Golden().State() == GoldenConfiguration::Unloaded,
            "spatial negative case uses the actual unloaded Golden provider");
        events->BeforeSpatialWaveEnd(1);
        Check(!events->Check(error) && error.message.find("loaded Golden plan") != std::string::npos,
            "unloaded Golden cannot stand in for a loaded empty spatial-end consumer");
        f.LoadEmptyGolden();
        Check(!events->Check(error), "later Golden load does not revive the provider that already failed");
    }
    for (const char* bytes : {static_cast<const char*>(nullptr), "{}"})
    {
        Fixture f(engine); f.LoadEmptyGolden(bytes); const auto events = InstallWaves(f); ConfigError error;
        events->BeforeSpatialWaveEnd(1);
        Check(events->Check(error) && error.message.empty(),
            "actual Missing or Empty loaded Golden has no pending plan for spatial wave end to remove");
    }
    {
        Fixture f(engine); const auto events = InstallWaves(f); ConfigError error;
        auto observer = std::make_shared<WaveObserver>();
        Require(events->Register(observer) != 0, "pre-round provider can hold an explicit subscription");
        events->WaveActivated(1); error = {93, "stale pre-round diagnostic"};
        Check(!events->Check(error) && error.message.empty() && observer->ids.empty(),
            "activation outside a live round cancels without inventing an observer call or diagnostic");
    }
    {
        Fixture f(engine); f.Ready(); const auto events = InstallWaves(f); ConfigError error;
        auto observer = std::make_shared<WaveObserver>();
        Require(events->Register(observer) != 0, "goal case has a real unused wave subscriber");
        events->GoalReached();
        Check(!events->Check(error) && error.message.find("FinishRound") != std::string::npos && observer->ids.empty(),
            "incomplete real FinishRound is explicit and cannot be replaced by a wave observer broadcast");
    }
}
}

void ScenarioMonsterServicesCases(sdk_test::Engine& engine)
{
    GoldenAndAssemblyCases(engine);
    LateRegistriesAndBossCases(engine);
    ClearEffectCases(engine);
    PlayerRetirementCases(engine);
    PlayerFeature3CancellationCases(engine);
}

void ScenarioWaveServicesCases(sdk_test::Engine& engine)
{
    WaveEmptyAndOrderCases(engine);
    WaveFailureCases(engine);
    WaveSubscriptionMutationCases(engine);
    WaveBoundaryCases(engine);
}
