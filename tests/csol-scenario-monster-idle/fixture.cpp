// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "hostage/hostage.h"

// Real source defines this global in hostage.cpp. No hostage behavior runs.
cvar_t cv_hostage_stop = {"hostage_stop", "0", FCVAR_SERVER, 0.0f, nullptr};

namespace idle_test
{
bool Near(float left, float right) { return std::abs(left - right) < .0001f; }
Navigation::Navigation()
{
    nav_loader_test::Files files;
    csol::scenario::NavigationFile data;
    data.areas={nav_loader_test::Area(1,0,0,0,2048)};
    owner=nav_loader_test::Load(files,data);
}
Navigation::~Navigation()
{
    owner->Invalidate();
    if(onDestroy)onDestroy();
}
Services::Services(Fixture& fixture) : f(fixture), navigation(std::make_shared<Navigation>()) {}
CCSOLScenarioMap& Services::Map() { return f.map; }
CCSOLMonsterPool& Services::Pool() { return f.pool; }
CCSOLMonsterDeathRequests& Services::Deaths() { return f.deathOwner->Requests(); }
CCSOLScenarioOccupancy& Services::Occupancy() { return f.occupancy; }
const CCSOLScenarioMessages& Services::Messages() const { return f.messages; }
std::shared_ptr<const CCSOLMonsterNavigation> Services::PinNavigation()
{ const auto action = onPin; if (action) action(); return navigation; }
bool Services::LogicStopped() const
{ f.log.emplace_back("D0"); const auto action = onStopped; if (action) action(); return stopped; }
bool Services::PlayerTargetExcluded(CSOLMonsterSpawnCall&, const EHANDLE& target, bool& result, ConfigError&)
{ ++exclusions; const auto action = onExcluded; if (action) action(); result = excluded && excluded(target); return true; }
bool Services::UseSkill(CSOLMonsterSpawnCall&, CSOLOrdinaryMonsterState& state, ConfigError&)
{ observed = &state; Require(bool(onSkill), "skill execution needs an explicit test observer"); onSkill(state); return true; }
bool Services::ApplyContactImpulse(CSOLMonsterSpawnCall&, const EHANDLE&, const Vector&, ConfigError&)
{ throw std::runtime_error("contact impulse outside idle fixture"); }
bool Services::FrozenDeath(CSOLMonsterDeathCall&, ConfigError&)
{ throw std::runtime_error("frozen-death business chain outside owner fixture"); }
bool DeathServices::RulesDeath(CSOLMonsterDeathCall&, ConfigError&)
{ log->emplace_back("death-rules"); const auto action = onRules; if (action) action(); return true; }
bool DeathServices::GoldenDeath(CSOLMonsterDeathCall&, ConfigError&) { log->emplace_back("death-Golden"); return true; }
std::unique_ptr<CCSOLMonsterSpawnBehavior> FactoryServices::CreateOrdinary(CSOLMonsterSpawnCall& call, ConfigError&)
{ last = call.Current(); return std::make_unique<CCSOLOrdinaryMonsterBehavior>(services); }
std::unique_ptr<CCSOLMonsterSpawnBehavior> FactoryServices::CreateBoss(CSOLMonsterSpawnCall&, std::int32_t, ConfigError&)
{ throw std::runtime_error("Boss cannot use ordinary fixture"); }
bool FactoryServices::ClearSpawnEffects(CSOLMonsterSpawnCall&, ConfigError&) { services->f.log.emplace_back("spawn-effects"); return true; }
bool FactoryServices::ResolveSpawnOverlap(CSOLMonsterSpawnCall&, ConfigError&) { services->f.log.emplace_back("spawn-overlap"); return true; }
bool FactoryServices::RulesMonsterSpawned(CSOLMonsterSpawnCall&, ConfigError&) { services->f.log.emplace_back("spawn-rules"); return true; }

Fixture::Fixture()
{
    Require(!active, "single ordinary engine"); active = this;
    engine.globals.time = 20;
    // This fixture tests factory/owner integration, not the already verified
    // encrypted loader. Give the base synthetic round its original physical
    // header/row positions and explicit zero attribute deltas.
    auto& tables = const_cast<csol::scenario::MapTables&>(*map.Tables());
    tables.mapName = "zs_deadend";
    Require(tables.rounds.headerRecord == 0, "known base synthetic round header");
    tables.rounds.source.records.insert(tables.rounds.source.records.begin(), 6, {});
    tables.rounds.headerRecord = 6;
    for (auto& row : tables.rounds.rows)
    {
        row.recordIndex += 6;
        auto& fields = tables.rounds.source.records[row.recordIndex].fields;
        fields.resize(10);
        for (std::size_t i = 3; i < 10; ++i) fields[i] = "0";
    }
    csol::scenario::MonsterType type;
    type.id = 40; type.appearance = "zombi_host"; type.health = 100; type.speed = 200;
    type.attackDamage = 12; type.attackRange = 60; type.attackInterval = .8f;
    tables.monsterTypes.rows = {type}; tables.monsterTypes.byId = {{40, 0}};
    g_engfuncs.pfnPrecacheSound = [](const char*) { return 1; };
    g_engfuncs.pfnSetModel = [](edict_t* ed, const char* path) { ed->v.model = Engine::AllocString(path); ed->v.modelindex = 1; };
    g_engfuncs.pfnSetSize = [](edict_t* ed, const float* lo, const float* hi)
    { ed->v.mins = Vector(lo); ed->v.maxs = Vector(hi); ed->v.size = ed->v.maxs - ed->v.mins; };
    g_engfuncs.pfnGetModelPtr = ModelQuery;
    g_engfuncs.pfnTraceLine = Trace;
    g_engfuncs.pfnRandomFloat = [](float lo, float hi)
    { Check(lo == .1f && hi == .2f, "factory random timer range"); return .15f; };
    g_engfuncs.pfnRandomLong = [](int lo, int hi)
    {
        Check(lo == 0 && hi == 100, "target RNG exact inclusive range");
        ++active->randomCalls; const auto action = active->onRandom; if (action) action(); return active->randomLong;
    };
    g_engfuncs.pfnEmitSound = [](edict_t*, int, const char*, float, float, int, int)
    { throw std::runtime_error("death sound must be explicitly suppressed"); };
    constexpr std::array labels{"zbs_idle1","zbs_walk","zbs_run","zbs_jump","zbs_attack_walk","zbs_attack1_run","zbs_attack2_run",
        "zbs_aim_grenade_idle1","zbs_aim_grenade_walk","zbs_aim_grenade_run","zbs_shoot_grenade_idle1","zbs_shoot_grenade_walk","zbs_shoot_grenade_run",
        "head_flinch","gut_flinch","head","death1","death2","death3","gutshot","left","back","right","forward",
        "zbs_shoot_grenade_idle1","zbs_skill_dash","zbs_skill1_idle","zbs_skill1_walk","zbs_skill1_run","skill1","zbs_climb","turnL","turnR"};
    model.header.length=sizeof(model);
    model.header.numseq = 35;
    model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequence.data()) - reinterpret_cast<char*>(&model));
    for (auto& sequence : model.sequence) { sequence.fps = 10; sequence.numframes = 11; sequence.flags = STUDIO_LOOPING; }
    for (std::size_t i = 0; i < labels.size(); ++i) std::strcpy(model.sequence[i + 2].label, labels[i]);
    ConfigError error;
    Require(models.Precache(tables.monsterTypes, error), "real ordinary appearance catalog precached");
    deathServices.log = &log;
    deathOwner = std::make_unique<CCSOLMonsterDeathOwner>(map, pool, models, messages, deathServices, 98);
    services = std::make_shared<Services>(*this);
    Require(occupancy.Initialize(std::list<CNavArea*>{services->navigation->owner->Areas().front()}), "ordinary occupancy initialized");
    factoryServices.services = services;
    factory = std::make_unique<CCSOLMonsterFactory>(map, pool, models, occupancy, deathOwner->Requests(), factoryServices, messages);
    cv_hostage_stop.value = 0;
}
Fixture::~Fixture()
{
    if (hooked) g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.unregisterHook(AnimationHook);
    onModel = {}; onTrace = {}; onRandom = {}; onAnimationHook = {};
    services->onPin = {};
    services->onExcluded = {}; services->onStopped = {}; services->onSkill = {};
    deathServices.onRules = {};
    deathOwner->Reset(); occupancy.Shutdown(); pool.Shutdown(); if (factory) factory->Reset();
    services->navigation->owner->Invalidate();
    factory.reset(); cv_hostage_stop.value = 0; active = nullptr;
}
CCSOLScenarioMonster& Fixture::Spawn()
{
    const auto result = factory->Spawn(1, 40, 7, 0);
    if (result != CSOLMonsterSpawnResult::Spawned) std::cerr << "ordinary factory: " << factory->Error().message << '\n';
    Require(result == CSOLMonsterSpawnResult::Spawned, "actual factory installs concrete ordinary behavior");
    Monster().pev->origin = Vector(0, 0, 0);
    Monster().pev->flags |= FL_ONGROUND;
    return Monster();
}
CCSOLOrdinaryMonsterBehavior& Fixture::Behavior()
{ auto* behavior = dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(Monster().Behavior()); Require(behavior != nullptr, "actual ordinary owner installed"); return *behavior; }
CSOLMonsterSpawnState& Fixture::SpawnState()
{ const auto* state = factory->State(Monster()); Require(state != nullptr, "actual factory state retained"); return *const_cast<CSOLMonsterSpawnState*>(state); }
CSOLMonsterSpawnCall Fixture::Call() { return {map, pool, deathOwner->Requests(), Monster()}; }
void Fixture::Tick(float now) { engine.globals.time = now; Monster().BehaviorThink(); }
bool Fixture::Reset() { auto call = Call(); ConfigError error; return Behavior().ResetForSpawn(call, SpawnState(), error); }
void Fixture::HookAnimation()
{ hooked = true; g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.registerHook(AnimationHook, 150); }
void Fixture::ReleaseMap()
{ deathOwner->Reset(); map.EndMap(); occupancy.Shutdown(); pool.Shutdown(); services->navigation->owner->Invalidate(); engine.DestroyWorld(); pool.ForgetMap(); }
void* Fixture::ModelQuery(edict_t*)
{ ++active->modelQueries; const auto action = active->onModel; if (action) action(); return active->overrideModelReply ? active->modelReply : &active->model; }
void Fixture::Trace(const float* from, const float* to, int flags, edict_t* ignored, TraceResult* result)
{
    ++active->traces; active->log.emplace_back("trace");
    const nav_loader_test::TraceCall call{Vector(from),Vector(to),flags,ignored};
    active->traceCalls.push_back(call);
    *result = {}; result->flFraction = 1; result->vecEndPos = Vector(to); result->vecPlaneNormal = Vector(0,0,1);
    const auto response=active->traceResponse;if(response)response(call,*result);
    const auto action=active->onTrace;if(action)action();
}
void Fixture::AnimationHook(Hook* chain, CBaseAnimating* monster)
{ const auto action = active->onAnimationHook; if (action) action(chain, monster); else chain->callNext(monster); }
}
