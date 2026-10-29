// SPDX-License-Identifier: GPL-3.0-or-later
// Full matched SDK component tests. Engine edges and explicit callback probes
// are controlled; no replacement PlayerRuntime, player layout or death service.
// These invoke individual consumers, not a completed first-wave death chain.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/scenario_death_owner.h"
#include "../../native/csol/server/scenario_death_services.h"
#include "../../native/csol/server/scenario_monster_damage.h"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_recovery_notice.h"
#include "../../native/csol/server/scenario_spawn_points.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/weapons.h"
#include "../../native/csol/server/characters.h"
#include "../csol-scenario-navigation-loader/support.hpp"
#include <utility>
#include <bit>
#include <cmath>
#include <limits>

namespace
{
using namespace sdk_test;
using csol::scenario::ConfigError;
using Phase = CSOLScenarioKillSourcePhase;

constexpr const char* ClassModFixtureCsv =
    "item,category,type,subtype,v1,v2,v3,v4,v5,v6,v7\r\n"
    "11,5,4,0,40,8,0,0,0,0,0\r\n12,1,2,0,0,0,0,0,0,0,0\r\n13,1,3,0,0,0,0,0,0,0,0\r\n"
    "14,5,8,9,1,2,3,4,5,6,7\r\n15,5,10,7,40,8,0,0,0,0,0\r\n16,5,9,5,0,0,0,0,0,0,0\r\n"
    "17,5,5,3,0,0,0,0,0,0,0\r\n65535,5,1001,-1,0,0,0,0,0,0,0\r\n";

std::string PresetItemRow(int id, const std::array<std::string, 5>& caps)
{
    std::vector<std::string> columns(26);
    columns[0] = std::to_string(id); columns[1] = "fixture-character"; columns[3] = "Character";
    columns[4] = "7"; columns[5] = "0";
    for (std::size_t i = 0; i < caps.size(); ++i) columns[21 + i] = caps[i];
    std::string row;
    for (std::size_t i = 0; i < columns.size(); ++i)
    { if (i) row += ','; row += '"'; row += columns[i]; row += '"'; }
    return row + "\r\n";
}

std::map<std::string, std::string> Resources(int monsterSkill = 0, bool displayMode = true, int ribbonMode = 0,
    bool presetCatalog = false)
{
    using difficulty_test::Encrypt;
    auto files = session_test::Resources();
    files["resource/buff/ClassMod.csv"] = ClassModFixtureCsv;
    files["maps/zbs_map_session_test.cso"] = Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes\r\n1,1\r\n");
    files["maps/ZBS_ZT_map_session_test.cso"] = Encrypt(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0," + std::to_string(monsterSkill) + ",255 255 255,0,0,0,0,7,1\r\n");
    files["maps/ZBS_DropItem.cso"] = Encrypt(
        "id,diff,effect,p,owner,count,remove,reward\r\n7,1,0,100,0,3,5,3000\r\n");
    std::string difficulty = "header\r\n";
    for (int i = 0; i < 64; ++i) { if (i) difficulty += ','; difficulty += '1'; }
    files["maps/ZBS_Difficulty_Default.cso"] = Encrypt(difficulty + "\r\n");
    std::string levels = "level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for (int i = 0; i <= 100; ++i) levels += std::to_string(i) + ",100,0,1,1,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"] = Encrypt(levels);
    files["resource/ZBSVER2/ZBS_class.cso"] = Encrypt("id,hp,damage,speed,armor,ammo,order\r\n");
    files["resource/buff/BuffWeapon.cso"] = Encrypt("header\r\n");
    files["resource/FloatingDamage/FloatingDamageInfo.jsoo"] = Encrypt(displayMode ?
        R"({"FLOATING_DMG_STYLE":{"MODE":[15]}})" : R"({"FLOATING_DMG_STYLE":{"MODE":[]}})");
    files["maps/ZBS.json"] = "{}";
    files["resource/item.csv"] = ";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n"
        "161,defaultKnife,defaultKnife,Equipment,11,4000\r\n31,HEGrenade,HEGrenade,Equipment,6,2003\r\n"
        "1036,AK47,AK47,Equipment,2,28\r\n10220,TKnife,TKnife,Equipment,2,220\r\n"
        "10144,Cannon,Cannon,Equipment,2,144\r\n";
    if (presetCatalog)
    {
        files["resource/item.csv"] += PresetItemRow(49, {"26,99", "23", "25", "22", "19"});
        files["resource/buff/ClassModPreset.csv"] = "ClassID,PresetID,slots\r\n49,73,12,13,14,15,65535\r\n";
    }
    files["resource/defaultitems.csv"] = "Class,Primary Weapon,Secondary Weapon,Knife,ThrowWeapon,???\r\n49,36,6,161,31,0\r\n";
    files["resource/AnnounceRibbon/announceribbon.cso"] = Encrypt(
        "id,name,unused,unused,priority,category\r\n1,fixture,0,0,1,8\r\n");
    files["resource/AnnounceRibbon/AnnounceCategory.cso"] = Encrypt("header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n");
    if (ribbonMode)
    {
        std::string rows = "id,name,unused,unused,priority,category\r\n";
        for (int row = 1; row <= 44; ++row)
            rows += std::to_string(row) + ",fixture,0,0,1,8\r\n";
        files["resource/AnnounceRibbon/announceribbon.cso"] = Encrypt(rows);
        files["resource/AnnounceRibbon/AnnounceCategory.cso"] = Encrypt(ribbonMode == 1 ?
            "header\r\n15,scenario,0,0,0,0,0,0,0,1\r\n" : "header\r\n15,scenario,0,0,0,0,0,0,0,0\r\n");
    }
    csol::scenario::NavigationFile nav;
    nav.areas = {nav_loader_test::Area(1, 0, 0, 0, 4096)};
    files["maps/map_session_test.nav"] = nav_loader_test::Encode(nav);
    return files;
}

void ClassModPrepareResources(std::map<std::string, std::string>& files)
{
    files["resource/item.csv"] += PresetItemRow(50, {"31", "30", "29", "28", "27"});
    files["resource/buff/ClassModPreset.csv"] += "50,91,16\r\n";
    files["resource/ZBSVER2/ZBS_class.cso"] = difficulty_test::Encrypt(
        "id,hp,damage,speed,armor,ammo,order\r\n49,20,20,20,20,20,hdsam\r\n50,20,20,20,20,20,hdsam\r\n");
    files["models/csol/characters.json"] = R"({"version":1,"characters":[
        {"id":149,"team":2,"skin":0,"male":true,"allowPlayerStyle":false,"handStyle":0,
         "scenarioClass":49,"name":"Fixture CT","model":"fixture_ct","hand":"","portrait":"gfx/csol/p/fixture_ct.tga"},
        {"id":150,"team":1,"skin":0,"male":false,"allowPlayerStyle":false,"handStyle":0,
         "scenarioClass":50,"name":"Fixture T","model":"fixture_t","hand":"","portrait":"gfx/csol/p/fixture_t.tga"}]})";
    files["shader/csol/characters.txt"] =
        "models/player/fixture_ct/fixture_ct.mdl\r\ngfx/csol/p/fixture_ct.tga\r\n"
        "models/player/fixture_t/fixture_t.mdl\r\ngfx/csol/p/fixture_t.tga\r\n";
    // Real character resource validation/selection; this SDK engine records
    // model precache identities. These bytes do not claim actual MDL rendering.
    for (const char* path : {"models/player/fixture_ct/fixture_ct.mdl", "gfx/csol/p/fixture_ct.tga",
        "models/player/fixture_t/fixture_t.mdl", "gfx/csol/p/fixture_t.tga"}) files[path] = "fixture";
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
    void GoalReached() override {}
};
struct UnusedExecutor final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&, const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("consumer cases must not claim full death execution"); }
};

struct Fixture
{
    Engine& engine;
    std::shared_ptr<CCSOLScenarioSession> session = std::make_shared<CCSOLScenarioSession>();
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    UnusedExecutor executor;
    CCSOLMonsterDeathRequests requests{executor};
    Waves waves;
    CBasePlayer* player{};
    CCSOLScenarioMonster* monster{};
    explicit Fixture(Engine& e, int monsterSkill = 0, bool displayMode = true, int ribbonMode = 0,
        bool presetCatalog = false, bool prepareCharacters = false,
        CSOLScenarioResourceScope scope = CSOLScenarioResourceScope::Combat,
        const std::function<void(std::map<std::string,std::string>&)>& resources = {},
        const std::function<void(const std::shared_ptr<CCSOLScenarioSession>&)>& onPrecached = {}) : engine(e)
    {
        e.Clear(); e.files = Resources(monsterSkill, displayMode, ribbonMode, presetCatalog); e.globals.time = 10; e.Prepare("0");
        if (prepareCharacters) { ClassModPrepareResources(e.files); CSOL_PrecacheCharacters(); }
        if (resources) resources(e.files);
        g_pGameRules = InstallGameRules(); Require(g_pGameRules && !CSOL_RulesRejected(), "real native rules host consumer cases");
        e.onTrace = [](edict_t*, TraceResult& trace) { trace.flFraction = 0.5f; trace.vecEndPos = Vector(0, 0, 0); };
        RequireSession(session->Precache("map_session_test", 1, scope));
        if (onPrecached) onPrecached(session);
        Markers(); RequireSession(session->Activate());
        players = std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate(), "real authority owns equipment, upgrades and catalog");
        player = Player(1);
        if (scope == CSOLScenarioResourceScope::Selection) return;
        RequireSession(session->BeginRound(0, e.globals.time, 0, waves));
        const auto* type = session->Map().Tables()->monsterTypes.Find(1);
        Require(type != nullptr, "authored resource parsed by production monster table");
        monster = session->Monsters().Acquire(*type, 1, session->Map().SpawnPoints().Points());
        Require(monster != nullptr, "real initialized pool lends a death lease");
    }
    void RequireSession(bool value) { Require(value, session->Error().detail.message.c_str()); }
    void Markers()
    {
        for (const char* name : {"playerspawn", "zombiespawn", "zbstrigger"})
            for (int id : {1, 2, 30})
            {
                if (std::strcmp(name, "zbstrigger") && id != 1) continue;
                auto* ed = CREATE_NAMED_ENTITY(Engine::String(name));
                if (auto* p = dynamic_cast<CCSOLPlayerSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 2; }
                if (auto* p = dynamic_cast<CCSOLMonsterSpawn*>(CBaseEntity::Instance(ed))) { p->m_iId = id; p->m_iTeam = 1; p->m_iMaxSpawn = 5; }
                if (auto* p = dynamic_cast<CCSOLScenarioTrigger*>(CBaseEntity::Instance(ed))) { p->m_iId = id; ed->v.model = Engine::String("*1"); }
                Require(engine.api.pfnSpawn(ed) == 0, "real marker DispatchSpawn");
                ed->v.mins = Vector(-8, -8, -8); ed->v.maxs = Vector(8, 8, 8);
            }
    }
    template<class T = CBasePlayer> T* Player(int slot, bool setProfile = true)
    {
        auto* ed = engine.Reserve(slot, "player");
        auto* result = GetClassPtr<CCSPlayer>(reinterpret_cast<T*>(&ed->v));
        ed->v.flags = FL_CLIENT; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
        result->m_iTeam = CT; result->m_iNumSpawns = 1; result->has_disconnected = false;
        if (setProfile) Require(players->SetProfile(*result, {}), "actual authority registers player generation");
        return result;
    }
    template<class T, class Wrapper = CCSEntity> T* Entity()
    {
        auto* ed = engine.Reserve(engine.nextEntity++, "death_tail_probe");
        return GetClassPtr<Wrapper>(reinterpret_cast<T*>(&ed->v));
    }
    template<class T> T* Weapon(int id)
    {
        auto* weapon = Entity<T, CCSPlayerWeapon>();
        weapon->m_iId = id; weapon->m_pPlayer = player;
        return weapon;
    }
    CSOLMonsterDeathCall Call(CBaseEntity* source = nullptr, std::uint8_t context20 = 0)
    {
        CSOLMonsterDeathArguments args; args.attacker = player; args.inflictor = source; args.context.bytes[20] = context20;
        return CSOLMonsterDeathCall(session->Map(), requests, *monster, args);
    }
    ~Fixture()
    {
        engine.onOffset = {}; engine.onIndexLookup = {}; engine.onFind = {}; engine.onTrace = {};
        engine.onModel = {}; engine.onOrigin = {}; engine.onSize = {}; engine.onSound = {};
        players->Invalidate(); Check(requests.Reset(), "retire consumer transaction generation");
        if (!session->Map().EntityAccessAllowed())
            for (auto& ed : engine.edicts) if (ed.pvPrivateData) Engine::FreePrivate(&ed);
        Check(session->Deactivate(), "consumer test closes real session"); engine.Clear();
    }
};

struct SourceState
{
    int isPlayerCalls{}, sourceCalls{};
    bool player{};
    std::vector<int> values{220, 144};
    std::function<void()> onPlayer, onSource;
};
struct SourceProbe final : CBaseEntity, CSOLWeaponDamageSource
{
    std::shared_ptr<SourceState> state = std::make_shared<SourceState>();
    BOOL IsPlayer() override
    {
        const auto keep = state; ++keep->isPlayerCalls;
        const bool value = keep->player; const auto callback = keep->onPlayer; if (callback) callback();
        return value;
    }
    int DamageWeaponId() const override
    {
        const auto keep = state; const auto index = keep->sourceCalls++;
        const int value = keep->values[std::min<std::size_t>(index, keep->values.size() - 1)];
        const auto callback = keep->onSource; if (callback) callback(); return value;
    }
    void OnDestroy() override { state.reset(); CBaseEntity::OnDestroy(); }
};

struct Component final : CSOLScenarioKillComponent
{
    std::function<bool(CSOLMonsterDeathCall&, int)> callback;
    std::function<bool(CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)> recovery;
    std::function<void()> released;
    ~Component() override
    {
        try { if (released) released(); }
        catch (...) { Check(false, "component destructor boundary must not throw"); }
    }
    bool MonsterKilled(CSOLMonsterDeathCall& call, std::int32_t source, ConfigError&) override
    { return callback ? callback(call, source) : true; }
    bool HealthRecovered(CSOLPlayerRecoveryCall& call, float amount,
        CSOLMonsterDeathContext& context, ConfigError& error) override
    {
        if (!recovery) throw std::runtime_error("this kill-only probe has no recovery callback");
        const auto action = recovery; return action(call, amount, context, error);
    }
};
struct WeaponState
{
    std::function<void(bool)> callback;
    int active{}, source{};
};
struct WeaponProbe final : CBasePlayerWeapon, CSOLWeaponMonsterKillCallbacks
{
    std::shared_ptr<WeaponState> state = std::make_shared<WeaponState>();
    void ActiveMonsterKilled(CCSOLScenarioMonster&, CBaseEntity*) override
    { const auto keep = state; ++keep->active; const auto fn = keep->callback; if (fn) fn(true); }
    void SourceMonsterKilled(CCSOLScenarioMonster&, CBaseEntity*) override
    { const auto keep = state; ++keep->source; const auto fn = keep->callback; if (fn) fn(false); }
    void OnDestroy() override { state.reset(); CBasePlayerWeapon::OnDestroy(); }
};

void SourceBranches(Engine& engine)
{
    Fixture f(engine); ConfigError error; CSOLScenarioKillSource out;
    auto* ak = f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem = ak;
    auto* other = f.Player(2); other->m_pActiveItem = f.Weapon<CKnife>(WEAPON_KNIFE);
    auto otherCall = f.Call(other);
    Check(f.players->ResolveKillSource(otherCall, Phase::Attribution, out, error) && out.weapon == 28 && out.item == 1036,
        "another player's IsPlayer selects ATTACKER active and actual catalog ID");
    other->pev->flags |= FL_SPECTATOR;
    Check(f.players->ResolveKillSource(otherCall, Phase::Attribution, out, error) && !out.weapon && !out.item,
        "native spectator flag selects verified player zero source");
    other->pev->flags &= ~FL_SPECTATOR; f.player->m_pActiveItem = nullptr;
    Check(f.players->ResolveKillSource(otherCall, Phase::Attribution, out, error) && !out.weapon,
        "native player without attacker active falls through to real zero source");
    auto* source = f.Entity<SourceProbe>(); const auto state = source->state; auto call = f.Call(source);
    for (const auto values : {std::vector<int>{0, 220}, std::vector<int>{220, 144}, std::vector<int>{220, 0}})
    {
        state->values = values; state->sourceCalls = state->isPlayerCalls = 0;
        Check(f.players->ResolveKillSource(call, Phase::Attribution, out, error) &&
            out.weapon == (values[0] ? values[1] : 0) && state->sourceCalls == (values[0] ? 2 : 1) && state->isPlayerCalls == 1,
            "source first read is predicate; second zero is preserved and actual calls counted");
    }
    state->player = true; state->values = {220, 144}; state->sourceCalls = state->isPlayerCalls = 0;
    Check(f.players->ResolveKillSource(call, Phase::Notification, out, error) && out.weapon == 144 && state->sourceCalls == 2,
        "IsPlayer true with missing active uses the explicit source interface twice");
    f.player->m_pActiveItem = ak; ak->m_iId = 0; state->sourceCalls = state->isPlayerCalls = 0;
    Check(f.players->ResolveKillSource(call, Phase::Attribution, out, error) && !out.weapon && !state->sourceCalls && state->isPlayerCalls == 1,
        "valid active ID zero is selected and does not fall through");
    ak->m_iId = 28; state->onPlayer = [&] { f.player->m_pActiveItem = nullptr; };
    Check(f.players->ResolveKillSource(call, Phase::Notification, out, error) && out.weapon == 144,
        "active is read after the virtual IsPlayer callback");
    state->onPlayer = {}; state->player = false; state->sourceCalls = state->isPlayerCalls = 0;
    auto gated = f.Call(source, 1);
    Check(f.players->ResolveKillSource(gated, Phase::Notification, out, error) && !out.weapon && !out.item &&
        !state->sourceCalls && !state->isPlayerCalls, "context20 skips all secondary source queries");
    Check(f.players->ResolveKillSource(gated, Phase::Attribution, out, error) && out.weapon == 144,
        "context20 does not gate first attribution");
    auto* knife = f.Weapon<CKnife>(999); auto knifeCall = f.Call(knife);
    Check(f.players->ResolveKillSource(knifeCall, Phase::Attribution, out, error) && out.weapon == 29 && out.item == 161,
        "native Knife fallback returns original constant29 rather than mutated item field");
    auto* he = f.Entity<CGrenade, CCSGrenade>(); Require(f.session->Weapons()->RecordHeGrenade(*he), "actual native HE registration");
    auto heCall = f.Call(he);
    Check(f.players->ResolveKillSource(heCall, Phase::Attribution, out, error) && out.weapon == 4 && out.item == 31,
        "registered HE uses session launch identity and selected catalog item");
    auto* unknown = f.Entity<CBaseEntity>(); auto unknownCall = f.Call(unknown); error = {};
    Check(!f.players->ResolveKillSource(unknownCall, Phase::Notification, out, error) && !error.message.empty(),
        "unverified entity is unsupported instead of fabricated source zero");
    auto nullCall = f.Call(); auto monsterCall = f.Call(f.monster); error = {};
    Check(f.players->ResolveKillSource(nullCall, Phase::Attribution, out, error) && !out.weapon &&
        f.players->ResolveKillSource(monsterCall, Phase::Attribution, out, error) && !out.weapon,
        "null and verified monster have actual zero-source contracts");
}

void SourceCancellation(Engine& engine)
{
    for (int stage : {0, 1, 2})
    {
        Fixture f(engine); auto* source = f.Entity<SourceProbe>(); const auto state = source->state;
        auto call = f.Call(source); CSOLScenarioKillSource out{77, 88}; ConfigError error;
        const EHANDLE old(source);
        const auto remove = [&, stage]
        {
            if ((stage == 0 && state->isPlayerCalls == 1) || (stage > 0 && state->sourceCalls == stage))
            { state->onPlayer = {}; state->onSource = {}; Engine::Remove(old.Get()); }
        };
        if (!stage) state->onPlayer = remove; else state->onSource = remove;
        Check(!f.players->ResolveKillSource(call, Phase::Attribution, out, error) && !out.weapon && !out.item &&
            !old.Get() && state->sourceCalls == stage, "each virtual boundary detects source deletion before another read or catalog use");
    }
    for (bool revoke : {false, true})
    {
        Fixture f(engine); auto* source = f.Entity<SourceProbe>(); const auto state = source->state;
        auto call = f.Call(source); CSOLScenarioKillSource out; ConfigError error;
        state->onSource = [&]
        {
            state->onSource = {};
            if (revoke) f.session->Map().RevokeEntityAccess();
            else { f.players->Remove(EHANDLE(f.player)); Require(f.players->SetProfile(*f.player, {}), "same entity receives a fresh authority generation"); }
        };
        Check(!f.players->ResolveKillSource(call, Phase::Notification, out, error) && state->sourceCalls == 1,
            "source callback generation replacement or access revocation cancels second read");
    }
}

void SourcePhaseIsolation(Engine& engine)
{
    for (bool zeroProbe : {true, false})
    {
        Fixture f(engine); auto* source = f.Entity<SourceProbe>();
        source->state->values = zeroProbe ? std::vector<int>{0, 220} : std::vector<int>{220, 0};
        auto call = f.Call(source); ConfigError error; CSOLScenarioKillSource out;
        f.session->Weapons()->Invalidate();
        const bool result = f.players->ResolveKillSource(call, Phase::Attribution, out, error);
        Check(result == zeroProbe && (zeroProbe ? error.message.empty() : !error.message.empty()),
            "zero probe skips unavailable catalog while second-read zero still requires catalog");
    }
    Fixture f(engine); auto* source = f.Entity<SourceProbe>(); auto call = f.Call(source);
    ConfigError error; CSOLScenarioKillSource first, second;
    source->state->values = {220, 144};
    Require(f.players->ResolveKillSource(call, Phase::Attribution, first, error), "first actual attribution sample");
    source->state->sourceCalls = 0; source->state->values = {144, 220};
    Require(f.players->ResolveKillSource(call, Phase::Notification, second, error), "later actual notification sample");
    Check(first.weapon == 144 && first.item == 10144 && second.weapon == 220 && !second.item,
        "two consumer positions independently sample current source and only first resolves catalog");
    // This intentionally constructs a valid transaction belonging to another
    // actual Map while both owners live. It is an API ownership test, not a
    // supported two-map server. No fabricated Current() or access token.
    CCSOLScenarioMap foreign;
    Require(foreign.BeginMap("map_session_test"), "foreign map loads actual authored resources");
    f.Markers(); Require(foreign.CollectEntities(), "foreign map collects actual engine markers");
    Require(foreign.BeginRound(0, engine.globals.time, 0, f.waves), "foreign map starts real round state");
    CSOLMonsterDeathArguments args; args.attacker = f.player; args.inflictor = source;
    CSOLMonsterDeathCall foreignCall(foreign, f.requests, *f.monster, args);
    Require(foreignCall.Current() != nullptr, "foreign call is independently current before authority rejection");
    csol::scenario::AnnounceCandidates announcements;
    Check(!f.players->ResolveKillSource(foreignCall, Phase::Notification, second, error) &&
        !f.players->ClassifyMonsterKill(foreignCall, 1, 1, announcements, error) &&
        !f.players->NotifyMonsterKill(foreignCall, 0, error) && f.players->KillPositions().empty(),
        "another current Map cannot consume this authority's source, classification or notification");
    Check(foreign.EndMap(), "foreign owner closes without replacing primary session");
}

void TailOrderAndRegistry(Engine& engine)
{
    Fixture f(engine); auto call = f.Call(); ConfigError error; std::vector<std::string> order;
    auto* active = f.Weapon<WeaponProbe>(70000); auto* next = f.Weapon<WeaponProbe>(70000);
    auto* chain = f.Weapon<WeaponProbe>(70000); const auto a = active->state, b = next->state, c = chain->state;
    f.player->m_pActiveItem = next;
    f.player->m_rgpPlayerItems[0] = active; f.player->m_rgpPlayerItems[1] = next;
    f.player->m_rgpPlayerItems[4] = active; active->m_pNext = chain;
    a->callback = [&](bool current) { order.emplace_back(current ? "active-a" : "source-a"); };
    b->callback = [&](bool current) { order.emplace_back(current ? "active-b" : "source-b"); };
    std::uint64_t firstToken{}, laterToken{}, addedToken{}; int addedCalls{}, released{};
    auto added = std::make_shared<Component>(); added->callback = [&](auto&, int) { ++addedCalls; return true; };
    auto later = std::make_shared<Component>(); later->callback = [&](auto&, int) { order.emplace_back("cancelled"); return true; };
    auto first = std::make_shared<Component>(); first->released = [&] { ++released; order.emplace_back("released-first"); };
    first->callback = [&](auto&, int id)
    {
        Check(id == 70000, "component receives full signed source before record narrowing"); order.emplace_back("component");
        f.player->m_pActiveItem = active;
        Check(f.players->RemoveKillComponent(EHANDLE(f.player), laterToken), "earlier callback cancels later token");
        addedToken = f.players->RegisterKillComponent(EHANDLE(f.player), added);
        Check(f.players->RemoveKillComponent(EHANDLE(f.player), firstToken), "component unregisters itself safely");
        return true;
    };
    firstToken = f.players->RegisterKillComponent(EHANDLE(f.player), first);
    laterToken = f.players->RegisterKillComponent(EHANDLE(f.player), later);
    Check(firstToken && firstToken == f.players->RegisterKillComponent(EHANDLE(f.player), first), "duplicate object registration is idempotent");
    first.reset(); later.reset();
    Require(f.players->NotifyMonsterKill(call, 70000, error), "actual registry and weapon tail complete");
    Check(order == std::vector<std::string>{"component", "released-first", "active-a", "source-a", "source-b", "source-a"} &&
        released == 1 && !addedCalls && !c->active && !c->source,
        "self destruction precedes active callback; six head slots retain duplicates and do not walk m_pNext");
    Check(f.players->KillPositions().size() == 1 && f.players->KillPositions()[0].source == static_cast<std::uint16_t>(70000),
        "full ID comparison precedes record ushort narrowing");
    Require(f.players->SetKillComponentEnabled(EHANDLE(f.player), addedToken, false), "disable existing component");
    Require(f.players->NotifyMonsterKill(call, 70000, error), "second disabled-component dispatch");
    Check(!addedCalls, "disabled component stays silent");
    Require(f.players->SetKillComponentEnabled(EHANDLE(f.player), addedToken, true), "reenable existing component");
    Require(f.players->NotifyMonsterKill(call, 70000, error), "third enabled-component dispatch");
    Check(addedCalls == 1, "new component first participates in a later enabled dispatch");
    const auto positions = f.players->KillPositions();
    const auto firstSequence = positions.front().sequence, lastSequence = positions.back().sequence;
    Check(!f.players->AcknowledgeKillPositions(lastSequence + 1) && f.players->AcknowledgeKillPositions(firstSequence) &&
        f.players->KillPositions().size() == 2 && !f.players->AcknowledgeKillPositions(firstSequence),
        "journal acknowledges only an existing prefix once");
    Check(f.players->AcknowledgeKillPositions(lastSequence) && f.players->KillPositions().empty(), "actual final prefix drains journal");
}

void TailCancellation(Engine& engine)
{
    for (bool inDestructor : {false, true})
    {
        Fixture f(engine); auto call = f.Call(); ConfigError error; std::uint64_t token{};
        auto* weapon = f.Weapon<WeaponProbe>(29); const auto weaponState = weapon->state; f.player->m_pActiveItem = weapon;
        auto component = std::make_shared<Component>();
        if (inDestructor) component->released = [&] { f.players->Invalidate(); };
        component->callback = [&](auto&, int)
        {
            if (inDestructor) f.players->RemoveKillComponent(EHANDLE(f.player), token); else f.players->Invalidate();
            return true;
        };
        token = f.players->RegisterKillComponent(EHANDLE(f.player), component); component.reset();
        Check(!f.players->NotifyMonsterKill(call, 29, error) && !weaponState->active && f.players->KillPositions().empty(),
            "component callback or self-removal destructor invalidation prevents weapons and journal");
    }
    for (bool sourceStage : {false, true})
    {
        Fixture f(engine); auto call = f.Call(); ConfigError error;
        auto* weapon = f.Weapon<WeaponProbe>(29); const auto state = weapon->state;
        f.player->m_pActiveItem = weapon; f.player->m_rgpPlayerItems[3] = weapon;
        state->callback = [&](bool active) { if (active != sourceStage) f.players->Remove(EHANDLE(f.player)); };
        Check(!f.players->NotifyMonsterKill(call, 29, error) && state->active == 1 && state->source == int(sourceStage) &&
            f.players->KillPositions().empty(), "active/source weapon callback player removal stops the remaining tail");
    }
    Fixture f(engine); auto call = f.Call(nullptr, 1); CSOLScenarioKillSource source; ConfigError error; int notifications{};
    auto component = std::make_shared<Component>(); component->callback = [&](auto&, int id) { Check(id == 0, "context20 passes zero source"); ++notifications; return true; };
    f.players->RegisterKillComponent(EHANDLE(f.player), component);
    Require(f.players->ResolveKillSource(call, Phase::Notification, source, error), "context20 source phase succeeds");
    Check(f.players->NotifyMonsterKill(call, source.weapon, error) && notifications == 1 && f.players->KillPositions().size() == 1,
        "context20 does not cancel component or position notification");
}

void NestedTailBound(Engine& engine)
{
    Fixture f(engine); auto call = f.Call(); ConfigError error; int calls{};
    auto component = std::make_shared<Component>();
    component->callback = [&](auto& current, int id)
    { ++calls; return f.players->NotifyMonsterKill(current, id, error); };
    const auto token = f.players->RegisterKillComponent(EHANDLE(f.player), component);
    Check(!f.players->NotifyMonsterKill(call, 29, error) && calls == 32 && !error.message.empty() &&
        f.players->KillPositions().empty(), "actual nested callbacks stop at documented host limit with no partial journal");
    Require(f.players->RemoveKillComponent(EHANDLE(f.player), token), "remove recursive subscription");
    error = {};
    Check(f.players->NotifyMonsterKill(call, 29, error) && f.players->KillPositions().size() == 1,
        "every failed nested scope unwinds before next independent notification");
}

void DestructorReentry(Engine& engine)
{
    {
        Fixture f(engine); int destructors{}; bool reopened = true;
        auto component = std::make_shared<Component>();
        component->released = [&] { ++destructors; reopened = f.players->Activate(); f.players->Invalidate(); };
        f.players->RegisterKillComponent(EHANDLE(f.player), component); component.reset();
        f.players->Invalidate();
        Check(destructors == 1 && !reopened && !f.players->Active(), "invalidation detaches all states and rejects destructor reopen");
        Check(f.players->Activate(), "after complete invalidation a deliberate new activation is allowed");
    }
    {
        Fixture f(engine); const auto oldGeneration = f.players->Find(EHANDLE(f.player))->generation; bool registered{};
        auto component = std::make_shared<Component>();
        component->released = [&] { registered = f.players->SetProfile(*f.player, {}); };
        f.players->RegisterKillComponent(EHANDLE(f.player), component); component.reset();
        f.players->Remove(EHANDLE(f.player)); const auto fresh = f.players->Find(EHANDLE(f.player));
        std::int32_t item{}; ConfigError error;
        Check(registered && fresh && fresh->generation > oldGeneration &&
            f.session->Weapons()->Item(*f.player, 29, item, error) && item == 161,
            "remove completes old cleanup before destructor registers a fresh same-entity selection");
    }
    {
        Fixture f(engine); int destructors{};
        auto component = std::make_shared<Component>(); component->released = [&] { ++destructors; f.players->Invalidate(); };
        f.players->RegisterKillComponent(EHANDLE(f.player), component); component.reset();
        auto* slot = f.player->edict(); Engine::Remove(slot);
        auto* ed = engine.Reserve(1, "player"); auto* replacement = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
        ed->v.flags = FL_CLIENT; replacement->m_iTeam = CT; replacement->has_disconnected = false;
        Check(!f.players->SetProfile(*replacement, {}) && destructors == 1 && !f.players->Active() &&
            !f.players->Find(EHANDLE(replacement)), "Ensure detects old-slot component destructor invalidating the newly published state");
        f.player = replacement;
    }
}

// The following group invokes only the concrete statistics consumers. Its
// controlled owner flag and damageable borrowed entity do not implement or
// exercise the complete damage service, health subtraction or addon phases.
struct StatisticsPlayer final : CBasePlayer
{
    bool bot{};
    int botQueries{};
    std::function<void()> onBot;
    BOOL IsBot() override
    {
        ++botQueries; const auto callback = onBot; const bool result = bot;
        if (callback) callback();
        return result;
    }
    void OnDestroy() override { onBot = {}; CBasePlayer::OnDestroy(); }
};

struct DamageSample
{
    Fixture& f;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    bool ownerActive{true};
    CSOLMonsterSpawnCall spawn;
    CSOLMonsterDamageCall call;
    DamageSample(Fixture& value, CBaseEntity* source, float damage,
        std::shared_ptr<CCSOLScenarioRuntime> suppliedRuntime = {})
        : f(value), runtime(suppliedRuntime ? std::move(suppliedRuntime) : std::make_shared<CCSOLScenarioRuntime>()),
          spawn(f.session->Map(), f.session->Monsters(), f.requests, *f.monster),
          call(spawn, EHANDLE(source), EHANDLE(f.player), damage, DMG_BULLET, {}, ownerActive,
              [](const void* context) { return static_cast<const DamageSample*>(context)->ownerActive; }, this)
    {
        runtime->session = f.session; runtime->players = f.players;
        f.monster->pev->health = 100; f.monster->pev->deadflag = DEAD_NO;
        f.monster->pev->takedamage = DAMAGE_YES; f.monster->pev->effects &= ~EF_NODRAW;
        Require(call.Current() == f.monster, "statistics sample owns a live actual pool lease and damage call");
    }
    bool Record(int source, ConfigError& error)
    { return f.players->RecordWeaponDamage(call, source, runtime, error); }
    bool Head(int captured, ConfigError& error)
    { return f.players->RecordMonsterHeadHit(call, captured, runtime, error); }
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> State() const
    {
        // Ensure() allocated a mutable state. This test-only cast seeds float
        // and uint32 boundaries; production callers still receive const state.
        auto state = std::const_pointer_cast<CSOLScenarioPlayerRuntimeState>(f.players->Find(EHANDLE(f.player)));
        Require(state != nullptr, "statistics test resolves actual registered player state"); return state;
    }
};

struct StatisticsWire
{
    struct Packet
    {
        int destination{}, id{}, recipient{};
        std::vector<std::uint8_t> bytes;
        std::vector<char> writes;
        bool ended{};
    };
    inline static StatisticsWire* current{};
    enginefuncs_t saved{g_engfuncs};
    std::vector<Packet> packets;
    std::function<void()> onBegin, onEnd;
    std::function<void(char)> onWrite;
    std::function<void(const edict_t*)> onEntIndex;
    StatisticsWire()
    {
        Require(!current, "one controlled statistics message capture"); current = this;
        g_engfuncs.pfnIndexOfEdict = [](const edict_t* ed)
        { const auto callback = current->onEntIndex; if (callback) callback(ed); return Engine::Index(ed); };
        g_engfuncs.pfnMessageBegin = [](int destination, int id, const float*, edict_t* recipient)
        {
            current->packets.push_back({destination, id, Engine::Index(recipient), {}, {}, false});
            const auto callback = current->onBegin; if (callback) callback();
        };
        g_engfuncs.pfnWriteByte = [](int value) { current->Write(value, 1, 'b'); };
        g_engfuncs.pfnWriteShort = [](int value) { current->Write(value, 2, 's'); };
        g_engfuncs.pfnWriteLong = [](int value) { current->Write(value, 4, 'l'); };
        g_engfuncs.pfnMessageEnd = []
        {
            Require(!current->packets.empty(), "head-hit packet has begun"); current->packets.back().ended = true;
            const auto callback = current->onEnd; if (callback) callback();
        };
    }
    void Write(int value, int count, char kind)
    {
        Require(!packets.empty() && !packets.back().ended, "statistic write belongs to the current open message");
        packets.back().writes.push_back(kind);
        for (int i = 0; i != count; ++i)
            packets.back().bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) >> (i * 8)));
        const auto callback = onWrite; if (callback) callback(kind);
    }
    bool Last(Engine& engine, int recipient, std::uint32_t count) const
    {
        if (packets.empty()) return false;
        std::vector<std::uint8_t> expected{2};
        for (int i = 0; i != 4; ++i) expected.push_back(static_cast<std::uint8_t>(count >> (i * 8)));
        const auto& p = packets.back();
        return p.ended && p.destination == MSG_ONE && p.id == engine.messages.at("ZBSVer2") &&
            p.recipient == recipient && p.bytes == expected && p.writes == std::vector<char>({'b', 'l'});
    }
    ~StatisticsWire() { onBegin = {}; onEnd = {}; onWrite = {}; onEntIndex = {}; g_engfuncs = saved; current = nullptr; }
};

void DamageCatalogAndArithmetic(Engine& engine)
{
    Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
    auto* ak = f.Weapon<CAK47>(WEAPON_AK47); auto* knife = f.Weapon<CKnife>(WEAPON_KNIFE);
    player->m_pActiveItem = ak;
    const int captured = CSOL_DamageWeaponId(player, player);
    player->m_pActiveItem = knife;
    DamageSample sample(f, player, 1.25f); const auto state = sample.State(); ConfigError error;
    Check(sample.Record(captured, error) && state->weaponDamage.at(1036) == 1.25f &&
        !state->weaponDamage.contains(WEAPON_AK47) && !state->weaponDamage.contains(161),
        "captured AK source resolves the original catalog item after a later active-weapon switch");
    auto* projectile = f.Entity<SourceProbe>(); projectile->state->values = {220, 144};
    const int projected = CSOL_DamageWeaponId(projectile, player);
    DamageSample projectileSample(f, projectile, 2.5f);
    Check(projectileSample.Record(projected, error) && projectile->state->sourceCalls == 1 &&
        state->weaponDamage.at(10220) == 2.5f && !state->weaponDamage.contains(10144),
        "projectile numeric source is sampled once before the helper and is not reread as its later value");
    sample.call.damage = 0;
    Check(sample.Record(144, error) && state->weaponDamage.contains(10144) && state->weaponDamage.at(10144) == 0,
        "zero damage creates the actual catalog statistic entry");
    sample.call.damage = -0.5f;
    Check(sample.Record(WEAPON_AK47, error) && state->weaponDamage.at(1036) == 0.75f,
        "negative damage preserves the original float accumulation rather than silently clamping to zero");
    sample.call.damage = 3;
    Check(sample.Record(WEAPON_KNIFE, error) && sample.Record(WEAPON_HEGRENADE, error) &&
        state->weaponDamage.at(161) == 3 && state->weaponDamage.at(31) == 3,
        "native knife and HE use the real player's selected catalog identities");
    const auto before = state->weaponDamage; const int queries = player->botQueries;
    Check(sample.Record(0, error) && sample.Record(123456, error) && state->weaponDamage == before && player->botQueries == queries,
        "zero and uncatalogued uncarried source produce no statistic or virtual Bot query");
    state->weaponDamage[1036] = 16777216.0f; sample.call.damage = 1;
    Check(sample.Record(WEAPON_AK47, error) && state->weaponDamage.at(1036) == 16777216.0f,
        "damage table commits float32 rounding at the exact 2-to-the-24 boundary");
    state->weaponDamage[1036] = std::numeric_limits<float>::max();
    sample.call.damage = std::numeric_limits<float>::max(); error = {};
    Check(!sample.Record(WEAPON_AK47, error) && !error.message.empty() && state->weaponDamage.at(1036) == std::numeric_limits<float>::max(),
        "nonfinite total is diagnosed before replacing the prior finite statistic");
    sample.call.damage = std::numeric_limits<float>::quiet_NaN(); error = {};
    Check(!sample.Record(144, error) && !error.message.empty() && state->weaponDamage.at(10144) == 0,
        "nonfinite input cannot corrupt an existing statistic");
}

void DamageEligibilityAndCapture(Engine& engine)
{
    for (int gate = 0; gate != 6; ++gate)
    {
        Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
        DamageSample sample(f, player, 6); const auto state = sample.State(); ConfigError error;
        StatisticsWire restore; // Restores every edited engine function before Fixture teardown.
        if (gate == 0) player->pev->flags |= FL_DORMANT;
        if (gate == 1) player->bot = true;
        if (gate == 2) { auto profile = state->profile; profile.weaponEffectsBlocked21F2 = true; Require(f.players->SetProfile(*player, profile), "set actual 21F2 state"); }
        if (gate == 3) g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t*) { return 0; };
        if (gate == 4) g_engfuncs.pfnEntOffsetOfPEntity = nullptr;
        if (gate == 5) { auto profile = state->profile; profile.weaponEffectsBlocked34F0 = true; Require(f.players->SetProfile(*player, profile), "set independent 34F0 state"); }
        const bool result = sample.Record(144, error);
        Check(result == (gate != 4) && (gate == 5 ? state->weaponDamage.at(10144) == 6 : state->weaponDamage.empty()),
            "dormant Bot 21F2 and zero OFFSET suppress statistics; missing OFFSET fails; 34F0 is an independent gate");
        Check(player->botQueries == (gate == 1 || gate == 2 || gate == 5 ? 1 : 0),
            "eligibility gate order avoids virtual Bot queries for dormant or invalid-offset players");
    }
    for (bool inBot : {false, true})
    {
        Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
        DamageSample sample(f, player, 4.25f); const auto state = sample.State(); ConfigError error;
        auto mutate = [&] { sample.call.damage = 999; player->m_pActiveItem = nullptr; };
        if (inBot) player->onBot = mutate;
        else engine.onOffset = [&](const edict_t* ed) { if (ed == player->edict()) mutate(); };
        Check(sample.Record(144, error) && state->weaponDamage.at(10144) == 4.25f && sample.call.damage == 999,
            "damage float and catalog item are captured before OFFSET and virtual Bot callbacks");
        player->onBot = {}; engine.onOffset = {};
    }
}

void CapturedItemMultiplier(Engine& engine)
{
    Fixture f(engine); auto* ak = f.Weapon<CAK47>(WEAPON_AK47);
    auto* knife = f.Weapon<CKnife>(WEAPON_KNIFE); const auto weapons = f.session->Weapons();
    Require(weapons && weapons->SetItemMultiplier(1036, {91, 2}) &&
        weapons->SetItemMultiplier(10220, {92, 3}) && weapons->SetItemMultiplier(10144, {93, 5}) &&
        weapons->SetItemMultiplier(161, {94, 11}), "real owned item multiplier table contains distinct source catalog factors");
    f.player->m_pActiveItem = ak; const int captured = CSOL_DamageWeaponId(f.player, f.player);
    f.player->m_pActiveItem = knife;
    DamageSample sample(f, f.player, 4); ConfigError error;
    Check(weapons->ApplyItemMultiplier(sample.call, captured, false, error) && sample.call.damage == 8,
        "direct actual multiplier uses captured AK catalog factor after switching to a different valid active gun");
    auto* projectile = f.Entity<SourceProbe>(); projectile->state->values = {220, 144};
    const int projected = CSOL_DamageWeaponId(projectile, f.player);
    DamageSample projectileSample(f, projectile, 4);
    Check(weapons->ApplyItemMultiplier(projectileSample.call, projected, false, error) &&
        projectileSample.call.damage == 12 && projectile->state->sourceCalls == 1,
        "direct actual multiplier never resamples the projectile source or uses the current gun's factor");
    Require(weapons->SetItemMultiplier(10220, {95, 0}), "owned zero factor is explicitly installed");
    projectileSample.call.damage = 4;
    Check(weapons->ApplyItemMultiplier(projectileSample.call, projected, false, error) && projectileSample.call.damage == 0 &&
        projectile->state->sourceCalls == 1, "zero source-catalog factor remains effective without another projectile query");
    sample.call.damage = 4;
    Check(weapons->ApplyItemMultiplier(sample.call, captured, true, error) && sample.call.damage == 4 &&
        sample.State()->weaponDamage.empty(), "21F2 skips multiplier and this isolated stage does not claim statistics or full damage execution");
}

void DamageStatisticsCancellation(Engine& engine)
{
    for (bool inBot : {false, true}) for (int cancellation = 0; cancellation != 3; ++cancellation)
    {
        Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
        DamageSample sample(f, player, 7); const auto old = sample.State(); ConfigError error; bool fired{};
        auto cancel = [&]
        {
            if (fired) return; fired = true;
            if (cancellation == 0) sample.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            else if (cancellation == 1)
            {
                f.players->Remove(EHANDLE(player));
                Require(f.players->SetProfile(*player, {}), "callback registers a new actual state after removing the captured one");
            }
            else sample.runtime->RequestClose();
        };
        if (inBot) player->onBot = cancel;
        else engine.onOffset = [&](const edict_t* ed) { if (ed == player->edict()) cancel(); };
        Check(!sample.Record(144, error) && fired && old->weaponDamage.empty() && error.message.empty() &&
            player->botQueries == int(inBot), "owner replacement state removal or Runtime close cancels before any damage-statistic write");
        if (cancellation == 1)
            Check(sample.State() != old && sample.State()->weaponDamage.empty(), "new same-player generation is not mutated by the retired statistics call");
        player->onBot = {}; engine.onOffset = {};
    }
    Fixture f(engine); DamageSample sample(f, f.player, 1); ConfigError error;
    auto foreign = std::make_shared<CCSOLScenarioRuntime>(); foreign->players = f.players;
    Check(!f.players->RecordWeaponDamage(sample.call, 144, foreign, error) &&
        !f.players->RecordMonsterHeadHit(sample.call, HITGROUP_HEAD, foreign, error) &&
        !f.players->RecordWeaponDamage(sample.call, 144, {}, error) && sample.State()->weaponDamage.empty(),
        "a supplied Runtime must own this session and player registry before either statistics consumer can run");
}

void HeadHitCountsAndWire(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 1); const auto state = sample.State(); ConfigError error;
    StatisticsWire wire;
    f.monster->HostState().lastHitgroup = HITGROUP_CHEST;
    Check(sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 1 && wire.Last(engine, 1, 1),
        "captured head hit survives changed current hitgroup and writes exact ZBSVer2 byte2 long1");
    f.monster->HostState().lastHitgroup = HITGROUP_HEAD;
    Check(sample.Head(HITGROUP_CHEST, error) && state->monsterHeadHits == 1 && wire.packets.size() == 1,
        "later target head state cannot turn a captured body hit into a head statistic");
    state->monsterHeadHits = 0x7fffffffu;
    Check(sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 0x80000000u && wire.Last(engine, 1, 0x80000000u),
        "unsigned counter preserves exact high-bit signed long packet representation");
    state->monsterHeadHits = 0xffffffffu;
    Check(sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 0 && wire.Last(engine, 1, 0),
        "head count increments modulo uint32 instead of saturating or invoking signed overflow");
    wire.onBegin = [&] { state->monsterHeadHits = 900; };
    Check(sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 900 && wire.Last(engine, 1, 1),
        "packet count is captured before MessageBegin callback changes current player statistics");
    wire.onBegin = {};
    const auto count = wire.packets.size(); g_engfuncs.pfnWriteLong = nullptr; error = {};
    Check(!sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 901 && wire.packets.size() == count &&
        !error.message.empty(), "message capability failure retains the already committed head-hit increment without starting a packet");
    g_engfuncs.pfnWriteLong = [](int value) { StatisticsWire::current->Write(value, 4, 'l'); };
    error = {}; bool nestedAccepted = true;
    wire.onBegin = [&] { nestedAccepted = f.session->Messages().SendMonsterHeadHits(f.player->edict(), 123); };
    Check(sample.Head(HITGROUP_HEAD, error) && !nestedAccepted && state->monsterHeadHits == 902 &&
        wire.packets.size() == count + 1 && wire.Last(engine, 1, 902),
        "nested statistics sender cannot interleave another packet and prior failure releases the open-message guard");
}

void HeadHitEligibility(Engine& engine)
{
    for (int skill : {999, 1000, 32767, 32768, 65535})
    {
        Fixture f(engine, skill); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        const bool accepted = skill == 999 || skill == 32768 || skill == 65535;
        Check(sample.Head(HITGROUP_HEAD, error) && sample.State()->monsterHeadHits == unsigned(accepted) &&
            wire.packets.size() == unsigned(accepted), "Classify17 head statistics preserve signed16 skill threshold semantics");
    }
    Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
    DamageSample sample(f, player, -1); const auto state = sample.State(); ConfigError error; StatisticsWire wire;
    player->bot = true; player->pev->flags |= FL_DORMANT;
    auto profile = state->profile; profile.weaponEffectsBlocked21F2 = true;
    Require(f.players->SetProfile(*player, profile), "head test installs actual profile gate");
    Check(sample.Head(HITGROUP_HEAD, error) && state->monsterHeadHits == 1 && player->botQueries == 0 && wire.Last(engine, 2, 1),
        "head counter has its own hitgroup skill gates and does not reuse weapon-damage Bot dormant or 21F2 filtering");
}

void HeadHitCancellation(Engine& engine)
{
    for (int cancellation = 0; cancellation != 3; ++cancellation)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); const auto old = sample.State(); ConfigError error;
        StatisticsWire wire; bool fired{};
        wire.onBegin = [&]
        {
            fired = true;
            if (cancellation == 0) sample.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            else if (cancellation == 1)
            {
                f.players->Remove(EHANDLE(f.player));
                Require(f.players->SetProfile(*f.player, {}), "message callback replaces the captured player state");
            }
            else sample.runtime->RequestClose();
        };
        Check(!sample.Head(HITGROUP_HEAD, error) && fired && old->monsterHeadHits == 1 && wire.Last(engine, 1, 1) &&
            error.message.empty(), "message callback cancellation preserves completed count and packet but rejects retired owner continuation");
        if (cancellation == 1)
            Check(sample.State() != old && sample.State()->monsterHeadHits == 0,
                "same-slot fresh player state never receives the retired call's count");
        wire.onBegin = {};
    }
}

struct BuffCondition final : CSOLScenarioBuffCondition
{
    int calls{};
    std::string name;
    std::vector<std::string>* order{};
    std::function<bool(CSOLMonsterDamageCall&, int, bool&, ConfigError&)> evaluate;
    std::function<void()> released;
    bool Evaluate(CSOLMonsterDamageCall& call, bool& applies, ConfigError& error) override
    {
        ++calls; if (order) order->push_back(name);
        applies = true;
        return !evaluate || evaluate(call, calls, applies, error);
    }
    ~BuffCondition() override
    {
        try { if (released) released(); }
        catch (...) { Check(false, "Buff condition destructor boundary must not throw"); }
    }
};

struct BuffEngineEdges
{
    inline static BuffEngineEdges* current{};
    enginefuncs_t saved{g_engfuncs};
    int randomCalls{};
    float roll{}, low{}, high{};
    std::function<void()> onRandom;
    BuffEngineEdges()
    {
        Require(!current, "one controlled Buff engine edge"); current = this;
        g_engfuncs.pfnRandomFloat = [](float low, float high)
        {
            auto& probe = *current; ++probe.randomCalls; probe.low = low; probe.high = high;
            const auto callback = probe.onRandom; if (callback) callback();
            return probe.roll;
        };
    }
    void IntegerRandom()
    {
        g_engfuncs.pfnRandomLong = [](int32 low, int32 high) -> int32
        {
            auto& probe = *current; ++probe.randomCalls; probe.low = low; probe.high = high;
            const auto callback = probe.onRandom; if (callback) callback();
            return static_cast<int32>(probe.roll);
        };
    }
    ~BuffEngineEdges() { onRandom = {}; g_engfuncs = saved; current = nullptr; }
};

CSOLScenarioStatBuff DamageBuff(int group, float attack, float chance = 0, float multiplier = 0,
    std::shared_ptr<CSOLScenarioBuffCondition> condition = {})
{
    CSOLScenarioStatBuff buff; buff.group = group; buff.attackRatio = attack;
    buff.criticalEnabled = true; buff.criticalChance = chance; buff.criticalMultiplier = multiplier;
    buff.condition = std::move(condition); return buff;
}

std::uint64_t InstallBuff(Fixture& f, CSOLScenarioStatBuff buff)
{
    const auto token = f.players->InstallStatBuff(EHANDLE(f.player), std::move(buff));
    Require(token != 0, "trusted test producer installs through the actual Buff API"); return token;
}

void BuffRegistrationAndProtection(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 10); const auto state = sample.State();
    const auto positive = InstallBuff(f, DamageBuff(5, 0.25f));
    const auto negative = InstallBuff(f, DamageBuff(-3, 0.5f));
    const auto equal = InstallBuff(f, DamageBuff(5, 1));
    Check(state->buffs.size() == 3 && state->buffs[0].token == negative && state->buffs[1].token == positive &&
        state->buffs[2].token == equal && state->buffsRevision == 3,
        "registration maintains signed group order and stable insertion order with one revision per insertion");
    Check(!f.players->ProtectionBlocksDamage(EHANDLE(f.player), engine.globals.time),
        "ordinary attack Buffs in the shared container do not create invulnerability");
    for (int invalid = 0; invalid != 7; ++invalid)
    {
        auto buff = DamageBuff(0, 1); const auto revision = state->buffsRevision;
        if (invalid == 0) buff.token = 1;
        if (invalid == 1) buff.renderHeld = true;
        if (invalid == 2) buff.labels.push_back(7);
        if (invalid == 3) buff.expires = std::numeric_limits<float>::infinity();
        if (invalid == 4) buff.criticalChance = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 5) buff.criticalMultiplier = -std::numeric_limits<float>::infinity();
        if (invalid == 6) buff.attackRatio = std::numeric_limits<float>::quiet_NaN();
        Check(f.players->InstallStatBuff(EHANDLE(f.player), buff) == 0 && state->buffs.size() == 3 &&
            state->buffsRevision == revision, "reserved ownership or nonfinite Buff data is rejected without mutation");
    }
    Check(f.players->InstallStatBuff(EHANDLE(), DamageBuff(0, 1)) == 0 &&
        !f.players->RemoveStatBuff(EHANDLE(f.player), 0) &&
        !f.players->RemoveStatBuff(EHANDLE(f.player), UINT64_MAX), "unknown owner and invalid tokens are rejected");
    auto protectedBuff = DamageBuff(2, 0); protectedBuff.protection = true; protectedBuff.mask = 8;
    const auto protection = InstallBuff(f, protectedBuff);
    Check(f.players->ProtectionBlocksDamage(EHANDLE(f.player), engine.globals.time) &&
        f.players->RemoveStatBuff(EHANDLE(f.player), protection) &&
        !f.players->ProtectionBlocksDamage(EHANDLE(f.player), engine.globals.time),
        "explicit protection marker alone participates in the root damage gate");
    Check(f.players->RemoveStatBuff(EHANDLE(f.player), positive) && !f.players->RemoveStatBuff(EHANDLE(f.player), positive) &&
        state->buffs.size() == 2 && state->buffs[0].token == negative && state->buffs[1].token == equal,
        "removal is token-specific and repeated removal does not disturb neighboring entries");
}

void CriticalThresholdAndGrouping(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
    auto critical = [&] { return f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error); };
    f.monster->HostState().damageEffect = 3;
    Check(critical() && sample.call.damage == 10 && f.monster->HostState().damageEffect == 8 &&
        edges.randomCalls == 1 && edges.low == 0 && edges.high == 100,
        "empty critical collection still rolls once and zero meets its inclusive threshold with factor one");
    edges.roll = 0.5f; f.monster->HostState().damageEffect = 3;
    Check(critical() && sample.call.damage == 10 && f.monster->HostState().damageEffect == 3 && edges.randomCalls == 2,
        "empty critical miss preserves damage and the previous effect");
    auto condition = std::make_shared<BuffCondition>();
    auto a = DamageBuff(1, 0, 20, 1.25f, condition); a.expires = -1; InstallBuff(f, a);
    InstallBuff(f, DamageBuff(1, 0, 30, 0.75f, condition));
    auto disabled = DamageBuff(1, 0, 99, 99, condition); disabled.criticalEnabled = false; InstallBuff(f, disabled);
    auto expired = DamageBuff(1, 0, 99, 99, condition); expired.expires = engine.globals.time; InstallBuff(f, expired);
    edges.roll = 50; sample.call.damage = 10;
    Check(critical() && sample.call.damage == 20 && condition->calls == 0,
        "critical group one sums enabled permanent entries, excludes exact expiry and never evaluates attack conditions");
    edges.roll = std::nextafter(50.0f, 100.0f); sample.call.damage = 10; f.monster->HostState().damageEffect = 3;
    Check(critical() && sample.call.damage == 10 && f.monster->HostState().damageEffect == 3,
        "the float immediately above accumulated chance misses");
}

void CriticalNonAdditiveGroups(Engine& engine)
{
    for (int selection = 0; selection != 4; ++selection)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        auto first = DamageBuff(-2, 0, 80, 4); first.criticalEnabled = false; InstallBuff(f, first);
        auto later = DamageBuff(-2, 0, 99, 9); later.criticalEnabled = false; InstallBuff(f, later);
        float expected = 40; edges.roll = 70;
        if (selection >= 1) { InstallBuff(f, DamageBuff(-2, 0, 30, 2)); expected = 20; edges.roll = 30; }
        if (selection >= 2) { InstallBuff(f, DamageBuff(-2, 0, 10, 3)); expected = 30; edges.roll = 10; }
        if (selection >= 3) { InstallBuff(f, DamageBuff(-2, 0, 40, 3)); edges.roll = 40; }
        Check(f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == expected,
            "non-one critical group keeps first disabled, accepts enabled replacement, then multiplier priority and chance tie-break");
    }
    for (int ordering = 0; ordering != 2; ++ordering)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        edges.roll = 0.5f; f.monster->HostState().damageEffect = 3;
        InstallBuff(f, DamageBuff(1, 0, 16777216.0f, 1));
        InstallBuff(f, DamageBuff(1, 0, ordering ? -16777216.0f : 1.0f, 1));
        InstallBuff(f, DamageBuff(1, 0, ordering ? 1.0f : -16777216.0f, 1));
        Check(f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error) &&
            sample.call.damage == (ordering ? 30.0f : 10.0f) &&
            f.monster->HostState().damageEffect == (ordering ? 8 : 3),
            "same-group chance sums retain insertion-ordered float32 rounding at two-to-the-24");
    }
}

void CriticalCallbacksAndFailures(Engine& engine)
{
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        const auto token = InstallBuff(f, DamageBuff(1, 0, 80, 2)); edges.roll = 50;
        edges.onRandom = [&]
        {
            sample.call.damage = 7;
            Require(f.players->RemoveStatBuff(EHANDLE(f.player), token), "RNG may remove the previously sampled Buff");
            InstallBuff(f, DamageBuff(1, 0, 0, 9));
        };
        Check(f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 14 &&
            f.monster->HostState().damageEffect == 8 && edges.randomCalls == 1,
            "critical uses pre-RNG chance/factor and post-RNG damage even when RNG changes Buff revision");
    }
    for (int cancellation = 0; cancellation != 3; ++cancellation)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        InstallBuff(f, DamageBuff(1, 0, 80, 2)); f.monster->HostState().damageEffect = 3;
        edges.onRandom = [&]
        {
            if (cancellation == 0) sample.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if (cancellation == 1) { f.players->Remove(EHANDLE(f.player)); Require(f.players->SetProfile(*f.player, {}), "RNG replaces state"); }
            if (cancellation == 2) sample.runtime->RequestClose();
        };
        Check(!f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 10 &&
            f.monster->HostState().damageEffect == 3 && edges.randomCalls == 1 && error.message.empty() &&
            sample.call.Cancelled() && !sample.call.Current(),
            "RNG owner/state retirement cancels both critical damage and effect commitment");
    }
    for (int failure = 0; failure != 5; ++failure)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        InstallBuff(f, DamageBuff(1, 0, 80, 2)); f.monster->HostState().damageEffect = 3;
        if (failure == 0) g_engfuncs.pfnRandomFloat = nullptr;
        if (failure == 1) edges.roll = std::numeric_limits<float>::quiet_NaN();
        if (failure == 2) { sample.call.damage = std::numeric_limits<float>::max(); }
        if (failure == 3) engine.globals.time = std::numeric_limits<float>::infinity();
        if (failure == 4) { InstallBuff(f, DamageBuff(1, 0, 0, std::numeric_limits<float>::max())); InstallBuff(f, DamageBuff(1, 0, 0, std::numeric_limits<float>::max())); }
        const float before = sample.call.damage;
        Check(!f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error) && !error.message.empty() &&
            sample.call.damage == before && f.monster->HostState().damageEffect == 3 &&
            edges.randomCalls == ((failure == 1 || failure == 2) ? 1 : 0) && !sample.call.Cancelled(),
            "invalid RNG, clock, sum or product is diagnosed without committing critical state");
        engine.globals.time = 10;
    }
    for (bool retired : {false, true})
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges;
        InstallBuff(f, DamageBuff(1, 0, 80, 2)); f.monster->HostState().damageEffect = 3;
        edges.onRandom = [&]
        {
            if (retired) f.players->Remove(EHANDLE(f.player));
            throw std::runtime_error("controlled critical RNG exception");
        };
        bool returned{}, threw{};
        try { returned = f.players->ApplyCriticalBuffDamage(sample.call, sample.runtime, error); }
        catch (const std::runtime_error& exception)
        { threw = std::string(exception.what()) == "controlled critical RNG exception"; }
        Check(!returned && threw == !retired && sample.call.Cancelled() == retired && edges.randomCalls == 1 &&
            sample.call.damage == 10 && f.monster->HostState().damageEffect == 3 && error.message.empty(),
            "RNG exception cancels after player retirement; valid-owner exception remains a real thrown failure");
    }
}

void AttackEntryGates(Engine& engine)
{
    for (int gate = 0; gate != 5; ++gate)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error; BuffEngineEdges edges; int offsets{};
        engine.onOffset = [&](const edict_t*) { ++offsets; };
        if (gate == 1) f.player->pev->flags |= FL_DORMANT;
        if (gate == 2) g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t*) { return 0; };
        if (gate == 3) g_engfuncs.pfnEntOffsetOfPEntity = nullptr;
        if (gate == 4) engine.onOffset = [&](const edict_t*) { ++offsets; InstallBuff(f, DamageBuff(0, 0.5f)); };
        const bool ok = f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error);
        Check(ok == (gate != 3) && sample.call.damage == (gate == 4 ? 15.0f : 10.0f) && edges.randomCalls == 0 &&
            offsets == ((gate == 0 || gate == 1 || gate == 4) ? 1 : 0) && error.message.empty() == (gate != 3),
            "empty attack collection still validates OFFSET; dormant/null skip, missing API fails, pre-revision insertion participates");
        engine.onOffset = {};
    }
    for (int cancellation = 0; cancellation != 3; ++cancellation)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 10); ConfigError error;
        auto condition = std::make_shared<BuffCondition>(); InstallBuff(f, DamageBuff(0, 1, 0, 0, condition));
        bool fired{};
        engine.onOffset = [&](const edict_t*)
        {
            if (fired) return;
            fired = true;
            if (cancellation == 0) sample.ownerActive = false;
            if (cancellation == 1) sample.runtime->RequestClose();
            if (cancellation == 2) { f.players->Remove(EHANDLE(f.player)); Require(f.players->SetProfile(*f.player, {}), "OFFSET replaces state"); }
        };
        Check(!f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && fired &&
            condition->calls == 0 && sample.call.damage == 10 && error.message.empty() &&
            sample.call.Cancelled() && !sample.call.Current(),
            "OFFSET owner or state retirement cancels before presence and condition evaluation");
        engine.onOffset = {};
    }
}

void AttackPresenceAndGroups(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error; BuffEngineEdges edges;
    std::vector<std::string> order;
    auto probe = [&](const char* name)
    { auto p = std::make_shared<BuffCondition>(); p->name = name; p->order = &order; return p; };
    const auto a = probe("A"), b = probe("B");
    InstallBuff(f, DamageBuff(0, 0, 0, 0, a)); InstallBuff(f, DamageBuff(0, 0.5f, 0, 0, b));
    Check(f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 12 &&
        order == std::vector<std::string>({"A", "A", "B"}) && edges.randomCalls == 0,
        "presence stops at an eligible zero-ratio Buff, then additive query reevaluates each condition");
    f.players->ReleaseProtectionState(); order.clear(); sample.call.damage = 8;
    const auto p = probe("positive"), n1 = probe("negative-first"), n2 = probe("negative-second"), z = probe("zero");
    InstallBuff(f, DamageBuff(9, 0.25f, 0, 0, p)); InstallBuff(f, DamageBuff(-9, 0.5f, 0, 0, n1));
    InstallBuff(f, DamageBuff(-9, 0.5f, 0, 0, n2)); InstallBuff(f, DamageBuff(0, 0.25f, 0, 0, z));
    Check(f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 16 &&
        order == std::vector<std::string>({"negative-first", "negative-first", "negative-second", "negative-first", "zero", "positive"}),
        "attack query follows signed groups and stable entries, sums group zero and selects one candidate in each other group");
}

void AttackCandidateRequeries(Engine& engine)
{
    for (int scenario = 0; scenario != 4; ++scenario)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        std::vector<std::string> order;
        auto a = std::make_shared<BuffCondition>(), b = std::make_shared<BuffCondition>(), c = std::make_shared<BuffCondition>();
        a->name = "A"; b->name = "B"; c->name = "C"; a->order = b->order = c->order = &order;
        a->evaluate = [scenario](CSOLMonsterDamageCall&, int call, bool& applies, ConfigError&)
        { applies = !(scenario == 1 && call >= 3); return true; };
        b->evaluate = [&, scenario](CSOLMonsterDamageCall&, int, bool& applies, ConfigError&)
        {
            applies = scenario == 0;
            if (scenario == 3) engine.globals.time = 20;
            return true;
        };
        auto first = DamageBuff(5, 0.5f, 0, 0, a); if (scenario == 3) first.expires = 20;
        InstallBuff(f, first); InstallBuff(f, DamageBuff(5, 0.5f, 0, 0, b));
        if (scenario == 0) InstallBuff(f, DamageBuff(5, 0.25f, 0, 0, c));
        const auto expected = scenario == 0 ? std::vector<std::string>({"A", "A", "B", "A", "C", "A"}) :
            scenario == 3 ? std::vector<std::string>({"A", "A", "B"}) : std::vector<std::string>({"A", "A", "B", "A"});
        Check(f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) &&
            sample.call.damage == ((scenario == 1 || scenario == 3) ? 8.0f : 12.0f) && order == expected,
            "nonzero group retests best after current, preserves ties, clears failed best and only checks expiry at commitment");
        engine.globals.time = 10;
    }
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        auto condition = std::make_shared<BuffCondition>();
        condition->evaluate = [&](CSOLMonsterDamageCall&, int call, bool&, ConfigError&)
        { if (call == 2) engine.globals.time = 20; return true; };
        auto buff = DamageBuff(2, 0.5f, 0, 0, condition); buff.expires = 20; InstallBuff(f, buff);
        Check(f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 8 && condition->calls == 2,
            "candidate expiring inside its query condition is rejected by final expiry without a third condition call");
        engine.globals.time = 10;
    }
}

void AttackConditionCancellation(Engine& engine)
{
    for (int cancellation = 0; cancellation != 6; ++cancellation)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        auto condition = std::make_shared<BuffCondition>(); std::uint64_t token{};
        condition->evaluate = [&](CSOLMonsterDamageCall& call, int, bool&, ConfigError&)
        {
            call.damage = 7; // Callback prefix remains observable after cancellation.
            if (cancellation == 0) InstallBuff(f, DamageBuff(0, 1));
            if (cancellation == 1) Require(f.players->RemoveStatBuff(EHANDLE(f.player), token), "condition removes itself");
            if (cancellation == 2) sample.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if (cancellation == 3) { f.players->Remove(EHANDLE(f.player)); Require(f.players->SetProfile(*f.player, {}), "condition replaces state"); }
            if (cancellation == 4) sample.runtime->RequestClose();
            if (cancellation == 5) sample.ownerActive = false;
            return true;
        };
        token = InstallBuff(f, DamageBuff(0, 0.5f, 0, 0, condition));
        Check(!f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 7 &&
            condition->calls == 1 && error.message.empty() && sample.call.Cancelled() && !sample.call.Current(),
            "structural edits and retired damage owners cancel traversal without rolling back the condition prefix");
    }
    for (int failure = 0; failure != 3; ++failure)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        auto condition = std::make_shared<BuffCondition>();
        condition->evaluate = [failure](CSOLMonsterDamageCall&, int, bool& applies, ConfigError& diagnostic)
        { applies = false; if (failure == 2) diagnostic.message = "condition diagnostic"; return failure == 0; };
        InstallBuff(f, DamageBuff(0, 0.5f, 0, 0, condition));
        Check(f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) == (failure == 0) &&
            sample.call.damage == 8 && condition->calls == 1 && !sample.call.Cancelled() && sample.call.Current() == f.monster &&
            error.message == (failure == 2 ? "condition diagnostic" : ""),
            "condition inapplicability differs from failed evaluation and preserves the supplied diagnostic");
    }
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); const auto old = sample.State(); ConfigError error;
        bool finished{}, destroyed{}; std::uint64_t token{};
        auto condition = std::make_shared<BuffCondition>(); const std::weak_ptr<BuffCondition> weak = condition;
        condition->evaluate = [&](CSOLMonsterDamageCall&, int, bool&, ConfigError&)
        {
            Require(f.players->RemoveStatBuff(EHANDLE(f.player), token), "self-removal retains the in-flight condition");
            Check(!weak.expired() && !destroyed, "condition stays alive until Evaluate returns");
            finished = true; return true;
        };
        condition->released = [&]
        {
            destroyed = true; Check(finished, "condition destructor runs after its evaluation prefix");
            f.players->Remove(EHANDLE(f.player));
            Require(f.players->SetProfile(*f.player, {}), "self-removed condition destructor replaces the player state");
        };
        token = InstallBuff(f, DamageBuff(0, 0.5f, 0, 0, condition)); condition.reset();
        Check(!f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error) && sample.call.damage == 8 &&
            destroyed && weak.expired() && sample.State() != old && error.message.empty() && sample.call.Cancelled(),
            "condition pin release participates in the revision and owner-state cancellation check");
    }
    for (bool retired : {false, true})
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        auto condition = std::make_shared<BuffCondition>();
        condition->evaluate = [&](CSOLMonsterDamageCall& call, int, bool&, ConfigError&) -> bool
        {
            call.damage = 7;
            if (retired) InstallBuff(f, DamageBuff(0, 1));
            throw std::runtime_error("controlled attack condition exception");
        };
        InstallBuff(f, DamageBuff(0, 0.5f, 0, 0, condition));
        bool returned{}, threw{};
        try { returned = f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error); }
        catch (const std::runtime_error& exception)
        { threw = std::string(exception.what()) == "controlled attack condition exception"; }
        Check(!returned && threw == !retired && sample.call.Cancelled() == retired && sample.call.damage == 7 &&
            condition->calls == 1 && error.message.empty(),
            "attack condition revision mutation before throw cancels only this hit and preserves its committed prefix");
    }
}

void AttackArithmeticAndFailures(Engine& engine)
{
    for (int arithmetic = 0; arithmetic != 5; ++arithmetic)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); ConfigError error;
        if (arithmetic == 0) { InstallBuff(f, DamageBuff(0, -0.75f)); InstallBuff(f, DamageBuff(0, -0.75f)); }
        if (arithmetic == 1) { sample.call.damage = 3; InstallBuff(f, DamageBuff(0, std::ldexp(1.0f, -24))); }
        if (arithmetic == 2) { InstallBuff(f, DamageBuff(0, std::numeric_limits<float>::max())); InstallBuff(f, DamageBuff(0, std::numeric_limits<float>::max())); }
        if (arithmetic == 3) { sample.call.damage = std::numeric_limits<float>::max(); InstallBuff(f, DamageBuff(0, 1)); }
        if (arithmetic == 4) { InstallBuff(f, DamageBuff(0, 1)); engine.globals.time = std::numeric_limits<float>::quiet_NaN(); }
        const float before = sample.call.damage;
        const bool ok = f.players->ApplyAttackBuffDamage(sample.call, sample.runtime, error);
        Check(ok == (arithmetic < 2) && error.message.empty() == (arithmetic < 2) &&
            sample.call.damage == (arithmetic == 0 ? 0.0f : arithmetic == 1 ? std::nextafter(3.0f, 4.0f) : before),
            "attack clamps only the final negative bonus, rounds double product once, and rejects nonfinite clock/sum/product");
        engine.globals.time = 10;
    }
}

void BuffDestructorReentry(Engine& engine)
{
    {
        Fixture f(engine); const EHANDLE player(f.player); bool released{}, reopened{true};
        auto condition = std::make_shared<BuffCondition>();
        condition->released = [&] { released = true; f.players->Invalidate(); reopened = f.players->Activate(); };
        InstallBuff(f, DamageBuff(0, 1, 0, 0, condition)); condition.reset();
        f.players->ReleaseProtectionState();
        const auto live = f.players->Find(player);
        Check(!released && f.players->Active() && live && live->buffs.empty(),
            "release removes live Buff records without invoking condition destructors");
        f.players->Invalidate();
        Check(released && !reopened && !f.players->Active() && !f.players->Find(player),
            "deferred condition destructor cannot recursively invalidate and reopen detached authority");
    }
    {
        Fixture f(engine); DamageSample sample(f, f.player, 8); bool released{}, closing{}; std::uint64_t generation{};
        auto condition = std::make_shared<BuffCondition>();
        condition->released = [&]
        {
            released = true; closing = sample.runtime->Closing(); generation = sample.runtime->Generation();
            sample.runtime->RequestClose();
        };
        InstallBuff(f, DamageBuff(0, 1, 0, 0, condition)); condition.reset();
        const auto before = sample.runtime->Generation(); sample.runtime->RequestClose();
        Check(!released && sample.runtime->Closing() && sample.runtime->Generation() == before + 1 &&
            !f.session->Map().EntityAccessAllowed(), "runtime close revokes access without running condition destructors");
        f.players->Invalidate();
        Check(released && closing && generation == before + 1 && sample.runtime->Generation() == generation,
            "deferred condition destruction sees a closed runtime and repeat close preserves its generation");
    }
}

bool DamageEffectPacket(const StatisticsWire::Packet& packet, Engine& engine, int recipient,
    int target, int subtype, std::uint32_t heightBits, std::uint32_t magnitudeBits)
{
    std::vector<std::uint8_t> expected{199, static_cast<std::uint8_t>(target),
        static_cast<std::uint8_t>(target >> 8), static_cast<std::uint8_t>(subtype)};
    for (const auto bits : {heightBits, magnitudeBits})
        for (int shift = 0; shift != 32; shift += 8) expected.push_back(static_cast<std::uint8_t>(bits >> shift));
    return packet.ended && packet.destination == MSG_ONE && packet.id == engine.messages.at("MPToCL") &&
        packet.recipient == recipient && packet.bytes == expected &&
        packet.writes == std::vector<char>({'b', 's', 'b', 'l', 'l'});
}

void SetDamageEffect(Fixture& f, int effect, double magnitude = 3.25)
{
    f.monster->HostState().damageEffect = effect;
    f.monster->HostState().damageEffectMagnitude = magnitude;
    f.monster->pev->maxs.z = 72;
    f.monster->pev->origin.z = 1000;
    f.monster->pev->rendermode = kRenderNormal;
}

void DamageEffectWireEncoding(Engine& engine)
{
    Fixture f(engine); StatisticsWire wire; const auto& messages = f.session->Messages();
    Check(messages.SendMonsterDamageEffect(f.player->edict(), 300, 43, 72, 0) && wire.packets.size() == 1 &&
        wire.packets[0].bytes == std::vector<std::uint8_t>({0xc7,0x2c,0x01,0x2b,0,0,0x90,0x42,0,0,0,0}) &&
        DamageEffectPacket(wire.packets[0], engine, 1, 300, 43, 0x42900000, 0),
        "reference notification is exactly twelve bytes with raw float words, not quantized coordinates");
    Check(messages.SendMonsterDamageEffect(f.player->edict(), 65535, 53, -0.0f, -3.25f) && wire.packets.size() == 2 &&
        DamageEffectPacket(wire.packets[1], engine, 1, 65535, 53, 0x80000000, 0xc0500000),
        "wire retains unsigned SHORT high bits, negative zero and negative float magnitude without numeric integer conversion");
    bool accepted{true};
    wire.onBegin = [&] { accepted = messages.SendMonsterDamageEffect(f.player->edict(), 300, 46, 1, 2); };
    Check(messages.SendMonsterDamageEffect(f.player->edict(), 300, 47, 72, 3.25f) && !accepted && wire.packets.size() == 3 &&
        DamageEffectPacket(wire.packets.back(), engine, 1, 300, 47, 0x42900000, 0x40500000),
        "nested message sender is rejected without interleaving the already open effect packet");
}

void DamageEffectSenderRejections(Engine& engine)
{
    for (int failure = 0; failure != 15; ++failure)
    {
        Fixture f(engine); StatisticsWire wire; CCSOLScenarioMessages unregistered;
        const auto& messages = failure == 0 ? unregistered : f.session->Messages();
        auto* recipient = f.player->edict(); int index = 300, subtype = 43; float height = 72, magnitude = 1;
        if (failure == 1) recipient = nullptr;
        if (failure == 2) recipient->free = TRUE;
        if (failure == 3) index = 0;
        if (failure == 4) index = 65536;
        if (failure == 5) subtype = 44;
        if (failure == 6) height = std::numeric_limits<float>::quiet_NaN();
        if (failure == 7) magnitude = std::numeric_limits<float>::infinity();
        if (failure == 8) g_engfuncs.pfnMessageBegin = nullptr;
        if (failure == 9) g_engfuncs.pfnMessageEnd = nullptr;
        if (failure == 10) g_engfuncs.pfnWriteByte = nullptr;
        if (failure == 11) g_engfuncs.pfnWriteShort = nullptr;
        if (failure == 12) g_engfuncs.pfnWriteLong = nullptr;
        if (failure == 13) index = -1;
        if (failure == 14) subtype = 256;
        Check(!messages.SendMonsterDamageEffect(recipient, index, subtype, height, magnitude) && wire.packets.empty(),
            "unregistered, invalid or unsupported effect message is rejected before beginning any packet");
        f.player->edict()->free = FALSE;
    }
}

void DamageEffectSkipAndCritical(Engine& engine)
{
    for (int gate = 0; gate != 8; ++gate)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        auto* source = f.Entity<SourceProbe>(); source->state->player = false;
        CBaseEntity* attacker = gate == 2 ? static_cast<CBaseEntity*>(source) : gate == 3 ? nullptr : f.player;
        CSOLMonsterDamageCall call(sample.spawn, {}, EHANDLE(attacker), 0, DMG_BULLET, {}, sample.ownerActive,
            [](const void* context) { return static_cast<const DamageSample*>(context)->ownerActive; }, &sample);
        SetDamageEffect(f, gate == 0 ? 0 : gate == 7 ? 99 : 8, 31.5);
        auto* edict = f.player->edict();
        if (gate == 1) f.player->pev->flags |= FL_SPECTATOR;
        if (gate == 4) f.player->pev->pContainingEntity = nullptr;
        if (gate == 5) g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t*) { return 0; };
        if (gate == 6) f.monster->pev->rendermode = kRenderTransAlpha;
        Check(CSOL_ConsumeMonsterDamageEffect(call, sample.runtime, error) && wire.packets.empty() &&
            f.monster->HostState().damageEffect == 0 &&
            f.monster->HostState().damageEffectMagnitude == (gate == 0 ? 31.5 : 0) && error.message.empty(),
            "effect zero preserves magnitude; invalid attacker, alpha-four or unknown nonzero effect still clears the common tail");
        Check(source->state->isPlayerCalls == (gate == 2 ? 1 : 0), "zero and unrelated effect paths do not query a substitute source");
        f.player->pev->pContainingEntity = edict;
    }
    Fixture f(engine); DamageSample sample(f, f.player, -1); ConfigError error; StatisticsWire wire;
    f.Player(2); SetDamageEffect(f, 8, std::numeric_limits<double>::infinity());
    const int target = Engine::Index(f.monster->edict());
    Check(CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && wire.packets.size() == 1 &&
        DamageEffectPacket(wire.packets[0], engine, 1, target, 43, 0x42900000, 0) && sample.call.damage == -1 &&
        f.monster->HostState().damageEffect == 0 && f.monster->HostState().damageEffectMagnitude == 0,
        "critical effect sends only to attacker, ignores stored magnitude, uses maxs.z and does not require positive damage");
}

void DamageEffectBroadcastRecipients(Engine& engine)
{
    for (const auto [effect, subtype] : {std::pair{10,46}, std::pair{11,47}, std::pair{15,53}})
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        auto* dead = f.Player(2); dead->m_iTeam = TERRORIST; dead->pev->deadflag = DEAD_DEAD; dead->has_disconnected = true;
        auto* dormant = f.Player(3); dormant->pev->flags |= FL_DORMANT | FL_SPECTATOR | FL_FAKECLIENT;
        auto* slot4 = engine.Reserve(4, "effect_recipient_probe");
        auto* privateEntity = GetClassPtr<CCSEntity>(reinterpret_cast<SourceProbe*>(&slot4->v));
        privateEntity->state->player = false;
        SetDamageEffect(f, effect, 3.25); const int target = Engine::Index(f.monster->edict());
        Check(CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && wire.packets.size() == 4 &&
            privateEntity->state->isPlayerCalls == 0 && sample.call.damage == 0,
            "broadcast effect enumerates all valid client private entities without team, life, dormant, Bot or IsPlayer filters");
        for (std::size_t i = 0; i < wire.packets.size(); ++i)
            Check(DamageEffectPacket(wire.packets[i], engine, static_cast<int>(i) + 1, target, subtype, 0x42900000, 0x40500000),
                "each ascending recipient gets its own registered MPToCL MSG_ONE packet and exact effect subtype");
        Check(f.monster->HostState().damageEffect == 0 && f.monster->HostState().damageEffectMagnitude == 0,
            "broadcast commits the shared effect cleanup after all recipients");
    }
    for (int missing = 0; missing != 5; ++missing)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        auto* second = f.Player(2); auto* recipient = second->edict();
        if (missing == 0) recipient->free = TRUE;
        if (missing == 1) Engine::FreePrivate(recipient);
        if (missing == 2) second->pev->pContainingEntity = nullptr;
        if (missing == 3) g_engfuncs.pfnPEntityOfEntIndex = [](int index) { return index == 2 ? nullptr : Engine::At(index); };
        if (missing == 4) g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t* ed) { return Engine::Index(ed) == 2 ? 0 : Engine::Index(ed) * static_cast<int>(sizeof(edict_t)); };
        SetDamageEffect(f, 10);
        Check(CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && wire.packets.size() == 1 &&
            wire.packets[0].recipient == 1 && error.message.empty(), "free, missing-private, containing-edict, lookup or OFFSET recipient is skipped");
        recipient->free = FALSE;
        if (missing == 2) second->pev->pContainingEntity = recipient;
    }
}

void DamageEffectResampling(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
    f.Player(2); f.Player(3); SetDamageEffect(f, 10, 1.0 + std::ldexp(1.0, -25)); const int target = Engine::Index(f.monster->edict());
    wire.onBegin = [&]
    {
        f.monster->pev->maxs.z = 999;
        f.monster->HostState().damageEffect = 15;
        f.monster->HostState().damageEffectMagnitude = wire.packets.size() == 1 ? 2.5 : -3.25;
    };
    Check(CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && wire.packets.size() == 3 &&
        DamageEffectPacket(wire.packets[0], engine, 1, target, 46, 0x42900000, 0x3f800000) &&
        DamageEffectPacket(wire.packets[1], engine, 2, target, 46, 0x42900000, 0x40200000) &&
        DamageEffectPacket(wire.packets[2], engine, 3, target, 46, 0x42900000, 0xc0500000) &&
        f.monster->HostState().damageEffect == 0 && f.monster->HostState().damageEffectMagnitude == 0,
        "each recipient samples fresh magnitude while initial height/subtype and each started packet remain fixed");
}

void DamageEffectConsumerFailures(Engine& engine)
{
    for (int failure = 0; failure != 9; ++failure)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 7); ConfigError error; StatisticsWire wire; SetDamageEffect(f, 10);
        if (failure == 0) g_engfuncs.pfnEntOffsetOfPEntity = nullptr;
        if (failure == 1) g_engfuncs.pfnIndexOfEdict = nullptr;
        if (failure == 2) g_engfuncs.pfnPEntityOfEntIndex = nullptr;
        if (failure == 3) g_engfuncs.pfnWriteLong = nullptr;
        if (failure == 4) f.monster->pev->maxs.z = std::numeric_limits<float>::quiet_NaN();
        if (failure == 5) f.monster->HostState().damageEffectMagnitude = std::numeric_limits<double>::max();
        if (failure == 6) engine.globals.maxClients = MAX_CLIENTS + 1;
        if (failure == 7) engine.globals.maxClients = -1;
        if (failure == 8) g_engfuncs.pfnIndexOfEdict = [](const edict_t*) { return 65536; };
        Check(!CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && !error.message.empty() &&
            wire.packets.empty() && f.monster->HostState().damageEffect == 0 &&
            f.monster->HostState().damageEffectMagnitude == 0 && sample.call.damage == 7,
            "consumer capability, capacity and nonfinite field failures report a diagnostic and clear only the live effect tail");
        engine.globals.maxClients = 4;
    }
    Fixture f(engine); DamageSample sample(f, f.player, 7); ConfigError error; StatisticsWire wire;
    f.Player(2); SetDamageEffect(f, 11); const int target = Engine::Index(f.monster->edict());
    wire.onBegin = [&] { f.monster->HostState().damageEffectMagnitude = std::numeric_limits<double>::infinity(); };
    Check(!CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && !error.message.empty() && wire.packets.size() == 1 &&
        DamageEffectPacket(wire.packets[0], engine, 1, target, 47, 0x42900000, 0x40500000) &&
        f.monster->HostState().damageEffect == 0 && f.monster->HostState().damageEffectMagnitude == 0,
        "a later invalid magnitude retains the completed first packet then clears live state without beginning a second packet");
}

void DamageEffectCancellation(Engine& engine)
{
    for (int replacement = 0; replacement != 2; ++replacement)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        auto* attacker = f.Entity<SourceProbe>(); const auto probe = attacker->state; probe->player = true;
        CSOLMonsterDamageCall call(sample.spawn, {}, EHANDLE(attacker), 0, DMG_BULLET, {}, sample.ownerActive,
            [](const void* context) { return static_cast<const DamageSample*>(context)->ownerActive; }, &sample);
        probe->onPlayer = [&]
        {
            if (replacement == 0) sample.ownerActive = false;
            else Engine::Remove(attacker->edict());
        };
        SetDamageEffect(f, 8, 19);
        Check(!CSOL_ConsumeMonsterDamageEffect(call, sample.runtime, error) && probe->isPlayerCalls == 1 &&
            wire.packets.empty() && error.message.empty() && f.monster->HostState().damageEffect == 8 &&
            f.monster->HostState().damageEffectMagnitude == 19,
            "virtual attacker query cancellation or removal cannot proceed to recipient lookup or clear the old hit state");
        probe->onPlayer = {};
    }
    for (int cancellation = 0; cancellation != 5; ++cancellation)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 7); ConfigError error; StatisticsWire wire;
        f.Player(2); SetDamageEffect(f, 10, 3.25); const int target = Engine::Index(f.monster->edict()); bool fired{};
        auto cancel = [&]
        {
            if (fired) return;
            fired = true;
            f.monster->HostState().damageEffect = 15;
            f.monster->HostState().damageEffectMagnitude = 123.5;
            if (cancellation == 3) sample.runtime->session = std::make_shared<CCSOLScenarioSession>();
            else if (cancellation == 4) sample.runtime->RequestClose();
            else sample.ownerActive = false;
        };
        if (cancellation == 0) engine.onOffset = [&](const edict_t*) { cancel(); };
        if (cancellation == 1) engine.onIndexLookup = [&](int, edict_t*) { cancel(); };
        if (cancellation >= 2) wire.onBegin = cancel;
        Check(!CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && fired && error.message.empty() &&
            f.monster->HostState().damageEffect == 15 && f.monster->HostState().damageEffectMagnitude == 123.5 &&
            wire.packets.size() == (cancellation >= 2 ? 1u : 0u),
            "identity, lookup or message callback retirement cancels without clearing state after ownership changed");
        if (cancellation >= 2 && !wire.packets.empty())
            Check(DamageEffectPacket(wire.packets[0], engine, 1, target, 46, 0x42900000, 0x40500000),
                "a packet begun before cancellation still finishes from its captured raw fields");
        engine.onOffset = {}; engine.onIndexLookup = {}; wire.onBegin = {};
    }
}

void DamageEffectLeaseReuseAndReentry(Engine& engine)
{
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error; StatisticsWire wire;
        SetDamageEffect(f, 8); const int target = Engine::Index(f.monster->edict());
        const auto lease = f.session->Monsters().Lease(*f.monster); bool recycled{};
        wire.onBegin = [&]
        {
            auto& pool = f.session->Monsters(); const auto* type = f.monster->Type();
            Require(pool.Free(*f.monster), "message callback returns the actual borrowed monster");
            for (std::size_t i = 0; i != CCSOLMonsterPool::Capacity; ++i)
            {
                auto* next = pool.Acquire(*type, 1, f.session->Map().SpawnPoints().Points());
                Require(next != nullptr, "cycle the real idle FIFO to the prior physical host");
                if (next == f.monster) { recycled = true; break; }
                Require(pool.Free(*next), "return intervening lease without exhausting ordinary category capacity");
            }
            Require(recycled && pool.Lease(*f.monster) != lease, "same object now owns a different actual pool lease");
            f.monster->pev->takedamage = DAMAGE_YES;
            f.monster->HostState().damageEffect = 15; f.monster->HostState().damageEffectMagnitude = 321.5;
        };
        Check(!CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && recycled && wire.packets.size() == 1 &&
            DamageEffectPacket(wire.packets[0], engine, 1, target, 43, 0x42900000, 0) &&
            f.monster->HostState().damageEffect == 15 && f.monster->HostState().damageEffectMagnitude == 321.5 && error.message.empty(),
            "old-lease message completion never clears the reused physical monster's new effect state");
    }
    {
        Fixture f(engine); DamageSample sample(f, f.player, 0); ConfigError error, nestedError; StatisticsWire wire;
        SetDamageEffect(f, 8); const int target = Engine::Index(f.monster->edict()); bool nested{true};
        wire.onBegin = [&] { nested = CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, nestedError); };
        Check(CSOL_ConsumeMonsterDamageEffect(sample.call, sample.runtime, error) && !nested && !nestedError.message.empty() &&
            error.message.empty() && wire.packets.size() == 1 && DamageEffectPacket(wire.packets[0], engine, 1, target, 43, 0x42900000, 0) &&
            f.monster->HostState().damageEffect == 0 && f.monster->HostState().damageEffectMagnitude == 0,
            "same-host reentry cannot open a nested packet and still leaves the original packet complete");
    }
}

struct AttributeSeed
{
    inline static AttributeSeed* current{};
    std::uint32_t value{5489};
    int calls{};
    std::function<void()> callback;
    AttributeSeed() { Require(!current, "one explicit attribute entropy boundary"); current=this; }
    ~AttributeSeed() { callback={}; current=nullptr; }
    static std::uint32_t Draw()
    {
        auto& probe=*current; ++probe.calls; const auto action=probe.callback;
        if(action)action(); return probe.value;
    }
};

struct AttributeDamageSetup
{
    Fixture& f;
    std::shared_ptr<CCSOLScenarioWeaponState> weapons;
    CBasePlayerWeapon* weapon{};
    std::uint32_t key{};
    explicit AttributeDamageSetup(Fixture& fixture) : f(fixture), weapons(f.session->Weapons())
    {
        weapon=f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem=weapon;
        const auto created=weapons->CreateAttributes(WEAPON_AK47,0);
        Require(created && weapons->SetAttributeKey(*weapon,*created), "actual session directory creates and binds a real SDK weapon");
        key=*created;
    }
    void Set(int kind,double value)
    { Require(weapons->SetAttribute(key,kind,CSOLWeaponAttributeValue{value},0), "trusted attribute producer installs a typed double"); }
    bool Prepare(DamageSample& sample,std::optional<float>& recovery,ConfigError& error)
    { return weapons->PrepareAttributeDamage(sample.call,sample.runtime,recovery,error,AttributeSeed::Draw); }
};

void AttributeDamageOrderAndRecovery(Engine& engine)
{
    Fixture f(engine); AttributeDamageSetup setup(f); DamageSample sample(f,f.player,8);
    ConfigError error; AttributeSeed seed; std::optional<float> recovery{99};
    constexpr double roll5489=0x1.1574f7b6848dcp-3;
    setup.Set(1,0.5);setup.Set(5,roll5489);setup.Set(6,1);setup.Set(7,0.25);
    f.monster->HostState().damageEffect=3; const float health=f.player->pev->health;
    seed.callback=[&]{Check(sample.call.damage==12 && f.monster->HostState().damageEffect==3,
        "kind1 commits before entropy while critical effect remains pending");};
    Check(setup.Prepare(sample,recovery,error) && sample.call.damage==24 && recovery==std::optional<float>{6} &&
        seed.calls==1 && f.monster->HostState().damageEffect==8 && f.player->pev->health==health,
        "exact chance equality hits, kind6 precedes kind7, and recovery is output without applying player health");
    seed.callback={};setup.Set(5,0);setup.Set(7,0.5);
    for(const float input : {0.0f,-8.0f})
    {
        sample.call.damage=input; recovery=99;
        Check(setup.Prepare(sample,recovery,error) && recovery.has_value() && *recovery==input*0.75f && seed.calls==1,
            "positive kind7 returns a present zero or negative recovery amount rather than dropping it");
    }
    setup.Set(1,0);setup.Set(7,0);sample.call.damage=3;recovery=99;
    Check(setup.Prepare(sample,recovery,error) && !recovery && sample.call.damage==3,
        "absent recovery clears caller's previous optional and leaves damage untouched by the recovery stage");
}

void AttributeDamageThresholds(Engine& engine)
{
    constexpr double epsilon=0x1p-23, roll5489=0x1.1574f7b6848dcp-3;
    const std::array chances{0.0,epsilon,std::nextafter(epsilon,1.0),std::numeric_limits<double>::quiet_NaN(),
        -1.0,roll5489,std::nextafter(roll5489,0.0),1.0,2.0};
    for(std::size_t i=0;i<chances.size();++i)
    {
        Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,8);
        ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};
        setup.Set(5,chances[i]);setup.Set(6,1);f.monster->HostState().damageEffect=3;
        const bool sampled=i==2 || i>=5, hit=i==5 || i>=7;
        Check(setup.Prepare(sample,recovery,error) && seed.calls==(sampled?1:0) && sample.call.damage==(hit?16:8) &&
            f.monster->HostState().damageEffect==(hit?8:3) && !recovery && error.message.empty(),
            "kind5 uses strict two-to-minus23 gate, skips NaN, includes exact roll equality and does not clamp finite probability");
    }
    for(const double ratio : {epsilon,std::nextafter(epsilon,1.0),std::numeric_limits<double>::quiet_NaN(),-1.0})
    {
        Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,8);
        ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};setup.Set(7,ratio);
        Check(setup.Prepare(sample,recovery,error) && seed.calls==0 && sample.call.damage==8 &&
            recovery.has_value()==(ratio>epsilon), "kind7 has its own strict epsilon gate and never samples entropy");
    }
    Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,3);
    ConfigError error;AttributeSeed seed;std::optional<float> recovery;
    setup.Set(1,0x1p-24);
    Check(setup.Prepare(sample,recovery,error) && sample.call.damage==std::nextafter(3.0f,4.0f),
        "kind1 multiplies in double before the single float commit");
}

void AttributeDamageSeedMutation(Engine& engine)
{
    Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,8);
    ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};
    setup.Set(1,0.5);setup.Set(5,1);setup.Set(6,1);setup.Set(7,0.25);
    const auto replacement=setup.weapons->CreateAttributes(WEAPON_AK47,55);
    Require(replacement && setup.weapons->SetAttribute(*replacement,1,CSOLWeaponAttributeValue{100.0},0) &&
        setup.weapons->SetAttribute(*replacement,5,CSOLWeaponAttributeValue{0.0},0) &&
        setup.weapons->SetAttribute(*replacement,6,CSOLWeaponAttributeValue{2.0},0) &&
        setup.weapons->SetAttribute(*replacement,7,CSOLWeaponAttributeValue{0.5},0), "prepare replacement key through real directory API");
    seed.callback=[&]
    {
        Check(sample.call.damage==12, "seed observes original kind1 prefix");sample.call.damage=7;
        Require(setup.weapons->SetAttributeKey(*setup.weapon,*replacement), "seed may change the existing weapon's attribute key");
    };
    Check(setup.Prepare(sample,recovery,error) && seed.calls==1 && sample.call.damage==21 && recovery==std::optional<float>{10.5f} &&
        f.monster->HostState().damageEffect==8,
        "seed mutation retains old chance but reads current damage and fresh key6/key7 without repeating key1");
}

void AttributeDamageGatesAndFailures(Engine& engine)
{
    {
        Fixture f(engine);auto* weapon=f.Weapon<CAK47>(WEAPON_AK47);f.player->m_pActiveItem=weapon;
        DamageSample sample(f,f.player,8);ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};
        Require(f.session->Weapons()->SetAttribute(0,1,CSOLWeaponAttributeValue{10.0},0), "default can contain value without extra object");
        Check(f.session->Weapons()->PrepareAttributeDamage(sample.call,sample.runtime,recovery,error,AttributeSeed::Draw) &&
            sample.call.damage==8 && !recovery && seed.calls==0,
            "one real default object skips whole attribute stage even when key-zero contains a modifier");
    }
    for(int gate=0;gate!=4;++gate)
    {
        Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,gate==0?nullptr:f.player,8);
        ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};setup.Set(1,1);setup.Set(5,1);
        if(gate==1)f.player->pev->flags|=FL_SPECTATOR;
        if(gate==2)f.player->m_pActiveItem=nullptr;
        if(gate==3)sample.runtime->players.reset();
        const bool ok=setup.Prepare(sample,recovery,error);
        Check(ok==(gate==1 || gate==2) && sample.call.damage==8 && !recovery && seed.calls==0 &&
            error.message.empty()==(gate==1 || gate==2),
            "world-source gap and missing player registry fail, while spectator or empty active weapon skip without entropy");
    }
    for(int failure=0;failure!=6;++failure)
    {
        Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,8);
        ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};
        setup.Set(1,0.5);setup.Set(5,1);setup.Set(6,1);setup.Set(7,0.25);f.monster->HostState().damageEffect=3;
        if(failure==0)setup.Set(1,std::numeric_limits<double>::quiet_NaN());
        if(failure==1)setup.Set(6,std::numeric_limits<double>::infinity());
        if(failure==2)setup.Set(7,std::numeric_limits<double>::infinity());
        if(failure==3)seed.callback=[] {throw std::runtime_error("controlled entropy failure");};
        if(failure==4){sample.call.damage=std::numeric_limits<float>::max();setup.Set(1,1);}
        if(failure==5)seed.callback=[&]{sample.ownerActive=false;throw std::runtime_error("retired entropy failure");};
        const float before=sample.call.damage;
        Check(!setup.Prepare(sample,recovery,error) && !recovery && error.message.empty()==(failure==5) &&
            sample.call.damage==((failure==0 || failure==4)?before:failure==2?24:12) &&
            f.monster->HostState().damageEffect==(failure==2?8:3) && seed.calls==((failure==0 || failure==4)?0:1) &&
            sample.call.Cancelled()==(failure==5),
            "nonfinite stage or entropy exception preserves completed prefixes and does not invent diagnostics after retirement");
    }
}

void AttributeDamageCancellation(Engine& engine)
{
    for(int cancellation=0;cancellation!=7;++cancellation)
    {
        Fixture f(engine);AttributeDamageSetup setup(f);DamageSample sample(f,f.player,8);
        ConfigError error;AttributeSeed seed;std::optional<float> recovery{99};
        setup.Set(1,0.5);setup.Set(5,1);setup.Set(6,1);setup.Set(7,0.25);f.monster->HostState().damageEffect=3;
        seed.callback=[&]
        {
            Check(sample.call.damage==12, "retiring entropy callback follows committed kind1");sample.call.damage=7;
            if(cancellation==0)sample.runtime->players=std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if(cancellation==1){f.players->Remove(EHANDLE(f.player));Require(f.players->SetProfile(*f.player,{}), "seed replaces player generation");}
            if(cancellation==2)sample.runtime->RequestClose();
            if(cancellation==3)sample.runtime->session=std::make_shared<CCSOLScenarioSession>();
            if(cancellation==4)setup.weapon->pev->flags|=FL_KILLME;
            if(cancellation==5)setup.weapons->Invalidate();
            if(cancellation==6)sample.ownerActive=false;
        };
        Check(!setup.Prepare(sample,recovery,error) && seed.calls==1 && sample.call.damage==7 && !recovery &&
            f.monster->HostState().damageEffect==3 && error.message.empty() && sample.call.Cancelled() && !sample.call.Current(),
            "entropy callback owner/state/session/access/weapon retirement cancels later multiplier, effect and recovery");
    }
}

void EnhancementLevelsAndDirectory(Engine& engine)
{
    Fixture f(engine); AttributeDamageSetup setup(f); DamageSample sample(f,f.player,8);
    Check(setup.weapons->EnhancementLevel(*setup.weapon)==0, "actual weapon starts with zero enhancement independently of attribute key");
    setup.Set(1,100); // This separate consumer must not apply property kind1.
    for (const std::uint16_t level : {0,1,8})
    {
        Require(setup.weapons->SetEnhancementLevel(*setup.weapon,level), "set actual uint16 enhancement level");
        ConfigError error;
        Check(setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && sample.call.damage==8 && error.message.empty(),
            "levels through eight need no enhancement directory and do not consume property damage");
    }
    Require(setup.weapons->SetEnhancementLevel(*setup.weapon,9), "first enhanced index is level nine");
    ConfigError missing;
    Check(!setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,missing) && !missing.message.empty() &&
        !sample.call.Cancelled() && sample.call.damage==8, "needed but unimported enhancement data fails without inventing zero percent");
    for (const int id : std::array<int,3>{WEAPON_KNIFE,WEAPON_HEGRENADE,9999})
    {
        setup.weapon->m_iId=id; ConfigError error;
        Check(setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && sample.call.damage==8 && error.message.empty(),
            "exact missing ItemTable key skips without native knife or HE selection substitution");
    }
    setup.weapon->m_iId=WEAPON_AK47;
    for (const auto& table : {std::map<std::int32_t,std::vector<std::uint32_t>>{},
        std::map<std::int32_t,std::vector<std::uint32_t>>{{10144,{25}}},
        std::map<std::int32_t,std::vector<std::uint32_t>>{{1036,{}}}})
    {
        Require(setup.weapons->ReplaceEnhancementDamage(table), "import known empty, unrelated or empty-vector directory");
        ConfigError error;
        Check(setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && sample.call.damage==8 && error.message.empty(),
            "known missing enhancement record or index is a successful unchanged result");
    }
    Require(setup.weapons->ReplaceEnhancementDamage({{1036,{25,50}}}), "import two explicit enhancement percentages");
    for (const std::uint16_t level : {9,10,11,65535})
    {
        Require(setup.weapons->SetEnhancementLevel(*setup.weapon,level), "set enhancement index boundary");
        sample.call.damage=8; ConfigError error;
        const float expected=level==9?10.0f:level==10?12.0f:8.0f;
        Check(setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && sample.call.damage==expected && error.message.empty(),
            "level minus nine selects its own entry; short vector and maximum uint16 do not wrap");
    }
}

void EnhancementArithmetic(Engine& engine)
{
    Fixture f(engine); AttributeDamageSetup setup(f); DamageSample sample(f,f.player,8);
    Require(setup.weapons->SetEnhancementLevel(*setup.weapon,9), "prepare first real enhancement entry");
    struct Row { std::uint32_t percent; float damage,expected; };
    // Literal results are independently rounded binary32 values, not a copy
    // of the production arithmetic used as an expected-value helper.
    constexpr Row rows[]={{25,8,10},{25,0,0},{25,-8,-10},{0,8,8},
        {0x80000000u,1,21474836.0f},{0xffffffffu,1,42949672.0f}};
    sample.call.bits=0x08000000; sample.call.context.bytes[5]=1;
    for (const auto& row : rows)
    {
        Require(setup.weapons->ReplaceEnhancementDamage({{1036,{row.percent}}}), "import unsigned DWORD percentage");
        sample.call.damage=row.damage; ConfigError error;
        Check(setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) &&
            std::bit_cast<std::uint32_t>(sample.call.damage)==std::bit_cast<std::uint32_t>(row.expected) && error.message.empty(),
            "enhancement preserves unsigned percentage, float32 commit, zero and negative damage without hint-message gates");
    }
    Require(setup.weapons->ReplaceEnhancementDamage({{1036,{100}}}), "prepare finite-input overflow boundary");
    sample.call.damage=std::numeric_limits<float>::max(); ConfigError error;
    Check(!setup.weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && !error.message.empty() &&
        !sample.call.Cancelled() && sample.call.damage==std::numeric_limits<float>::max(),
        "host nonfinite-result validation retains input; this is not an original ASM eligibility gate");
}

struct EnhancementWeaponProbe final : CBasePlayerWeapon
{
    struct State { int calls{}; bool isWeapon{true}; std::function<void()> callback; };
    std::shared_ptr<State> state=std::make_shared<State>();
    BOOL IsWeapon() override
    {
        const auto keep=state; ++keep->calls; const bool result=keep->isWeapon;
        const auto callback=keep->callback; if(callback)callback(); return result;
    }
    void OnDestroy() override { state.reset(); CBasePlayerWeapon::OnDestroy(); }
};

void EnhancementSourceHeads(Engine& engine)
{
    Fixture f(engine); const auto weapons=f.session->Weapons();
    auto* source=f.Entity<SourceProbe>(); const auto state=source->state;
    auto* match=f.Weapon<EnhancementWeaponProbe>(WEAPON_AK47);
    auto* head=f.Weapon<EnhancementWeaponProbe>(CSOL_WEAPON_CANNON);
    Require(weapons->SetEnhancementLevel(*match,9) && weapons->ReplaceEnhancementDamage({{1036,{25}}}), "configure actual fourth-slot matching entity");
    DamageSample sample(f,source,8);
    for (int variant=0;variant<4;++variant)
    {
        source->state->values=variant==3?std::vector<int>{WEAPON_AK47,CSOL_WEAPON_CANNON}:std::vector<int>{WEAPON_AK47};
        state->sourceCalls=state->isPlayerCalls=0; sample.call.damage=8; ConfigError error;
        match->state->isWeapon=variant!=2; head->m_pNext=match;
        f.player->m_pActiveItem=match; // A projectile never falls back to active.
        f.player->m_rgpPlayerItems[GRENADE_SLOT]=variant==1?head:match;
        Check(weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error) && error.message.empty() &&
            sample.call.damage==(variant==0?10.0f:8.0f) && state->isPlayerCalls==1 &&
            state->sourceCalls==(variant==2?1:2),
            "projectile selector includes slot4, considers only its head, requires IsWeapon and re-reads source for comparison");
    }
    head->m_pNext=nullptr; f.player->m_rgpPlayerItems[GRENADE_SLOT]=nullptr;
    f.player->m_pActiveItem=match; match->state->isWeapon=false;
    DamageSample direct(f,f.player,8); ConfigError error;
    Check(weapons->ApplyEnhancementDamage(direct.call,direct.runtime,error) && direct.call.damage==8 && error.message.empty(),
        "direct attacker source also requires the actual active item's IsWeapon predicate");
}

void EnhancementIdentityLifecycle(Engine& engine)
{
    Fixture f(engine); AttributeDamageSetup setup(f); auto* weapon=setup.weapon;
    Require(setup.weapons->SetEnhancementLevel(*weapon,10) && setup.weapons->ReplaceEnhancementDamage({{1036,{25,50}}}), "bind enhancement to real weapon EHANDLE");
    const EHANDLE old(weapon); auto* slot=weapon->edict(); const int index=Engine::Index(slot),serial=slot->serialnumber;
    f.player->m_pActiveItem=nullptr; weapon->m_pPlayer=nullptr;
    Check(setup.weapons->EnhancementLevel(*weapon)==10, "detaching ownership retains weapon-instance enhancement");
    auto* receiver=f.Player(2); weapon->m_pPlayer=receiver; receiver->m_pActiveItem=weapon; f.player=receiver;
    DamageSample transferred(f,receiver,8); ConfigError error;
    Check(setup.weapons->ApplyEnhancementDamage(transferred.call,transferred.runtime,error) && transferred.call.damage==12 && error.message.empty(),
        "same actual weapon keeps enhancement when a different registered player owns it");
    receiver->m_pActiveItem=nullptr; weapon->m_pPlayer=nullptr; Engine::Remove(slot);
    auto* ed=engine.Reserve(index,"enhancement_reuse");
    auto* replacement=GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<CAK47*>(&ed->v));
    replacement->m_iId=WEAPON_AK47; replacement->m_pPlayer=receiver; receiver->m_pActiveItem=replacement;
    DamageSample reused(f,receiver,8); ConfigError nextError;
    Check(ed==slot && ed->serialnumber!=serial && !old.Get<CBaseEntity>() && setup.weapons->EnhancementLevel(*replacement)==0 &&
        setup.weapons->ApplyEnhancementDamage(reused.call,reused.runtime,nextError) && reused.call.damage==8 && nextError.message.empty(),
        "actual free and same-edict serial reuse cannot inherit a previous weapon's level");
}

void EnhancementCancellation(Engine& engine)
{
    for(int boundary=0;boundary<8;++boundary)
    {
        Fixture f(engine); const auto weapons=f.session->Weapons();
        auto* source=f.Entity<SourceProbe>(); const auto sourceState=source->state; sourceState->values={WEAPON_AK47};
        auto* weapon=f.Weapon<EnhancementWeaponProbe>(WEAPON_AK47); const auto weaponState=weapon->state;
        const EHANDLE playerHandle(f.player),weaponHandle(weapon);
        f.player->m_pActiveItem=weapon; f.player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=weapon;
        Require(weapons->SetEnhancementLevel(*weapon,9) && weapons->ReplaceEnhancementDamage({{1036,{25}}}), "prepare callback cancellation with usable imported data");
        DamageSample sample(f,boundary==2 || boundary==7?static_cast<CBaseEntity*>(f.player):source,8);
        int callbacks{}; ConfigError error;
        const auto cancel=[&]
        {
            if(callbacks)return;
            ++callbacks;
            if(boundary==0)Engine::Remove(source->edict());
            if(boundary==1)sample.ownerActive=false;
            if(boundary==2 || boundary==3 || boundary==4)Engine::Remove(weapon->edict());
            if(boundary==5){f.players->Remove(playerHandle);Require(f.players->SetProfile(*f.player,{}), "source callback replaces actual registered player generation");}
            if(boundary==6)sample.runtime->players=std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if(boundary==7)Engine::Remove(f.player->edict());
        };
        if(boundary==1)sourceState->onPlayer=cancel;
        else if(boundary==2 || boundary==3 || boundary==7)weaponState->callback=cancel;
        else sourceState->onSource=[&]{if(boundary!=4 || sourceState->sourceCalls==2)cancel();};
        const bool result=weapons->ApplyEnhancementDamage(sample.call,sample.runtime,error);
        sourceState->onPlayer={}; sourceState->onSource={}; weaponState->callback={};
        Check(!result && sample.call.Cancelled() && !sample.call.Current() && error.message.empty() && sample.call.damage==8 && callbacks==1,
            "source/IsWeapon boundaries cancel on owner, player registry, source, weapon or player retirement without committing enhancement");
        // The deletion cases intentionally leave stale raw inventory pointers
        // during the consumer call, then clear them before fixture teardown.
        if(auto* player=playerHandle.Get<CBasePlayer>())
        { player->m_pActiveItem=nullptr; player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr; }
        if(auto* live=weaponHandle.Get<CBasePlayerWeapon>())live->m_pPlayer=nullptr;
    }
}

// This is an explicit service-boundary probe, not the original v720/v3EC
// policy or a 206 sender. Each recovery test installs its own allowed action.
struct RecoveryNotice final : CSOLScenarioRecoveryNotice
{
    int calls{};
    bool (*ownerValid)(const void*){};
    const void* ownerContext{};
    std::function<bool(CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)> action;
    bool NotifySelfRecovery(CSOLPlayerRecoveryCall& call, float amount,
        CSOLMonsterDeathContext& context, ConfigError& error,
        bool (*valid)(const void*), const void* borrowedContext) override
    {
        // Expose the borrowed predicate only while action is on the stack.
        // This probe is not the concrete original notification policy.
        struct Scope
        {
            RecoveryNotice& notice;
            bool (*previousValid)(const void*);
            const void* previousContext;
            ~Scope() { notice.ownerValid = previousValid; notice.ownerContext = previousContext; }
        } scope{*this, ownerValid, ownerContext};
        ownerValid = valid; ownerContext = borrowedContext;
        ++calls; const auto callback = action;
        if (!callback) throw std::runtime_error("recovery notice probe needs an explicit policy outcome");
        return callback(call, amount, context, error);
    }
};

struct RecoverySample
{
    Fixture& f;
    DamageSample hit;
    std::shared_ptr<RecoveryNotice> notice = std::make_shared<RecoveryNotice>();
    ConfigError error;
    bool applied{true};
    explicit RecoverySample(Fixture& fixture) : f(fixture), hit(f, f.player, 8)
    {
        f.player->pev->health = 40; f.player->pev->max_health = 100; f.player->pev->takedamage = DAMAGE_YES;
        notice->action = [](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { return true; };
        hit.runtime->recoveryNotice = notice;
    }
    bool Apply(float amount)
    { return f.players->ApplyAttributeRecovery(hit.call, hit.runtime, amount, applied, error); }
    std::uint64_t Add(const std::shared_ptr<Component>& component)
    {
        const auto token = f.players->RegisterKillComponent(EHANDLE(f.player), component);
        Require(token != 0, "register an actual recovery ability in the shared player registry"); return token;
    }
    void Special(std::optional<float> maximum)
    {
        CSOLScenarioPlayerProfile profile; profile.weaponEffectsBlocked21F2 = true;
        Require(f.players->SetProfile(*f.player, profile) &&
            f.players->SetSpecialRecoveryMaximum(EHANDLE(f.player), maximum), "trusted special recovery state");
    }
};

void RecoveryLimitsAndGates(Engine& engine)
{
    for (bool special : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); int callbacks{};
        if (special) { recovery.Special(60); f.player->pev->max_health = 15; }
        else Require(f.players->SetSpecialRecoveryMaximum(EHANDLE(f.player), 7), "ordinary branch ignores special cap");
        const float expected = special ? 60 : 75;
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
        { ++callbacks; Check(amount == expected - 40 && f.player->pev->health == expected, "ability sees branch cap and actual delta"); return true; };
        recovery.Add(component);
        Check(recovery.Apply(35) && recovery.applied && f.player->pev->health == expected &&
            recovery.notice->calls == 1 && callbacks == 1, "ordinary max_health and explicit special maximum are distinct");
    }
    for (int gate = 0; gate != 4; ++gate)
    {
        Fixture f(engine); RecoverySample recovery(f);
        if (gate == 0) f.player->pev->takedamage = DAMAGE_NO;
        if (gate == 1) f.player->pev->health = 100;
        if (gate == 2) { recovery.Special(30); }
        if (gate == 3) { recovery.Special(std::nullopt); f.player->pev->takedamage = DAMAGE_NO; }
        const float before = f.player->pev->health;
        Check(recovery.Apply(-5) && !recovery.applied && f.player->pev->health == before &&
            recovery.notice->calls == 0 && recovery.error.message.empty(),
            "takedamage and health-at-or-above-cap reject normally, even negative amounts and an unused missing special cap");
    }
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(std::nullopt);
        Check(!recovery.Apply(10) && !recovery.applied && f.player->pev->health == 40 &&
            recovery.notice->calls == 0 && !recovery.error.message.empty(), "selected special branch needs its real maximum producer");
    }
    {
        Fixture f(engine); RecoverySample recovery(f); f.player->pev->deadflag = DEAD_DEAD;
        Check(recovery.Apply(5) && recovery.applied && f.player->pev->health == 45,
            "recovery has no invented IsAlive gate when actual takedamage and cap allow");
    }
    for (bool special : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); const float maximum = std::numeric_limits<float>::max();
        f.player->pev->health = maximum / 2; f.player->pev->max_health = maximum;
        if (special) recovery.Special(maximum);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
        { Check(amount == maximum - maximum / 2 && std::isfinite(amount), "clamped positive overflow has a finite actual recovery delta"); return true; };
        Check(recovery.Apply(maximum) && recovery.applied && f.player->pev->health == maximum && recovery.notice->calls == 1,
            "finite positive operands clamp temporary infinity to the finite cap before validating the stored result");
    }
}

void RecoveryDeadlineAndSetters(Engine& engine)
{
    const std::array deadlines{0.0, -1.0, 9.0, 10.0, std::nextafter(10.0, 11.0), 11.0};
    for (const double deadline : deadlines)
    {
        Fixture f(engine); RecoverySample recovery(f);
        const auto state = f.players->Find(EHANDLE(f.player));
        Check(state && state->recoveryBlockedUntil == 0 && !state->specialRecoveryMaximum,
            "new authority has verified zero deadline and no invented special limit");
        Require(f.players->SetRecoveryBlockedUntil(EHANDLE(f.player), deadline), "install exact double deadline");
        Check(recovery.Apply(0) && recovery.applied == (deadline <= 10) && f.player->pev->health == 40 &&
            state->recoveryBlockedUntil == deadline && recovery.notice->calls == 0,
            "strict deadline gate permits equality and preserves sub-float double precision");
    }
    Fixture f(engine); RecoverySample recovery(f); const EHANDLE handle(f.player);
    Require(f.players->SetRecoveryBlockedUntil(handle, 12) && f.players->SetSpecialRecoveryMaximum(handle, 80), "finite owned recovery settings");
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        Check(!f.players->SetRecoveryBlockedUntil(handle, invalid) && f.players->Find(handle)->recoveryBlockedUntil == 12,
            "nonfinite deadline setter leaves previous owned value intact");
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        Check(!f.players->SetSpecialRecoveryMaximum(handle, invalid) && f.players->Find(handle)->specialRecoveryMaximum == std::optional<float>{80},
            "nonfinite special cap setter leaves previous owned value intact");
    Check(f.players->SetSpecialRecoveryMaximum(handle, std::nullopt) && !f.players->Find(handle)->specialRecoveryMaximum,
        "trusted producer can explicitly revoke special cap availability");
    const float saved = engine.globals.time; engine.globals.time = std::numeric_limits<float>::quiet_NaN();
    Check(!recovery.Apply(1) && !recovery.applied && f.player->pev->health == 40 && !recovery.error.message.empty(),
        "positive recovery deadline requires a finite engine clock");
    engine.globals.time = saved;
}

void RecoveryAmountsAndContext(Engine& engine)
{
    constexpr std::array<std::uint8_t, 32> expectedContext{
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0};
    for (bool special : {false, true}) for (const float amount : {0.0f, -50.0f, 20.0f})
    {
        Fixture f(engine); RecoverySample recovery(f); if (special) recovery.Special(100);
        recovery.hit.call.context.bytes.fill(0x5a); const auto hitContext = recovery.hit.call.context.bytes;
        f.player->m_bitsDamageType = DMG_BULLET | DMG_BURN | DMG_POISON;
        const int damageTypes = f.player->m_bitsDamageType, hitBits = recovery.hit.call.bits;
        int callbacks{}; auto component = std::make_shared<Component>();
        const auto inspect = [&](CSOLPlayerRecoveryCall& call, float delta, CSOLMonsterDeathContext& context)
        {
            Check(call.Current() == f.player && call.Player() == f.player && call.Runtime() == recovery.hit.runtime &&
                &context != &recovery.hit.call.context && context.bytes == expectedContext && recovery.hit.call.Current() == f.monster &&
                recovery.hit.call.context.bytes == hitContext && recovery.hit.call.bits == hitBits && f.player->m_bitsDamageType == damageTypes &&
                delta == amount && f.player->pev->health == 40 + amount,
                "kind7 preserves damage bits and hit context while providing a new default recovery context after health write");
        };
        recovery.notice->action = [&](CSOLPlayerRecoveryCall& call, float delta, CSOLMonsterDeathContext& context, ConfigError&)
        { inspect(call, delta, context); return true; };
        component->recovery = [&](CSOLPlayerRecoveryCall& call, float delta, CSOLMonsterDeathContext& context, ConfigError&)
        { ++callbacks; inspect(call, delta, context); return true; };
        recovery.Add(component);
        Check(recovery.Apply(amount) && recovery.applied && callbacks == 1 && recovery.notice->calls == (amount > 0 ? 1 : 0),
            "zero and negative recovery remain applied and notify abilities, with no lower clamp or nonpositive display notice");
    }
}

void RecoveryNoticeMutation(Engine& engine)
{
    Fixture f(engine); RecoverySample recovery(f); std::vector<int> order; CSOLMonsterDeathContext* shared{};
    auto first = std::make_shared<Component>(), second = std::make_shared<Component>();
    recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float delta, CSOLMonsterDeathContext& context, ConfigError&)
    {
        order.push_back(1); Check(delta == 10 && f.player->pev->health == 50, "notice observes initial actual recovery after health write");
        shared = &context; context.bytes[20] = 7; f.player->pev->health = 65; return true;
    };
    first->recovery = [&](CSOLPlayerRecoveryCall&, float delta, CSOLMonsterDeathContext& context, ConfigError&)
    {
        order.push_back(2); Check(delta == 25 && &context == shared && context.bytes[20] == 7,
            "ability delta is resampled after notice and shares its context");
        context.bytes[20] = 9; f.player->pev->health = 80; return true;
    };
    second->recovery = [&](CSOLPlayerRecoveryCall&, float delta, CSOLMonsterDeathContext& context, ConfigError&)
    {
        order.push_back(3); Check(delta == 25 && f.player->pev->health == 80 && &context == shared && context.bytes[20] == 9,
            "later abilities share mutated context but retain one sampled delta despite earlier ability health changes"); return true;
    };
    recovery.Add(first); recovery.Add(second);
    Check(recovery.Apply(10) && recovery.applied && order == std::vector<int>({1,2,3}) && f.player->pev->health == 80,
        "notice then registered recovery abilities keep original observable ordering");
}

void RecoveryRegistryMutation(Engine& engine)
{
    Fixture f(engine); RecoverySample recovery(f); const EHANDLE handle(f.player); std::vector<int> order;
    auto first = std::make_shared<Component>(), removed = std::make_shared<Component>(), disabled = std::make_shared<Component>();
    auto fromNotice = std::make_shared<Component>(), fromAbility = std::make_shared<Component>();
    auto callback = [&](int id)
    { return [&, id](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { order.push_back(id); return true; }; };
    removed->recovery = callback(2); disabled->recovery = callback(3);
    fromNotice->recovery = callback(4); fromAbility->recovery = callback(5);
    std::uint64_t firstToken{}, removedToken{}, disabledToken{}, addedToken{};
    first->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
    {
        order.push_back(1);
        Require(f.players->RemoveKillComponent(handle, removedToken) &&
            f.players->SetKillComponentEnabled(handle, disabledToken, false), "earlier recovery ability removes and disables later entries");
        addedToken = recovery.Add(fromAbility);
        Require(f.players->RemoveKillComponent(handle, firstToken), "recovery ability unregisters itself"); return true;
    };
    firstToken = recovery.Add(first); removedToken = recovery.Add(removed); disabledToken = recovery.Add(disabled);
    recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
    {
        order.push_back(0); const auto token = recovery.Add(fromNotice);
        Check(token == recovery.Add(fromNotice), "duplicate recovery ability registration is idempotent"); return true;
    };
    Check(recovery.Apply(10) && order == std::vector<int>({0,1,4}) && addedToken,
        "notice additions join this dispatch; ability additions wait while removal and disable take effect immediately");
    order.clear(); Require(f.players->SetKillComponentEnabled(handle, disabledToken, true), "reenable retained recovery subscription");
    Check(recovery.Apply(0) && order == std::vector<int>({3,4,5}) && recovery.notice->calls == 1,
        "next recovery sees surviving registry order, reenabled entry and prior ability addition");
}

void RecoveryFailures(Engine& engine)
{
    for (int failure = 0; failure != 9; ++failure)
    {
        Fixture f(engine); RecoverySample recovery(f); int callbacks{}; float amount = 10;
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++callbacks; return true; };
        recovery.Add(component);
        if (failure == 0) recovery.hit.runtime->recoveryNotice.reset();
        if (failure == 1) recovery.notice->action = [](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError& error)
            { error.message = "controlled notice failure"; return false; };
        if (failure == 2) amount = std::numeric_limits<float>::quiet_NaN();
        if (failure == 3) amount = std::numeric_limits<float>::infinity();
        if (failure == 4) f.player->pev->health = std::numeric_limits<float>::quiet_NaN();
        if (failure == 5) f.player->pev->max_health = std::numeric_limits<float>::infinity();
        if (failure == 6)
        { f.player->pev->health = -std::numeric_limits<float>::max() * 0.75f; f.player->pev->max_health = std::numeric_limits<float>::max(); amount = -std::numeric_limits<float>::max(); }
        if (failure == 7) recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
            { f.player->pev->health = std::numeric_limits<float>::quiet_NaN(); return true; };
        if (failure == 8)
        {
            f.player->pev->health = -std::numeric_limits<float>::max(); f.player->pev->max_health = std::numeric_limits<float>::max();
            amount = std::numeric_limits<float>::max();
            recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
                { f.player->pev->health = std::numeric_limits<float>::max(); return true; };
        }
        const float before = f.player->pev->health;
        Check(!recovery.Apply(amount) && recovery.applied == (failure < 2 || failure > 6) && callbacks == 0 &&
            !recovery.error.message.empty() && !recovery.hit.call.Cancelled() && recovery.hit.call.Current() == f.monster,
            "invalid arithmetic or required notice failure is not identity cancellation and does not call later abilities");
        if (failure < 2) Check(f.player->pev->health == 50, "missing or failing notification preserves committed health prefix");
        else if (failure < 7) Check((std::isnan(before) && std::isnan(f.player->pev->health)) || f.player->pev->health == before,
            "pre-write failure preserves entrance health");
        else Check(failure == 7 ? std::isnan(f.player->pev->health) : f.player->pev->health == std::numeric_limits<float>::max(),
            "invalid delta after notice retains the callback's health mutation");
    }
    Fixture f(engine); RecoverySample recovery(f); recovery.hit.runtime->recoveryNotice.reset();
    int callbacks{}; auto first = std::make_shared<Component>(), later = std::make_shared<Component>();
    first->recovery = [](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError& error)
        { error.message = "controlled recovery ability failure"; return false; };
    later->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++callbacks; return true; };
    recovery.Add(first); recovery.Add(later);
    Check(!recovery.Apply(-5) && recovery.applied && f.player->pev->health == 35 && callbacks == 0 &&
        !recovery.error.message.empty(), "nonpositive recovery needs no notice policy; ability failure retains health and stops later abilities");
}

void RecoveryCancellation(Engine& engine)
{
    for (bool atNotice : {true, false}) for (int cancellation = 0; cancellation != 7; ++cancellation)
    {
        Fixture f(engine); RecoverySample recovery(f); const EHANDLE original(f.player); int callbacks{}, laterCalls{};
        float observedPrefix{}; auto first = std::make_shared<Component>(), later = std::make_shared<Component>();
        const auto cancel = [&]
        {
            observedPrefix = f.player->pev->health;
            if (cancellation == 0) recovery.hit.ownerActive = false;
            if (cancellation == 1) recovery.hit.runtime->RequestClose();
            if (cancellation == 2) recovery.hit.runtime->session = std::make_shared<CCSOLScenarioSession>();
            if (cancellation == 3) recovery.hit.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if (cancellation == 4)
            { f.players->Remove(original); Require(f.players->SetProfile(*f.player, {}), "replace registered recovery state during callback"); }
            if (cancellation == 5) f.players->Invalidate();
            if (cancellation == 6)
            {
                Engine::Remove(f.player->edict()); f.player = f.Player(1); f.player->pev->health = 73;
                Check(!original.Get<CBaseEntity>(), "actual SDK free/recreate changes player edict generation");
            }
        };
        recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
            { if (atNotice) cancel(); return true; };
        first->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
            { ++callbacks; if (!atNotice) cancel(); return true; };
        later->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++laterCalls; return true; };
        recovery.Add(first); recovery.Add(later);
        Check(!recovery.Apply(10) && recovery.applied && observedPrefix == 50 && callbacks == (atNotice ? 0 : 1) &&
            laterCalls == 0 && recovery.error.message.empty() && f.player->pev->health == (cancellation == 6 ? 73 : 50) &&
            recovery.hit.call.Cancelled() && !recovery.hit.call.Current(),
            "notice and ability retirement stop subsequent callbacks without undoing applied health or touching a replacement player");
    }
    for (bool reset : {true, false})
    {
        Fixture f(engine); RecoverySample recovery(f); int callbacks{}; auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++callbacks; return true; };
        recovery.Add(component);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
        {
            if (reset) recovery.hit.runtime->recoveryNotice.reset();
            else recovery.hit.runtime->recoveryNotice = std::make_shared<RecoveryNotice>();
            return true;
        };
        Check(!recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && callbacks == 0 &&
            recovery.error.message.empty() && recovery.hit.call.Cancelled() && !recovery.hit.call.Current(),
            "retired notification policy explicitly cancels the hit and cannot authorize remaining recovery callbacks");
    }
}

void AttributeRecoveryIntegration(Engine& engine)
{
    for (int gate = 0; gate != 3; ++gate)
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f); int callbacks{};
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
            { ++callbacks; Check(amount == 6, "whole attribute stage applies final damage times kind7"); return true; };
        recovery.Add(component);
        if (gate == 1) Require(f.players->SetRecoveryBlockedUntil(EHANDLE(f.player), 11), "whole-stage normal recovery time rejection");
        if (gate == 2) f.player->pev->health = 100;
        Check(attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) &&
            recovery.hit.call.damage == 12 && f.player->pev->health == (gate == 0 ? 46 : gate == 1 ? 40 : 100) &&
            callbacks == (gate == 0 ? 1 : 0) && recovery.notice->calls == (gate == 0 ? 1 : 0),
            "whole attribute stage consumes present recovery but normal blocked/full health does not reject the hit");
    }
    for (bool retireDirectory : {false, true})
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f); int callbacks{};
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
            { ++callbacks; return true; };
        recovery.Add(component);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError& error)
        {
            if (retireDirectory) { attributes.weapons->Invalidate(); return true; }
            error.message = "controlled integration notice failure"; return false;
        };
        Check(!attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) &&
            recovery.hit.call.damage == 12 && f.player->pev->health == 46 && recovery.notice->calls == 1 &&
            callbacks == 0 && recovery.error.message.empty() == retireDirectory &&
            recovery.hit.call.Cancelled() == retireDirectory,
            "failed notice or directory retirement stops abilities immediately and retains damage and health prefixes");
    }
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f); int firstCalls{}, laterCalls{};
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        auto first = std::make_shared<Component>(), later = std::make_shared<Component>();
        first->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
        {
            ++firstCalls; Check(amount == 6 && f.player->pev->health == 46, "first ability observes committed whole-stage recovery");
            attributes.weapons->Invalidate(); return true;
        };
        later->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&)
            { ++laterCalls; return true; };
        recovery.Add(first); recovery.Add(later);
        Check(!attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) &&
            recovery.hit.call.damage == 12 && f.player->pev->health == 46 && recovery.notice->calls == 1 &&
            firstCalls == 1 && laterCalls == 0 && recovery.error.message.empty(),
            "directory cancellation inside first ability propagates before later abilities without rolling back either prefix");
    }
}

void RecoveryNoticeOwnerGuard(Engine& engine)
{
    for (bool retireDirectory : {false, true})
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f);
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        int simulatedSends{}, abilities{}; auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
            { ++abilities; Check(amount == 6, "guarded notification precedes actual recovery ability delta"); return true; };
        recovery.Add(component);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall& call, float amount, CSOLMonsterDeathContext&, ConfigError&)
        {
            const auto predicate = recovery.notice->ownerValid;
            const auto* borrowed = recovery.notice->ownerContext;
            Check(predicate && borrowed && call.Current() == f.player && recovery.hit.call.Current() == f.monster && amount == 6 && f.player->pev->health == 46,
                "actual attribute stage forwards its nonempty synchronous owner predicate after committing recovery");
            if (!predicate || !borrowed) return false;
            Check(predicate(borrowed), "enclosing weapon directory is valid at notification entry");
            if (retireDirectory) attributes.weapons->Invalidate();
            const bool currentOwner = predicate(borrowed);
            Check(recovery.hit.call.Current() == f.monster && bool(call.Current()) == !retireDirectory && currentOwner == !retireDirectory,
                "directory retirement revokes player recovery and passed predicate while independent damage call remains current");
            // Explicit probe-only send boundary: no engine message is emitted.
            // The player recovery call also carries this enclosing lifetime.
            if (!currentOwner) return false;
            ++simulatedSends; return true;
        };
        const bool result = attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error);
        Check(result == !retireDirectory && simulatedSends == (retireDirectory ? 0 : 1) && abilities == (retireDirectory ? 0 : 1) &&
            recovery.hit.call.damage == 12 && f.player->pev->health == 46 && recovery.notice->calls == 1 &&
            recovery.error.message.empty(),
            "notification internal owner check stops simulated send and later abilities without reverting committed prefixes");
        Check(!recovery.notice->ownerValid && !recovery.notice->ownerContext,
            "probe does not retain borrowed weapon predicate or stack context after synchronous return");
    }
    {
        Fixture f(engine); RecoverySample recovery(f); int simulatedSends{}, abilities{};
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
            { ++abilities; Check(amount == 10, "direct recovery ability receives actual delta"); return true; };
        recovery.Add(component);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall& call, float, CSOLMonsterDeathContext&, ConfigError&)
        {
            Check(recovery.notice->ownerValid && recovery.notice->ownerContext &&
                recovery.notice->ownerValid(recovery.notice->ownerContext) && call.Current() == f.player,
                "standalone recovery has its own valid internal notice guard without a monster owner");
            ++simulatedSends; return true;
        };
        CSOLPlayerRecoveryCall direct(recovery.hit.runtime,EHANDLE(f.player));
        Check(f.players->ApplyRecovery(direct,10,recovery.applied,recovery.error) && recovery.applied && f.player->pev->health == 50 && simulatedSends == 1 && abilities == 1 &&
            !recovery.notice->ownerValid && !recovery.notice->ownerContext,
            "standalone positive recovery works with no optional enclosing owner and releases notice guard on return");
    }
}

void RecoveryNoticeNestedGuard(Engine& engine)
{
    Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f);
    attributes.Set(1, 0.5); attributes.Set(7, 0.5);
    int depth{}, simulatedSends{}; std::vector<float> deltas;
    auto component = std::make_shared<Component>();
    component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
        { deltas.push_back(amount); return true; };
    recovery.Add(component);
    recovery.notice->action = [&](CSOLPlayerRecoveryCall& call, float amount, CSOLMonsterDeathContext&, ConfigError&)
    {
        if (depth)
        {
            Check(depth == 1 && amount == 1 && recovery.notice->ownerValid && recovery.notice->ownerContext &&
                recovery.notice->ownerValid(recovery.notice->ownerContext) && call.Current() == f.player,
                "nested standalone recovery temporarily exposes its own valid predicate on the shared probe");
            ++simulatedSends; return true;
        }
        const auto predicate = recovery.notice->ownerValid;
        const auto* borrowed = recovery.notice->ownerContext;
        Check(predicate && borrowed && amount == 6, "outer attribute notification owns a borrowed nonempty predicate");
        if (!predicate || !borrowed) return false;
        ++depth; bool applied{}; ConfigError nestedError;
        CSOLPlayerRecoveryCall inner(call.Runtime(),call.PlayerHandle());
        const bool nested = f.players->ApplyRecovery(inner,1,applied,nestedError);
        --depth;
        Check(nested && applied && nestedError.message.empty() && f.player->pev->health == 47 &&
            recovery.notice->ownerValid == predicate && recovery.notice->ownerContext == borrowed && predicate(borrowed),
            "nested probe return restores the still-live outer predicate and context before continuing notification");
        if (!nested || !applied || !predicate(borrowed)) return false;
        ++simulatedSends; return true;
    };
    Check(attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) &&
        recovery.hit.call.damage == 12 && f.player->pev->health == 47 && recovery.notice->calls == 2 && simulatedSends == 2 &&
        deltas == std::vector<float>({1, 7}) && depth == 0 && recovery.error.message.empty() &&
        !recovery.notice->ownerValid && !recovery.notice->ownerContext,
        "real nested recovery samples its own delta then outer delta while borrowed probe fields unwind fully");
}

std::shared_ptr<CCSOLScenarioRecoveryNotice> InstallConcreteRecoveryNotice(RecoverySample& recovery)
{
    auto notice = std::make_shared<CCSOLScenarioRecoveryNotice>(recovery.hit.runtime);
    recovery.hit.runtime->recoveryNotice = notice; return notice;
}

bool RecoveryNumberPacket(const StatisticsWire::Packet& packet, Engine& engine, int playerIndex, std::uint32_t value)
{
    std::vector<std::uint8_t> expected{206, static_cast<std::uint8_t>(playerIndex),
        static_cast<std::uint8_t>(playerIndex >> 8), 6, 4};
    for (int shift = 0; shift != 32; shift += 8) expected.push_back(static_cast<std::uint8_t>(value >> shift));
    return packet.ended && packet.destination == MSG_ONE_UNRELIABLE && packet.id == engine.messages.at("MPToCL") &&
        packet.recipient == playerIndex && packet.bytes == expected && packet.writes == std::vector<char>({'b','s','b','b','l'});
}

void ConcreteRecoveryOrdinary(Engine& engine)
{
    for (const float amount : {10.0f, 0.25f})
    {
        Fixture f(engine); RecoverySample recovery(f); InstallConcreteRecoveryNotice(recovery);
        StatisticsWire wire; int offsets{}, abilities{};
        const auto modes = f.session->DamageDisplayModes();
        Check(modes && modes->Contains(15) && recovery.hit.runtime->room.floatingDamageDisplay == 1,
            "actual Combat session loaded explicit MODE fixture and verified room option initial byte");
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float delta, CSOLMonsterDeathContext&, ConfigError&)
        {
            ++abilities; Check(delta == amount && wire.packets.size() == 1 && wire.packets.front().ended,
                "actual concrete notification completes before recovery ability observes actual delta"); return true;
        };
        recovery.Add(component);
        engine.onOffset = [&](const edict_t* ed) { if (ed == f.player->edict()) ++offsets; };
        Check(recovery.Apply(amount) && recovery.applied && f.player->pev->health == 40 + amount && abilities == 1 &&
            offsets == 3 && wire.packets.size() == 1 && RecoveryNumberPacket(wire.packets.front(), engine, 1, amount == 10 ? 10 : 0) &&
            recovery.error.message.empty() && recovery.notice->calls == 0,
            "concrete self recovery writes actual 206 with source/target validity rereads and truncates positive fraction to zero");
        engine.onOffset = {};
    }
}

void ConcreteRecoveryEarlyGates(Engine& engine)
{
    for (int gate = 0; gate != 3; ++gate)
    {
        Fixture f(engine, 0, gate == 0); RecoverySample recovery(f); recovery.Special(100);
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; int offsets{}, abilities{};
        if (gate != 2) recovery.hit.runtime->room.floatingDamageDisplay = 0;
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float delta, CSOLMonsterDeathContext&, ConfigError&)
            { ++abilities; Check(delta == 10, "normal display skip does not suppress actual recovery ability"); return true; };
        recovery.Add(component);
        engine.onOffset = [&](const edict_t*) { ++offsets; };
        Check(recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && abilities == 1 && offsets == 0 &&
            wire.packets.empty() && recovery.error.message.empty(),
            "room disabled or real MODE excluding15 skips entity/special gates and messages while successful recovery continues");
        engine.onOffset = {};
    }
    for (int gate = 0; gate != 3; ++gate)
    {
        Fixture f(engine); auto* player = f.Player<StatisticsPlayer>(2); f.player = player;
        RecoverySample recovery(f); InstallConcreteRecoveryNotice(recovery); StatisticsWire wire;
        if (gate == 0) player->bot = true;
        if (gate == 1) player->pev->flags |= FL_DORMANT;
        if (gate == 2) player->pev->flags |= FL_SPECTATOR;
        Check(recovery.Apply(10) && recovery.applied && player->pev->health == 50 && wire.packets.empty() &&
            recovery.error.message.empty() && player->botQueries == (gate == 0 ? 1 : 0),
            "actual Bot or SDK dormant/spectator source gate declines only the concrete display, not health application");
    }
    {
        Fixture f(engine); RecoverySample recovery(f); const auto notice = InstallConcreteRecoveryNotice(recovery);
        StatisticsWire wire; CSOLMonsterDeathContext context; context.bytes[0x1c] = 1;
        CSOLPlayerRecoveryCall direct(recovery.hit.runtime,EHANDLE(f.player));
        Check(notice->NotifySelfRecovery(direct, 10, context, recovery.error, nullptr, nullptr) &&
            wire.packets.empty() && f.player->pev->health == 40 && recovery.error.message.empty(),
            "direct concrete notice respects nonzero context byte1C without rewriting health or sending a packet");
    }
}

void ConcreteRecoverySpecialPolicy(Engine& engine)
{
    for (int stateCase = 0; stateCase != 7; ++stateCase)
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(100);
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; int abilities{};
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++abilities; return true; };
        recovery.Add(component);
        std::optional<std::uint8_t> displayState; std::optional<float> deadline;
        if (stateCase == 1) displayState = 0;
        if (stateCase == 2) displayState = 2;
        if (stateCase >= 3) displayState = 1;
        if (stateCase == 4) deadline = 9;
        if (stateCase == 5) deadline = 10;
        if (stateCase == 6) deadline = 11;
        if (stateCase == 6) Require(f.players->SetDisplayAbilities(EHANDLE(f.player), {}),
            "explicitly revoke initialized display directory for missing-owner negative case");
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), displayState, deadline),
            "trusted special display state is installed through actual public owner API");
        const bool expected = stateCase == 1 || stateCase == 2 || stateCase == 4 || stateCase == 5;
        const bool result = recovery.Apply(10);
        Check(result == expected && recovery.applied && f.player->pev->health == 50 && abilities == (expected ? 1 : 0) &&
            wire.packets.size() == (expected ? 1 : 0) && recovery.error.message.empty() == expected &&
            !recovery.hit.call.Cancelled() && recovery.hit.call.Current() == f.monster,
            "known non1 state or expired/equal deadline allows display; unknown special policy fails after health prefix");
        if (expected) Check(wire.packets.size() == 1 && RecoveryNumberPacket(wire.packets.front(), engine, 1, 10),
            "allowed special branch uses real self recovery writer");
        else
        {
            const std::string expectedError = stateCase == 0 ? "self recovery special display state is unavailable" :
                stateCase == 3 ? "self recovery special display deadline is unavailable" :
                "scenario display ability directory is unavailable";
            Check(recovery.error.message == expectedError,
                "missing special state/deadline or future-interval ability28/29/3 is explicit rather than default allow");
        }
    }
}

void ConcreteRecoveryDirectoryCancellation(Engine& engine)
{
    for (int boundary = 1; boundary != 4; ++boundary)
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f);
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire;
        attributes.Set(1, 0.5); attributes.Set(7, 0.5); int offsets{}, abilities{};
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++abilities; return true; };
        recovery.Add(component);
        engine.onOffset = [&](const edict_t* ed)
        {
            if (ed == f.player->edict() && ++offsets == boundary)
            {
                attributes.weapons->Invalidate();
                Check(recovery.hit.call.Current() == f.monster, "OFFSET retirement leaves independent hit current but revokes real directory");
            }
        };
        Check(!attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) &&
            recovery.hit.call.damage == 12 && f.player->pev->health == 46 && offsets == boundary && abilities == 0 &&
            wire.packets.empty() && recovery.notice->calls == 0 && recovery.error.message.empty() &&
            recovery.hit.call.Cancelled() && !recovery.hit.call.Current(),
            "actual notification stops after each source/target OFFSET cancellation before sending or invoking abilities");
        engine.onOffset = {};
    }
}

// Only the original callable Clone boundary is controlled. Classification is
// always computed by the real Feature3 records and per-node clock consumer.
struct DisplayConditionProbe final : csol::scenario::Feature3Condition
{
    int* clones{};
    std::function<void()> onClone;
    bool success{true};
    ConfigError cloneError;
    bool Clone(std::unique_ptr<csol::scenario::Feature3Condition>& result, ConfigError& error) const override
    {
        if (clones) ++*clones;
        const auto callback = onClone;
        if (callback) callback();
        result = std::make_unique<DisplayConditionProbe>();
        if (!success) error = cloneError;
        return success;
    }
};

std::shared_ptr<CSOLScenarioDisplayAbilities> InstallDisplayAbilities(Fixture& f)
{
    auto directory = std::make_shared<CSOLScenarioDisplayAbilities>();
    Require(f.players->SetDisplayAbilities(EHANDLE(f.player), directory), "trusted producer binds actual display ability directory");
    return directory;
}

void Feature3PlayerQueries(Engine& engine)
{
    Fixture f(engine); const EHANDLE handle(f.player); ConfigError error; bool active = true, allowed = false;
    const auto initial = f.players->Find(handle)->displayAbilities;
    Check(initial && !initial->feature28 && !initial->feature29 && !initial->feature3 &&
        f.players->Feature3Active(handle, active, error, nullptr, nullptr) && !active &&
        f.players->SpecialRecoveryDisplayAllowed(handle, allowed, error, nullptr, nullptr) && allowed && error.message.empty(),
        "actual player registration initializes known absent mode15 display keys");
    Require(f.players->SetDisplayAbilities(handle, {}), "explicitly revoke initialized directory for unknown-owner queries");
    active = true; allowed = false;
    Check(!f.players->Feature3Active(handle, active, error, nullptr, nullptr) && active &&
        error.message == "scenario display ability directory is unavailable", "unknown directory is a query failure, not inactive");
    error = {};
    Check(!f.players->SpecialRecoveryDisplayAllowed(handle, allowed, error, nullptr, nullptr) && !allowed &&
        error.message == "scenario display ability directory is unavailable", "unknown display directory is not implicit permission");
    const auto directory = InstallDisplayAbilities(f); error = {};
    Check(f.players->Feature3Active(handle, active, error, nullptr, nullptr) && !active &&
        f.players->SpecialRecoveryDisplayAllowed(handle, allowed, error, nullptr, nullptr) && allowed && error.message.empty(),
        "explicitly empty authoritative directory confirms feature3 absence and allows recovery display");
    directory->feature28 = 1;
    Check(f.players->SpecialRecoveryDisplayAllowed(handle, allowed, error, nullptr, nullptr) && allowed,
        "known absent feature3 permits even a known category6 ability");
    directory->feature3 = std::make_shared<csol::scenario::Feature3>();
    Check(f.players->Feature3Active(handle, active, error, nullptr, nullptr) && !active,
        "present actual empty Feature3 tree is inactive");
    Require(f.players->SetDisplayAbilities(handle, {}), "trusted producer may revoke directory knowledge");
    active = true; error = {};
    Check(!f.players->Feature3Active(handle, active, error, nullptr, nullptr) && active && !error.message.empty(),
        "revoked known-empty owner returns to unknown instead of retaining cached inactive result");
}

void Feature3RecoveryRules(Engine& engine)
{
    for (int rule = 0; rule != 6; ++rule)
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(100);
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), 1, 1000), "future special display interval");
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; int abilities{}, clones{};
        const auto directory = InstallDisplayAbilities(f);
        if (rule != 0)
        {
            directory->feature3 = std::make_shared<csol::scenario::Feature3>();
            const auto condition = std::make_shared<DisplayConditionProbe>(); condition->clones = &clones;
            const bool allEnded = rule == 3;
            Require(directory->feature3->Put(-4, {{0, 0, 0, 9}, {}, {}}) &&
                directory->feature3->Put(8, {{0, 0, allEnded ? 0.0f : 20.0f, allEnded ? 10.0f : 30.0f}, condition, {}}),
                "actual signed-key records include ended first node and dynamically classified second node");
            if (rule == 1 || rule == 3) directory->feature28 = 1;
            if (rule == 2) directory->feature29 = 0.5f;
            if (rule == 4) { directory->feature28 = 0; directory->feature29 = std::numeric_limits<float>::quiet_NaN(); }
            if (rule == 5) directory->feature29 = -1.0f;
        }
        bool active{}, allowed{}; ConfigError queryError;
        const bool expectedActive = rule != 0 && rule != 3;
        const bool expectedAllowed = rule != 1 && rule != 2;
        Check(f.players->Feature3Active(EHANDLE(f.player), active, queryError, nullptr, nullptr) && active == expectedActive &&
            f.players->SpecialRecoveryDisplayAllowed(EHANDLE(f.player), allowed, queryError, nullptr, nullptr) &&
            allowed == expectedAllowed && queryError.message.empty(),
            "player and display consumers share real classification while key28/key29 select the category gate");
        const int beforeRecoveryClones = clones;
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
            { ++abilities; Check(amount == 10, "display suppression does not cancel successful recovery ability delta"); return true; };
        recovery.Add(component);
        Check(recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && abilities == 1 &&
            wire.packets.size() == (expectedAllowed ? 1 : 0) && recovery.error.message.empty(),
            "category6/7 with a non3 record normally suppresses packet; known empty/all3/NaN or negative29 allows it");
        if (expectedAllowed) Check(wire.packets.size() == 1 && RecoveryNumberPacket(wire.packets.front(), engine, 1, 10),
            "allowed dynamic classification reaches actual self recovery writer");
        Check(clones - beforeRecoveryClones == ((rule >= 1 && rule <= 3) ? 1 : 0),
            "no category6/7 skips Feature3 cloning even when a live non3 record exists");
    }
}

void Feature3CloneTiming(Engine& engine)
{
    for (bool changeClock : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(100);
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), 1, 1000), "timing case remains in future special interval");
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire;
        const auto directory = InstallDisplayAbilities(f); directory->feature28 = 1;
        const auto feature = directory->feature3 = std::make_shared<csol::scenario::Feature3>();
        int clones{}; auto condition = std::make_shared<DisplayConditionProbe>(); condition->clones = &clones;
        condition->onClone = [&]
        {
            if (changeClock) engine.globals.time = 40;
            else Require(feature->SetTiming(7, {0, 0, 20, 30}), "clone updates live C/D before classifier samples them");
        };
        Require(feature->Put(7, {{0,0,changeClock ? 20.0f : 0.0f,changeClock ? 30.0f : 0.0f}, condition, {}}),
            "timing fixture binds an actual clonable condition to the real record");
        bool active{}; ConfigError error;
        Check(f.players->Feature3Active(EHANDLE(f.player), active, error, nullptr, nullptr) && active == !changeClock && clones == 1,
            "player query observes clone-mutated C/D or current time instead of cached classifications");
        Check(recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && clones == 2 &&
            wire.packets.size() == (changeClock ? 1 : 0) && recovery.error.message.empty(),
            "actual recovery re-queries live Feature3 timing on its own invocation");
        engine.globals.time = 10;
    }
}

CSOLMonsterDamageCall IndependentHit(DamageSample& sample, float damage)
{
    return CSOLMonsterDamageCall(sample.spawn, EHANDLE(sample.f.player), EHANDLE(sample.f.player),
        damage, DMG_BULLET, {}, sample.ownerActive,
        [](const void* context) { return static_cast<const DamageSample*>(context)->ownerActive; }, &sample);
}

void Feature3RecoveryCancellation(Engine& engine)
{
    for (int cancellation = 0; cancellation != 6; ++cancellation)
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); RecoverySample recovery(f); recovery.Special(100);
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), 1, 1000), "cancel case enters dynamic display policy");
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire;
        const auto directory = InstallDisplayAbilities(f); directory->feature28 = 1;
        const auto feature = directory->feature3 = std::make_shared<csol::scenario::Feature3>();
        int clones{}, abilities{}; auto condition = std::make_shared<DisplayConditionProbe>(); condition->clones = &clones;
        condition->onClone = [&]
        {
            Check(f.player->pev->health == 46 && recovery.hit.call.damage == 12, "clone callback follows both damage and health prefixes");
            if (cancellation == 0) Require(f.players->SetDisplayAbilities(EHANDLE(f.player), std::make_shared<CSOLScenarioDisplayAbilities>()),
                "clone replaces the player ability directory identity");
            if (cancellation == 1) directory->feature3 = std::make_shared<csol::scenario::Feature3>();
            if (cancellation == 2) f.players->Remove(EHANDLE(f.player));
            if (cancellation == 3) attributes.weapons->Invalidate();
            if (cancellation == 4) feature->Invalidate();
            if (cancellation == 5) Require(feature->Put(5, {{0,0,0,0}, {}, {}}), "clone replaces current node identity");
        };
        Require(feature->Put(5, {{0,0,0,0}, condition, {}}), "clone retirement interrupts a real otherwise-ended record");
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++abilities; return true; };
        recovery.Add(component);
        Check(!attributes.weapons->ApplyAttributeDamage(recovery.hit.call, recovery.hit.runtime, recovery.error) && clones == 1 &&
            recovery.hit.call.damage == 12 && f.player->pev->health == 46 && abilities == 0 && wire.packets.empty() &&
            recovery.error.message.empty() && recovery.hit.call.Cancelled() && !recovery.hit.call.Current(),
            "clone directory/feature/player/outer-weapon/node retirement explicitly cancels without rollback");
        if (cancellation == 0 || cancellation == 1 || cancellation == 5)
        {
            auto next = IndependentHit(recovery.hit, 8); ConfigError nextError;
            Check(attributes.weapons->ApplyAttributeDamage(next, recovery.hit.runtime, nextError) &&
                !next.Cancelled() && next.Current() == f.monster && next.damage == 12 && f.player->pev->health == 52 &&
                abilities == 1 && clones == 1 && nextError.message.empty() && wire.packets.size() == 1 &&
                RecoveryNumberPacket(wire.packets.front(), engine, 1, 6) && recovery.hit.call.Cancelled(),
                "new hit on the same actual monster consumes replacement state and does not inherit the old hit cancellation");
        }
    }
}

void DamageCallCancellationIdentity(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 8);
    struct Owner { bool active{true}; mutable int checks{}; } owner;
    CSOLMonsterDamageCall call(sample.spawn, EHANDLE(f.player), EHANDLE(f.player), 8, DMG_BULLET, {}, owner.active,
        [](const void* context) { const auto& value = *static_cast<const Owner*>(context); ++value.checks; return value.active; }, &owner);
    Require(call.Current() == f.monster, "direct cancellation case starts with a live actual damage lease");
    call.damage = 12; call.context.bytes[9] = 37;
    f.player->pev->health = 46; f.monster->pev->health = 70; f.monster->pev->armorvalue = 3;
    const int checked = owner.checks;
    Check(!call.Cancel() && call.Cancelled() && !call.Cancel() && !call.Current() && !call.Player() &&
        !call.Attacker() && !call.Inflictor() && owner.checks == checked && call.damage == 12 &&
        call.OriginalDamage() == 8 && call.context.bytes[9] == 37 && f.player->pev->health == 46 &&
        f.monster->pev->health == 70 && f.monster->pev->armorvalue == 3 && call.Spawn().Current() == f.monster,
        "Cancel is idempotent, rejects reads before owner callbacks and preserves committed scalar/entity state and spawn lease");
    auto next = IndependentHit(sample, 5);
    Check(!sample.call.Cancelled() && sample.call.Current() == f.monster && !next.Cancelled() && next.Current() == f.monster,
        "sibling and newly constructed calls remain independently current on the same monster");
}

void RecoveryNestedCancellation(Engine& engine)
{
    for (bool cancelOuter : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); int depth{}, abilities{}; std::vector<float> deltas;
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
            { ++abilities; deltas.push_back(amount); return true; };
        recovery.Add(component);
        recovery.notice->action = [&](CSOLPlayerRecoveryCall& outer, float, CSOLMonsterDeathContext&, ConfigError&)
        {
            if (depth) return true;
            ++depth;
            auto nested = IndependentHit(recovery.hit, 1); bool applied = true; ConfigError error;
            if (cancelOuter) outer.Cancel(); else nested.Cancel();
            const bool result = f.players->ApplyAttributeRecovery(nested, recovery.hit.runtime, 1, applied, error);
            --depth;
            Check(result == cancelOuter && applied == cancelOuter && nested.Cancelled() == !cancelOuter &&
                outer.Cancelled() == cancelOuter && error.message.empty() &&
                f.player->pev->health == (cancelOuter ? 51 : 50),
                "nested recovery owns its own cancellation even when sharing the runtime, recipient and spawn lease");
            return true;
        };
        const bool result = recovery.Apply(10);
        Check(result == !cancelOuter && recovery.applied && recovery.hit.call.Cancelled() == cancelOuter &&
            abilities == 1 && deltas == std::vector<float>{cancelOuter ? 1.0f : 10.0f} && depth == 0 &&
            recovery.notice->calls == (cancelOuter ? 2 : 1) && recovery.error.message.empty() &&
            f.player->pev->health == (cancelOuter ? 51 : 50),
            "inner cancellation leaves outer recovery valid; outer cancellation retains nested health and skips only outer abilities");
    }
}

void RecoveryFailureIsNotCancellation(Engine& engine)
{
    {
        Fixture f(engine); RecoverySample recovery(f); bool applied = true; ConfigError error;
        Check(!f.players->ApplyAttributeRecovery(recovery.hit.call, {}, 10, applied, error) && !applied &&
            !recovery.hit.call.Cancelled() && recovery.hit.call.Current() == f.monster && f.player->pev->health == 40,
            "missing runtime at entry is not an observed mid-call cancellation");
    }
    for (int failure = 0; failure != 4; ++failure)
    {
        Fixture f(engine); RecoverySample recovery(f); int firstCalls{}, laterCalls{};
        auto first = std::make_shared<Component>(), later = std::make_shared<Component>();
        recovery.notice->action = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError& error)
        {
            if (failure >= 2) return true;
            if (failure == 1) error = {23, "controlled notice rejection"};
            return false;
        };
        first->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError& error)
        {
            ++firstCalls;
            if (failure == 3) error = {24, "controlled ability rejection"};
            return false;
        };
        later->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++laterCalls; return true; };
        recovery.Add(first); recovery.Add(later);
        Check(!recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && firstCalls == (failure >= 2 ? 1 : 0) &&
            laterCalls == 0 && recovery.notice->calls == 1 && !recovery.hit.call.Cancelled() &&
            recovery.hit.call.Current() == f.monster && recovery.error.message.empty() == (failure % 2 == 0),
            "replaceable notice or ability false is a genuine failure, including an empty diagnostic");
        if (failure == 1 || failure == 3)
            Check(recovery.error.byteOffset == (failure == 1 ? 23 : 24) &&
                recovery.error.message == (failure == 1 ? "controlled notice rejection" : "controlled ability rejection"),
                "true provider diagnostic is preserved verbatim");
    }
    for (bool withDiagnostic : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(100);
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), 1, 1000), "clone failure enters actual future display query");
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; int clones{}, abilities{};
        const auto directory = InstallDisplayAbilities(f); directory->feature28 = 1;
        directory->feature3 = std::make_shared<csol::scenario::Feature3>();
        const auto condition = std::make_shared<DisplayConditionProbe>(); condition->clones = &clones; condition->success = false;
        if (withDiagnostic) condition->cloneError = {31, "controlled actual Feature3 clone failure"};
        Require(directory->feature3->Put(5, {{0,0,0,0}, condition, {}}), "failed clone belongs to a real otherwise-ended node");
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++abilities; return true; };
        recovery.Add(component);
        Check(!recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && clones == 1 && abilities == 0 &&
            wire.packets.empty() && !recovery.hit.call.Cancelled() && recovery.hit.call.Current() == f.monster &&
            recovery.error.message == (withDiagnostic ? "controlled actual Feature3 clone failure" : "feature3 condition clone failed") &&
            recovery.error.byteOffset == (withDiagnostic ? 31 : 0),
            "actual Feature3 clone failure carries explicit or synthesized diagnostic and cannot masquerade as cancellation");
    }
}

void ConcreteNoticeCancellationAfterBegin(Engine& engine)
{
    Fixture f(engine); RecoverySample recovery(f); InstallConcreteRecoveryNotice(recovery);
    StatisticsWire wire; int abilities{};
    auto component = std::make_shared<Component>();
    component->recovery = [&](CSOLPlayerRecoveryCall&, float amount, CSOLMonsterDeathContext&, ConfigError&)
        { ++abilities; Check(amount == 5, "only the later independent hit reaches recovery abilities"); return true; };
    recovery.Add(component);
    wire.onBegin = [&] { InstallConcreteRecoveryNotice(recovery); };
    Check(!recovery.Apply(10) && recovery.applied && recovery.hit.call.Cancelled() && !recovery.hit.call.Current() &&
        recovery.error.message.empty() && f.player->pev->health == 50 && abilities == 0 && wire.packets.size() == 1 &&
        RecoveryNumberPacket(wire.packets.front(), engine, 1, 10),
        "concrete notice replacement during Begin completes its packet then cancels without reverting health or invoking abilities");
    wire.onBegin = {};
    auto next = IndependentHit(recovery.hit, 5); bool applied{}; ConfigError error;
    Check(f.players->ApplyAttributeRecovery(next, recovery.hit.runtime, 5, applied, error) && applied && !next.Cancelled() &&
        next.Current() == f.monster && f.player->pev->health == 55 && abilities == 1 && error.message.empty() &&
        wire.packets.size() == 2 && RecoveryNumberPacket(wire.packets.back(), engine, 1, 5),
        "replacement concrete notice handles a later hit on the same runtime without an inherited cancelled state");
}

void RecoveryExceptionCancellation(Engine& engine)
{
    for (bool atNotice : {true, false}) for (int mode = 0; mode != 3; ++mode)
    {
        Fixture f(engine); RecoverySample recovery(f); int callbacks{}, laterCalls{};
        const auto action = [&](CSOLPlayerRecoveryCall& call)
        {
            ++callbacks;
            if (mode == 1)
            {
                if (atNotice) recovery.hit.runtime->recoveryNotice = std::make_shared<RecoveryNotice>();
                else f.players->Remove(EHANDLE(f.player));
            }
            if (mode == 2)
            {
                recovery.error = {19, "diagnostic before explicit cancellation"};
                call.Cancel();
            }
            throw std::runtime_error("controlled recovery callback exception");
        };
        recovery.notice->action = [&](CSOLPlayerRecoveryCall& call, float, CSOLMonsterDeathContext&, ConfigError&)
            { if (atNotice) action(call); return true; };
        auto first = std::make_shared<Component>(), later = std::make_shared<Component>();
        first->recovery = [&](CSOLPlayerRecoveryCall& call, float, CSOLMonsterDeathContext&, ConfigError&)
            { if (!atNotice) action(call); return true; };
        later->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++laterCalls; return true; };
        recovery.Add(first); recovery.Add(later);
        bool returned{}, threw{};
        try { returned = recovery.Apply(10); }
        catch (const std::runtime_error& exception)
        { threw = std::string(exception.what()) == "controlled recovery callback exception"; }
        Check(!returned && threw == (mode == 0) && recovery.applied && f.player->pev->health == 50 && callbacks == 1 &&
            laterCalls == 0 && recovery.hit.call.Cancelled() == (mode != 0) &&
            recovery.error.message == (mode == 2 ? "diagnostic before explicit cancellation" : "") &&
            recovery.error.byteOffset == (mode == 2 ? 19 : 0) &&
            bool(recovery.hit.call.Current()) == (mode == 0),
            "notice/ability exception cancels only after identity retirement or explicit Cancel; otherwise it rethrows to the real caller");
    }
    for (bool retired : {false, true})
    {
        Fixture f(engine); RecoverySample recovery(f); recovery.Special(100);
        Require(f.players->SetRecoveryDisplayState(EHANDLE(f.player), 1, 1000), "throwing clone reaches concrete display policy");
        InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; int clones{}, abilities{};
        const auto directory = InstallDisplayAbilities(f); directory->feature28 = 1;
        const auto feature = directory->feature3 = std::make_shared<csol::scenario::Feature3>();
        const auto condition = std::make_shared<DisplayConditionProbe>(); condition->clones = &clones;
        condition->onClone = [&]
        {
            if (retired) Require(feature->Put(5, {{0,0,0,0}, {}, {}}), "throwing clone first replaces its current node");
            throw std::runtime_error("controlled clone exception");
        };
        Require(feature->Put(5, {{0,0,0,0}, condition, {}}), "real Feature3 contains throwing source");
        auto component = std::make_shared<Component>();
        component->recovery = [&](CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) { ++abilities; return true; };
        recovery.Add(component);
        Check(!recovery.Apply(10) && recovery.applied && f.player->pev->health == 50 && clones == 1 && abilities == 0 &&
            wire.packets.empty() && recovery.hit.call.Cancelled() == retired &&
            recovery.error.message == (retired ? "" : "feature3 condition clone threw"),
            "actual clone exception becomes a real diagnostic unless its node retired before the throw");
    }
    for (bool retired : {false, true})
    {
        Fixture f(engine); AttributeDamageSetup attributes(f); auto* source = f.Entity<SourceProbe>();
        DamageSample sample(f, source, 8); ConfigError error; std::optional<float> recovery;
        attributes.Set(1, 0.5); attributes.Set(7, 0.5);
        source->state->onPlayer = [&]
        {
            if (retired) attributes.weapons->Invalidate();
            throw std::runtime_error("controlled attribute source exception");
        };
        bool returned{}, threw{};
        try { returned = attributes.Prepare(sample, recovery, error); }
        catch (const std::runtime_error& exception)
        { threw = std::string(exception.what()) == "controlled attribute source exception"; }
        Check(!returned && threw == !retired && sample.call.Cancelled() == retired && !recovery && sample.call.damage == 8 &&
            error.message.empty(), "source selection exception cancels only after observed weapon directory retirement");
    }
}
bool KillChainPacket(const StatisticsWire::Packet& packet, Engine& engine, int recipient, std::uint32_t count)
{
    std::vector<std::uint8_t> expected{1};
    for (int shift = 0; shift != 32; shift += 8) expected.push_back(static_cast<std::uint8_t>(count >> shift));
    return packet.ended && packet.destination == MSG_ONE && packet.id == engine.messages.at("ZBSVer2") &&
        packet.recipient == recipient && packet.bytes == expected && packet.writes == std::vector<char>({'b','l'});
}

bool KillChainRibbonPacket(const StatisticsWire::Packet& packet, Engine& engine, int recipient)
{
    return packet.ended && packet.destination == MSG_ONE && packet.id == engine.messages.at("MPToCL") &&
        packet.recipient == recipient && packet.bytes == std::vector<std::uint8_t>({124,1,44}) &&
        packet.writes == std::vector<char>({'b','b','b'});
}

void KillChainTimeAndSignedCases(Engine& engine)
{
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    struct Row { std::uint32_t old, best; float last, now; std::uint32_t count, newBest; bool packet; };
    const Row rows[]{
        {0,0,0,0,1,1,true}, {0,7,0,10,0,7,false},
        {29,0,10,12.5f,30,30,true}, {30,7,10,12.5f,0,7,false},
        {29,7,10,13,0,7,false}, {30,7,10,12,0,7,false},
        {29,0,10,std::nextafter(13.0f,0.0f),30,30,true},
        {30,0,10,std::nextafter(12.0f,0.0f),31,31,true},
        {0,7,16777216.0f,16777220.0f,0,7,false},
        {0,0,16777216.0f,16777218.0f,1,1,true},
        {0x7fffffffu,9,10,10,0x80000000u,9,true},
        {0xffffffffu,9,10,12.5f,0,9,true},
        {0,0xffffffffu,10,10,1,1,true},
        {0x80000000u,0x80000000u,10,12.5f,0x80000001u,0x80000001u,true},
        {9,11,10,nan,0,11,false}, {9,11,nan,10,0,11,false},
        {9,11,inf,inf,0,11,false}, {9,0,inf,10,10,10,true},
        {9,0,10,-inf,10,10,true}, {9,11,-inf,10,0,11,false}
    };
    for (const auto& row : rows)
    {
        Fixture f(engine); DamageSample sample(f,f.player,1); const auto state = sample.State(); ConfigError error; StatisticsWire wire;
        f.monster->pev->health = 0; state->monsterKillChain = row.old; state->bestMonsterKillChain = row.best;
        state->lastMonsterKillAt = row.last; engine.globals.time = row.now;
        Check(f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) &&
            state->monsterKillChain == row.count && state->bestMonsterKillChain == row.newBest &&
            std::bit_cast<std::uint32_t>(state->lastMonsterKillAt) == std::bit_cast<std::uint32_t>(row.now) &&
            wire.packets.size() == unsigned(row.packet) && f.players->KillChainRecords().empty() &&
            error.message.empty() && !sample.call.Cancelled(),
            "kill chain uses old signed count window, strict float32 deadline, ordered nonfinite comparisons and raw uint32 wrap");
        if (row.packet) Check(wire.packets.size() == 1 && KillChainPacket(wire.packets.front(),engine,1,row.count),
            "actual kill-chain packet preserves raw signed LONG bits without saturation");
        engine.globals.time = 10;
    }
}

void KillChainHealthAndBossCases(Engine& engine)
{
    for (const float health : {1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, -1.0f,
        -std::numeric_limits<float>::infinity()})
    {
        Fixture f(engine); DamageSample sample(f,f.player,1); const auto state=sample.State(); ConfigError error; StatisticsWire wire;
        f.monster->pev->health=health; state->monsterKillChain=4; state->lastMonsterKillAt=9;
        const bool count = health <= 0;
        Check(f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) &&
            state->monsterKillChain == (count?5u:4u) && state->lastMonsterKillAt == (count?10:9) &&
            wire.packets.size()==unsigned(count) && error.message.empty(),
            "positive and unordered health skip the helper while zero, negative and negative infinity enter the original gate");
    }
    struct Row { int type, cached, current; const char* appearance; bool column18, accepted, failure; };
    const Row rows[]{
        {1,98,98,"zombi_origin",false,true,false},
        {32000,98,98,"zombi_origin",false,false,false},
        {32000,143,98,"zombi_origin",false,true,false},
        {32001,98,98,"zombi_origin",false,false,false},
        {31805,0,0,"zbs_bossL_big01",true,true,false},
        {1,98,98,"zbs_bossL_big01",false,false,false},
        {1,98,361,"zbs_bossL_big01",false,true,false},
        {1,98,361,"zbs_bossL_big01",true,false,false},
        {32000,0,98,"zombi_origin",false,false,true},
        {1,98,0,"zombi_origin",false,false,true}
    };
    for (const auto& row:rows)
    {
        csol::scenario::MonsterType type; // Outlives the fixture pool's borrowed type pointer.
        Fixture f(engine); type=*f.monster->Type(); type.id=row.type; type.appearance=row.appearance;
        type.column18=row.column18; type.skill=1000;
        Require(f.session->Monsters().Free(*f.monster),"release initial lease before binding an authored statistics type");
        f.monster=f.session->Monsters().Acquire(type,1,f.session->Map().SpawnPoints().Points());
        Require(f.monster && f.monster->Classify()==17,"actual pool entity keeps its Classify17 identity");
        DamageSample sample(f,f.player,1);const auto state=sample.State();ConfigError error;StatisticsWire wire;
        f.monster->pev->health=0;state->lastMonsterKillAt=9;
        Check(f.players->RecordMonsterKillChain(sample.call,sample.runtime,row.cached,row.current,error)==!row.failure &&
            state->monsterKillChain==unsigned(row.accepted) && wire.packets.size()==unsigned(row.accepted) &&
            state->lastMonsterKillAt==(row.accepted?10:9) && error.message.empty()==!row.failure && !sample.call.Cancelled(),
            "Classify17 uses actual statistics Boss map/type/appearance rules, not the independent skill threshold");
    }
}

void KillChainRibbonGates(Engine& engine)
{
    using Kind=CSOLScenarioKillChainRecord::Kind;
    for(int gate=0;gate!=8;++gate)
    {
        Fixture f(engine,0,true,gate==5?0:gate==6?2:1);auto* player=f.Player<StatisticsPlayer>(2);f.player=player;
        DamageSample sample(f,player,1);const auto state=sample.State();ConfigError error;StatisticsWire wire;int offsets{},indices{};
        f.monster->pev->health=0;state->monsterKillChain=99;state->lastMonsterKillAt=9;
        const auto table=f.session->Announcements();
        Require(table && table->Allowed(15,44)==(gate!=5 && gate!=6),"actual decoded positional row44 and category8/mode15 decide permission");
        if(gate==1)sample.runtime->room.noRibbon=true;
        if(gate==2)player->bot=true;
        if(gate==3)player->onBot=[&]{if(player->botQueries==1)player->bot=true;};
        if(gate==4)player->pev->flags|=FL_DORMANT;
        engine.onOffset=[&](const edict_t*){++offsets;if(gate==7 && offsets==3)player->pev->flags|=FL_DORMANT;};
        wire.onEntIndex=[&](const edict_t*){++indices;};
        const bool ribbon=gate==0 || gate==1;
        const bool journal=gate==0 || gate==3 || gate==7;
        Check(f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) &&
            state->monsterKillChain==100 && state->bestMonsterKillChain==100 && state->lastMonsterKillAt==10 &&
            wire.packets.size()==(ribbon?2u:1u) && error.message.empty() && !sample.call.Cancelled(),
            "Bot, dormant, table and NoRibbon gates affect only their original ribbon stages, not the kill-chain counter packet");
        const auto records=f.players->KillChainRecords();
        Check(records.size()==(journal?2u:1u) && records.back().kind==Kind::EventC6 &&
            (!journal || records.front().kind==Kind::Ribbon44),"Ribbon44 journal precedes empty typed EventC6; NoRibbon suppresses only its journal");
        const int expectedOffsets=gate==4?1:(gate==2 || gate==5 || gate==6)?2:3;
        const int expectedBots=gate==4?0:(gate==2 || gate==5 || gate==6 || gate==7)?1:2;
        Check(offsets==expectedOffsets && player->botQueries==expectedBots && indices==(gate==4?0:1),
            "actual ribbon path independently repeats three identity checks, two Bot checks and one slot lookup");
        if(ribbon)Check(KillChainRibbonPacket(wire.packets.front(),engine,2),"ribbon writer sends the actual single44 MPToCL124 packet");
        Check(KillChainPacket(wire.packets.back(),engine,2,100),"chain always retains its separate ZBSVer2 byte1 LONG packet");
        engine.onOffset={};
    }
}

void KillChainCallbackSampling(Engine& engine)
{
    using Kind=CSOLScenarioKillChainRecord::Kind;
    Fixture f(engine,0,true,1);DamageSample sample(f,f.player,1);const auto state=sample.State();ConfigError error;StatisticsWire wire;
    f.monster->pev->health=0;state->monsterKillChain=99;state->lastMonsterKillAt=9;
    wire.onBegin=[&]
    {
        const auto records=f.players->KillChainRecords();
        if(wire.packets.back().id==engine.messages.at("MPToCL"))
        {
            Check(records.size()==1 && records.front().kind==Kind::Ribbon44,"first journal commit precedes actual ribbon Begin");
            state->monsterKillChain=150;engine.globals.time=20;
        }
        else
        {
            Check(records.size()==2 && records.back().kind==Kind::EventC6,"post-ribbon chain150 is re-read before C6 and chain packet");
            state->monsterKillChain=173;
        }
    };
    wire.onEnd=[&]{if(wire.packets.back().id==engine.messages.at("ZBSVer2")){state->monsterKillChain=200;engine.globals.time=30;}};
    Check(f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) && wire.packets.size()==2 &&
        KillChainRibbonPacket(wire.packets.front(),engine,1) && KillChainPacket(wire.packets.back(),engine,1,150) &&
        state->monsterKillChain==200 && state->bestMonsterKillChain==200 && state->lastMonsterKillAt==30 &&
        f.players->KillChainRecords().size()==2 && error.message.empty(),
        "fifty milestone re-reads after ribbon, packet snapshots before Begin, best re-reads after End and final time re-reads last");
    wire.onBegin={};wire.onEnd={};engine.globals.time=31;auto next=IndependentHit(sample,1);
    Check(f.players->RecordMonsterKillChain(next,sample.runtime,98,98,error) && !next.Cancelled() &&
        state->monsterKillChain==201 && state->bestMonsterKillChain==201 && state->lastMonsterKillAt==31 &&
        wire.packets.size()==3 && KillChainPacket(wire.packets.back(),engine,1,201),"new hit continues actual mutated chain without reusing prior packet snapshots");
    state->monsterKillChain=99;state->bestMonsterKillChain=0;state->lastMonsterKillAt=30;
    int lookups{};engine.onIndexLookup=[&](int index,edict_t*)
    {
        Require(index==1,"original resolver uses the registered player slot");
        ++lookups;state->monsterKillChain=lookups==1?150:lookups==2?151:152;
    };
    wire.onEnd=[&]{if(wire.packets.back().id==engine.messages.at("ZBSVer2"))engine.globals.time=40;};
    auto resolverHit=IndependentHit(sample,1);
    Check(f.players->RecordMonsterKillChain(resolverHit,sample.runtime,98,98,error) && lookups==3 &&
        wire.packets.size()==5 && KillChainPacket(wire.packets.back(),engine,1,151) &&
        state->monsterKillChain==152 && state->bestMonsterKillChain==152 && state->lastMonsterKillAt==40 &&
        f.players->KillChainRecords().size()==4,
        "hundred, fifty and sender each resolve INDEXENT; packet151 is captured before last lookup changes best to152");
    engine.onIndexLookup={};wire.onEnd={};
    engine.globals.time=10;
}

void KillChainCancellationCases(Engine& engine)
{
    for(int boundary=0;boundary!=13;++boundary)
    {
        Fixture f(engine,0,true,1);auto* player=f.Player<StatisticsPlayer>(2);f.player=player;
        DamageSample sample(f,player,1);const auto state=sample.State();ConfigError error;StatisticsWire wire;int offsets{},lookups{};bool retired{};
        f.monster->pev->health=0;state->monsterKillChain=99;state->bestMonsterKillChain=17;state->lastMonsterKillAt=9;
        const auto retire=[&]{if(!retired){retired=true;f.players->Remove(EHANDLE(player));}};
        engine.onOffset=[&](const edict_t*){++offsets;if(boundary<3 && offsets==boundary+1)retire();};
        wire.onEntIndex=[&](const edict_t*){if(boundary==3)retire();};
        engine.onIndexLookup=[&](int,edict_t*){++lookups;if(boundary>=10 && lookups==boundary-9)retire();};
        player->onBot=[&]{if((boundary==4 && player->botQueries==1)||(boundary==5 && player->botQueries==2))retire();};
        wire.onBegin=[&]
        {
            if((boundary==6 && wire.packets.back().id==engine.messages.at("MPToCL"))||
                (boundary==8 && wire.packets.back().id==engine.messages.at("ZBSVer2")))retire();
        };
        wire.onEnd=[&]
        {
            if((boundary==7 && wire.packets.back().id==engine.messages.at("MPToCL"))||
                (boundary==9 && wire.packets.back().id==engine.messages.at("ZBSVer2")))retire();
        };
        const std::size_t packets=boundary>=10?(boundary==10?0:1):boundary>=8?2:boundary>=6?1:0;
        const std::size_t records=boundary>=10?boundary-10:boundary>=8?2:(boundary==2 || boundary==5 || boundary>=6)?1:0;
        Check(!f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) && retired && sample.call.Cancelled() &&
            !sample.call.Current() && state->monsterKillChain==100 && state->bestMonsterKillChain==17 && state->lastMonsterKillAt==9 &&
            wire.packets.size()==packets && f.players->KillChainRecords().size()==records && error.message.empty(),
            "identity and message boundaries cancel after counter/journal/packet prefixes without updating best or final time");
        for(const auto& packet:wire.packets)Check(packet.ended,"packets already begun complete before cancelled owner returns");
        engine.onOffset={};engine.onIndexLookup={};wire.onEntIndex={};player->onBot={};wire.onBegin={};wire.onEnd={};
        if(boundary==9)
        {
            Require(f.players->SetProfile(*player,{}),"same live entity registers a new generation after old hit retirement");
            const auto fresh=sample.State();fresh->lastMonsterKillAt=10;auto next=IndependentHit(sample,1);ConfigError nextError;
            Check(f.players->RecordMonsterKillChain(next,sample.runtime,98,98,nextError) && !next.Cancelled() &&
                fresh->monsterKillChain==1 && fresh->bestMonsterKillChain==1 && wire.packets.size()==3 &&
                KillChainPacket(wire.packets.back(),engine,2,1) && sample.call.Cancelled(),
                "new player generation and new damage call work without inheriting the cancelled old hit");
        }
    }
}

void KillChainMessageFailures(Engine& engine)
{
    for(int failure=0;failure!=4;++failure)
    {
        Fixture f(engine,0,true,1);DamageSample sample(f,f.player,1);const auto state=sample.State();ConfigError error;StatisticsWire wire;
        f.monster->pev->health=0;state->monsterKillChain=failure==0?1:failure==1?49:99;
        state->bestMonsterKillChain=7;state->lastMonsterKillAt=9;
        if(failure==3)g_engfuncs.pfnWriteByte=nullptr;else g_engfuncs.pfnWriteLong=nullptr;
        Check(!f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error) && !sample.call.Cancelled() &&
            sample.call.Current()==f.monster && state->monsterKillChain==(failure==0?2u:failure==1?50u:100u) &&
            state->bestMonsterKillChain==7 && state->lastMonsterKillAt==9 && wire.packets.size()==(failure==2?1u:0u) &&
            f.players->KillChainRecords().size()==(failure==0?0u:failure==2?2u:1u) && !error.message.empty(),
            "actual message capability failure retains increment and committed journals but does not advance best or time");
    }
}

void KillChainJournalLifecycle(Engine& engine)
{
    using Kind=CSOLScenarioKillChainRecord::Kind;
    Fixture f(engine,0,true,1);DamageSample sample(f,f.player,1);auto state=sample.State();ConfigError error;StatisticsWire wire;
    f.monster->pev->health=0;state->monsterKillChain=49;state->lastMonsterKillAt=9;
    const auto generation=state->generation;
    Require(f.players->RecordMonsterKillChain(sample.call,sample.runtime,98,98,error),"real fiftieth kill appends first local record");
    state->monsterKillChain=99;auto next=IndependentHit(sample,1);
    Require(f.players->RecordMonsterKillChain(next,sample.runtime,98,98,error),"real hundredth kill appends ordered ribbon and C6");
    const auto records=f.players->KillChainRecords();Require(records.size()==3,"three exact local submissions");
    const auto first=records[0],second=records[1],third=records[2];
    Check(first.kind==Kind::EventC6 && second.kind==Kind::Ribbon44 && third.kind==Kind::EventC6 &&
        first.sequence<second.sequence && second.sequence<third.sequence && first.playerGeneration==generation &&
        first.localIdentity==static_cast<std::uint32_t>(generation) && first.mapGeneration==f.session->Map().SpawnPoints().Generation() &&
        first.roundGeneration==f.session->Map().RoundGeneration(),"typed empty C6 and ribbon records preserve local player/map/round identity and commit order");
    Check(!f.players->AcknowledgeKillChainRecords(0) && !f.players->AcknowledgeKillChainRecords(UINT64_MAX) &&
        f.players->AcknowledgeKillChainRecords(second.sequence) && f.players->KillChainRecords().size()==1 &&
        f.players->KillChainRecords().front().sequence==third.sequence && !f.players->AcknowledgeKillChainRecords(second.sequence),
        "ack requires an exact present sequence and removes only its committed prefix");
    f.players->Remove(EHANDLE(f.player));Require(f.players->SetProfile(*f.player,{}),"journal case creates a fresh player state");
    state=sample.State();
    Check(state->generation>generation && state->monsterKillChain==0 && state->bestMonsterKillChain==0 && state->lastMonsterKillAt==0 &&
        f.players->KillChainRecords().size()==1 && f.players->KillChainRecords().front().playerGeneration==generation,
        "player removal resets new chain state while pending scalar journal retains its original identity");
    f.players->Invalidate();Check(f.players->KillChainRecords().size()==1 &&
        f.players->KillChainRecords().front().sequence==third.sequence,
        "owner invalidation retains pending pure values for shutdown storage");
    Require(f.players->Activate() && f.players->SetProfile(*f.player,{}),"same session reactivates actual player authority");
    Check(f.players->KillChainRecords().empty() && !f.players->AcknowledgeKillChainRecords(third.sequence),
        "new activation starts a fresh journal without reusing acknowledgements");
    state=sample.State();state->monsterKillChain=49;state->lastMonsterKillAt=9;auto last=IndependentHit(sample,1);
    Require(f.players->RecordMonsterKillChain(last,sample.runtime,98,98,error),"reactivated owner records a new actual milestone");
    Check(f.players->KillChainRecords().size()==1 && f.players->KillChainRecords().front().sequence>third.sequence &&
        f.players->KillChainRecords().front().playerGeneration!=generation,"new lifecycle cannot reuse a pending acknowledgement identity");
}
}

namespace
{
// Explicit controlled ability boundaries, not implementations of type12's
// area effect or the original 0x26 progress sender. Unconfigured methods fail.
enum class PostHitStage : std::size_t
{ ActiveGate, ActiveHit, AutoBoss, ActivateBoss, BossHit, OrdinaryHit, ProgressGate, Progress, Count };
constexpr std::size_t PostHitIndex(PostHitStage stage) { return static_cast<std::size_t>(stage); }
struct PostHitSkillState
{
    using Action = std::function<bool(CSOLMonsterDamageCall&, float, ConfigError&)>;
    std::array<Action, PostHitIndex(PostHitStage::Count)> actions;
    std::shared_ptr<std::vector<std::string>> trace = std::make_shared<std::vector<std::string>>();
    std::string name{"skill"};
    int type{12}, typeCalls{}, destroyed{};
    bool active{true}, automatic{true}, progress{true};
    std::function<void()> onType, onDestroy;
};
int PostHitType(const std::shared_ptr<PostHitSkillState>& state)
{
    ++state->typeCalls; state->trace->push_back(state->name + ".type");
    const int result = state->type; const auto action = state->onType;
    if (action) action(); return result;
}
void PostHitDestroy(const std::shared_ptr<PostHitSkillState>& state)
{
    ++state->destroyed;
    try { const auto action = state->onDestroy; if (action) action(); }
    catch (...) { Check(false, "post-hit skill destructor must not throw"); }
}
struct PostHitSkill final : CSOLScenarioDamageSkill
{
    std::shared_ptr<PostHitSkillState> state;
    explicit PostHitSkill(std::shared_ptr<PostHitSkillState> value) : state(std::move(value)) {}
    ~PostHitSkill() override { PostHitDestroy(state); }
    int Type() const override { return PostHitType(state); }
    bool Ready(CSOLMonsterDeathCall&, bool&, ConfigError&) override
    { throw std::runtime_error("damage probe must not use death readiness"); }
    bool Activate(CSOLMonsterDeathCall&, ConfigError&) override
    { throw std::runtime_error("damage probe must not use death activation"); }
    bool Invoke(PostHitStage stage, CSOLMonsterDamageCall& call, float amount, ConfigError& error)
    {
        const auto keep = state;
        static constexpr const char* names[]{"active", "area", "automatic", "activate", "boss", "ordinary", "usesProgress", "advance"};
        keep->trace->push_back(keep->name + "." + names[PostHitIndex(stage)]);
        Require(call.Current() != nullptr, "damage ability receives the actual current pool lease");
        const auto action = keep->actions[PostHitIndex(stage)];
        if (!action) throw std::runtime_error("post-hit probe callback was not configured");
        return action(call, amount, error);
    }
    bool ActiveForMonsterHit(CSOLMonsterDamageCall& call, bool& value, ConfigError& error) override
    { const bool result = state->active; const bool ok = Invoke(PostHitStage::ActiveGate, call, 0, error); value = result; return ok; }
    bool ActiveMonsterHit(CSOLMonsterDamageCall& call, ConfigError& error) override
    { return Invoke(PostHitStage::ActiveHit, call, 0, error); }
    bool AutoActivateOnBossDamage(CSOLMonsterDamageCall& call, bool& value, ConfigError& error) override
    { const bool result = state->automatic; const bool ok = Invoke(PostHitStage::AutoBoss, call, 0, error); value = result; return ok; }
    bool ActivateOnBossDamage(CSOLMonsterDamageCall& call, ConfigError& error) override
    { return Invoke(PostHitStage::ActivateBoss, call, 0, error); }
    bool BossDamaged(CSOLMonsterDamageCall& call, float amount, ConfigError& error) override
    { return Invoke(PostHitStage::BossHit, call, amount, error); }
    bool MonsterDamaged(CSOLMonsterDamageCall& call, float amount, ConfigError& error) override
    { return Invoke(PostHitStage::OrdinaryHit, call, amount, error); }
    bool UsesDamageProgress(CSOLMonsterDamageCall& call, bool& value, ConfigError& error) override
    { const bool result = state->progress; const bool ok = Invoke(PostHitStage::ProgressGate, call, 0, error); value = result; return ok; }
    bool AdvanceDamageProgress(CSOLMonsterDamageCall& call, ConfigError& error) override
    { return Invoke(PostHitStage::Progress, call, 0, error); }
};
struct PostHitDeathOnlySkill final : CSOLScenarioActiveSkill
{
    std::shared_ptr<PostHitSkillState> state;
    explicit PostHitDeathOnlySkill(std::shared_ptr<PostHitSkillState> value) : state(std::move(value)) {}
    ~PostHitDeathOnlySkill() override { PostHitDestroy(state); }
    int Type() const override { return PostHitType(state); }
    bool Ready(CSOLMonsterDeathCall&, bool&, ConfigError&) override
    { throw std::runtime_error("post-hit path called death-only readiness"); }
    bool Activate(CSOLMonsterDeathCall&, ConfigError&) override
    { throw std::runtime_error("post-hit path called death-only activation"); }
};
void BindPostHitSkill(Fixture& f, const std::shared_ptr<PostHitSkillState>& state, bool deathOnly = false)
{
    std::shared_ptr<CSOLScenarioActiveSkill> skill;
    if (deathOnly) skill = std::make_shared<PostHitDeathOnlySkill>(state);
    else skill = std::make_shared<PostHitSkill>(state);
    Require(f.players->SetActiveSkill(EHANDLE(f.player), std::move(skill)), "bind post-hit ability through the real player registry");
}

void PostHitStatisticsArithmetic(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 99); const auto state = sample.State();
    StatisticsWire wire; int indices{}; wire.onEntIndex = [&](const edict_t*) { ++indices; };
    state->bossDamage = 16777216.0f; state->ordinaryMonsterDamage = 7;
    state->monsterDamageHits = std::numeric_limits<std::uint32_t>::max(); ConfigError error;
    Check(f.players->RecordMonsterDamage(sample.call, sample.runtime, true, 1, error) &&
        state->bossDamage == 16777216.0f && state->ordinaryMonsterDamage == 7 && state->monsterDamageHits == 0,
        "Boss damage uses float32 addition while hit count wraps unsigned 32 bits");
    auto ordinary = IndependentHit(sample, 200);
    Check(f.players->RecordMonsterDamage(ordinary, sample.runtime, false, 2.5f, error) &&
        state->ordinaryMonsterDamage == 9.5f && state->bossDamage == 16777216.0f && state->monsterDamageHits == 1,
        "ordinary statistics use the by-value amount and their independent float32 bucket");
    auto boss = IndependentHit(sample, -200);
    Check(f.players->RecordMonsterDamage(boss, sample.runtime, true, 2, error) && state->bossDamage == 16777218.0f &&
        state->monsterDamageHits == 2 && sample.call.damage == 99 && ordinary.damage == 200 && boss.damage == -200,
        "separate hits retain float32 granularity without rewriting the live damage call");
    Check(f.players->NotifyActiveSkillMonsterHit(sample.call, sample.runtime, error) && indices == 4 && wire.packets.empty() &&
        state->monsterDamageHits == 2 && error.message.empty(),
        "empty ability paths resolve ENTINDEX once per entry and outer notification does not add statistics");
}

void PostHitOuterCapture(Engine& engine)
{
    for (int replacement = 0; replacement != 4; ++replacement)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 5); ConfigError error;
        auto original = std::make_shared<PostHitSkillState>(); original->name = "original";
        auto next = std::make_shared<PostHitSkillState>(); next->name = "next"; next->type = 11; next->trace = original->trace;
        original->active = replacement != 3;
        if (replacement == 0) original->onType = [&] { BindPostHitSkill(f, next); };
        original->actions[PostHitIndex(PostHitStage::ActiveGate)] = [&](auto&, float, auto&)
        { if (replacement == 1) BindPostHitSkill(f, next); return true; };
        original->actions[PostHitIndex(PostHitStage::ActiveHit)] = [&](auto& call, float, auto&)
        { call.context.bytes[20] = 19; if (replacement == 2) BindPostHitSkill(f, next); return true; };
        BindPostHitSkill(f, original); StatisticsWire wire; int indices{};
        wire.onEntIndex = [&](const edict_t*) { ++indices; };
        std::vector<std::string> expected{"original.type", "original.active"};
        if (replacement != 3) expected.push_back("original.area");
        Check(f.players->NotifyActiveSkillMonsterHit(sample.call, sample.runtime, error) && *original->trace == expected &&
            original->typeCalls == 1 && next->typeCalls == 0 && original->destroyed == int(replacement != 3) &&
            indices == 1 && sample.call.context.bytes[20] == (replacement == 3 ? 0 : 19) && error.message.empty(),
            "outer type12 keeps its original object through Type, v10 and area callback despite registry replacement");
    }
    Fixture f(engine); DamageSample sample(f, f.player, 1); ConfigError error;
    auto other = std::make_shared<PostHitSkillState>(); other->type = 11; BindPostHitSkill(f, other, true);
    Check(f.players->NotifyActiveSkillMonsterHit(sample.call, sample.runtime, error) && other->typeCalls == 1 &&
        other->trace->size() == 1 && error.message.empty(), "outer non-type12 does not invent a damage capability requirement");
}

void PostHitBossReplacement(Engine& engine)
{
    for (bool fallback13 : {false, true})
    {
        Fixture f(engine); DamageSample sample(f, f.player, 100); const auto state = sample.State(); ConfigError error;
        auto trace = std::make_shared<std::vector<std::string>>();
        const auto make = [&](const char* name)
        { auto value = std::make_shared<PostHitSkillState>(); value->name = name; value->trace = trace; return value; };
        auto automatic = make("auto"), first = make("first"), second = make("second"), activation = make("activation");
        auto hit = make("hit"), gate = make("gate"), progress = make("progress");
        first->type = fallback13 ? 12 : 11; second->type = 13;
        automatic->actions[PostHitIndex(PostHitStage::AutoBoss)] = [&](auto& call, float, auto&)
        { call.damage = 77; BindPostHitSkill(f, first, true); return true; };
        first->onType = [&] { BindPostHitSkill(f, fallback13 ? second : activation, fallback13); };
        second->onType = [&] { BindPostHitSkill(f, activation); };
        activation->actions[PostHitIndex(PostHitStage::ActivateBoss)] = [&](auto&, float, auto&)
        { BindPostHitSkill(f, hit); return true; };
        hit->actions[PostHitIndex(PostHitStage::BossHit)] = [&](auto& call, float amount, auto&)
        {
            Check(amount == 3.25f && call.damage == 77 && state->bossDamage == 13.25f && state->monsterDamageHits == 1,
                "Boss callback sees the committed prefix and original by-value amount after earlier damage mutation");
            call.context.bytes[20] = 41; BindPostHitSkill(f, gate); return true;
        };
        gate->actions[PostHitIndex(PostHitStage::ProgressGate)] = [&](auto& call, float, auto&)
        { Check(call.context.bytes[20] == 41, "progress gate borrows current context"); BindPostHitSkill(f, progress); return true; };
        progress->actions[PostHitIndex(PostHitStage::Progress)] = [&](auto& call, float, auto&)
        { Check(call.context.bytes[20] == 41, "newly read progress object observes prior context mutation"); return true; };
        state->bossDamage = 10; state->ordinaryMonsterDamage = 2; BindPostHitSkill(f, automatic);
        StatisticsWire wire; int indices{}; wire.onEntIndex = [&](const edict_t*) { ++indices; };
        std::vector<std::string> expected{"auto.automatic", "first.type"};
        if (fallback13) expected.push_back("second.type");
        expected.insert(expected.end(), {"activation.activate", "hit.boss", "gate.usesProgress", "progress.advance"});
        Check(f.players->RecordMonsterDamage(sample.call, sample.runtime, true, 3.25f, error) && *trace == expected &&
            first->typeCalls == 1 && second->typeCalls == int(fallback13) && automatic->destroyed == 1 && first->destroyed == 1 &&
            second->destroyed == int(fallback13) && activation->destroyed == 1 && hit->destroyed == 1 && gate->destroyed == 1 &&
            progress->destroyed == 0 && state->ordinaryMonsterDamage == 2 && indices == 1 && error.message.empty(),
            "Boss sequence re-reads each D8 object, permits base Type11/13 queries and preserves the Type11 short circuit");
    }
}

void PostHitOrdinaryAndGates(Engine& engine)
{
    {
        Fixture f(engine); DamageSample sample(f, f.player, 100); const auto state = sample.State(); ConfigError error;
        auto hit = std::make_shared<PostHitSkillState>(); hit->name = "hit";
        auto gate = std::make_shared<PostHitSkillState>(); gate->name = "gate"; gate->trace = hit->trace;
        auto progress = std::make_shared<PostHitSkillState>(); progress->name = "progress"; progress->trace = hit->trace;
        hit->actions[PostHitIndex(PostHitStage::OrdinaryHit)] = [&](auto& call, float amount, auto&)
        { Check(amount == 4.5f && state->ordinaryMonsterDamage == 4.5f, "ordinary callback sees by-value amount and prefix");
          call.damage = -9; call.context.bytes[20] = 23; BindPostHitSkill(f, gate); return true; };
        gate->actions[PostHitIndex(PostHitStage::ProgressGate)] = [&](auto&, float, auto&)
        { BindPostHitSkill(f, progress); return true; };
        progress->actions[PostHitIndex(PostHitStage::Progress)] = [&](auto& call, float, auto&)
        { Check(call.damage == -9 && call.context.bytes[20] == 23, "ordinary progress sees live context/damage mutations"); return true; };
        BindPostHitSkill(f, hit);
        Check(f.players->RecordMonsterDamage(sample.call, sample.runtime, false, 4.5f, error) &&
            *hit->trace == std::vector<std::string>({"hit.ordinary", "gate.usesProgress", "progress.advance"}) &&
            hit->destroyed == 1 && gate->destroyed == 1 && !hit->typeCalls && !gate->typeCalls && !progress->typeCalls &&
            state->bossDamage == 0 && state->ordinaryMonsterDamage == 4.5f && state->monsterDamageHits == 1,
            "ordinary v24 and v1C each re-read D8, with no Boss or Type callbacks");
    }
    for (bool automatic : {false, true})
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); ConfigError error;
        auto skill = std::make_shared<PostHitSkillState>(); skill->automatic = automatic; skill->type = 10; skill->progress = false;
        int callbacks{};
        for (auto stage : {PostHitStage::AutoBoss, PostHitStage::BossHit, PostHitStage::ProgressGate})
            skill->actions[PostHitIndex(stage)] = [&](auto&, float, auto&) { ++callbacks; return true; };
        BindPostHitSkill(f, skill);
        std::vector<std::string> expected{"skill.automatic"};
        if (automatic) expected.insert(expected.end(), {"skill.type", "skill.type"});
        expected.insert(expected.end(), {"skill.boss", "skill.usesProgress"});
        Check(f.players->RecordMonsterDamage(sample.call, sample.runtime, true, 1, error) && *skill->trace == expected &&
            callbacks == 3 && skill->typeCalls == (automatic ? 2 : 0) && error.message.empty(),
            "disabled automatic activation or unmatched Type skips only activation; disabled v1C skips progress");
    }
}

void PostHitFailurePrefixes(Engine& engine)
{
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); const auto state = sample.State(); ConfigError outer, statistics;
        auto skill = std::make_shared<PostHitSkillState>(); BindPostHitSkill(f, skill, true);
        Check(!f.players->NotifyActiveSkillMonsterHit(sample.call, sample.runtime, outer) && !outer.message.empty() &&
            state->monsterDamageHits == 0 && !sample.call.Cancelled(), "nonempty type12 without damage interface fails explicitly");
        Check(!f.players->RecordMonsterDamage(sample.call, sample.runtime, false, 6, statistics) && !statistics.message.empty() &&
            state->ordinaryMonsterDamage == 6 && state->monsterDamageHits == 1 && skill->typeCalls == 1 && !sample.call.Cancelled(),
            "death-only nonempty statistics ability fails after preserving the real statistics prefix");
    }
    for (int failure = 0; failure != 6; ++failure)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); const auto state = sample.State(); ConfigError error;
        auto skill = std::make_shared<PostHitSkillState>();
        skill->actions[PostHitIndex(PostHitStage::OrdinaryHit)] = [&](auto& call, float, auto& output)
        {
            if (failure == 5) { Require(f.players->SetActiveSkill(EHANDLE(f.player), {}), "callback clears registered ability"); return true; }
            if (failure == 4) call.Cancel();
            if (failure == 3 || failure == 4) throw std::runtime_error("controlled post-hit exception");
            if (failure == 1 || failure == 2) output.message = "controlled post-hit failure";
            return failure == 2;
        };
        BindPostHitSkill(f, skill); bool returned{}, threw{};
        try { returned = f.players->RecordMonsterDamage(sample.call, sample.runtime, false, 6, error); }
        catch (const std::runtime_error& exception) { threw = std::string(exception.what()) == "controlled post-hit exception"; }
        Check(!returned && threw == (failure == 3) && sample.call.Cancelled() == (failure == 4) &&
            state->ordinaryMonsterDamage == 6 && state->monsterDamageHits == 1 && skill->trace->size() == 1 &&
            error.message.empty() == (failure == 0 || failure == 3 || failure == 4),
            "false, diagnostic, exception, explicit cancellation and cleared D8 stop the suffix without rolling back statistics");
        Require(f.players->SetActiveSkill(EHANDLE(f.player), {}), "retire failed probe before a new independent hit");
        auto next = IndependentHit(sample, 8); ConfigError nextError;
        Check(f.players->RecordMonsterDamage(next, sample.runtime, false, 2, nextError) && state->ordinaryMonsterDamage == 8 &&
            state->monsterDamageHits == 2 && nextError.message.empty() && !next.Cancelled(),
            "failed or cancelled post-hit callback cannot poison a later independent hit");
    }
}

void PostHitCancellationAndRelease(Engine& engine)
{
    // Both index entries, outer Type/v10/area, Boss automatic/activation,
    // ordinary damage/progress, and final pin release use actual owner identity.
    for (int boundary = 0; boundary != 12; ++boundary)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); const auto old = sample.State(); ConfigError error;
        auto skill = std::make_shared<PostHitSkillState>(); skill->type = boundary == 6 ? 11 : 12;
        auto replacement = std::make_shared<PostHitSkillState>(); replacement->name = "replacement"; replacement->trace = skill->trace;
        const bool outer = boundary == 1 || (boundary >= 2 && boundary <= 4) || boundary == 10;
        const bool boss = boundary == 5 || boundary == 6;
        const bool indexStage = boundary <= 1;
        int cancellations{};
        const auto replacePlayer = [&]
        {
            f.players->Remove(EHANDLE(f.player));
            Require(f.players->SetProfile(*f.player, {}), "post-hit callback registers a new player generation");
        };
        const auto cancel = [&]
        {
            ++cancellations;
            if (boundary == 3 || boundary == 5) sample.ownerActive = false;
            else if (boundary == 4 || boundary == 6 || boundary == 7 || boundary == 9) sample.call.Cancel();
            else replacePlayer();
        };
        if (boundary == 2) skill->onType = cancel;
        const auto set = [&](PostHitStage stage, bool stop)
        { skill->actions[PostHitIndex(stage)] = [&, stop](auto&, float, auto&) { if (stop) cancel(); return true; }; };
        set(PostHitStage::ActiveGate, boundary == 3); set(PostHitStage::ActiveHit, boundary == 4);
        set(PostHitStage::AutoBoss, boundary == 5); set(PostHitStage::ActivateBoss, boundary == 6);
        set(PostHitStage::OrdinaryHit, boundary == 7); set(PostHitStage::ProgressGate, boundary == 8);
        set(PostHitStage::Progress, boundary == 9);
        if (boundary == 10 || boundary == 11)
        {
            skill->actions[PostHitIndex(boundary == 10 ? PostHitStage::ActiveHit : PostHitStage::OrdinaryHit)] =
                [&](auto&, float, auto&) { BindPostHitSkill(f, replacement); return true; };
            skill->onDestroy = cancel;
        }
        BindPostHitSkill(f, skill); StatisticsWire wire;
        if (indexStage) wire.onEntIndex = [&](const edict_t*) { if (!cancellations) cancel(); };
        const bool result = outer ? f.players->NotifyActiveSkillMonsterHit(sample.call, sample.runtime, error) :
            f.players->RecordMonsterDamage(sample.call, sample.runtime, boss, 7, error);
        wire.onEntIndex = {}; skill->onDestroy = {};
        const bool prefix = !outer && !indexStage;
        Check(!result && sample.call.Cancelled() && cancellations == 1 && error.message.empty() &&
            old->monsterDamageHits == unsigned(prefix) && old->bossDamage == (prefix && boss ? 7 : 0) &&
            old->ordinaryMonsterDamage == (prefix && !boss ? 7 : 0) && replacement->typeCalls == 0,
            "identity loss or explicit cancellation stops post-hit suffix and retains only the committed statistics prefix");
        if (boundary == 10 || boundary == 11)
            Check(skill->destroyed == 1 && skill->trace->back() == (outer ? "skill.area" : "skill.ordinary"),
                "last pinned skill destruction is checked before invoking any replacement ability");
        sample.ownerActive = true;
        Require(f.players->SetActiveSkill(EHANDLE(f.player), {}), "clear callback probe for independent hit after cancellation");
        const auto fresh = sample.State(); const auto before = fresh->monsterDamageHits;
        if (fresh != old) Check(fresh->monsterDamageHits == 0 && fresh->bossDamage == 0 && fresh->ordinaryMonsterDamage == 0,
            "replacement player starts with untouched post-hit statistics");
        auto next = IndependentHit(sample, 20); ConfigError nextError;
        Check(f.players->RecordMonsterDamage(next, sample.runtime, false, 2, nextError) &&
            fresh->monsterDamageHits == before + 1 && nextError.message.empty() && !next.Cancelled(),
            "new hit resolves the current player after a prior owner/callback cancellation");
    }
}
}

namespace
{
struct HitCloakingProbe final : CSOLScenarioKillComponent, CSOLScenarioCloakingObserver
{
    std::function<bool(CSOLMonsterDamageCall&, ConfigError&)> action;
    bool MonsterKilled(CSOLMonsterDeathCall&, std::int32_t, ConfigError&) override
    { throw std::runtime_error("cloaking test must not invoke kill callback"); }
    bool HealthRecovered(CSOLPlayerRecoveryCall&, float, CSOLMonsterDeathContext&, ConfigError&) override
    { throw std::runtime_error("cloaking test must not invoke recovery callback"); }
    bool CloakingChanged(CSOLMonsterDamageCall& call, ConfigError& error) override
    { const auto callback = action; if (!callback) throw std::runtime_error("cloaking callback is unconfigured"); return callback(call, error); }
};
std::shared_ptr<csol::scenario::Feature3> InstallHitFeature(Fixture& f)
{
    const auto directory = InstallDisplayAbilities(f);
    auto feature = directory->feature3 = std::make_shared<csol::scenario::Feature3>();
    Require(feature->Put(14, {{0,4,0,80}, {}, {}, 55}), "install actual key14 in the same display Feature3 tree");
    return feature;
}
int HitFeatureCategory(const std::shared_ptr<csol::scenario::Feature3>& feature, float now)
{
    const csol::scenario::Feature3Query query{
        [](const void*) { return true; }, nullptr,
        [](const void* context, float& value) { value = *static_cast<const float*>(context); return true; }, &now};
    int category = -1; ConfigError error;
    Require(feature->Classify(14, query, category, error), "inspect actual post-hit timer using the public classifier"); return category;
}
void HitComponentSourceGates(Engine& engine)
{
    // Direct player, missing source, raw zero/negative/dormant, unknown source,
    // each context byte, then OFFSET/raw mutations at their original positions.
    for (int mode = 0; mode != 11; ++mode)
    {
        Fixture f(engine); const auto feature = InstallHitFeature(f); CBaseEntity* source = f.player;
        std::shared_ptr<SourceState> raw;
        if (mode == 1) source = nullptr;
        if (mode == 2 || mode == 3 || mode == 4 || mode == 10)
        {
            auto* probe = f.Entity<SourceProbe>(); source = probe; raw = probe->state;
            raw->values = {mode == 2 ? 0 : mode == 3 ? -7 : 47}; raw->player = true;
            if (mode == 4) probe->pev->flags |= FL_DORMANT;
        }
        if (mode == 5) source = f.Entity<CBaseEntity>();
        DamageSample sample(f, source, 5); ConfigError error; int callbacks{}, indices{}, offsets{}, entindices{};
        auto observer = std::make_shared<HitCloakingProbe>();
        observer->action = [&](auto&, auto&) { ++callbacks; return true; };
        Require(f.players->RegisterKillComponent(EHANDLE(f.player), observer) != 0, "register actual cloaking subscriber");
        if (mode >= 6 && mode <= 8) sample.call.context.bytes[mode == 6 ? 2 : mode == 7 ? 20 : 7] = 1;
        engine.onIndexLookup = [&](int, edict_t*) { ++indices; };
        engine.onOffset = [&](const edict_t*) { ++offsets; if (mode == 9) sample.call.context.bytes[2] = 1; };
        if (mode == 10) raw->onSource = [&] { sample.call.context.bytes[20] = 1; };
        StatisticsWire wire; wire.onEntIndex = [&](const edict_t*) { ++entindices; };
        const bool changed = mode == 0 || mode == 3 || mode == 4 || mode == 10;
        const int lookups = mode == 1 || (mode >= 6 && mode <= 9) ? 1 : mode == 2 || mode == 5 ? 2 : 3;
        Check(f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error) == (mode != 5) &&
            indices == lookups && offsets == (mode == 1 ? 0 : 1) && entindices == 0 && callbacks == int(changed) &&
            error.message.empty() == (mode != 5) && !sample.call.Cancelled() && HitFeatureCategory(feature, 12) == (changed ? 2 : 1),
            "component uses three INDEXENT stages, OFFSET before context and raw nonzero source without a dormant gate");
        if (raw) Check(raw->sourceCalls == 1 && raw->isPlayerCalls == 0,
            "component uses one raw source virtual, never source IsPlayer or the current gun catalog");
    }
}
void HitComponentLookupCancellation(Engine& engine)
{
    for (int cancelAt = 1; cancelAt != 4; ++cancelAt)
    {
        Fixture f(engine); const auto feature = InstallHitFeature(f); DamageSample sample(f, f.player, 1); ConfigError error;
        int lookups{}, callbacks{}; auto observer = std::make_shared<HitCloakingProbe>();
        observer->action = [&](auto&, auto&) { ++callbacks; return true; };
        Require(f.players->RegisterKillComponent(EHANDLE(f.player), observer) != 0, "register lookup cancellation observer");
        engine.onIndexLookup = [&](int, edict_t*) { if (++lookups == cancelAt) sample.call.Cancel(); };
        Check(!f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error) && sample.call.Cancelled() &&
            lookups == cancelAt && callbacks == 0 && HitFeatureCategory(feature, 12) == 1 && error.message.empty(),
            "cancellation at each real INDEXENT prevents later lookups, timing writes and broadcast");
    }
}
void HitComponentBroadcast(Engine& engine)
{
    {
        Fixture f(engine); const auto feature = InstallHitFeature(f); DamageSample sample(f, f.player, 1); ConfigError error;
        const EHANDLE handle(f.player); std::vector<int> order; std::uint64_t firstToken{}, laterToken{}, addedToken{};
        auto first = std::make_shared<HitCloakingProbe>(), later = std::make_shared<HitCloakingProbe>(), added = std::make_shared<HitCloakingProbe>();
        added->action = [&](auto&, auto&) { order.push_back(3); return true; };
        later->action = [&](auto&, auto&) { order.push_back(2); return true; };
        first->action = [&](auto&, auto&)
        {
            order.push_back(1); Check(HitFeatureCategory(feature, 12) == 2, "timing mutation precedes v104 broadcast");
            Require(f.players->RemoveKillComponent(handle, firstToken) && f.players->SetKillComponentEnabled(handle, laterToken, false),
                "current module removes itself and disables a later module");
            addedToken = f.players->RegisterKillComponent(handle, added); return addedToken != 0;
        };
        firstToken = f.players->RegisterKillComponent(handle, first); laterToken = f.players->RegisterKillComponent(handle, later);
        Check(f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error) && order == std::vector<int>({1,3}) && addedToken,
            "broadcast follows live tokens so an appended enabled successor participates immediately");
        order.clear(); Check(f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error) && order.empty(),
            "already fading key does not broadcast a second change");
    }
    for (bool enabled : {false, true})
    {
        Fixture f(engine); const auto feature = InstallHitFeature(f); DamageSample sample(f, f.player, 1); ConfigError error;
        const auto token = f.players->RegisterKillComponent(EHANDLE(f.player), std::make_shared<Component>());
        Require(token && f.players->SetKillComponentEnabled(EHANDLE(f.player), token, enabled), "set real unknown module enabled state");
        Check(f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error) == !enabled &&
            error.message.empty() == !enabled && HitFeatureCategory(feature, 12) == 2 && !sample.call.Cancelled(),
            "disabled unknown module is skipped; enabled missing v104 fails after preserving the timing prefix");
    }
    for (bool replace : {false, true})
    {
        Fixture f(engine); const auto feature = InstallHitFeature(f); DamageSample sample(f, f.player, 1); ConfigError error;
        auto first = std::make_shared<HitCloakingProbe>(), later = std::make_shared<HitCloakingProbe>(); int laterCalls{};
        first->action = [&](auto&, auto&) -> bool
        {
            if (replace) InstallHitFeature(f); // Same player remains live; only directory/Feature3 identity changes.
            throw std::runtime_error("controlled cloaking observer throw");
        };
        later->action = [&](auto&, auto&) { ++laterCalls; return true; };
        f.players->RegisterKillComponent(EHANDLE(f.player), first); f.players->RegisterKillComponent(EHANDLE(f.player), later);
        bool returned{}, threw{};
        try { returned = f.players->NotifyMonsterHitComponent(sample.call, sample.runtime, error); }
        catch (const std::runtime_error& exception) { threw = std::string(exception.what()) == "controlled cloaking observer throw"; }
        Check(!returned && threw == !replace && sample.call.Cancelled() == replace && !laterCalls && error.message.empty() &&
            HitFeatureCategory(feature, 12) == 2 && sample.State()->player.Get<CBaseEntity>() == f.player,
            "Feature3 replacement then throw cancels the old operation; a live unchanged feature preserves the exception");
    }
}
struct HitEventState
{
    std::function<bool(int, int, const CSOLScenarioMonsterHitEvent&, ConfigError&)> action;
    std::function<void()> released;
    int calls{}, destroyed{};
};
struct HitEventProbe final : CSOLScenarioMonsterHitEvents
{
    std::shared_ptr<HitEventState> state;
    explicit HitEventProbe(std::shared_ptr<HitEventState> value) : state(std::move(value)) {}
    ~HitEventProbe() override
    { ++state->destroyed; try { const auto callback = state->released; if (callback) callback(); }
      catch (...) { Check(false, "event service destructor must not throw"); } }
    bool Submit(int index, int event, const CSOLScenarioMonsterHitEvent& value, ConfigError& error) override
    { const auto keep = state; ++keep->calls; const auto callback = keep->action;
      if (!callback) throw std::runtime_error("hit event Submit is unconfigured"); return callback(index, event, value, error); }
};
void BindHitEvents(Fixture& f, const std::shared_ptr<HitEventState>& state)
{ Require(f.players->SetMonsterHitEvents(std::make_shared<HitEventProbe>(state)), "install explicit real hit-event service"); }
void HitEventCaptureAndLifetime(Engine& engine)
{
    for (int mode = 0; mode != 3; ++mode)
    {
        Fixture f(engine); DamageSample sample(f, f.player, 1); const auto playerState = sample.State(); ConfigError error;
        const auto generation = playerState->generation;
        auto original = std::make_shared<HitEventState>(), next = std::make_shared<HitEventState>(); int indices{}; bool restoring{};
        original->action = [&](int index, int event, const CSOLScenarioMonsterHitEvent& value, ConfigError&)
        {
            Check(index == 1 && event == 7 && std::get<0>(value.payload) && std::get<1>(value.payload).Get<CBaseEntity>() == f.player &&
                value.playerGeneration == generation && value.mapGeneration == f.session->Map().SpawnPoints().Generation() &&
                value.roundGeneration == f.session->Map().RoundGeneration(),
                "event7 receives ordered [true, actual attacker] with separate captured lifetime metadata");
            if (mode != 0) BindHitEvents(f, next); return true;
        };
        if (mode == 2) original->released = [&]
        { restoring = true; f.players->Remove(EHANDLE(f.player));
          Require(f.players->SetProfile(*f.player, {}), "old event destructor replaces player generation"); restoring = false; };
        BindHitEvents(f, original); StatisticsWire wire;
        wire.onEntIndex = [&](const edict_t*) { if (!restoring) ++indices; if (mode == 0) BindHitEvents(f, next); };
        Check(f.players->NotifyMonsterHitEvent(sample.call, sample.runtime, error) == (mode != 2) && original->calls == 1 &&
            next->calls == 0 && original->destroyed == 1 && indices == 1 &&
            sample.call.Cancelled() == (mode == 2) && error.message.empty(),
            "service captured before ENTINDEX handles this event; last-pin destructor retirement cancels afterwards");
        // Exclude the real SetProfile fixture's own slot lookups from the entry count.
        wire.onEntIndex = {}; original->released = {};
    }
}
void HitEventGatesAndFailures(Engine& engine)
{
    Fixture f(engine); DamageSample sample(f, f.player, 1); ConfigError error; StatisticsWire wire; int indices{};
    wire.onEntIndex = [&](const edict_t*) { ++indices; };
    Check(f.players->NotifyMonsterHitEvent(sample.call, sample.runtime, error) && !indices,
        "actual empty event service skips ENTINDEX entirely");
    auto state = std::make_shared<HitEventState>(); state->action = [](int, int, const auto&, auto&) { return false; };
    BindHitEvents(f, state);
    Check(!f.players->NotifyMonsterHitEvent(sample.call, sample.runtime, error) && state->calls == 1 && indices == 1 &&
        !sample.call.Cancelled() && error.message.empty(), "unexplained Submit false remains failure rather than successful empty work");
    wire.onEntIndex = [&](const edict_t*) { ++indices; sample.call.Cancel(); };
    Check(!f.players->NotifyMonsterHitEvent(sample.call, sample.runtime, error) && sample.call.Cancelled() && state->calls == 1 && indices == 2,
        "cancelled ENTINDEX prevents Submit on the captured service");
    wire.onEntIndex = {}; auto next = IndependentHit(sample, 1);
    state->action = [](int, int, const auto&, auto& output) { output.message = "controlled event failure"; return true; };
    Check(!f.players->NotifyMonsterHitEvent(next, sample.runtime, error) && !next.Cancelled() && state->calls == 2 &&
        error.message == "controlled event failure", "Submit diagnostic is not discarded when service returns true");
}
}

namespace
{
using ClassModCaps = csol::scenario::EquipmentLoadout::ClassCaps;
struct ClassModModeProbe
{
    bool current{true}; int failAt{}; mutable int reads{};
    std::vector<int> values{15}; std::function<void(int)> onRead;
    csol::scenario::ClassModQuery Query()
    {
        return {[](const void* p) { return static_cast<const ClassModModeProbe*>(p)->current; }, this,
            [](const void* p, int& mode)
            {
                const auto& probe = *static_cast<const ClassModModeProbe*>(p); const int read = ++probe.reads;
                mode = probe.values[std::min<std::size_t>(read - 1, probe.values.size() - 1)];
                const auto callback = probe.onRead; if (callback) callback(read);
                return read != probe.failAt;
            }, this};
    }
};
std::shared_ptr<csol::scenario::ClassModTable> ClassModTableFixture()
{
    auto table = std::make_shared<csol::scenario::ClassModTable>(); ConfigError error;
    Require(csol::scenario::ReadClassMods(ClassModFixtureCsv, *table, error), "parse authored ClassMod.csv with the real resource parser");
    return table;
}
void ClassModParserCases()
{
    csol::scenario::ClassModTable table; ConfigError error;
    const std::string header = "item,category,type,subtype,v1,v2,v3,v4,v5,v6,v7\r\n";
    const std::string row = "65535,9,65546,65535,40.5,,1.25e-2,-2,0,16777217,3tail\r\n";
    Require(csol::scenario::ReadClassMods(header + row, table, error), "valid full ClassMod row is parsed");
    const auto& definition = table.definitions.at(65535);
    Check(definition.item == 65535 && definition.category == 9 && definition.type == 10 && definition.subtype == -1 &&
        definition.values == std::array<float,7>{40.5f,0,0.0125f,-2,0,16777216.0f,3} && table.source.records.size() == 2,
        "eleven columns preserve float values and empty zero while type/subtype store signed low16 halves");
    const std::string invalid[]{"1,2,3\r\n", ",1,2,0,0,0,0,0,0,0,0\r\n", row,
        "1,1,2,0,1e99,0,0,0,0,0,0\r\n"};
    for (const auto& bad : invalid)
    {
        error = {};
        Check(!csol::scenario::ReadClassMods(header + row + bad, table, error) && !error.message.empty() &&
            table.definitions.size() == 1 && table.definitions.at(65535).values[5] == 16777216.0f && table.source.records.size() == 2,
            "short row, empty key, duplicate item and nonfinite float reject without replacing the prior directory");
    }
    ClassModCaps caps; Check(std::all_of(caps.slots.begin(), caps.slots.end(), [](int v) { return v == -1; }) &&
        !csol::scenario::EquipmentLoadout{}.classModsComplete, "default 45 slots are unsent and default import completeness is unknown");
}
void ClassModModeCases()
{
    struct Row { int type; std::vector<int> values; bool accepted; int reads; };
    const Row rows[]{{2,{15,0},false,2}, {5,{15,60},true,2}, {5,{60},false,1},
        {8,{15,0,48},true,3}, {10,{15,15},true,2}, {9,{15,48},false,2},
        {1001,{-2,15},true,2}, {-1,{-2,60},false,2}, {1,{15},true,1}};
    for (const auto& row : rows)
    {
        ClassModModeProbe probe; probe.values = row.values; csol::scenario::ClassModDefinition definition;
        definition.type = static_cast<std::int16_t>(row.type); bool eligible = !row.accepted; ConfigError error;
        Check(csol::scenario::ClassModEligible(definition, probe.Query(), eligible, error) && eligible == row.accepted &&
            probe.reads == row.reads && error.message.empty(), "mode eligibility samples every original branch instead of caching the first mode");
    }
    ClassModModeProbe probe; probe.failAt = 2; csol::scenario::ClassModDefinition definition; definition.type = 8;
    bool eligible = true; ConfigError error;
    Check(!csol::scenario::ClassModEligible(definition, probe.Query(), eligible, error) && !eligible && !error.message.empty(),
        "failed later mode getter is not a normal ineligible result");
}
void ClassModRebuildOrder()
{
    const auto table = ClassModTableFixture(); ClassModModeProbe probe; ConfigError error;
    ClassModCaps first, other, duplicate; first.classId = duplicate.classId = 49; other.classId = 50;
    first.slots[0] = 14; first.slots[4] = 999; first.slots[5] = 15; first.slots[44] = 65535;
    other.slots[0] = 16; duplicate.slots[0] = 14; duplicate.slots[44] = 16;
    const std::array records{first,other,duplicate}; csol::scenario::ClassModCurrent current;
    Require(csol::scenario::RebuildClassMods(1,49,records,table,current,probe.Query(),error), "rebuild complete same-class records");
    std::vector<int> ids; for (const auto* item : current.records) ids.push_back(item->item);
    Check(ids == std::vector<int>({14,15,65535,14,16}) && current.complete && !current.building && current.classId == 49,
        "all matching duplicate records retain order, last slot, uint16 65535 and duplicate items while missing definitions skip");
    const auto pointers = current.records; error.message = "preserved identity-zero diagnostic";
    Check(csol::scenario::RebuildClassMods(0,0,{},nullptr,current,{},error) && current.records == pointers && current.complete &&
        current.classId == 49 && current.table == table && error.message == "preserved identity-zero diagnostic",
        "identity zero is a full no-op even without current/query/directory");
    probe.reads = 0; probe.values = {58,15,15}; const csol::scenario::ClassModDefinition* selected{};
    Check(csol::scenario::FindClassModType(current,8,probe.Query(),selected,error) && selected == current.records[3] && probe.reads == 3,
        "v54 retries mode qualification on the next matching type after a rejected candidate");
    const int reads = probe.reads;
    Check(csol::scenario::FindClassModPair(current,65544,65545,selected,error) && selected == current.records[0] && probe.reads == reads,
        "v50 matches packed low16 type/subtype directly without resampling mode");
    Check(csol::scenario::FindClassModType(current,0,probe.Query(),selected,error) && !selected && probe.reads == reads,
        "type-zero getter returns absent without invoking mode eligibility");
    probe = {}; probe.failAt = 3; probe.onRead = [&](int read)
    { if (read == 3) Check(current.building && !current.complete && current.records.size() == 1,
        "mode callback observes the already cleared/rebuilt prefix before the next candidate"); };
    error = {};
    Check(!csol::scenario::RebuildClassMods(2,49,records,table,current,probe.Query(),error) && !current.complete && !current.building &&
        current.records.size() == 1 && current.records.front()->item == 14 && !error.message.empty(),
        "failed rebuild retains the new ordered prefix and never restores the old complete list");
}
void ClassModCombinedAndEmpty()
{
    for (int variant = 0; variant != 3; ++variant)
    {
        auto table = ClassModTableFixture(); if (variant == 1) table->definitions.erase(12);
        if (variant == 2) { table->definitions[99] = table->definitions.at(12); table->definitions[99].item = 99; }
        ClassModCaps record; record.classId = 0; record.slots[0] = 12; record.slots[1] = 11; record.slots[2] = 13;
        const std::array records{record}; csol::scenario::ClassModCurrent current; ClassModModeProbe probe; ConfigError error;
        const bool result = csol::scenario::RebuildClassMods(1,0,records,table,current,probe.Query(),error);
        if (variant == 0) Check(result && current.complete && current.records.size() == 5 &&
            current.records[0] == current.records[3] && current.records[2] == current.records[4],
            "type4 appends both default definitions in order even when they are already present");
        if (variant == 1) Check(!result && !current.complete && current.records.size() == 4 && current.records[0]->item == 11 &&
            current.records[1]->item == 13 && current.records[2] == nullptr && current.records[3]->item == 13 && !error.message.empty(),
            "missing type2 default preserves its null append and the later type3 append before explicit failure");
        if (variant == 2) Check(!result && !current.complete && current.records.size() == 3 && !error.message.empty(),
            "ambiguous pair does not invent an item-sort winner");
    }
    csol::scenario::ClassModCurrent current; const csol::scenario::ClassModDefinition* selected{}; ConfigError error; ClassModModeProbe probe;
    Check(!csol::scenario::FindClassModType(current,10,probe.Query(),selected,error) && !error.message.empty(),
        "unbuilt current list is unknown rather than an empty result");
    error = {}; const auto table = ClassModTableFixture();
    Check(csol::scenario::RebuildClassMods(1,49,{},table,current,probe.Query(),error) && current.complete && current.records.empty() &&
        csol::scenario::FindClassModType(current,10,probe.Query(),selected,error) && !selected && error.message.empty(),
        "explicit complete empty records produce a known empty current list");
}
void ClassModSessionResourceCases(Engine& engine)
{
    for (bool combat : {false, true})
    {
        engine.Clear(); engine.files = Resources(); engine.files.erase("resource/buff/ClassMod.csv"); engine.Prepare("0");
        g_pGameRules = InstallGameRules(); auto session = std::make_shared<CCSOLScenarioSession>();
        const auto firstEvent = engine.events.size();
        const bool loaded = session->Precache("map_session_test",1,combat ? CSOLScenarioResourceScope::Combat : CSOLScenarioResourceScope::Selection);
        const auto loads = std::count(engine.events.begin() + firstEvent, engine.events.end(), "load:resource/buff/ClassMod.csv");
        Check(loaded == !combat && loads == int(combat) && !session->ClassMods() &&
            (!combat || session->Error().path == "resource/buff/ClassMod.csv"),
            "Selection does not request ClassMod.csv; Combat requires its exact resource path");
        Require(session->Deactivate(), "resource scope test releases actual session"); session.reset(); engine.Clear();
    }
}
void ClassModPlayerRevisionCases(Engine& engine)
{
    Fixture f(engine); const EHANDLE player(f.player); const auto state = f.players->Find(player);
    const auto table = f.session->ClassMods(); Require(table && table->definitions.size() == 8,
        "real Combat Session loads the authored ClassMod.csv through engine resources");
    std::optional<csol::scenario::ClassModDefinition> selected; ConfigError error;
    Check(!f.players->CurrentClassMod(player,10,selected,error) && !selected && !error.message.empty(), "new player has unknown import");
    ClassModCaps record; record.classId = 49; record.slots[44] = 15; record.values[0] = 12;
    const auto generation = state->generation; const auto profileRevision = state->profileRevision;
    Require(f.players->SetClassModRecords(*f.player,{record}), "trusted import keeps caps and 45 slots in the same profile record");
    error = {};
    Check(state->profileRevision == profileRevision + 1 && state->profile.equipment.classModsComplete &&
        state->profile.equipment.classCaps.front().values[0] == 12 && !f.players->CurrentClassMod(player,10,selected,error) &&
        !error.message.empty(), "import changes revision but does not claim current list already rebuilt");
    Require(f.players->RefreshClassMods(player,49,error) && error.message.empty(),
        "actual player reducer clears prior unknown/stale diagnostic when rebuilding complete input");
    const auto revision = state->classModRevision;
    Check(f.players->CurrentClassMod(player,10,selected,error) && selected && selected->item == 15 && selected->subtype == 7 &&
        f.players->RefreshClassMods(player,49,error) && state->classModRevision == revision && state->generation == generation,
        "same class/profile/table revision reuses the completed list without incrementing its rebuild revision");
    error.message = "previous operation failed";
    Check(f.players->CurrentClassModPair(player,65546,65543,selected,error) && selected && selected->item == 15 && error.message.empty(),
        "actual player v50 wrapper returns low16 pair and clears old diagnostics without a mode lookup");
    record.slots[44] = 14; Require(f.players->SetClassModRecords(*f.player,{record}), "same-class trusted import changes selected record");
    error = {}; Check(!f.players->CurrentClassMod(player,10,selected,error) && !selected && !error.message.empty(),
        "same-generation old current list is stale immediately after profile replacement");
    error = {}; Check(f.players->RefreshClassMods(player,49,error) && state->classModRevision == revision + 1 &&
        f.players->CurrentClassMod(player,8,selected,error) && selected && selected->item == 14 && !state->activeSkill &&
        state->displayAbilities && !state->displayAbilities->feature3,
        "refresh updates selection without inventing installed D8 or display Feature3 objects");
    Require(f.players->SetClassModRecords(*f.player,{}), "trusted producer can import an explicit empty vector");
    error = {}; Check(f.players->RefreshClassMods(player,49,error) && f.players->CurrentClassMod(player,10,selected,error) && !selected &&
        state->classMods.complete && state->classMods.records.empty(), "empty complete import is distinct from unknown player state");
    f.players->Remove(player); Require(f.players->SetProfile(*f.player,{}), "new generation starts independent ClassMod state");
    error = {}; Check(f.players->Find(player)->generation > generation && !f.players->CurrentClassMod(player,10,selected,error) &&
        !selected && !error.message.empty(), "player replacement cannot borrow prior generation current records");
}
}

namespace
{
constexpr const char* PresetHeader = "ClassID,PresetID,slots\r\n";
csol::scenario::CsvDocument PresetItems(std::string rows)
{
    csol::scenario::CsvDocument items; ConfigError error;
    Require(csol::scenario::ReadCsv(";metadata\r\n;ID,Name,resource,ClassName,Category,InGameID\r\n" + rows, items, error),
        "parse physical item metadata/header/data rows for preset caps"); return items;
}
void PresetConfigurationCases()
{
    std::array<std::int32_t,9> counts{}; ConfigError error;
    const std::array<std::int32_t,9> expected{5,4,2,0,1,2,0,1,1};
    Require(csol::scenario::ReadClassModCategorySlots(R"({"CategoryMaxSlots":[5,4,2,0,1,2,0,1,1]})", counts, error),
        "parse all nine configured group counts");
    Check(counts == expected, "configuration preserves individual counts instead of fixed preset column positions");
    for (const char* malformed : {R"({"CategoryMaxSlots":[0,0,1]})", R"({"CategoryMaxSlots":[0,0,6,0,0,0,0,0,0]})",
        R"({"CategoryMaxSlots":[0,0,-1,0,0,0,0,0,0]})", R"({"CategoryMaxSlots":[0,0,1.5,0,0,0,0,0,0]})"})
        Check(!csol::scenario::ReadClassModCategorySlots(malformed,counts,error) && !error.message.empty() && counts == expected,
            "wrong count length, overflow, negative and noninteger reject without changing prior configuration");
}
void PresetSlotMappingCases()
{
    const auto items = PresetItems(PresetItemRow(49,{"26","23","25","22","19"}));
    const std::array<std::int32_t,9> counts{5,4,2,0,1,2,0,1,1};
    const std::string first = std::string(PresetHeader) + "49,not-a-number,101,,0,-1,103,,65535\r\n";
    csol::scenario::ClassModPresetTable table; csol::scenario::ResourceError error;
    Require(csol::scenario::ReadClassModPresets(first,counts,items,table,error), "preset ID is ignored even when nonnumeric");
    auto expected = ClassModCaps{}.slots; expected[10]=101; expected[20]=0; expected[25]=-1; expected[26]=103; expected[40]=65535;
    Check(table.categorySlots == counts && table.records.at(49).slots == expected && table.records.at(49).values ==
        std::array<std::int32_t,5>{26,23,25,22,19},
        "groups2..8 consume dynamic consecutive columns; groups0/1, empty slots and unused slots stay minus one");
    Require(csol::scenario::ReadClassModPresets(first + "49,999,201\r\n",counts,items,table,error),
        "later physical row with the same class replaces the preset");
    expected = ClassModCaps{}.slots; expected[10]=201;
    Check(table.records.size()==1 && table.records.at(49).slots==expected && table.source.records.size()==3,
        "duplicate class replaces the entire record irrespective of PresetID; missing columns reset to minus one");
    const std::array<std::int32_t,9> shifted{0,0,0,1,0,0,0,0,0};
    Require(csol::scenario::ReadClassModPresets(std::string(PresetHeader)+"49,2,301\r\n",shifted,items,table,error),
        "zero count advances to the next group without consuming a source column");
    expected = ClassModCaps{}.slots; expected[15]=301;
    Check(table.records.at(49).slots==expected, "column2 moves to group3 when group2 count is zero");
}
void PresetCapsAndTransactions()
{
    const std::array<std::int32_t,9> counts{0,0,1,0,0,0,0,0,0};
    const auto row = PresetItemRow(49,{",,26,,30,","",",,,"," -2.75,7"," ,9"});
    const auto items = PresetItems(row); const std::string input = std::string(PresetHeader)+"49,77,15\r\n50,0,16\r\n";
    csol::scenario::ClassModPresetTable table; csol::scenario::ResourceError error;
    Require(csol::scenario::ReadClassModPresets(input,counts,items,table,error), "caps come from the actual item CSV fields");
    Check(table.records.at(49).values==std::array<std::int32_t,5>{26,0,0,-2,0} &&
        table.records.at(50).values==std::array<std::int32_t,5>{} && table.records.at(50).slots[10]==16,
        "caps use columns21..25 first nonempty comma token, keep whitespace-token zero, and missing item keeps five zero caps");
    const auto original = table.source.sourceBytes;
    for (int failure=0;failure!=5;++failure)
    {
        auto badItems = items; auto badCounts = counts; std::string badInput = input; const char* path="resource/item.csv";
        if (failure==0) badItems=PresetItems("49,name,res,Character,7,0\r\n");
        if (failure==1) badItems=PresetItems(row+row);
        if (failure==2) badItems=PresetItems(PresetItemRow(49,{"2147483648","0","0","0","0"}));
        if (failure==3) { badInput += "51,ignored,2147483648\r\n"; path="resource/buff/ClassModPreset.csv"; }
        if (failure==4) { badCounts[2]=6; path="resource/buff/ClassModConfig.json"; }
        Check(!csol::scenario::ReadClassModPresets(badInput,badCounts,badItems,table,error) && error.path==path &&
            !error.detail.message.empty() && table.source.sourceBytes==original && table.records.size()==2 &&
            table.records.at(49).values==std::array<std::int32_t,5>{26,0,0,-2,0} && table.records.at(50).slots[10]==16,
            "referenced missing cap column, duplicate item, overflow and invalid configuration retain prior table with exact error origin");
    }
    const auto unrelated = PresetItems(row+"99,name,res,Character,7,0\r\n");
    Check(csol::scenario::ReadClassModPresets(input,counts,unrelated,table,error) && error.path.empty() && error.detail.message.empty(),
        "unreferenced short item row adds no cap-column requirement and successful parse clears the previous diagnostic");
}
struct PresetFileRetirement
{
    inline static PresetFileRetirement* current{};
    enginefuncs_t saved{g_engfuncs}; CCSOLScenarioSession& session; void* settings{}; int retired{};
    explicit PresetFileRetirement(CCSOLScenarioSession& owner):session(owner)
    {
        Require(!current,"one preset file retirement boundary"); current=this;
        g_engfuncs.pfnLoadFileForMe=[](const char* path,int* size)
        {
            auto* data=current->saved.pfnLoadFileForMe(path,size);
            if (std::strcmp(path,"resource/buff/ClassModConfig.json")==0) current->settings=data;
            return data;
        };
        g_engfuncs.pfnFreeFile=[](void* data)
        {
            const bool settings=data==current->settings; current->saved.pfnFreeFile(data);
            if (settings) { current->settings=nullptr; ++current->retired; current->session.Map().RevokeEntityAccess(); }
        };
    }
    ~PresetFileRetirement() { g_engfuncs=saved; current=nullptr; }
};
void PresetSessionScopeAndCancellation(Engine& engine)
{
    for (const char* missing : {"resource/buff/ClassModConfig.json","resource/buff/ClassModPreset.csv"})
        for (bool combat : {false,true})
    {
        engine.Clear(); engine.files=Resources(); engine.files.erase(missing); engine.Prepare("0");
        g_pGameRules=InstallGameRules(); auto session=std::make_shared<CCSOLScenarioSession>(); const auto start=engine.events.size();
        const bool loaded=session->Precache("map_session_test",1,combat?CSOLScenarioResourceScope::Combat:CSOLScenarioResourceScope::Selection);
        const auto count=[&](const char* path)
        { return std::count(engine.events.begin()+start,engine.events.end(),std::string("load:")+path); };
        Check(loaded==!combat && !session->ClassModPresets() && count("resource/buff/ClassModConfig.json")==int(combat) &&
            count("resource/buff/ClassModPreset.csv")==int(combat && std::strcmp(missing,"resource/buff/ClassModPreset.csv")==0) &&
            (!combat || session->Error().path==missing), "Selection never reads new preset resources; Combat reports the first missing exact path");
        Require(session->Deactivate(),"close scope-specific preset resource test"); session.reset(); engine.Clear();
    }
    engine.Clear(); engine.files=Resources(); engine.Prepare("0"); g_pGameRules=InstallGameRules();
    auto session=std::make_shared<CCSOLScenarioSession>(); const auto start=engine.events.size();
    {
        PresetFileRetirement boundary(*session);
        Check(!session->Precache("map_session_test",1,CSOLScenarioResourceScope::Combat) && boundary.retired==1 &&
            !session->Map().EntityAccessAllowed() && !session->ClassModPresets() && session->Error().detail.message.empty() &&
            std::count(engine.events.begin()+start,engine.events.end(),"load:resource/buff/ClassModPreset.csv")==0 && engine.buffers.empty(),
            "actual config-file release revokes session access; loader releases its buffer and stops before the second file or snapshot publish");
    }
    Require(session->Deactivate(),"close cancelled preset session"); session.reset(); engine.Clear();
}
void PresetSnapshotLifecycle(Engine& engine)
{
    std::shared_ptr<const csol::scenario::ClassModPresetTable> presets;
    std::shared_ptr<const csol::scenario::CsvDocument> items;
    std::shared_ptr<CCSOLScenarioWeaponState> weapons; std::shared_ptr<CCSOLScenarioSession> session;
    engine.Clear(); // Clear preserves event history; exclude previous fixture cleanup from this interval.
    const auto start=engine.events.size();
    {
        Fixture f(engine,0,true,0,true); session=f.session; presets=session->ClassModPresets(); weapons=session->Weapons(); items=weapons->ItemCatalog();
        Require(presets && items,"Combat publishes real preset and shared item snapshots");
        const auto count=[&](const char* path)
        { return std::count(engine.events.begin()+start,engine.events.end(),std::string("load:")+path); };
        Check(count("resource/item.csv")==1 && count("resource/buff/ClassModConfig.json")==1 && count("resource/buff/ClassModPreset.csv")==1 &&
            presets->records.at(49).values==std::array<std::int32_t,5>{26,23,25,22,19} && presets->records.at(49).slots[25]==65535 &&
            items->records.back().fields.size()==26 && items->records.back().fields[21]=="26,99",
            "Combat loads each resource once and resolves preset caps from the weapon owner's one physical item snapshot");
        const auto state=f.players->Find(EHANDLE(f.player)); std::optional<csol::scenario::ClassModDefinition> selected; ConfigError error;
        Check(!state->profile.equipment.classModsComplete && state->profile.equipment.classCaps.empty() &&
            !f.players->CurrentClassMod(EHANDLE(f.player),10,selected,error) && !selected && !error.message.empty(),
            "nonempty default preset cache does not silently become a player's complete imported loadout");
        engine.files["resource/item.csv"]="changed after load"; engine.files["resource/buff/ClassModPreset.csv"]="changed after load";
        Check(weapons->ItemCatalog()==items && f.session->ClassModPresets()==presets && presets->records.at(49).values[0]==26,
            "published snapshots remain immutable when backing fixture bytes change");
    }
    Check(!session->ClassModPresets() && session->State()==CSOLScenarioSessionState::Empty &&
        !weapons->Current() && !weapons->ItemCatalog() && items->records.back().fields[21]=="26,99" &&
        presets->records.at(49).values==std::array<std::int32_t,5>{26,23,25,22,19},
        "session exit invalidates owner queries while externally held immutable item/preset snapshots remain readable");
}

void SelectPrepareCharacter(Fixture& f, CBasePlayer& player, TeamName team, int expectedClass)
{
    player.pev->flags |= FL_FAKECLIENT; player.m_iTeam = team;
    const auto access = f.session->Map().EntityAccess();
    Require(CSOL_EnsureScenarioBotCharacter(&player, access), "real bot selection chooses a catalog character for this team");
    int actualClass = -1;
    Require(CSOL_CharacterScenarioClass(&player, actualClass, access) && actualClass == expectedClass,
        "actual character owner resolves the selected scenario class through ENTINDEX and GETPLAYERUSERID");
}

void ClassModLocalDefaultPrepare(Engine& engine)
{
    Fixture f(engine,0,true,0,true,true);
    auto* player = f.Player(2,false); const EHANDLE handle(player);
    Check(!f.players->Find(handle), "untouched player has never been supplied to SetProfile");
    SelectPrepareCharacter(f,*player,CT,49);
    const auto firstEvent = engine.events.size();
    Require(f.players->Prepare(*player), "first actual Combat Prepare configures untouched player");
    const auto state = f.players->Find(handle); const auto upgrades = f.session->Players()->Upgrades(handle);
    Require(state && upgrades, "actual Prepare owns both runtime and upgrade state");
    const auto& records = state->profile.equipment.classCaps;
    Check(state->classModSource == CSOLScenarioClassModSource::HostDefault && state->profileRevision == 2 &&
        state->appliedRevision == 2 && state->classId == 49 && upgrades->ConfiguredClass() == 49 &&
        state->profile.equipment.classModsComplete && records.size() == 2 &&
        records[0].classId == 49 && records[0].values == std::array<std::int32_t,5>{26,23,25,22,19} &&
        records[0].slots[10] == 12 && records[0].slots[25] == 65535 &&
        records[1].classId == 50 && records[1].values == std::array<std::int32_t,5>{31,30,29,28,27} && records[1].slots[10] == 16,
        "first Prepare imports the entire verified preset catalog once, including the other character");
    std::optional<csol::scenario::ClassModDefinition> selected; ConfigError error;
    Check(state->classMods.complete && state->classMods.records.size() == 5 &&
        state->classMods.table == f.session->ClassMods() && state->classModProfileRevision == 2 &&
        f.players->CurrentClassMod(handle,10,selected,error) && selected && selected->item == 15,
        "first real Prepare rebuilds and exposes current ClassMods before returning");
    const auto revision = state->profileRevision, rebuild = state->classModRevision, generation = state->generation;
    const auto* imported = records.data(); const auto currentRecords = state->classMods.records;
    Require(f.players->Prepare(*player), "repeat actual Prepare succeeds for the configured class");
    Check(state->profileRevision == revision && state->classModRevision == rebuild &&
        state->profile.equipment.classCaps.data() == imported && state->classMods.records == currentRecords &&
        f.session->Players()->Upgrades(handle) == upgrades,
        "repeat Prepare neither reimports records nor rebuilds unchanged ClassMods or upgrades");
    SelectPrepareCharacter(f,*player,TERRORIST,50);
    Require(f.players->Prepare(*player), "actual Prepare reconfigures a different selected character");
    Check(state->classModSource == CSOLScenarioClassModSource::HostDefault && state->profileRevision == revision &&
        state->profile.equipment.classCaps.data() == imported && state->profile.equipment.classCaps.size() == 2 &&
        state->classModRevision == rebuild + 1 && state->classMods.classId == 50 && state->classMods.records.size() == 1 &&
        state->classId == 50 && upgrades->ConfiguredClass() == 50 &&
        f.players->CurrentClassMod(handle,9,selected,error) && selected && selected->item == 16,
        "character change rebuilds only the current list and upgrades, retaining the full imported profile");
    Check(std::none_of(engine.events.begin()+firstEvent,engine.events.end(),[](const std::string& event)
        { return event == "load:resource/buff/ClassModPreset.csv" || event == "load:resource/item.csv"; }),
        "player preparation uses published snapshots without rereading preset or item files");
    Require(f.players->SetClassModRecords(*player,{}), "explicit complete-empty import replaces an already initialized host default");
    Require(f.players->Prepare(*player), "actual Prepare consumes the explicit replacement");
    SelectPrepareCharacter(f,*player,CT,49);
    Require(f.players->Prepare(*player), "character change after explicit replacement does not restore host defaults");
    Check(state->classModSource == CSOLScenarioClassModSource::ExplicitProfile && state->profileRevision == revision + 1 &&
        state->profile.equipment.classModsComplete && state->profile.equipment.classCaps.empty() &&
        state->classMods.complete && state->classMods.records.empty() &&
        f.players->CurrentClassMod(handle,10,selected,error) && !selected && error.message.empty(),
        "HostDefault transitions to authoritative ExplicitProfile and stays empty across character changes");
    f.players->Remove(handle); Engine::Remove(player->edict());
    player = f.Player(2,false); const EHANDLE replacement(player);
    Check(!handle.Get() && replacement.GetSerialNumber() != handle.GetSerialNumber() && !f.players->Find(replacement),
        "slot reuse has fresh actual SDK private allocation and EHANDLE generation, without SetProfile");
    SelectPrepareCharacter(f,*player,CT,49);
    Require(f.players->Prepare(*player), "new player generation independently prepares default profile");
    const auto fresh = f.players->Find(replacement);
    Check(fresh && fresh != state && fresh->generation > generation && state->generation == 0 &&
        fresh->classModSource == CSOLScenarioClassModSource::HostDefault && fresh->profileRevision == 2 &&
        fresh->classModRevision == 1 && fresh->profile.equipment.classCaps.size() == 2 &&
        fresh->classId == 49 && f.players->CurrentClassMod(replacement,10,selected,error) && selected && selected->item == 15,
        "retired generation cannot lend its selected class/current list or suppress fresh default initialization");
}

void ClassModExplicitProfilePrepare(Engine& engine)
{
    for (int variant = 0; variant < 3; ++variant)
    {
        Fixture f(engine,0,true,0,true,true); auto* player = f.Player(2,false); const EHANDLE handle(player);
        SelectPrepareCharacter(f,*player,CT,49);
        CSOLScenarioPlayerProfile profile;
        profile.equipment.classModsComplete = variant != 2;
        ClassModCaps modified; modified.classId = 49; modified.values = {9,10,11,12,13}; modified.slots[44] = 16;
        if (variant) profile.equipment.classCaps.push_back(modified);
        Require(f.players->SetProfile(*player,std::move(profile)), "explicit empty, modified or incomplete profile is supplied before Prepare");
        const auto state = f.players->Find(handle); const auto revision = state->profileRevision;
        Require(f.players->Prepare(*player), "actual Prepare configures upgrades from explicit profile");
        Require(f.players->Prepare(*player), "repeat actual Prepare retains explicit profile");
        Check(state->classModSource == CSOLScenarioClassModSource::ExplicitProfile && state->profileRevision == revision &&
            state->appliedRevision == revision && state->profile.equipment.classModsComplete == (variant != 2) &&
            state->profile.equipment.classCaps.size() == (variant ? 1u : 0u) &&
            (!variant || (state->profile.equipment.classCaps[0].values == modified.values &&
                state->profile.equipment.classCaps[0].slots == modified.slots)) &&
            f.session->Players()->Upgrades(handle)->ConfiguredClass() == 49,
            "explicit complete-empty, modified and incomplete profiles never fall back to nonempty host defaults");
        std::optional<csol::scenario::ClassModDefinition> selected; ConfigError error;
        if (!variant)
            Check(state->classMods.complete && state->classMods.records.empty() &&
                f.players->CurrentClassMod(handle,10,selected,error) && !selected && error.message.empty(),
                "explicit complete-empty import builds a known empty current list");
        else if (variant == 1)
            Check(state->classMods.complete && state->classMods.records.size() == 1 &&
                f.players->CurrentClassMod(handle,9,selected,error) && selected && selected->item == 16,
                "explicit modified profile rebuilds its supplied skill rather than the preset skill");
        else
            Check(!state->classMods.complete && state->classModRevision == 0 &&
                !f.players->CurrentClassMod(handle,9,selected,error) && !selected && !error.message.empty(),
                "explicit incomplete profile remains unknown despite successful real upgrade preparation");
    }
}

void ClassModSelectionPrepare(Engine& engine)
{
    Fixture f(engine,0,true,0,true,true,CSOLScenarioResourceScope::Selection);
    auto* player = f.Player(2,false); const EHANDLE handle(player);
    SelectPrepareCharacter(f,*player,CT,49);
    Require(f.players->Prepare(*player), "actual Selection Prepare configures an untouched player");
    const auto state = f.players->Find(handle);
    SelectPrepareCharacter(f,*player,TERRORIST,50);
    Require(f.players->Prepare(*player), "actual Selection Prepare also reconfigures a different character");
    std::optional<csol::scenario::ClassModDefinition> selected; ConfigError error;
    Check(state && state->classModSource == CSOLScenarioClassModSource::Uninitialized && state->profileRevision == 1 &&
        state->appliedRevision == 1 && state->classId == 50 && f.session->Players()->Upgrades(handle)->ConfiguredClass() == 50 &&
        !state->profile.equipment.classModsComplete && state->profile.equipment.classCaps.empty() &&
        !state->classMods.complete && state->classModRevision == 0 && !f.session->ClassMods() && !f.session->ClassModPresets() &&
        !f.players->CurrentClassMod(handle,9,selected,error) && !selected && !error.message.empty(),
        "Selection keeps default source Uninitialized and ClassMods unknown while real character upgrades work");
}

StatisticsPlayer* PrepareClassModDamagePlayer(Fixture& f)
{
    auto* player = f.Player<StatisticsPlayer>(2,false); f.player = player;
    SelectPrepareCharacter(f,*player,CT,49);
    Require(f.players->Prepare(*player), "AA tests use actual default import, character selection and current list rebuild");
    return player;
}
bool ClassModDamagePacket(const StatisticsWire& wire, Engine& engine, int slot, std::int32_t amount)
{
    if (wire.packets.size() != 1) return false;
    const auto value = static_cast<std::uint32_t>(amount);
    const std::vector<std::uint8_t> expected{0xAA,static_cast<std::uint8_t>(slot),
        static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8),
        static_cast<std::uint8_t>(value>>16),static_cast<std::uint8_t>(value>>24)};
    const auto& packet = wire.packets.front();
    return packet.ended && packet.destination == MSG_ONE && packet.id == engine.messages.at("MPToCL") &&
        packet.recipient == slot && packet.bytes == expected && packet.writes == std::vector<char>{'b','b','l'};
}
void ClassModDamageValues(Engine& engine)
{
    Fixture f(engine,0,true,0,true,true); auto* player = PrepareClassModDamagePlayer(f);
    // This concrete tail has no active-weapon, Bot, life, dormant, context5 or
    // damage-bit gate. The outer complete TakeDamage path is a separate test.
    player->bot = true; player->pev->flags |= FL_DORMANT;
    player->pev->health = 0; player->pev->deadflag = DEAD_DEAD;
    DamageSample sample(f,nullptr,0); sample.call.context.bytes[5] = 1; sample.call.bits = 0x08000000;
    StatisticsWire wire;
    const std::array<std::pair<float,std::int32_t>,9> cases{{{0,0},{1.875f,1},{-1.875f,-1},
        {2147483520.0f,2147483520},{2147483648.0f,INT32_MIN},{-2147483648.0f,INT32_MIN},
        {-2147483904.0f,INT32_MIN},{std::numeric_limits<float>::infinity(),INT32_MIN},
        {std::numeric_limits<float>::quiet_NaN(),INT32_MIN}}};
    for (const auto [damage, expected] : cases)
    {
        sample.call.damage = damage; ConfigError error; wire.packets.clear();
        Check(f.players->NotifyClassModMonsterDamage(sample.call,sample.runtime,error) && error.message.empty() &&
            !sample.call.Cancelled() && ClassModDamagePacket(wire,engine,2,expected),
            "real AA consumer sends to the attacker, never monster, with zero/negative truncation and cvttss2si indefinite boundaries");
    }
    Check(!player->m_pActiveItem && player->botQueries == 0 && Engine::Index(f.monster->edict()) != 2,
        "AA requires the actual selected type2 record and adds no weapon/Bot/life/context/damage-bit filter");
}
void ClassModDamageProfileGates(Engine& engine)
{
    Fixture f(engine,0,true,0,true,true); auto* player = PrepareClassModDamagePlayer(f);
    DamageSample sample(f,nullptr,4); const EHANDLE handle(player); StatisticsWire wire;
    for (std::uint8_t flag : {0,1,2})
    {
        auto profile = f.players->Find(handle)->profile; profile.weaponEffectsBlocked21F2 = flag;
        Require(f.players->SetProfile(*player,std::move(profile)) && f.players->Prepare(*player), "refresh actual profile and current ClassMods after 21F2 change");
        ConfigError error; wire.packets.clear();
        Check(f.players->NotifyClassModMonsterDamage(sample.call,sample.runtime,error) && error.message.empty() &&
            (flag == 1 ? wire.packets.empty() : ClassModDamagePacket(wire,engine,2,4)),
            "byte21F2 exactly1 suppresses AA; 0 and2 both query type2 and send");
    }
    for (int variant = 0; variant < 4; ++variant)
    {
        CSOLScenarioPlayerProfile profile; profile.weaponEffectsBlocked21F2 = variant == 3 ? 1 : 0;
        profile.equipment.classModsComplete = variant == 0 || variant == 2;
        Require(f.players->SetProfile(*player,std::move(profile)), "publish explicit empty, unknown or stale ClassMod input");
        if (variant == 0) Require(f.players->Prepare(*player), "known-empty input is really rebuilt before AA query");
        ConfigError error; wire.packets.clear();
        const bool allowed = variant == 0 || variant == 3;
        Check(f.players->NotifyClassModMonsterDamage(sample.call,sample.runtime,error) == allowed &&
            error.message.empty() == allowed && wire.packets.empty() && !sample.call.Cancelled(),
            "known absent type2 is a successful skip; unknown/stale current data is an error; flag1 skips even an unknown query");
    }
    auto* source = f.Entity<CBaseEntity>(); ConfigError error;
    CSOLMonsterDamageCall nonplayer(sample.spawn,EHANDLE(source),EHANDLE(source),3,0,{},sample.ownerActive,
        [](const void* context) { return static_cast<const DamageSample*>(context)->ownerActive; },&sample);
    wire.packets.clear();
    Check(f.players->NotifyClassModMonsterDamage(nonplayer,sample.runtime,error) && error.message.empty() && wire.packets.empty(),
        "actual nonplayer attacker has no ClassMod AA tail");
}
void ClassModDamageCancellation(Engine& engine)
{
    // An ENTINDEX cancellation happens before Begin. Later cancellation closes
    // the already-open scalar packet, then stops this owner's remaining tail.
    for (int boundary = 0; boundary < 6; ++boundary)
    {
        Fixture f(engine,0,true,0,true,true); auto* player = PrepareClassModDamagePlayer(f);
        DamageSample sample(f,nullptr,12.875f); const EHANDLE handle(player);
        const auto old = f.players->Find(handle); const auto profile = old->profile; const auto generation = old->generation;
        StatisticsWire wire; int cancellations{}, bytes{}; ConfigError error;
        const auto replace = [&]
        {
            if (cancellations) return;
            ++cancellations;
            f.players->Remove(handle);
            Require(f.players->SetProfile(*player,{}), "engine callback registers a different actual player generation");
            sample.call.damage = 99;
        };
        if (boundary == 0) wire.onEntIndex = [&](const edict_t* ed) { Require(ed == player->edict(), "AA samples attacker ENTINDEX"); replace(); };
        if (boundary == 1) wire.onBegin = replace;
        if (boundary >= 2 && boundary <= 4) wire.onWrite = [&](char kind)
        {
            if (kind == 'b') ++bytes;
            if ((boundary == 2 && kind == 'b' && bytes == 1) || (boundary == 3 && kind == 'b' && bytes == 2) ||
                (boundary == 4 && kind == 'l')) replace();
        };
        if (boundary == 5) wire.onEnd = replace;
        const bool result = f.players->NotifyClassModMonsterDamage(sample.call,sample.runtime,error);
        wire.onEntIndex = {}; wire.onBegin = {}; wire.onWrite = {}; wire.onEnd = {};
        const auto fresh = f.players->Find(handle);
        Check(!result && sample.call.Cancelled() && error.message.empty() && cancellations == 1 && fresh != old &&
            fresh->generation > generation && old->generation == 0 && !fresh->classMods.complete &&
            (boundary == 0 ? wire.packets.empty() : ClassModDamagePacket(wire,engine,2,12)),
            "player replacement at ENTINDEX or any packet boundary cancels without borrowing replacement state or mutating captured scalar payload");
        Require(f.players->SetProfile(*player,profile) && f.players->Prepare(*player), "replacement generation independently configures real current records");
        auto next = IndependentHit(sample,-2.75f); ConfigError nextError; wire.packets.clear();
        Check(f.players->NotifyClassModMonsterDamage(next,sample.runtime,nextError) && nextError.message.empty() &&
            ClassModDamagePacket(wire,engine,2,-2), "subsequent independent hit works after generation cancellation and packet guard release");
    }
}
void ClassModDamageEngineFailures(Engine& engine)
{
    for (int failure = 0; failure < 5; ++failure)
    {
        Fixture f(engine,0,true,0,true,true); PrepareClassModDamagePlayer(f);
        DamageSample sample(f,nullptr,3); StatisticsWire wire; ConfigError error;
        if (failure == 0) g_engfuncs.pfnIndexOfEdict = nullptr;
        if (failure == 1) g_engfuncs.pfnIndexOfEdict = [](const edict_t*) { return 0; };
        if (failure == 2) g_engfuncs.pfnIndexOfEdict = [](const edict_t*) { return MAX_CLIENTS + 1; };
        if (failure == 3) g_engfuncs.pfnWriteLong = nullptr;
        if (failure == 4) wire.onEntIndex = [&](const edict_t*) { sample.ownerActive = false; };
        Check(!f.players->NotifyClassModMonsterDamage(sample.call,sample.runtime,error) && wire.packets.empty() &&
            sample.call.Cancelled() == (failure == 4) && error.message.empty() == (failure == 4),
            "missing/invalid engine sender inputs report failure; owner retirement at index lookup is cancellation before Begin");
    }
}

void AddonDamageResources(std::map<std::string,std::string>& files)
{
    std::string json="{\"ZBS_Addon\":{\"Addons\":[";
    for(int index=0;index<26;++index)
    {
        if(index)json+=',';
        json+="{\"ID\":"+std::to_string(9000+index)+",\"ItemID\":"+std::to_string(8000+index)+",\"Params\":{";
        switch(index)
        {
        case 0:json+="\"Damage\":10,\"DamageAdd\":5";break;
        case 1:json+="\"DmgPercent\":50,\"DmgPercentAdd\":25";break;
        case 2:json+="\"Percent\":0,\"PercentAdd\":100,\"DmgPercent\":25,\"DmgPercentAdd\":25";break;
        case 4:json+="\"DMGPercent\":25,\"DMGPercentAdd\":25,\"DmgPercent\":900,\"DmgPercentAdd\":900";break;
        case 7:json+="\"DmgPercent\":25,\"DmgPercentAdd\":25";break;
        case 8:json+="\"DmgPercent\":100";break;
        case 25:json+="\"DmgPercent\":25,\"MaxPercent\":75";break;
        }
        json+="}}";
    }
    files["maps/ZBS.json"]=json+"]}}";
    std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
    for(int level=0;level<=100;++level)levels+=std::to_string(level)+",100,0,1,2,1,1,1\r\n";
    files["resource/ZBSVER2/ZBS_neededexp.cso"]=difficulty_test::Encrypt(levels);
}
void AddonLevels(Fixture& f,std::map<std::size_t,std::int32_t> levels)
{
    auto profile=f.players->Find(EHANDLE(f.player))->profile;
    profile.equipment.addonLevels=std::move(levels);
    profile.equipment.classModsComplete=true;
    Require(f.players->SetProfile(*f.player,std::move(profile)), "publish trusted addon levels through actual player profile");
}
void AddonInputsAndParameters(Engine& engine)
{
    Fixture f(engine,0,true,0,false,false,CSOLScenarioResourceScope::Combat,AddonDamageResources);
    DamageSample sample(f,f.player,8); const auto state=sample.State(); const EHANDLE handle(f.player);
    const auto weapons=f.session->Weapons(); auto* weapon=f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem=weapon;
    Check(!state->addonDamage.state11 && !state->addonDamage.count18 && state->addonDamage.deadline1C==0 &&
        !state->addonDamage.count48 && !state->addonCriticalPending && !weapons->AddonWeaponInput(*weapon),
        "module constructor inputs are zero while unknown weapon160 remains optional");
    AddonLevels(f,{{1,1}}); ConfigError zeroFlag;
    Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,zeroFlag) && sample.call.damage==8 && zeroFlag.message.empty(),
        "zero state11 makes conjunction false without inventing unknown weapon input");
    Require(f.players->SetAddonDamageInputs(handle,{1,0,0,0}), "set actual explicit state11 input");
    ConfigError missing;
    Check(!f.players->ApplyAddonDamage(sample.call,sample.runtime,missing) && !missing.message.empty() &&
        sample.call.damage==8 && !sample.call.Cancelled(), "enabled addon1 reports missing weapon input");
    for(const int value : {-2,1,2})
    {
        Require(weapons->SetAddonWeaponInput(*weapon,value), "supply actual signed weapon field");
        sample.call.damage=8; ConfigError error;
        Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,error) && sample.call.damage==(value<2?8.0f:12.0f) && error.message.empty(),
            "addon1 requires signed weapon input at least two");
    }
    AddonLevels(f,{{1,3}}); sample.call.damage=8; ConfigError increment;
    Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,increment) && sample.call.damage==16 && increment.message.empty(),
        "physical addon1 uses base plus two level increments");
    struct Row { std::size_t index; int level; CSOLScenarioAddonDamageInputs inputs; float expected; };
    const Row rows[]={{4,2,{0,2,0,0},16},{9004,2,{0,2,0,0},8},{4,2,{0,-2,0,0},8},
        {25,1,{0,0,0,-2},4},{25,1,{0,0,0,4},14},{7,2,{0,0,10,0},8},
        {7,2,{0,0,std::nextafter(10.0f,11.0f),0},12}};
    for(const auto& row : rows)
    {
        AddonLevels(f,{{row.index,row.level}});
        Require(f.players->SetAddonDamageInputs(handle,row.inputs), "publish explicit signed counters and deadline");
        sample.call.damage=8; ConfigError error;
        Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,error) && sample.call.damage==row.expected && error.message.empty(),
            "case-sensitive DMGPercent and physical index; count18 positive gate; signed count48 MINSS; deadline strictly greater");
    }
}
void AddonRandomAndPost(Engine& engine)
{
    Fixture f(engine,0,true,0,false,false,CSOLScenarioResourceScope::Combat,AddonDamageResources);
    DamageSample sample(f,f.player,8); const auto state=sample.State(); BuffEngineEdges edges; edges.IntegerRandom();
    f.monster->HostState().damageEffect=3; f.monster->HostState().damageEffectMagnitude=9;
    AddonLevels(f,{{2,1}}); edges.onRandom=[&]{sample.call.damage=99;}; ConfigError zero;
    Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,zero) && edges.randomCalls==1 && edges.low==0 && edges.high==99 &&
        sample.call.damage==8 && !state->addonCriticalPending && zero.message.empty(),
        "zero threshold still calls RANDOM_LONG(0,99) once and retains by-value input D across callback mutation");
    AddonLevels(f,{{2,2}}); sample.call.damage=8;
    edges.onRandom=[&]{sample.call.damage=99;AddonLevels(f,{{2,1}});}; ConfigError hit;
    Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,hit) && sample.call.damage==10 && state->addonCriticalPending &&
        f.monster->HostState().damageEffect==3 && f.monster->HostState().damageEffectMagnitude==9 && hit.message.empty(),
        "threshold is captured before RNG; hit reads updated DmgPercent after callback and defers marker until post-health");
    edges.onRandom={}; const int draws=edges.randomCalls;
    for(int gate=0;gate<3;++gate)
    {
        sample.call.damage=6; ConfigError error;
        if(gate==0)engine.onOffset=[&](const edict_t* ed)
        { if(ed==f.player->edict()){sample.call.damage=99;f.player->pev->flags|=FL_DORMANT;} };
        if(gate==1)sample.call.context.bytes[2]=1;
        if(gate==2)f.monster->pev->flags|=FL_DORMANT;
        Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,error) && sample.call.damage==6 && state->addonCriticalPending &&
            edges.randomCalls==draws && error.message.empty(),
            "owner/context/target early returns preserve byte40 and captured D even after owner OFFSET mutation");
        engine.onOffset={}; f.player->pev->flags&=~FL_DORMANT; sample.call.context.bytes[2]=0; f.monster->pev->flags&=~FL_DORMANT;
    }
    f.monster->pev->flags|=FL_DORMANT; ConfigError invalid;
    Check(f.players->FinishAddonDamage(sample.call,sample.runtime,invalid) && state->addonCriticalPending &&
        f.monster->HostState().damageEffect==3 && invalid.message.empty(), "invalid post target retains shared pending critical flag");
    f.monster->pev->flags&=~FL_DORMANT; f.player->pev->flags|=FL_DORMANT;
    sample.call.context.bytes[2]=1; sample.call.damage=-5; ConfigError post;
    Check(f.players->FinishAddonDamage(sample.call,sample.runtime,post) && !state->addonCriticalPending && sample.call.damage==-5 &&
        f.monster->HostState().damageEffect==8 && f.monster->HostState().damageEffectMagnitude==9 && edges.randomCalls==draws && post.message.empty(),
        "valid post ignores owner dormancy/context/D, writes marker8 once, preserves magnitude and consumes byte40");
    f.player->pev->flags&=~FL_DORMANT; sample.call.context.bytes[2]=0;
    f.monster->HostState().damageEffect=3;
    Check(f.players->FinishAddonDamage(sample.call,sample.runtime,post) && f.monster->HostState().damageEffect==3,
        "second post does not repeat consumed marker");
}
void AddonFixedDamageAndBoss(Engine& engine)
{
    for(const int skill : {999,1000})
    {
        Fixture f(engine,skill,true,0,true,true,CSOLScenarioResourceScope::Combat,AddonDamageResources);
        SelectPrepareCharacter(f,*f.player,CT,49); AddonLevels(f,{{0,3},{8,1}});
        Require(f.players->Prepare(*f.player), "actual character/equipment Configure prepares modern ATK factor");
        const auto upgrades=f.session->Players()->Upgrades(EHANDLE(f.player)); float factor{};
        Require(upgrades && upgrades->StatValue(csol::scenario::UpgradeStat::Damage,factor) && factor==2,
            "actual encrypted upgrade resource yields effective Damage factor two");
        DamageSample sample(f,f.player,8); ConfigError error;
        Check(f.players->ApplyAddonDamage(sample.call,sample.runtime,error) && sample.call.damage==(skill==1000?56.0f:48.0f) && error.message.empty(),
            "skill threshold controls addon8; actual ATK multiplies fixed20 only, after percentage-scaled D");
    }
}
void AddonCancellation(Engine& engine)
{
    for(int boundary=0;boundary<4;++boundary)
    {
        Fixture f(engine,0,true,0,false,false,CSOLScenarioResourceScope::Combat,AddonDamageResources);
        DamageSample sample(f,f.player,8); const auto state=sample.State(); const EHANDLE handle(f.player);
        auto* weapon=f.Weapon<EnhancementWeaponProbe>(WEAPON_AK47); const auto weaponState=weapon->state;
        f.player->m_pActiveItem=weapon; const auto weapons=f.session->Weapons();
        Require(weapons->SetAddonWeaponInput(*weapon,2) && f.players->SetAddonDamageInputs(handle,{1,0,0,0}), "prepare usable addon1 weapon inputs");
        AddonLevels(f,boundary==2?std::map<std::size_t,std::int32_t>{{1,1}}:std::map<std::size_t,std::int32_t>{{2,2}});
        BuffEngineEdges edges; edges.IntegerRandom(); ConfigError error; int callbacks{};
        f.monster->HostState().damageEffect=3;
        if(boundary==3)
            Require(f.players->ApplyAddonDamage(sample.call,sample.runtime,error) && state->addonCriticalPending, "produce actual pending byte40 before post cancellation");
        const float before=sample.call.damage;
        const auto retire=[&]
        {
            if(callbacks)return;
            ++callbacks;
            if(boundary==0)sample.ownerActive=false;
            if(boundary==1){f.players->Remove(handle);Require(f.players->SetProfile(*f.player,{}), "RNG installs distinct player registration");}
            if(boundary==2)Engine::Remove(weapon->edict());
            if(boundary==3)sample.runtime->RequestClose();
        };
        if(boundary==0 || boundary==3)engine.onOffset=[&](const edict_t*){retire();};
        if(boundary==1)edges.onRandom=retire;
        if(boundary==2)weaponState->callback=retire;
        const bool ok=boundary==3?f.players->FinishAddonDamage(sample.call,sample.runtime,error):
            f.players->ApplyAddonDamage(sample.call,sample.runtime,error);
        engine.onOffset={}; edges.onRandom={}; weaponState->callback={}; f.player->m_pActiveItem=nullptr;
        Check(!ok && callbacks==1 && sample.call.Cancelled() && error.message.empty() && sample.call.damage==before &&
            f.monster->HostState().damageEffect==3 && (boundary!=3 || state->addonCriticalPending),
            "OFFSET/RNG/IsWeapon cancellation stops stale damage or post marker; raw freed weapon pointer is not dereferenced");
    }
}
void DamageHintResources(std::map<std::string,std::string>& files)
{
    files["resource/item.csv"]+="1000,zero,zero,Equipment,2,0\r\n";
    std::string csv="header\r\n";
    for(const int item : {1036,10144,161,31,1000})
        csv+=std::to_string(item)+",0,0,0,0,0,1,"+(item==10144?"3":"0")+",0,0,0,0,0,0,0,0,0,0\r\n";
    files["resource/buff/BuffWeapon.cso"]=difficulty_test::Encrypt(csv);
}
void DamageHintDefinitionsAndGates(Engine& engine)
{
    Fixture f(engine,0,true,0,false,false,CSOLScenarioResourceScope::Combat,DamageHintResources);
    auto* weapon=f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem=weapon;
    DamageSample sample(f,f.player,1); StatisticsWire wire; const auto weapons=f.session->Weapons();
    const auto& loadout=sample.State()->profile.equipment;
    Check(loadout.buffItems.empty() && loadout.carriedItems.empty() &&
        std::all_of(loadout.equippedItems.begin(),loadout.equippedItems.end(),[](int id){return id==0;}), "hint has no activated or worn player items");
    for(const int source : std::array<int,7>{WEAPON_AK47,CSOL_WEAPON_CANNON,220,9999,WEAPON_KNIFE,WEAPON_HEGRENADE,0})
    {
        weapon->m_iId=source; wire.packets.clear(); ConfigError error; const bool present=source==WEAPON_AK47 || source==0;
        Check(weapons->NotifyDamageBuff(sample.call,sample.runtime,error) && error.message.empty() && wire.packets.size()==std::size_t(present),
            "loaded static type0 is sufficient; other type/missing bucket or item skip; direct key0 works without knife/HE substitution");
        if(present){const auto& p=wire.packets.front();Check(p.ended && p.destination==MSG_ONE && p.id==engine.messages.at("MPToCL") &&
            p.recipient==1 && p.bytes==std::vector<std::uint8_t>{0x4D} && p.writes==std::vector<char>{'b'}, "hint is exactly one reliable 4D byte to attacker");}
    }
    weapon->m_iId=WEAPON_AK47;
    for(int gate=0;gate<4;++gate)
    {
        sample.call.damage=gate==0?0:gate==1?-1:gate==2?std::numeric_limits<float>::quiet_NaN():1;
        sample.call.bits=gate==3?0x08000000:DMG_BULLET; wire.packets.clear(); ConfigError error;
        Check(weapons->NotifyDamageBuff(sample.call,sample.runtime,error) && error.message.empty() && wire.packets.empty(), "nonpositive/unordered D and08000000 skip hint");
    }
}
struct HintSourcePlayer final : CBasePlayer, CSOLWeaponDamageSource
{ int DamageWeaponId() const override { return WEAPON_AK47; } };
void DamageHintSourceCancellation(Engine& engine)
{
    Fixture f(engine,0,true,0,false,false,CSOLScenarioResourceScope::Combat,DamageHintResources);
    f.player=f.Player<HintSourcePlayer>(2); DamageSample raw(f,f.player,1); StatisticsWire wire; ConfigError error;
    Check(f.session->Weapons()->NotifyDamageBuff(raw.call,raw.runtime,error) && wire.packets.size()==1 && wire.packets[0].recipient==2,
        "same attacker without an active weapon still queries its real raw damage-source interface"); wire.packets.clear();
    auto* source=f.Entity<SourceProbe>(); const auto probe=source->state; probe->values={WEAPON_AK47};
    DamageSample sample(f,source,1);
    probe->onSource=[&]{f.players->Remove(EHANDLE(f.player));};
    Check(!f.session->Weapons()->NotifyDamageBuff(sample.call,sample.runtime,error) && sample.call.Cancelled() &&
        probe->sourceCalls==1 && error.message.empty() && wire.packets.empty(), "source callback retirement cancels before any hint packet");
    probe->onSource={};
}
CSOLWeaponPartEffect PartHitRow(int kind,double chance,double amount,double extra=0)
{
    CSOLWeaponPartEffect row; row.id=17; row.kind=kind; row.modes={1u<<15}; row.parameters={chance,amount,extra}; return row;
}
void EquipPartHit(Fixture& f)
{
    auto* weapon=f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem=weapon;
    Require(f.session->Weapons()->SetParts(*weapon,{17,0}), "bind trusted part ID to actual SDK weapon");
}
void PartHitCriticalCases(Engine& engine)
{
    Fixture f(engine); EquipPartHit(f); DamageSample sample(f,f.player,8); AttributeSeed seed;
    const auto weapons=f.session->Weapons(); auto& host=f.monster->HostState(); ConfigError empty;
    host.damageEffect=3; host.damageEffectMagnitude=9;
    Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,empty,AttributeSeed::Draw) && seed.calls==0 &&
        sample.call.damage==8 && host.damageEffect==3 && host.damageEffectMagnitude==9 && empty.message.empty(), "no matching part records consumes no seed and preserves caller state");
    constexpr double roll=0x1.1574f7b6848dcp-3;
    const std::vector<std::vector<CSOLWeaponPartEffect>> rows{{PartHitRow(8,0,100)},
        {PartHitRow(8,roll/2,0.5),PartHitRow(8,roll/2,3)},
        {PartHitRow(8,roll,0.5)},{PartHitRow(8,std::nextafter(roll,0.0),3)}};
    for(std::size_t index=0;index<rows.size();++index)
    {
        Require(weapons->ReplacePartEffects(rows[index]), "replace finite trusted part records");
        sample.call.damage=8; host.damageEffect=3; ConfigError error;
        Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,error,AttributeSeed::Draw) && seed.calls==int(index)+1 &&
            sample.call.damage==(index==1?24.0f:8.0f) && host.damageEffect==(index==1 || index==2?8:3) &&
            host.damageEffectMagnitude==9 && error.message.empty(), "part8 sums chances, includes equality, uses max multiplier with floor1 and one seed per nonempty kind");
    }
}
void PartHitDurationCases(Engine& engine)
{
    Fixture f(engine); EquipPartHit(f); DamageSample sample(f,f.player,8); AttributeSeed seed;
    const auto weapons=f.session->Weapons(); auto& host=f.monster->HostState(); auto& speed=f.monster->SpeedState();
    Check(speed.impulseProtectionUntil==0 && speed.boostUntil==0 && speed.boostAdd==1, "existing speed state owns original1378/1380/1388 initial values");
    Require(weapons->ReplacePartEffects({PartHitRow(8,1,2),PartHitRow(10,1,3),PartHitRow(10,0.5,100),PartHitRow(10,1,7),
        PartHitRow(11,1,4,-0.5),PartHitRow(11,1,4,2),PartHitRow(11,0.5,100,1)}), "install ordered per-kind continuous-roll records");
    ConfigError error;
    Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,error,AttributeSeed::Draw) && seed.calls==3 && sample.call.damage==16 &&
        speed.impulseProtectionUntil==17 && speed.boostUntil==14 && speed.boostAdd==-0.5 && host.damageEffect==11 &&
        host.damageEffectMagnitude==4 && error.message.empty(), "each kind reseeds once; failed later rolls do not pick100; longest duration wins and equal slow duration retains first parameter");
    speed.baseSpeed=100;
    Check(speed.UpdateMaxSpeed(*f.monster->pev,10) && f.monster->pev->maxspeed==50 && host.frozenRemaining==0,
        "part11 feeds existing (1+boostAdd)*baseSpeed; part10 does not freeze movement");
    for(int later=0;later<2;++later)
    {
        speed.impulseProtectionUntil=17+later; speed.boostUntil=14+later; speed.boostAdd=0.75;
        sample.call.damage=8; host.damageEffect=3; host.damageEffectMagnitude=99; ConfigError repeat;
        Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,repeat,AttributeSeed::Draw) && seed.calls==6+later*3 &&
            speed.impulseProtectionUntil==17+later && speed.boostUntil==14+later && speed.boostAdd==0.75 &&
            host.damageEffect==8 && host.damageEffectMagnitude==99 && repeat.message.empty(), "equal or later deadlines prevent10/11 overwriting critical marker or existing duration/multiplier");
    }
    Require(weapons->ReplacePartEffects({PartHitRow(10,1,0),PartHitRow(11,1,-1,5)}), "successful nonpositive durations remain inert");
    host.damageEffect=3; ConfigError inert;
    Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,inert,AttributeSeed::Draw) && seed.calls==11 &&
        host.damageEffect==3 && host.damageEffectMagnitude==99 && speed.impulseProtectionUntil==18 && speed.boostUntil==15 &&
        speed.boostAdd==0.75 && inert.message.empty(), "zero/negative duration consumes each nonempty kind's seed but changes no deadline or marker");
}
void PartHitBossCases(Engine& engine)
{
    for(const int skill : {999,1000,32768})
    {
        Fixture f(engine,skill); EquipPartHit(f); DamageSample sample(f,f.player,8); AttributeSeed seed; ConfigError error;
        const auto weapons=f.session->Weapons(); auto& speed=f.monster->SpeedState(); auto& host=f.monster->HostState();
        Require(weapons->ReplacePartEffects({PartHitRow(8,1,2),PartHitRow(10,1,2),PartHitRow(11,1,3,-0.5)}), "prepare all three effects for signed skill boundary");
        const bool boss=skill==1000;
        Check(weapons->ApplyPartHitEffects(sample.call,sample.runtime,error,AttributeSeed::Draw) && sample.call.damage==16 &&
            seed.calls==(boss?1:3) && speed.impulseProtectionUntil==(boss?0:12) && speed.boostUntil==(boss?0:13) &&
            speed.boostAdd==(boss?1:-0.5) && host.damageEffect==(boss?8:11) && error.message.empty(), "signed16 skill>=1000 skips only10/11; critical8 still applies");
    }
}
void PartHitSeedCancellation(Engine& engine)
{
    for(int stage=1;stage<=3;++stage)
    {
        Fixture f(engine); EquipPartHit(f); DamageSample sample(f,f.player,8); AttributeSeed seed; ConfigError error;
        const auto weapons=f.session->Weapons(); auto& speed=f.monster->SpeedState(); auto& host=f.monster->HostState(); host.damageEffect=3;
        Require(weapons->ReplacePartEffects({PartHitRow(8,1,2),PartHitRow(10,1,2),PartHitRow(11,1,3,-0.5)}), "prepare ordered prefix cancellation");
        seed.callback=[&]
        {
            if(seed.calls!=stage)return;
            if(stage==1)f.players->Remove(EHANDLE(f.player));
            if(stage==2)sample.runtime->players=std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if(stage==3)sample.ownerActive=false;
        };
        Check(!weapons->ApplyPartHitEffects(sample.call,sample.runtime,error,AttributeSeed::Draw) && sample.call.Cancelled() &&
            seed.calls==stage && sample.call.damage==(stage==1?8:16) && host.damageEffect==(stage==1?3:stage==2?8:10) &&
            speed.impulseProtectionUntil==(stage==3?12:0) && speed.boostUntil==0 && speed.boostAdd==1 && error.message.empty(),
            "seed callback player removal/registry replacement/owner retirement preserves completed prefixes and cancels all later effects");
    }
}
void BloodSuckingAccumulationAndSwitch(Engine& engine)
{
    Fixture f(engine); RecoverySample recovery(f); EquipPartHit(f); const auto weapons=f.session->Weapons();
    const EHANDLE handle(f.player); const auto state=f.players->Find(handle); f.player->pev->max_health=200;
    auto* firing=dynamic_cast<CBasePlayerWeapon*>(f.player->m_pActiveItem); f.player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=firing;
    auto* before=f.Weapon<EnhancementWeaponProbe>(WEAPON_USP); const auto beforeState=before->state;
    auto* current=f.Weapon<CKnife>(WEAPON_KNIFE); f.player->m_rgpPlayerItems[KNIFE_SLOT]=current;
    Require(weapons->SetParts(*before,{18,0}) && weapons->SetParts(*current,{19,0}), "bind distinct firing and settlement weapons");
    auto sourceRow=PartHitRow(9,0.25,20), beforeRow=PartHitRow(9,0.5,50), currentRow=PartHitRow(9,0,10), passive=PartHitRow(9,0,5);
    beforeRow.id=18; currentRow.id=passive.id=19; passive.passive=true;
    Require(weapons->ReplacePartEffects({sourceRow,beforeRow,currentRow,passive}), "install active ratios and independent passive cap");
    ConfigError error;
    Check(weapons->RecordPartBloodSucking(recovery.hit.call,recovery.hit.runtime,error) && state->bloodSuckingRegistered &&
        state->bloodSuckingPending==2 && f.player->pev->health==40, "kind9 adds current hit ratio without immediate health recovery");
    f.player->m_pActiveItem=before; auto* projectile=f.Entity<SourceProbe>(); projectile->state->values={WEAPON_AK47};
    DamageSample hit(f,projectile,320);
    Check(weapons->RecordPartBloodSucking(hit.call,hit.runtime,error) && state->bloodSuckingPending==82 && f.player->pev->health==40,
        "after switching, projectile hit still collects firing weapon ratio and accumulates into the same module");
    beforeState->callback=[&]{f.player->m_pActiveItem=current;};
    InstallConcreteRecoveryNotice(recovery); StatisticsWire wire; f.monster->pev->deadflag=DEAD_DEAD;
    CSOLPlayerRecoveryCall tick(recovery.hit.runtime,handle);
    Check(tick.Current()==f.player && tick.Player()==f.player && !recovery.hit.call.Current(), "deferred player recovery has no surviving monster requirement");
    Check(f.players->UpdateBloodSucking(tick,error) && !tick.Cancelled() && beforeState->calls==1 && f.player->pev->health==70 &&
        state->bloodSuckingPending==0 && state->bloodSuckingCapOverride==0 && wire.packets.size()==1 &&
        RecoveryNumberPacket(wire.packets.front(),engine,1,30) && error.message.empty(),
        "settlement re-reads active after IsWeapon, sums current10 plus passive5 percent of maxhealth200 and uses actual self206 recovery");
    beforeState->callback={};
}
void BloodSuckingEmptyDeadAndDisabled(Engine& engine)
{
    Fixture f(engine,0,true,0,true,true); RecoverySample recovery(f); const EHANDLE handle(f.player); const auto state=f.players->Find(handle);
    CSOLPlayerRecoveryCall tick(recovery.hit.runtime,handle); ConfigError error;
    Check(state->bloodSuckingRegistered && state->bloodSuckingEnabled && state->bloodSuckingPending==0 && state->bloodSuckingCapOverride==0 &&
        f.players->UpdateBloodSucking(tick,error) && state->bloodSuckingRegistered && state->bloodSuckingPending==0 &&
        state->bloodSuckingCapOverride==0 && recovery.notice->calls==0, "player initialization registers zero module; an empty settlement leaves it unchanged");
    Require(f.players->SetBloodSuckingCap(handle,20) && f.players->SetBloodSuckingCap(handle,10), "cap producer overwrites rather than maximizes");
    Check(f.players->UpdateBloodSucking(tick,error) && state->bloodSuckingCapOverride==10 && recovery.notice->calls==0,
        "zero pending preserves override and performs no recovery");
    Require(f.players->AddBloodSucking(handle,5), "accumulate before death settlement"); f.player->pev->deadflag=DEAD_DEAD;
    Check(f.players->UpdateBloodSucking(tick,error) && state->bloodSuckingPending==0 && state->bloodSuckingCapOverride==10 && f.player->pev->health==40,
        "dead owner clears pending but retains override without calling common recovery"); f.player->pev->deadflag=DEAD_NO;
    Require(f.players->AddBloodSucking(handle,7) && f.players->SetBloodSuckingEnabled(handle,false), "disable registered module with pending data");
    Check(f.players->UpdateBloodSucking(tick,error) && state->bloodSuckingPending==7 && state->bloodSuckingCapOverride==10 && recovery.notice->calls==0,
        "disabled module preserves pending and override");
    Require(f.players->SetBloodSuckingEnabled(handle,true), "restore actual module dispatch flag");
    Check(f.players->UpdateBloodSucking(tick,error) && state->bloodSuckingPending==0 && state->bloodSuckingCapOverride==0 &&
        f.player->pev->health==47 && recovery.notice->calls==1 && error.message.empty(), "enabled module consumes retained state through common recovery");
    Require(f.players->AddBloodSucking(handle,5) && f.players->SetBloodSuckingCap(handle,9) &&
        f.players->SetBloodSuckingEnabled(handle,false), "prepare retained module state before profile and character refresh");
    SelectPrepareCharacter(f,*f.player,CT,49);
    Require(f.players->SetProfile(*f.player,{}) && f.players->Prepare(*f.player) && f.players->Prepare(*f.player),
        "actual profile replacement and repeated Prepare reuse the registered player");
    Check(f.players->Find(handle)==state && state->bloodSuckingRegistered && !state->bloodSuckingEnabled &&
        state->bloodSuckingPending==5 && state->bloodSuckingCapOverride==9 && recovery.notice->calls==1,
        "profile and repeated character preparation preserve module identity, enabled flag, pending and override");
}
void BloodSuckingRecoveryCallbacks(Engine& engine)
{
    for(int outcome=0;outcome<3;++outcome)
    {
        const bool retire=outcome==1, rejected=outcome==2;
        Fixture f(engine); RecoverySample recovery(f); const EHANDLE handle(f.player); const auto state=f.players->Find(handle);
        Require(f.players->SetBloodSuckingCap(handle,25) && f.players->AddBloodSucking(handle,50), "prepare pending above explicit override cap");
        int notices{};
        recovery.notice->action=[&](CSOLPlayerRecoveryCall& call,float amount,CSOLMonsterDeathContext&,ConfigError&)
        {
            ++notices; Check(call.Current()==f.player && amount==25 && f.player->pev->health==65 &&
                state->bloodSuckingPending==25 && state->bloodSuckingCapOverride==0, "health callback sees capped pending and cleared override before final clear");
            Require(f.players->AddBloodSucking(handle,7), "reentrant recovery callback appends actual pending amount");
            if(retire)
            { f.players->Remove(handle); Require(f.players->SetProfile(*f.player,{}) && f.players->AddBloodSucking(handle,4), "callback installs independent replacement registry state"); }
            return !rejected;
        };
        CSOLPlayerRecoveryCall tick(recovery.hit.runtime,handle); ConfigError error;
        const bool result=f.players->UpdateBloodSucking(tick,error); const auto after=f.players->Find(handle);
        Check(result==(!retire && !rejected) && tick.Cancelled()==retire && notices==1 && f.player->pev->health==65 && error.message.empty() &&
            state->bloodSuckingPending==(retire?32:0) && state->bloodSuckingCapOverride==0 && !state->updatingBloodSucking &&
            (retire ? after!=state && after->bloodSuckingPending==4 : after==state),
            "normal or rejected recovery return clears callback additions; retirement preserves prefix and replacement player's pending");
        if(rejected)Check(f.players->UpdateBloodSucking(tick,error) && f.player->pev->health==65 && notices==1,
            "notice failure after health write cannot apply the same pending amount again on next update");
    }
}

// Actual concrete services, with engine/weapon callback probes only. The
// optional Fixture hook loads Golden while Session is genuinely Precached;
// none of these cases claim ordinary Apply, damage subtraction or live combat.
void PrecacheDamageOwner(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const std::shared_ptr<CCSOLScenarioSession>& session)
{
    runtime->session = session;
    auto deaths = std::make_shared<CCSOLScenarioDeathServices>(runtime);
    runtime->deathServices = deaths;
    csol::scenario::ResourceError error;
    Require(deaths->Precache({Engine::Load, Engine::Free}, error),
        "real death owner loads missing Golden file before actual Session Activate");
}

std::shared_ptr<CCSOLScenarioDamageServices> InstallDamageOwner(DamageSample& sample)
{
    auto services = std::make_shared<CCSOLScenarioDamageServices>(sample.runtime);
    sample.runtime->monsterDamageServices = services;
    return services;
}

void ConcreteDamageCallbackOwnership(Engine& engine)
{
    for (int stage = 0; stage < 4; ++stage) for (int outcome = 0; outcome < 3; ++outcome)
    {
        auto runtime = std::make_shared<CCSOLScenarioRuntime>();
        Fixture f(engine, 0, true, 0, false, false, CSOLScenarioResourceScope::Combat, {},
            [&](const auto& session) { PrecacheDamageOwner(runtime, session); });
        auto* source = f.Entity<SourceProbe>(); source->state->values = {WEAPON_AK47};
        DamageSample sample(f, source, 8, runtime); const auto services = InstallDamageOwner(sample);
        const auto state = sample.State(); BuffEngineEdges edges; edges.roll = 50;
        ConfigError error; CSOLMonsterHitAccount account; int callbacks{};
        const auto callback = [&]
        {
            if (callbacks++) return;
            if (outcome != 2)
                runtime->monsterDamageServices = std::make_shared<CCSOLScenarioDamageServices>(runtime);
            if (outcome != 0) throw std::runtime_error("concrete damage callback sentinel");
            error.message = "callback returned a diagnostic after replacing its owner";
        };
        if (stage == 0 || stage == 3) engine.onOffset = [&](const edict_t*) { callback(); };
        if (stage == 1) edges.onRandom = callback;
        if (stage == 2) source->state->onSource = callback;
        bool returned{}, threw{};
        try
        {
            if (stage == 0) returned = services->RulesAfterAttackDamage(sample.call, WEAPON_AK47, error);
            if (stage == 1) returned = services->BeforeHitReward(sample.call, account, error);
            if (stage == 2) returned = services->AfterHitReward(sample.call, error);
            if (stage == 3) returned = services->AfterHealthDamage(sample.call, error);
        }
        catch (const std::runtime_error& ex)
        { threw = std::string_view(ex.what()) == "concrete damage callback sentinel"; }
        engine.onOffset = {}; edges.onRandom = {}; source->state->onSource = {};
        Check(!returned && callbacks >= 1 && threw == (outcome == 2) &&
            sample.call.Cancelled() == (outcome != 2) &&
            (outcome == 2 ? sample.call.Current() == f.monster : !sample.call.Current()) &&
            state->weaponDamage.empty() && state->ordinaryMonsterDamage == 0 && state->bossDamage == 0,
            "each actual stage cancels replaced provider on false/throw, stops later statistics, and preserves a current-owner exception");
        if (outcome != 2)
            Check(runtime->monsterDamageServices != services && !runtime->Closing(),
                "cancelled old hit leaves replacement service and Runtime alive");
    }
}

void ConcreteDamageSourceCapture(Engine& engine)
{
    Fixture f(engine, 0, true, 0, false, false, CSOLScenarioResourceScope::Combat, AddonDamageResources);
    auto* ak = f.Weapon<CAK47>(WEAPON_AK47); auto* knife = f.Weapon<CKnife>(WEAPON_KNIFE);
    f.player->m_pActiveItem = ak;
    const int captured = CSOL_DamageWeaponId(f.player, f.player);
    DamageSample sample(f, f.player, 8); auto services = InstallDamageOwner(sample);
    const auto state = sample.State(); AddonLevels(f, {{2, 2}});
    BuffEngineEdges edges; edges.IntegerRandom(); edges.roll = 0;
    edges.onRandom = [&] { f.player->m_pActiveItem = knife; sample.call.damage = 99; };
    ConfigError error;
    Check(services->RulesAfterAttackDamage(sample.call, captured, error) && error.message.empty() &&
        sample.call.damage == 12 && state->weaponDamage.at(1036) == 12 &&
        !state->weaponDamage.contains(161) && state->addonCriticalPending,
        "concrete rule stage applies addon before statistics, retains addon D snapshot and original captured source after callback switches gun");
}

void ConcreteDamageNonPlayerAndGolden(Engine& engine)
{
    auto runtime = std::make_shared<CCSOLScenarioRuntime>();
    Fixture f(engine, 0, true, 0, false, false, CSOLScenarioResourceScope::Combat, {},
        [&](const auto& session) { PrecacheDamageOwner(runtime, session); });
    DamageSample sample(f, f.player, 8, runtime); auto services = InstallDamageOwner(sample);
    auto& host = f.monster->HostState(); host.damageEffect = 10; host.damageEffectMagnitude = 7;
    CSOLMonsterDamageCall world(sample.spawn, {}, {}, 8, DMG_BLAST, {}, sample.ownerActive,
        [](const void* p) { return static_cast<const DamageSample*>(p)->ownerActive; }, &sample);
    ConfigError error; CSOLMonsterHitAccount account{true, 999};
    runtime->players.reset();
    Check(services->RulesAfterAttackDamage(world, 0, error) && services->BeforeHitReward(world, account, error) &&
        services->AfterHitReward(world, error) && services->AfterHealthDamage(world, error) &&
        error.message.empty() && !world.Cancelled() && !account.moneyExtractor && account.roomOption == 0 &&
        host.damageEffect == 10 && host.damageEffectMagnitude == 7,
        "nonplayer stages skip all player effects without requiring registry, preserving ordinary target tail and untouched effect");
    runtime->players = f.players;
    const auto deaths = runtime->deathServices; StatisticsWire wire; int indices{};
    wire.onEntIndex = [&](const edict_t* ed)
    { Check(ed == f.monster->edict(), "Golden samples target index, never attacker account identity"); ++indices; };
    Check(deaths->GoldenHit(sample.call, error) && indices == 1 && error.message.empty(),
        "actually loaded EmptyPlan still queries target index before missing membership return");
    auto unloaded = std::make_shared<CCSOLScenarioDeathServices>(runtime); runtime->deathServices = unloaded;
    ConfigError unloadedError;
    Check(!unloaded->GoldenHit(sample.call, unloadedError) && !unloadedError.message.empty() && indices == 1 &&
        !sample.call.Cancelled(), "unloaded Golden owner fails rather than treating default null configuration as loaded empty");
    runtime->deathServices = deaths;
    wire.onEntIndex = [&](const edict_t*) { ++indices; runtime->deathServices = unloaded; };
    Check(!deaths->GoldenHit(sample.call, error) && sample.call.Cancelled() && indices == 2 &&
        runtime->deathServices == unloaded, "Golden target-index callback replacement cancels old owner and preserves replacement");
}

void ConcretePostHealthSnapshot(Engine& engine)
{
    auto runtime = std::make_shared<CCSOLScenarioRuntime>();
    Fixture f(engine, 0, true, 0, false, false, CSOLScenarioResourceScope::Combat, {},
        [&](const auto& session) { PrecacheDamageOwner(runtime, session); });
    auto* weapon = f.Weapon<CAK47>(WEAPON_AK47); f.player->m_pActiveItem = weapon;
    DamageSample sample(f, f.player, 8, runtime); const auto services = InstallDamageOwner(sample);
    const auto state = sample.State(); ConfigError error;
    Require(f.players->SetClassModRecords(*f.player, {}) && f.players->RefreshClassMods(EHANDLE(f.player), 49, error),
        "actual complete empty ClassMod import establishes absent type10 and type2 without a fake ability");
    state->addonCriticalPending = true; f.monster->HostState().lastHitgroup = HITGROUP_HEAD;
    StatisticsWire wire; bool changed{};
    engine.onOffset = [&](const edict_t* ed)
    {
        if (changed || ed != f.monster->edict()) return;
        changed = true; sample.call.damage = 99; f.monster->HostState().lastHitgroup = HITGROUP_CHEST;
    };
    Check(services->AfterHealthDamage(sample.call, error) && changed && error.message.empty() && !sample.call.Cancelled() &&
        !state->addonCriticalPending && f.monster->HostState().damageEffect == 8 && state->monsterHeadHits == 1 &&
        state->ordinaryMonsterDamage == 8 && state->monsterDamageHits == 1 && state->bossDamage == 0 &&
        sample.call.damage == 99 && state->displayAbilities && !state->displayAbilities->feature3 && !state->activeSkill &&
        wire.packets.size() == 1 && wire.Last(engine, 1, 1),
        "concrete post-health keeps entry D/hitgroup through addon callback mutation, sends actual head statistic and completes initialized ordinary tails");
    engine.onOffset = {};
}

struct MeleeState
{
    std::uint8_t type{1}, accepted{1};
    int typeCalls{}, parameterCalls{};
    std::function<void()> onType, onParameters;
};
struct MeleeWeaponProbe final : CBasePlayerWeapon, CSOLWeaponMonsterMelee
{
    std::shared_ptr<MeleeState> state = std::make_shared<MeleeState>();
    std::uint8_t MonsterMeleeType() override
    {
        const auto keep = state; ++keep->typeCalls; const auto fn = keep->onType;
        const auto result = keep->type; if (fn) fn(); return result;
    }
    std::uint8_t MonsterMeleeParameters(std::array<float, 7>& out) override
    {
        const auto keep = state; ++keep->parameterCalls;
        Check(out == std::array<float, 7>{1,1,1,1,1,1,0.7f}, "melee output defaults reach actual provider callback");
        out[4] = 2; out[5] = 0.5f;
        const auto result = keep->accepted; const auto fn = keep->onParameters; if (fn) fn(); return result;
    }
    void OnDestroy() override { state.reset(); CBasePlayerWeapon::OnDestroy(); }
};

void ConcreteMeleeImpulse(Engine& engine)
{
    for (int scenario = 0; scenario < 8; ++scenario)
    {
        Fixture f(engine); auto* weapon = f.Weapon<MeleeWeaponProbe>(WEAPON_AK47);
        f.player->m_pActiveItem = weapon; const auto probe = weapon->state;
        DamageSample sample(f, f.player, 0); const auto services = InstallDamageOwner(sample);
        ConfigError error;
        f.player->pev->origin = Vector(0,0,0); f.monster->pev->origin = scenario == 2 ? Vector(0,0,0) : Vector(3,4,0);
        f.monster->pev->velocity = Vector(1,2,3); f.monster->HostState().moveAfter = 4;
        if (scenario == 1) probe->accepted = 2;
        if (scenario == 6) probe->type = 0;
        if (scenario == 7) probe->accepted = 0;
        std::shared_ptr<MeleeState> switched;
        if (scenario == 3)
        {
            auto* next = f.Weapon<MeleeWeaponProbe>(WEAPON_KNIFE); switched = next->state;
            probe->onType = [&, next] { f.player->m_pActiveItem = next; };
        }
        if (scenario == 4) probe->onParameters = [&]
        { sample.runtime->monsterDamageServices = std::make_shared<CCSOLScenarioDamageServices>(sample.runtime); };
        if (scenario == 5) probe->onParameters = [&]
        {
            sample.runtime->monsterDamageServices = std::make_shared<CCSOLScenarioDamageServices>(sample.runtime);
            throw std::runtime_error("retired melee provider");
        };
        bool result{}, threw{};
        try { result = CSOL_ApplyMonsterMeleeImpulse(sample.call, sample.runtime, error); }
        catch (...) { threw = true; }
        const bool applied = scenario == 0 || scenario == 2 || scenario == 3;
        const bool cancelled = scenario == 4 || scenario == 5;
        const Vector expected = !applied ? Vector(1,2,3) : scenario == 2 ? Vector(1,2,128) : Vector(241,322,128);
        const Vector delta = f.monster->pev->velocity - expected;
        Check(!threw && result == !cancelled && sample.call.Cancelled() == cancelled &&
            std::abs(delta.x) < 0.0001f && std::abs(delta.y) < 0.0001f && std::abs(delta.z) < 0.0001f &&
            f.monster->HostState().moveAfter == (applied ? 11 : 4) &&
            error.message.empty() && probe->typeCalls == 1 && probe->parameterCalls == (scenario == 3 || scenario == 6 ? 0 : 1) &&
            (!switched || (switched->typeCalls == 0 && switched->parameterCalls == 1)),
            "melee accepts exactly byte1, retains additive XY/Z at zero D/coincident points, rereads switched active, and cancels provider replacement even on throw");
        probe->onType = {}; probe->onParameters = {};
    }
}

void DisplayDirectoryRegistrationLifecycle(Engine& engine)
{
    Fixture f(engine, 0, true, 0, true, true); const EHANDLE player(f.player);
    const auto first = f.players->Find(player); const auto initial = first->displayAbilities;
    Require(initial && !initial->feature28 && !initial->feature29 && !initial->feature3,
        "new player state owns its actual initialized absent-key directory");
    auto installed = std::make_shared<CSOLScenarioDisplayAbilities>();
    installed->feature28 = 4; installed->feature29 = 0.5f;
    installed->feature3 = std::make_shared<csol::scenario::Feature3>();
    Require(f.players->SetDisplayAbilities(player, installed), "install trusted live display states");
    SelectPrepareCharacter(f, *f.player, CT, 49);
    Require(f.players->SetProfile(*f.player, {}) && f.players->Prepare(*f.player) && f.players->Prepare(*f.player),
        "actual profile refresh and repeated character preparation reuse same registration");
    Check(f.players->Find(player) == first && first->displayAbilities == installed &&
        installed->feature28 == 4 && installed->feature29 == 0.5f && installed->feature3,
        "repeated Ensure during profile/Prepare does not erase externally installed current abilities");
    const auto generation = first->generation;
    f.players->Remove(player); Require(f.players->SetProfile(*f.player, {}), "register new player generation");
    const auto fresh = f.players->Find(player); const auto directory = fresh->displayAbilities;
    Check(fresh != first && fresh->generation > generation && directory && directory != initial && directory != installed &&
        !directory->feature28 && !directory->feature29 && !directory->feature3,
        "Remove then Ensure creates independent absent-key directory rather than carrying prior generation abilities");
}
}

void ScenarioDeathRuntimeCases(sdk_test::Engine& engine)
{
    SourceBranches(engine); SourceCancellation(engine); SourcePhaseIsolation(engine); TailOrderAndRegistry(engine);
    TailCancellation(engine); NestedTailBound(engine); DestructorReentry(engine);
}

void ScenarioDamageStatisticsCases(sdk_test::Engine& engine)
{
    DamageCatalogAndArithmetic(engine); DamageEligibilityAndCapture(engine); CapturedItemMultiplier(engine); DamageStatisticsCancellation(engine);
    HeadHitCountsAndWire(engine); HeadHitEligibility(engine); HeadHitCancellation(engine);
    KillChainTimeAndSignedCases(engine); KillChainHealthAndBossCases(engine); KillChainRibbonGates(engine);
    KillChainCallbackSampling(engine); KillChainCancellationCases(engine); KillChainMessageFailures(engine); KillChainJournalLifecycle(engine);
    PostHitStatisticsArithmetic(engine); PostHitOuterCapture(engine); PostHitBossReplacement(engine);
    PostHitOrdinaryAndGates(engine); PostHitFailurePrefixes(engine); PostHitCancellationAndRelease(engine);
    HitComponentSourceGates(engine); HitComponentLookupCancellation(engine); HitComponentBroadcast(engine);
    HitEventCaptureAndLifetime(engine); HitEventGatesAndFailures(engine);
    ClassModParserCases(); ClassModModeCases(); ClassModRebuildOrder(); ClassModCombinedAndEmpty();
    ClassModSessionResourceCases(engine); ClassModPlayerRevisionCases(engine);
    PresetConfigurationCases(); PresetSlotMappingCases(); PresetCapsAndTransactions();
    PresetSessionScopeAndCancellation(engine); PresetSnapshotLifecycle(engine);
    ClassModLocalDefaultPrepare(engine); ClassModExplicitProfilePrepare(engine); ClassModSelectionPrepare(engine);
    ClassModDamageValues(engine); ClassModDamageProfileGates(engine); ClassModDamageCancellation(engine); ClassModDamageEngineFailures(engine);
    DamageHintDefinitionsAndGates(engine); DamageHintSourceCancellation(engine);
    ConcreteDamageCallbackOwnership(engine); ConcreteDamageSourceCapture(engine);
    ConcreteDamageNonPlayerAndGolden(engine); ConcreteMeleeImpulse(engine);
    ConcretePostHealthSnapshot(engine);
    DisplayDirectoryRegistrationLifecycle(engine);
}

void ScenarioDamageBuffCases(sdk_test::Engine& engine)
{
    BuffRegistrationAndProtection(engine); CriticalThresholdAndGrouping(engine); CriticalNonAdditiveGroups(engine);
    CriticalCallbacksAndFailures(engine); AttackEntryGates(engine); AttackPresenceAndGroups(engine);
    AttackCandidateRequeries(engine); AttackConditionCancellation(engine); AttackArithmeticAndFailures(engine);
    BuffDestructorReentry(engine);
    AttributeDamageOrderAndRecovery(engine);AttributeDamageThresholds(engine);AttributeDamageSeedMutation(engine);
    AttributeDamageGatesAndFailures(engine);AttributeDamageCancellation(engine);
    EnhancementLevelsAndDirectory(engine); EnhancementArithmetic(engine); EnhancementSourceHeads(engine);
    EnhancementIdentityLifecycle(engine); EnhancementCancellation(engine);
    RecoveryLimitsAndGates(engine); RecoveryDeadlineAndSetters(engine); RecoveryAmountsAndContext(engine);
    RecoveryNoticeMutation(engine); RecoveryRegistryMutation(engine); RecoveryFailures(engine);
    RecoveryCancellation(engine); AttributeRecoveryIntegration(engine);
    RecoveryNoticeOwnerGuard(engine); RecoveryNoticeNestedGuard(engine);
    ConcreteRecoveryOrdinary(engine); ConcreteRecoveryEarlyGates(engine);
    ConcreteRecoverySpecialPolicy(engine); ConcreteRecoveryDirectoryCancellation(engine);
    Feature3PlayerQueries(engine); Feature3RecoveryRules(engine); Feature3CloneTiming(engine); Feature3RecoveryCancellation(engine);
    DamageCallCancellationIdentity(engine); RecoveryNestedCancellation(engine);
    RecoveryFailureIsNotCancellation(engine); ConcreteNoticeCancellationAfterBegin(engine);
    RecoveryExceptionCancellation(engine);
    AddonInputsAndParameters(engine); AddonRandomAndPost(engine); AddonFixedDamageAndBoss(engine); AddonCancellation(engine);
    PartHitCriticalCases(engine); PartHitDurationCases(engine); PartHitBossCases(engine); PartHitSeedCancellation(engine);
    BloodSuckingAccumulationAndSwitch(engine); BloodSuckingEmptyDeadAndDisabled(engine); BloodSuckingRecoveryCallbacks(engine);
}

void ScenarioDamageEffectCases(sdk_test::Engine& engine)
{
    DamageEffectWireEncoding(engine); DamageEffectSenderRejections(engine); DamageEffectSkipAndCritical(engine);
    DamageEffectBroadcastRecipients(engine); DamageEffectResampling(engine); DamageEffectConsumerFailures(engine);
    DamageEffectCancellation(engine); DamageEffectLeaseReuseAndReentry(engine);
}
