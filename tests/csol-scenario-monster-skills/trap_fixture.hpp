// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_monster_skills.h"
#include "../../native/csol/server/scenario_monster_factory.h"
#include "../../native/csol/server/scenario_monster_resources.h"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/characters.h"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_session.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../csol-scenario-navigation-loader/support.hpp"
#include "../../native/csol/scenario_kill.hpp"
#include "../../native/csol/scenario_healing.hpp"
#include "../../native/csol/scenario_smoke.hpp"
#include "../../native/csol/scenario_upgrades.hpp"
#include "../../native/csol/scenario_abilities.hpp"
#include "../../native/csol/scenario_monster_effects.hpp"

// Test-only read-only registry observation, appended to the isolated rules TU.
// Production installation, session startup and retirement remain unchanged.
std::shared_ptr<CCSOLScenarioRuntime> CSOL_TestScenarioRuntimeForAccess(const CSOLScenarioEntityAccess*);

namespace trap_test
{
using namespace sdk_test;
using csol::scenario::ConfigError;

inline std::map<std::string, std::string> Resources(float resistance)
{
    using difficulty_test::Encrypt;
    auto files = session_test::Resources();
    files["maps/zbs_map_session_test.cso"] = Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes\r\n1,1\r\n");
    files["maps/ZBS_ZT_map_session_test.cso"] = Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,3,255 255 255,0,0,0,0,7,1\r\n");
    files["maps/ZBS_DropItem.cso"] = Encrypt("id,diff,effect,p,owner,count,remove,reward\r\n7,1,0,100,0,1,5,3000\r\n");
    std::string difficulty = "header\r\n";
    for (int i = 0; i < 64; ++i) { if (i) difficulty += ','; difficulty += '1'; }
    files["maps/ZBS_Difficulty_Default.cso"] = Encrypt(difficulty + "\r\n");
    std::string levels = "level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for (int i = 0; i <= 100; ++i) levels += std::to_string(i) + ",100,0,1,1,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"] = Encrypt(levels);
    files["resource/ZBSVER2/ZBS_class.cso"] = Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    files["resource/buff/BuffWeapon.cso"] = Encrypt("header\r\n");
    files["resource/FloatingDamage/FloatingDamageInfo.jsoo"] = Encrypt(R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})");
    std::string addons = R"({"ZBS_Addon":{"Addons":[)";
    for (int i = 0; i < 13; ++i) { if (i) addons += ','; addons += "{}"; }
    addons += ",{\"ItemID\":13,\"Params\":{\"Percent\":" + std::to_string(resistance) +
        ",\"PercentAdd\":5}}]},\"zbs_add_attribute\":{\"restrictWeaponDrop\":{\"default\":{\"Difficulty\":7}}}}";
    files["maps/ZBS.json"] = std::move(addons);
    files["resource/item.csv"] = ";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n";
    files["resource/defaultitems.csv"] = "Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"] = Encrypt("id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"] = Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    csol::scenario::NavigationFile nav;
    nav.areas = {nav_loader_test::Area(1, 0, 0, 0, 4096)};
    files["maps/map_session_test.nav"] = nav_loader_test::Encode(nav);
    // Exact authored existence fixtures, not a permissive filesystem fallback.
    // The real Session validates this closure; actual MDL/image/audio decoding
    // is outside this controlled-engine component suite.
    for (const auto& image : csol::scenario_kill::Resources) files[image.path] = "fixture";
    for (const auto& image : csol::upgrades::Resources) files[image.path] = "fixture";
    for (const char* image : csol::abilities::DownloadResources) files[image] = "fixture";
    for (const char* sprite : csol::healing::Sprites) files[sprite] = "fixture";
    for (const char* path : {csol::upgrades::FontPath, csol::upgrades::DigitSpritePath,
        CSOLScenarioInitialMonsterModel, csol::smoke::Sprite, "events/createsmoke.sc",
        CSOLMonsterTrapModel, CSOLMonsterDisarmModel, CSOLMonsterDisarmTrail,
        csol::monster_effects::ImpactSprite, "sprites/320_pain.spr", "sprites/640_pain.spr"}) files[path] = "fixture";
    for (const char* sound : {csol::upgrades::SoundPath, "vox/roundclear.wav",
        "zombi/Heavy_newTrap.wav", "zombi/deimos_skill_hit.wav",
        "zombi/Zombi_Trapped.wav", "zombi/Zombi_Trapped_Female.wav",
        csol::monster_effects::ImpactSound}) files[std::string("sound/") + sound] = "fixture";
    files["models/csol/characters.json"] = R"({"version":1,"characters":[{"id":1,"team":2,"skin":0,"scenarioClass":0,"male":true,"allowPlayerStyle":false,"handStyle":0,"name":"Fixture","model":"fixture","hand":"","portrait":"gfx/csol/p/fixture.tga"}]})";
    files["shader/csol/characters.txt"] = "models/player/fixture/fixture.mdl\ngfx/csol/p/fixture.tga\n";
    files["models/player/fixture/fixture.mdl"] = "fixture";
    files["gfx/csol/p/fixture.tga"] = "fixture";
    files["bgm/Scenario_Ready.mp3"] = "fixture-bgm";
    return files;
}

struct Waves final : csol::scenario::TriggerWaveEvents
{
    bool Check(ConfigError&) const override { return true; }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override {}
};
struct NoDeath final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&, const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("trap component fixture does not execute monster deaths"); }
};

struct Fixture
{
    inline static Fixture* active{};
    struct Registration { cvar_t* variable; std::string value; };
    inline static std::map<std::string, Registration> registrations;
    // The controlled engine owns a 1 MiB string table plus all edicts. Keep
    // this test world off the normal 1 MiB Windows thread stack.
    std::unique_ptr<Engine> engineStorage{std::make_unique<Engine>()};
    Engine& engine{*engineStorage};
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    NoDeath noDeath;
    CCSOLMonsterDeathRequests deaths{noDeath};
    Waves waves;
    CCSOLScenarioMonster* monster{};
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 2> sequence{}; } model;
    std::function<void()> onRandom;
    std::function<void(CBasePlayer&)> onReset;
    std::function<void(int)> onMessage;
    int randomValue{99}, randomCalls{}, resetCalls{};
    int directionMessages{};
    bool hooks{};

    explicit Fixture(float resistance = 0,
        std::function<void(std::map<std::string, std::string>&)> configure = {})
    {
        Require(!active, "one trap SDK fixture"); active = this;
        // GameDLL cvars register once for the module lifetime. A new test
        // engine must rebind those real registrations to its own string
        // storage before BeginMap; never leave pointers into the last engine.
        for (const auto& [name, entry] : registrations)
        {
            engine.Var(name.c_str()).registered = entry.variable;
            engine.Set(name.c_str(), entry.value.c_str());
        }
        engine.files = Resources(resistance);
        if (configure) configure(engine.files);
        engine.globals.time = 10; engine.Prepare("2");
        // Every case owns a new controlled engine message registry. Reset the
        // native registration latch, then use the actual complete SDK writer.
        gmsgCurWeapon = 0;
        LinkUserMessages();
        Require(gmsgShake >= 64 && gmsgShake <= 255, "native shake message is registered in this fixture engine");
        CSOL_PrecacheCharacters();
        g_pGameRules = InstallGameRules();
        Require(g_pGameRules && !CSOL_RulesRejected(), "real scenario rules are installed");
        {
            CSOLScenarioSdkScope scope;
            runtime = CSOL_TestScenarioRuntimeForAccess(scope.EntityAccess().get());
            Require(scope.Current() && runtime, "runtime comes from actual rules registration");
        }
        session = runtime->session;
        engine.onTrace = [](edict_t*, TraceResult& trace)
        { trace.flFraction = .5f; trace.vecEndPos = Vector(0, 0, 0); };
        const bool precached = session->Precache("map_session_test", 1, CSOLScenarioResourceScope::Combat);
        if (!precached) std::cerr << "SESSION " << session->Error().path << " " << session->Error().detail.message << '\n';
        Require(precached, "real session loads authored trap fixture resources");
        for (const char* name : {"playerspawn", "zombiespawn", "zbstrigger"})
            for (const int id : {1, 2, 30})
            {
                if (std::strcmp(name, "zbstrigger") && id != 1) continue;
                auto* ed = Engine::Create(Engine::String(name));
                if (auto* p = dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 2; }
                if (auto* p = dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 1; p->m_iMaxSpawn = 5; }
                if (auto* p = dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))) { p->m_iId = id; ed->v.model = Engine::String("*1"); }
                Require(engine.api.pfnSpawn(ed) == 0, "marker uses real DispatchSpawn");
                ed->v.mins = Vector(-8, -8, -8); ed->v.maxs = Vector(8, 8, 8);
            }
        Require(session->Activate(), "real session activates map and pool");
        players = runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(), "real player runtime activates");
        Require(session->BeginRound(0, 10, 0, waves), "real map and round identity established");
        const auto* type = session->Map().Tables()->monsterTypes.Find(1);
        Require(type, "actual parser resolves fixture monster type");
        monster = session->Monsters().Acquire(*type, 1, session->Map().SpawnPoints().Points());
        Require(monster, "real pool lends trap creator");
        monster->pev->health = 100; monster->pev->team = TERRORIST;
        model.header.numseq = 2; model.header.length = sizeof(model);
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequence.data()) - reinterpret_cast<char*>(&model));
        for (auto& sequence : model.sequence) { sequence.fps = 10; sequence.numframes = 11; sequence.flags = STUDIO_LOOPING; }
        g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { return &active->model; };
        g_engfuncs.pfnRandomLong = [](int32 low, int32 high) -> int32
        {
            auto& f = *active; ++f.randomCalls;
            Check(low == 0 && high == 99, "trap resistance uses inclusive 0..99");
            const int result = f.randomValue; const auto callback = f.onRandom;
            if (callback) callback(); return result;
        };
        g_engfuncs.pfnMessageBegin = [](int destination, int id, const float*, edict_t*)
        {
            auto& f = *active;
            f.engine.Record("message:" + std::to_string(destination) + ":" + std::to_string(id));
            const auto found = f.engine.messages.find(csol::monster_effects::Message);
            if (found != f.engine.messages.end() && found->second == id) ++f.directionMessages;
            const auto callback = f.onMessage; if (callback) callback(id);
        };
        g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.registerHook(Reset, 150); hooks = true;
    }
    static void Reset(IReGameHook_CBasePlayer_ResetMaxSpeed* chain, CBasePlayer* player)
    {
        auto& f = *active; ++f.resetCalls;
        chain->callNext(player);
        // These component cases keep the actual rules in Loading. Explicitly
        // invoke the real movement consumer after native ResetMaxSpeed; this
        // does not claim full Ready-phase movement-hook acceptance.
        if (f.players) f.players->ApplyMonsterTrapMovement(*player, f.engine.globals.time);
        const auto callback = f.onReset; if (callback) callback(*player);
    }
    CBasePlayer& Player(int index = 1, int addonLevel = 0)
    {
        auto* ed = engine.Reserve(index, "player");
        auto* player = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags = FL_CLIENT; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
        ed->v.gravity = .75f; ed->v.velocity = Vector(9, 8, 7);
        player->m_iTeam = CT; player->m_iNumSpawns = 1; player->m_bJustConnected = false;
        player->m_iJoiningState = JOINED; player->has_disconnected = false;
        // Public catalog-selection path supplies the gender required by real
        // TrapTouch. Restore the normal client flag after choosing the entry.
        ed->v.flags |= FL_FAKECLIENT;
        Require(CSOL_EnsureScenarioBotCharacter(player, session->Map().EntityAccess()), "real character catalog selects trap victim gender");
        ed->v.flags &= ~FL_FAKECLIENT;
        CSOLScenarioPlayerProfile profile; profile.equipment.classModsComplete = true;
        if (addonLevel) profile.equipment.addonLevels[13] = addonLevel;
        Require(players->SetProfile(*player, std::move(profile)), "actual player generation registered");
        return *player;
    }
    EHANDLE Trap()
    {
        auto call = CSOLMonsterSpawnCall(session->Map(), session->Monsters(), deaths, *monster);
        const int begin = engine.nextEntity;
        ConfigError error;
        Require(CSOL_CreateMonsterTrap(call, runtime, error), "complete real trap entity TU creates and spawns trap");
        CBaseEntity* found{};
        for (int i = begin; i < engine.nextEntity; ++i)
            if (auto* entity = CBaseEntity::Instance(Engine::At(i)); entity && FClassnameIs(entity->pev, "zombitrap")) found = entity;
        Require(found && !(found->pev->flags & FL_KILLME), "one active trap is visible via normal entity registry");
        found->pev->flags |= FL_ONGROUND;
        return EHANDLE(found);
    }
    void Touch(const EHANDLE& trap, CBasePlayer& player)
    { engine.api.pfnTouch(trap.Get(), player.edict()); }
    void Destroy(const EHANDLE& entity)
    { if (auto* ed = entity.Get()) Engine::Remove(ed); }
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> State(CBasePlayer& player)
    {
        const auto state = players->Find(EHANDLE(&player)); Require(state != nullptr, "current player state exists"); return state;
    }
    ~Fixture()
    {
        onRandom = {}; onReset = {}; onMessage = {};
        engine.onOffset = {}; engine.onIndexLookup = {}; engine.onFind = {}; engine.onTrace = {};
        engine.onModel = {}; engine.onOrigin = {}; engine.onSize = {}; engine.onSound = {}; engine.afterFreeCallback = {};
        if (hooks) g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.unregisterHook(Reset);
        // Real engine free callback owns OnDestroy. It does not call the C++
        // entity destructor; this is essential to the shared_ptr leak cases.
        engine.Clear(); players.reset(); session.reset(); runtime.reset();
        for (const auto& [name, entry] : engine.cvars)
            if (entry.registered) registrations[name] = {entry.registered, entry.text};
        active = nullptr;
    }
};
}
