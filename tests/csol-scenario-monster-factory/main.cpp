// SPDX-License-Identifier: GPL-3.0-or-later
// Real factory, pool, occupancy, death owner and ReAPI chain. AI/business
// services below are explicit observation contracts, not gameplay substitutes.
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_monster_factory.h"
#include "../../native/csol/server/scenario_monster_effects.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>

// Only the SDK destructor's global Bot notifications are excluded. The
// separately extracted four-corner constructor and all members are real.
CNavArea::~CNavArea() {}
void CBaseAnimating::ResetSequenceInfo()
{ g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(&CBaseAnimating::ResetSequenceInfo_OrigFunc, this); }
int SENTENCEG_Lookup(const char*, char (&)[32])
{ throw std::runtime_error("unexpected sentence in factory test"); }
void Sys_Error(const char*, ...) { throw std::runtime_error("unexpected SDK fatal error"); }

namespace factory_test
{
using death_events_test::Check;
using death_events_test::Require;
using csol::scenario::ConfigError;
using csol::scenario::MonsterType;
using Result = CSOLMonsterSpawnResult;
using Hook = IReGameHook_CBaseAnimating_ResetSequenceInfo;

MonsterType Type(int id, int priority = 0)
{
    MonsterType row; row.id = id; row.appearance = "zombi_host"; row.priority = static_cast<std::int8_t>(priority);
    row.health = 1234; row.armor = 55; row.speed = 87; row.attackDamage = 12; row.attackRange = 60;
    row.attackInterval = .6f; row.bonusMoney = 6; row.color = {71, 72, 73}; row.column21 = {2, 3, 155};
    return row;
}
struct State
{
    std::string name;
    int resets{}, destroyed{}, recreateQueries{}, deathBefore{}, deathPost{};
    bool recreate{}, earlyPark{};
    std::function<bool(CSOLMonsterSpawnCall&, CSOLMonsterSpawnState&, ConfigError&)> onReset;
    std::function<void()> onDestroy, onRecreate;
    std::vector<std::string>* log{};
};
struct Behavior final : CCSOLMonsterSpawnBehavior
{
    std::shared_ptr<State> state;
    explicit Behavior(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~Behavior() override
    {
        ++state->destroyed; state->log->push_back("destroy:" + state->name);
        try { if (state->onDestroy) state->onDestroy(); } catch (...) { Check(false, "destructor action must not throw"); }
    }
    bool RecreateForSpawn() const override
    { ++state->recreateQueries; if (state->onRecreate) state->onRecreate(); return state->recreate; }
    bool ResetForSpawn(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& runtime, ConfigError& error) override
    {
        ++state->resets; state->log->push_back("reset:" + state->name);
        auto* monster = call.Current(); Require(monster != nullptr, "reset gets current lease");
        Check(monster->pev->takedamage == DAMAGE_NO && monster->pev->health == runtime.attributes.health,
            "model/attributes precede Reset and damage stays disabled through Reset");
        return state->onReset ? state->onReset(call, runtime, error) : true;
    }
    void Think(CCSOLScenarioMonster&) override { throw std::runtime_error("AI not part of factory spy"); }
    BOOL TakeDamage(CCSOLScenarioMonster&, entvars_t*, entvars_t*, float, int) override
    { throw std::runtime_error("damage implementation not part of factory spy"); }
    void Killed(CCSOLScenarioMonster&, entvars_t*, int) override
    { throw std::runtime_error("test death must use the real request owner"); }
    CSOLMonsterDeathParameters DeathParameters() const override { return {}; }
    bool BeforeDeath(CSOLMonsterDeathCall& call, ConfigError&) override
    { ++state->deathBefore; auto* m = call.Current(); return m && (!state->earlyPark || m->Pool()->ParkForDeath(*m)); }
    bool SelectDeathSequence(CSOLMonsterDeathCall&, int& sequence, ConfigError&) override { sequence = 7; return true; }
    bool AdjustDeathDuration(CSOLMonsterDeathCall&, float& seconds, ConfigError&) override { seconds = 3; return true; }
    bool PostDeath(CSOLMonsterDeathCall&, ConfigError&) override { ++state->deathPost; return true; }
};
struct DeathServices final : CCSOLMonsterDeathServices
{
    bool allowed{}; int next{}, effectNotifications{}, effectDestructions{};
    bool Step(int expected, CSOLMonsterDeathCall& call)
    { Require(allowed && next++ == expected && call.Current(), "explicit death service order rules/Golden"); return true; }
    std::shared_ptr<CCSOLScenarioPlayers> Players() const override { throw std::runtime_error("unexpected money registry"); }
    bool MoneyExtractor(CSOLMonsterDeathCall&, const EHANDLE&, bool&, ConfigError&) override { throw std::runtime_error("unexpected money extractor"); }
    bool RoomOption(CSOLMonsterDeathCall&, int&, ConfigError&) override { throw std::runtime_error("unexpected money room query"); }
    bool RulesDeath(CSOLMonsterDeathCall& call, ConfigError&) override { return Step(0, call); }
    bool PlayerKilledMonster(CSOLMonsterDeathCall&, ConfigError&) override { throw std::runtime_error("unexpected player reward"); }
    bool GoldenDeath(CSOLMonsterDeathCall& call, ConfigError&) override
    {
        Check(effectNotifications == 1 && effectDestructions == 1,
            "Golden follows one actual source notification and destruction");
        return Step(1, call);
    }
};
struct ClearDeaths final : CCSOLSpawnClearDeaths
{
    bool BeforeSpawnClear(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected occupancy clear"); }
    bool RequestSpawnClearDeath(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected occupancy clear death"); }
};
struct Services final : CCSOLMonsterFactoryServices
{
    std::vector<std::string>& log;
    std::vector<std::shared_ptr<State>> states;
    std::function<void(const std::shared_ptr<State>&)> configure;
    std::function<void()> onProtected;
    std::function<bool(CSOLMonsterSpawnCall&, ConfigError&)> onEffects, onOverlap, onRules;
    mutable int protectedQueries{};
    std::vector<std::size_t> protectedSlots;
    CCSOLScenarioMonster* last{};
    std::vector<int> bossIds;
    int rules{}, effects{}, overlaps{}, created{};
    explicit Services(std::vector<std::string>& value) : log(value) {}
    std::unique_ptr<CCSOLMonsterSpawnBehavior> Make(CSOLMonsterSpawnCall& call, bool boss, int id)
    {
        last = call.Current(); Require(last != nullptr, "factory service receives current lease");
        log.emplace_back(boss ? "create-boss" : "create"); ++created; if (boss) bossIds.push_back(id);
        auto state = std::make_shared<State>(); state->name = std::to_string(created); state->log = &log;
        states.push_back(state); if (configure) configure(state); return std::make_unique<Behavior>(state);
    }
    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateOrdinary(CSOLMonsterSpawnCall& call, ConfigError&) override { return Make(call, false, 0); }
    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateBoss(CSOLMonsterSpawnCall& call, std::int32_t id, ConfigError&) override { return Make(call, true, id); }
    std::span<const std::size_t> ProtectedSlots() const override
    { ++protectedQueries; log.emplace_back("protected"); if (onProtected) onProtected(); return protectedSlots; }
    bool ClearSpawnEffects(CSOLMonsterSpawnCall& call, ConfigError& error) override
    { ++effects; log.emplace_back("effects"); last = call.Current(); Require(last != nullptr, "effects gets current lease"); return onEffects ? onEffects(call,error) : true; }
    bool ResolveSpawnOverlap(CSOLMonsterSpawnCall& call, ConfigError& error) override
    { ++overlaps; log.emplace_back("overlap"); Require(call.Current() != nullptr, "overlap gets current lease"); return onOverlap ? onOverlap(call,error) : true; }
    bool RulesMonsterSpawned(CSOLMonsterSpawnCall& call, ConfigError& error) override
    { ++rules; log.emplace_back("rules"); Require(call.Current() && call.Current()->pev->health > 0, "rules only sees positive health"); return onRules ? onRules(call,error) : true; }
};

struct Fixture
{
    inline static Fixture* active{};
    death_events_test::EventEngine engine;
    death_events_test::Events events;
    std::vector<std::string> log;
    CCSOLMonsterPool pool;
    CCSOLScenarioMap map;
    CCSOLMonsterModels models;
    CCSOLScenarioMessages messages;
    DeathServices deathServices;
    ClearDeaths clearDeaths;
    CCSOLScenarioOccupancy occupancy{pool, clearDeaths};
    Services services{log};
    std::unique_ptr<CCSOLMonsterDeathOwner> owner;
    std::unique_ptr<CCSOLMonsterFactory> factory;
    std::unique_ptr<CNavArea> area;
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t,35> sequences{}; } model;
    std::function<void(std::string_view, edict_t*)> onEngine;
    std::function<void(Hook*, CBaseAnimating*)> onHook;
    int randomCalls{}, queries{};
    bool advanceClock{}, capture{}, hooks{};

    Fixture()
    {
        active = this; engine.globals.time = 20;
        auto catalog = engine.files.at("resource/maplist.csv");
        const auto name = catalog.find("map_session_test"); Require(name != catalog.npos, "synthetic catalog source present");
        catalog.replace(name, std::strlen("map_session_test"), "zs_deadend"); engine.files["resource/maplist.csv"] = catalog;
        for (const auto prefix : {"zbs_wave_", "zbs_", "ZBS_ZT_", "ZBS_TRG_"})
            engine.files[std::string("maps/") + prefix + "zs_deadend.cso"] = engine.files.at(std::string("maps/") + prefix + "map_session_test.cso");
        // This factory consumes original physical rows: six prefix records,
        // header at 6, then rounds at 7/8. The map-session fixture's header at
        // zero is sufficient for wave lifecycle tests, but not attributes.
        // Encoded independent CSV retains waves 1/0 and seven zero deltas.
        engine.files["maps/zbs_zs_deadend.cso"] = death_events_test::EventEngine::Hex(
            "63a4682c3efd918316a4fe1ed6785f2acf8b064baef95e16ce4104ab4f2bae9465d878c73fbc8d243dae9a6acaa7c6fd"
            "79288059cf7914a7f6ea22a69ebbe9c48d125dd2a445e0770e92aa59d83614035de1f704ea68945f473fb1e34a5ac3c5b"
            "0eaf7bb5754ad97f7477e7a22920f1edfc4cfe2fe22d51c1082fc6685d346ce1082fc6685d346ce4606ebbba67a8401");
        engine.Markers(); Require(map.BeginMap("zs_deadend") && map.CollectEntities(), "real zs_deadend map setup");
        auto& tables = const_cast<csol::scenario::MapTables&>(*map.Tables());
        Require(tables.rounds.headerRecord == 6 && tables.rounds.rows.size() == 2 &&
            tables.rounds.rows[0].recordIndex == 7 && tables.rounds.rows[1].recordIndex == 8,
            "factory round fixture retains original physical records");
        tables.monsterTypes.rows = {Type(40), Type(41,1), Type(42,2)}; tables.monsterTypes.byId = {{40,0},{41,1},{42,2}};
        Require(map.BeginRound(0,0,0,events) && pool.Initialize() && messages.Register(), "real round/pool/messages setup");
        g_engfuncs.pfnPrecacheSound = [](const char*) { return 1; };
        g_engfuncs.pfnSetModel = SetModel; g_engfuncs.pfnSetSize = SetSize;
        g_engfuncs.pfnSetOrigin = SetOrigin; g_engfuncs.pfnGetModelPtr = GetModel;
        g_engfuncs.pfnRandomFloat = Random; g_engfuncs.pfnEmitSound = [](edict_t*,int,const char*,float,float,int,int) { throw std::runtime_error("death must suppress sound"); };
        ConfigError error; Require(models.Precache(tables.monsterTypes,error), "actual model catalog precache");
        const Vector low(-1024,-1024,0), high(1024,1024,0), ne(1024,-1024,0), sw(-1024,1024,0);
        area = std::make_unique<CNavArea>(&low,&ne,&high,&sw);
        Require(occupancy.Initialize(std::list<CNavArea*>{area.get()}), "real occupancy grid initialized");
        owner = std::make_unique<CCSOLMonsterDeathOwner>(map,pool,models,messages,deathServices,98);
        factory = std::make_unique<CCSOLMonsterFactory>(map,pool,models,occupancy,owner->Requests(),services,messages);
        // Labels independently transcribed in the earlier sequence oracle;
        // initial sequence 1 differs deliberately from resolved idle sequence 2.
        constexpr std::array labels{"zbs_idle1","zbs_walk","zbs_run","zbs_jump","zbs_attack_walk","zbs_attack1_run","zbs_attack2_run",
            "zbs_aim_grenade_idle1","zbs_aim_grenade_walk","zbs_aim_grenade_run","zbs_shoot_grenade_idle1","zbs_shoot_grenade_walk","zbs_shoot_grenade_run",
            "head_flinch","gut_flinch","head","death1","death2","death3","gutshot","left","back","right","forward",
            "zbs_shoot_grenade_idle1","zbs_skill_dash","zbs_skill1_idle","zbs_skill1_walk","zbs_skill1_run","skill1","zbs_climb","turnL","turnR"};
        model.header.numseq=35; model.header.seqindex=static_cast<int>(reinterpret_cast<char*>(model.sequences.data())-reinterpret_cast<char*>(&model));
        for (auto& seq : model.sequences) { seq.fps=10; seq.numframes=11; seq.flags=STUDIO_LOOPING; seq.linearmovement[0]=2; }
        std::strcpy(model.sequences[1].label,"scene_appear");
        for(std::size_t i=0;i<labels.size();++i) std::strcpy(model.sequences[i+2].label,labels[i]);
        capture=true; log.clear();
    }
    ~Fixture()
    {
        Unhook(); onEngine={}; onHook={}; engine.onModel={}; engine.onQuery={}; engine.onMessage={}; engine.onRemove={}; engine.findOverride={};
        services.onProtected={}; services.onEffects={}; services.onOverlap={}; services.onRules={}; services.configure={};
        for(auto& state:services.states) { state->onReset={}; state->onRecreate={}; state->onDestroy={}; }
        owner->Reset(); occupancy.Shutdown(); pool.Shutdown(); factory->Reset(); map.EndMap(); engine.DisposeAll(); engine.DestroyWorld(); pool.ForgetMap();
        active=nullptr;
    }
    static void Event(std::string_view name, edict_t* ed)
    { auto& f=*active; if(f.capture) f.log.emplace_back(name); const auto callback=f.onEngine; if(callback) callback(name,ed); }
    static void SetModel(edict_t* ed, const char* name)
    { ed->v.model=Engine::AllocString(name); ed->v.modelindex=123; Event("model",ed); }
    static void SetSize(edict_t* ed,const float* low,const float* high)
    { ed->v.mins=Vector(low); ed->v.maxs=Vector(high); ed->v.size=ed->v.maxs-ed->v.mins; Event("size",ed); }
    static void SetOrigin(edict_t* ed,const float* point)
    { Engine::SetOrigin(ed,point); Event("origin",ed); }
    static void* GetModel(edict_t* ed)
    { ++active->queries; Event("query",ed); return &active->model; }
    static float Random(float low,float high)
    {
        auto& f=*active; Check(low==.1f && high==.2f,"RNG exact original range");
        const int index=f.randomCalls++; Require(index<3,"exactly three engine RNG calls per spawn");
        if(f.advanceClock) f.engine.globals.time+=1;
        Event("rng",nullptr); return std::array{.11f,.13f,.17f}[index];
    }
    static void FirstHook(Hook* chain,CBaseAnimating* monster)
    { active->log.emplace_back("hook-high"); if(active->onHook) active->onHook(chain,monster); else chain->callNext(monster); active->log.emplace_back("hook-high-return"); }
    static void SecondHook(Hook* chain,CBaseAnimating* monster)
    { active->log.emplace_back("hook-low"); chain->callNext(monster); active->log.emplace_back("hook-low-return"); }
    void Hooked()
    { hooks=true; auto& registry=g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo; registry.registerHook(SecondHook,50); registry.registerHook(FirstHook,150); }
    void Unhook()
    { if(hooks) { auto& registry=g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo; registry.unregisterHook(FirstHook); registry.unregisterHook(SecondHook); hooks=false; } }
    Result Spawn(int requested=40,int row=0,int point=7)
    {
        randomCalls=0;
        const auto result=factory->Spawn(1,requested,point,row);
        if(result==Result::Failed)
            std::cerr<<"factory result="<<static_cast<int>(result)<<" error="<<factory->Error().message<<'\n';
        return result;
    }
    MonsterType& Row(int key) { return const_cast<MonsterType&>(*map.Tables()->monsterTypes.Find(key)); }
    std::uint32_t Quota() const { return map.SpawnPoints().Points().FindMonsterPoint(1,7)->consumed; }
    CCSOLScenarioMonster& Last() { Require(services.last!=nullptr,"created monster captured"); return *services.last; }
    std::vector<std::string> NoQueries() const
    { auto result=log; std::erase(result,"query"); return result; }
    void Fill()
    { while(pool.AvailableCount()) Require(pool.Acquire(Row(41),7,map.SpawnPoints().Points())!=nullptr,"fill real pool without factory side effects"); }
    void Reborrow(CCSOLScenarioMonster& old)
    { Fill();const auto lease=pool.Lease(old);Require(pool.Free(old),"return exact slot"); auto* next=pool.Acquire(Row(41),7,map.SpawnPoints().Points());Require(next==&old&&pool.Lease(old)!=lease,"same address different lease"); }
    void ReleaseMap()
    { Require(owner->Reset() && map.EndMap() && occupancy.Shutdown() && pool.Shutdown(),"cancel map before releasing storage"); engine.DisposeAll();engine.DestroyWorld();Require(pool.ForgetMap(),"forget released pool"); }
};

void Order()
{
    Fixture f; f.advanceClock=true; f.Hooked();
    auto& target=f.engine.AddTarget(500,"mm_zb_spawn_41");
    target.action=[&] { f.log.emplace_back("target");auto& m=f.Last();Check(m.pev->takedamage==DAMAGE_YES,"targets see reset-complete damage state"); };
    Require(f.Spawn(41,0x1ab)==Result::Spawned,"nonzero priority factory succeeds"); auto& m=f.Last();const auto* state=f.factory->State(m);
    const std::vector<std::string> expected{"protected","model","size","create","reset:1","target","effects","origin","rng","rng","rng","hook-high","hook-low","hook-low-return","hook-high-return","overlap","rules"};
    Check(f.NoQueries()==expected,"full external stage order and genuine hook priority/order");
    Check(f.engine.uses.size()==1&&f.engine.uses[0].type==USE_SET&&f.engine.uses[0].value==0&&
        !f.engine.uses[0].activator&&!f.engine.uses[0].caller,"spawn targets use null/null SET zero");
    Require(state!=nullptr,"spawn state available");
    Check(m.pev->nextthink==20+.11f&&state->updateAt==21+.13f&&state->animationAt==22+.17f&&f.randomCalls==3,
        "three independent random offsets capture time before each callback");
    Check(state->ambientAt>=30&&state->ambientAt<=44,"ambient timer uses independent CRT range after third callback");
    Check(m.pev->sequence==1&&m.pev->frame==0&&m.m_flFrameRate==256&&m.m_fSequenceLoops&&m.pev->animtime==23,
        "initial sequence1 and real SDK sequence metadata refreshed");
    Check(state->sequences[csol::scenario::MonsterSequence::Idle]==2&&state->rowArgument==0xab,
        "resolved idle differs from initial1 and row byte narrows");
    Check(m.pev->health==1234&&m.pev->armorvalue==55&&m.pev->maxspeed==87&&m.pev->renderamt==155&&m.pev->rendercolor==Vector(71,72,73),
        "attribute/render data reaches real entity");
    Check(f.Quota()==1&&f.pool.CategoryCount(1)==1&&f.services.rules==1,"attempt quota and category count retained");
}

CSOLMonsterReplacement Replacement(int id=100)
{ CSOLMonsterReplacement r;r.type=Type(id,1);r.start=0;r.end=100;r.next=0;r.interval=10;r.remaining=1;return r; }
void ReplacementCases()
{
    for(int edge=0;edge<5;++edge)
    {
        Fixture f;auto r=Replacement(); if(edge==0)r.start=20;if(edge==1)r.end=20;if(edge==2)r.next=20;if(edge==3)r.remaining=0;
        if(edge==4) { r.start=std::nextafter(20.f,0.f);r.end=std::nextafter(20.f,100.f);r.next=std::nextafter(20.f,0.f); }
        Require(f.factory->ScheduleReplacement(r),"schedule boundary candidate");Require(f.Spawn()==Result::Spawned,"boundary spawn");
        Check(f.Last().Type()->id==(edge==4?100:40),"strict start/end/next/count admission");
    }
    for(int mode=0;mode<3;++mode)
    {
        Fixture f;auto r=Replacement();r.type.column18=true;r.useRecordIdentity=mode==1;if(mode==2)r.type.column20=77;
        Require(f.factory->ScheduleReplacement(r)&&f.Spawn()==Result::Spawned,"identity variant spawn");
        Check(f.Last().TypeId()==(mode?100:40)&&f.Last().pev->body==(mode==1?1:0),"runtime58 and column20 identity; column18 does not substitute");
    }
    for(const char* excluded:{"zombieclimber","flydrone","elite_forceC","elite_forceT"})
    {
        Fixture f;f.Row(40).appearance=excluded;Require(f.factory->ScheduleReplacement(Replacement()),"queue for exclusion");
        // The existing model cache intentionally lacks the new appearance.
        // Exact exclusion reaches the base record and fails its model/category
        // contract; an erroneously admitted replacement would succeed.
        Check(f.Spawn()==Result::Failed&&f.services.created==0,"excluded appearance does not use otherwise valid replacement");
    }
    {
        Fixture f; f.Fill();auto first=Replacement(100),second=Replacement(200);first.type.priority=2;second.type.priority=2;
        Require(f.factory->ScheduleReplacement(first)&&f.factory->ScheduleReplacement(second),"two ordered replacements");
        Check(f.Spawn()==Result::Skipped&&f.services.protectedQueries==2&&f.Quota()==1,
            "first allocation miss tries base once, never next candidate, quota remains consumed");
        Require(f.pool.Free(*f.pool.At(0)),"make one slot available");f.engine.globals.time=20.1f;
        Require(f.Spawn()==Result::Spawned,"candidate next deadline after miss");Check(f.Last().Type()->id==200,
            "first miss advances its next without advancing untried second candidate");
        Require(f.pool.Free(*f.pool.At(1)),"make next slot available");f.engine.globals.time=31;
        Require(f.Spawn()==Result::Spawned,"first candidate retries after interval");Check(f.Last().Type()->id==100,"miss retained first candidate count");
        Require(f.pool.Free(*f.pool.At(2)),"make final slot available");f.engine.globals.time=42;
        Require(f.Spawn()==Result::Spawned,"both candidates exhausted");Check(f.Last().Type()->id==40,"only successful replacement decrements remaining");
    }
    {
        Fixture f;for(int i=0;i<39;++i)Require(f.pool.Acquire(f.Row(40),7,f.map.SpawnPoints().Points()),"ordinary category filled");
        Check(f.Spawn()==Result::Skipped&&f.Quota()==1&&f.services.protectedQueries==0,"quota consumed before category cap");
        Check(f.Spawn(999)==Result::Skipped&&f.Quota()==1,"unknown type consumes no quota");
        Check(f.Spawn(40,0,999)==Result::Skipped&&f.Quota()==1,"missing point skips before type/quota");
        Require(f.Spawn(42)==Result::Spawned,"priority2 bypasses quota");Check(f.Quota()==1,"priority2 quota unchanged");
    }
}

void ReuseAndEarlyDeath()
{
    for(int variant=0;variant<4;++variant)
    {
        Fixture f;Require(f.Spawn()==Result::Spawned,"initial behavior");auto& monster=f.Last();auto previous=f.services.states[0];
        f.Fill();Require(f.pool.Free(monster),"same-slot reuse availability");
        if(variant==1)previous->recreate=true;
        if(variant==2)f.Row(41).skill=1000;
        if(variant==3)f.Row(40).id=32800;
        const int queries=f.queries;Require(f.Spawn(variant==0||variant==1?40:41)==Result::Spawned,"reuse/recreate spawn");
        Check(&f.Last()==&monster,"same entity address reused");
        Check(f.services.created==(variant==0?1:2)&&previous->resets==(variant==0?2:1),"ordinary reuse vs explicit/boss/category recreation");
        if(variant==0)Check(f.queries-queries==2,"same type sequence cache avoids repeated label queries");
        if(variant==2)Check(f.services.bossIds==std::vector<int>{41},"Boss creation receives record ID");
        Check(previous->destroyed==(variant==0?0:1),"old behavior lifetime ends after replacement completes");
    }
    for(bool early:{false,true})
    {
        Fixture f;f.services.configure=[&](const auto& state){state->earlyPark=early;};f.deathServices.allowed=true;
        auto& source=f.engine.Client(1);
        auto& target=f.engine.AddTarget(500,"mm_zb_spawn_41");std::uint64_t lease{};
        target.action=[&]
        {
            auto& m=f.Last();lease=f.pool.Lease(m);
            CSOLMonsterSpawnCall call(f.map,f.pool,f.owner->Requests(),m);
            const CSOLMonsterEffectGuard guard{[](const void* context)
                {return static_cast<const CSOLMonsterSpawnCall*>(context)->Current();},&call};
            struct EffectLifetime
            {
                DeathServices* services;
                explicit EffectLifetime(DeathServices& value) : services(&value) {}
                ~EffectLifetime()
                {
                    ++services->effectDestructions;
                    Check(services->next==1&&services->effectNotifications==1,
                        "actual source destruction follows notification between rules and Golden");
                }
            };
            auto token=std::make_shared<EffectLifetime>(f.deathServices);
            Require(m.DamageEffects()->Install(guard,source.pev,6,3,0,725,-1,{},[&f,token]
            {
                ++f.deathServices.effectNotifications;
                Check(f.deathServices.next==1&&f.deathServices.effectDestructions==0,
                    "actual source notification occurs after rules and before Golden");
            }),"spawn target installs actual effect before requesting death");
            token.reset();
            CSOLMonsterDeathArguments args;args.suppressSound=true;
            Require(f.owner->Requests().Request(m,args)==CSOLDeathRequestResult::Executed,"target runs full real synchronous death owner");
        };
        Require(f.Spawn(41)==Result::Spawned,"same lease early death accepted as completed spawn");auto& m=f.Last();
        Check(f.pool.Lease(m)==lease&&m.pev->health==0&&m.pev->takedamage==DAMAGE_NO&&m.pev->deadflag==DEAD_DEAD,
            "spawn never revives or reenables damage after target death");
        Check(f.services.rules==0&&f.services.overlaps==1&&f.deathServices.next==2&&
            f.deathServices.effectNotifications==1&&f.deathServices.effectDestructions==1&&
            !m.DamageEffects()->At(1).notify&&m.DamageEffects()->At(1).count==0&&f.Quota()==0,
            "late overlap still runs but positive-health rules skipped; owner returns quota once");
        Check(f.pool.Borrowed(m)!=early&&f.services.states[0]->deathBefore==1&&f.services.states[0]->deathPost==1,
            "parked and borrowed death paths preserve their actual pool state");
    }
    {
        Fixture f;Require(f.Spawn()==Result::Spawned,"spawn retained behavior before model-query replacement");
        auto& monster=f.Last();auto old=f.services.states[0];f.Fill();Require(f.pool.Free(monster),"reuse same slot");
        auto replacement=std::make_shared<State>();replacement->name="query";replacement->log=&f.log;f.services.states.push_back(replacement);
        bool replaced{};f.onEngine=[&](auto stage,auto* ed)
        { if(stage=="query"&&!replaced){replaced=true;Require(static_cast<CCSOLScenarioMonster*>(ed->pvPrivateData)->InstallBehavior(std::make_unique<Behavior>(replacement)),"replace after model operation during label query");} };
        f.services.onRules=[&](auto&,auto&){Check(old->destroyed==0,"entry behavior retained through final rules callback");return true;};
        old->onDestroy=[&]{Require(f.owner->Reset(),"retained behavior destructor cancels dispatcher");};
        Check(f.Spawn(41)==Result::Cancelled&&replacement->resets==1&&old->destroyed==1,
            "query replacement reset uses current behavior; final destructor cancellation rechecked");
        Check(f.pool.ActiveCount()==44,"cancelled same lease returned without touching other slots");
    }
    {
        Fixture f;Require(f.Spawn()==Result::Spawned,"spawn before reset gates");auto& m=f.Last();
        Check(!f.factory->Reset(),"reset refuses borrowed type");Require(f.pool.Free(m),"park slot");
        Check(!f.factory->Reset(),"empty active count is insufficient while type pointer remains");
        Require(f.pool.RestartRound()&&f.factory->Reset(),"round reset clears type pointers before caches");
        Check(!f.factory->State(m),"factory reset drops runtime slot state");
    }
}

void ClearPointCases()
{
    {
        Fixture f; f.Row(41).clearPoint = 100;
        auto& target = f.engine.AddTarget(500, "mm_zb_spawn_41");
        target.action = [&] { Check(f.map.ClearPoint() == -1, "spawn target observes progress still disabled"); };
        f.services.onOverlap = [&](auto&, auto&)
        { Check(f.map.ClearPoint() == -1, "overlap runs before ordinary clear-point enabling"); return true; };
        f.services.onRules = [&](auto&, auto&)
        { Check(f.map.ClearPoint() == 0, "spawn rule observer sees progress already enabled"); return true; };
        Require(f.Spawn(41) == Result::Spawned, "positive-point type spawns through real factory");
        Check(f.map.ClearPoint() == 0 && f.services.rules == 1 && f.services.overlaps == 1 && f.engine.uses.size() == 1,
            "normal spawn enables progress once after target and overlap");
        target.action = {};
        f.services.onEffects = [](auto&, auto&)->bool { throw std::runtime_error("later spawn effect fault"); };
        Check(f.Spawn(41) == Result::Failed && f.map.ClearPoint() == 0 && f.services.rules == 1,
            "later failed spawn does not disable existing manager progress");
    }
    for (const float points : {0.f, -10.f})
    {
        Fixture f; f.Row(40).clearPoint = points;
        Require(f.Spawn() == Result::Spawned, "nonpositive-point type still spawns normally");
        Check(f.map.ClearPoint() == -1 && f.services.rules == 1,
            "successful nonpositive-point monster does not enable manager progress");
    }
    for (const bool enableInOverlap : {false, true})
    {
        Fixture f; f.Row(41).clearPoint = enableInOverlap ? 0 : 50;
        f.services.onOverlap = [&](auto& call, auto&)
        {
            Require(call.Current() && call.Current()->Type() == &f.Row(41), "overlap sees current catalog record");
            f.Row(41).clearPoint = enableInOverlap ? 50 : 0;
            return true;
        };
        Require(f.Spawn(41) == Result::Spawned, "spawn with overlap record mutation");
        Check(f.map.ClearPoint() == (enableInOverlap ? 0 : -1),
            "ordinary enabling reads current type value after overlap callback");
    }
    {
        Fixture f; auto replacement = Replacement(); replacement.type.clearPoint = 80;
        Require(f.factory->ScheduleReplacement(replacement) && f.Spawn() == Result::Spawned,
            "real scheduled replacement spawns instead of zero-point requested type");
        Check(f.Last().Type()->id == 100 && f.Row(40).clearPoint == 0 && f.map.ClearPoint() == 0,
            "clear-point enabling uses actual replacement record rather than requested type");
    }
    // Retain the existing death-service contract by installing and retiring a
    // real effect source in every early-death case; do not seed spy counters.
    for (const int stage : {0, 1, 2})
    {
        Fixture f; f.Row(41).clearPoint = 100;
        f.services.configure = [&](const auto& state) { state->earlyPark = stage == 1; };
        f.deathServices.allowed = true; auto& source = f.engine.Client(1);
        int deaths{}; std::uint64_t lease{};
        const auto kill = [&]
        {
            ++deaths; auto& monster = f.Last(); lease = f.pool.Lease(monster);
            Check(f.map.ClearPoint() == -1, "early death begins while progress is disabled");
            CSOLMonsterSpawnCall call(f.map, f.pool, f.owner->Requests(), monster);
            const CSOLMonsterEffectGuard guard{[](const void* context)
                { return static_cast<const CSOLMonsterSpawnCall*>(context)->Current(); }, &call};
            struct EffectLifetime
            {
                DeathServices& services;
                explicit EffectLifetime(DeathServices& value) : services(value) {}
                ~EffectLifetime()
                {
                    ++services.effectDestructions;
                    Check(services.next == 1 && services.effectNotifications == 1,
                        "clear-point early-death effect retires between rules and Golden");
                }
            };
            auto token = std::make_shared<EffectLifetime>(f.deathServices);
            Require(monster.DamageEffects()->Install(guard, source.pev, 6, 3, 0, 725, -1, {}, [&f, token]
            {
                ++f.deathServices.effectNotifications;
                Check(f.deathServices.next == 1 && f.deathServices.effectDestructions == 0,
                    "clear-point early-death effect notification precedes source retirement");
            }), "install actual effect before early death");
            token.reset();
            CSOLMonsterDeathArguments args; args.suppressSound = true;
            Require(f.owner->Requests().Request(monster, args) == CSOLDeathRequestResult::Executed,
                "early-death case uses real synchronous death owner");
        };
        if (stage == 2) f.services.onOverlap = [&](auto&, auto&) { kill(); return true; };
        else f.engine.AddTarget(500, "mm_zb_spawn_41").action = kill;
        Require(f.Spawn(41) == Result::Spawned, "early death preserves completed same-lease spawn");
        Check(deaths == 1 && f.pool.Lease(f.Last()) == lease && f.Last().pev->health == 0 &&
            f.map.ClearPoint() == -1 && f.services.rules == 0 && f.services.overlaps == 1 && f.deathServices.next == 2,
            "target death, parked target death and overlap death all skip positive-health manager tail");
    }
    for (const int failure : {0, 1, 2})
    {
        Fixture f; f.Row(40).clearPoint = 100;
        if (failure == 0) f.Fill();
        else if (failure == 1)
            for (int i = 0; i < 39; ++i)
                Require(f.pool.Acquire(f.Row(40), 7, f.map.SpawnPoints().Points()) != nullptr, "fill actual ordinary category");
        else f.services.onEffects = [](auto&, auto&)->bool { throw std::runtime_error("pre-tail effect fault"); };
        Check(f.Spawn() == (failure == 2 ? Result::Failed : Result::Skipped) &&
            f.map.ClearPoint() == -1 && f.services.rules == 0 && f.services.overlaps == 0,
            "full pool, category cap and effect failure never enable clear-point progress");
    }
}

void Cancellation()
{
    for(const char* stage:{"protected","model","size","query","origin","rng"})
    {
        Fixture f;bool ran{};const auto cancel=[&]{if(!ran){ran=true;Require(f.owner->Reset(),"dispatcher reset callback");}};
        if(std::strcmp(stage,"protected")==0)f.services.onProtected=cancel;
        else f.onEngine=[&](auto name,auto*){if(name==stage)cancel();};
        Check(f.Spawn()==Result::Cancelled&&ran&&f.pool.ActiveCount()==0&&f.factory->Error().message.empty(),
            "stage generation cancellation returns only incomplete lease and keeps factory error clean");
    }
    for(bool releaseStorage:{false,true})
    {
        Fixture f;f.Hooked();f.onHook=[&](Hook* chain,CBaseAnimating* m)
        { if(releaseStorage) f.ReleaseMap();else Require(f.owner->Reset(),"ReAPI dispatcher cancel");chain->callOriginal(m); };
        Check(f.Spawn()==Result::Cancelled&&f.services.overlaps==0,"real ReAPI original endpoint rejects cancelled or freed entity");
    }
    for(int query: {1,34,35})
    {
        Fixture f;f.onEngine=[&](auto stage,auto*) { if(stage=="query"&&f.queries==query)f.ReleaseMap(); };
        Check(f.Spawn()==Result::Cancelled&&f.services.rules==0,"model query callback storage release stops cache/first/second sequence query safely");
    }
    for(int mode=0;mode<3;++mode)
    {
        Fixture f;f.Hooked();auto* other=f.pool.At(44);Require(other!=nullptr,"independent redirected entity");
        f.onHook=[&](Hook* chain,CBaseAnimating* m)
        { if(mode==0)return;if(mode==1)chain->callOriginal(m);else chain->callOriginal(other); };
        const auto result=f.Spawn();
        Check(result==(mode==2?Result::Failed:Result::Spawned),"ReAPI suppress/original/redirection semantics");
        Check(std::count(f.log.begin(),f.log.end(),"hook-low")==0,"callOriginal/suppress skip lower hook");
        if(mode==0)Check(f.queries==33,"suppressed original performs no metadata queries");
        if(mode==1)Check(f.queries==35,"callOriginal invokes both real metadata queries");
        if(mode==2)Check(f.pool.ActiveCount()==0&&f.services.overlaps==0,"redirected original fails without registering incomplete slot");
    }
    {
        Fixture f;f.services.onEffects=[&](auto& call,auto&){f.Reborrow(*call.Current());return true;};
        Check(f.Spawn()==Result::Failed&&f.pool.ActiveCount()==45,"same-slot reborrow is not returned as incomplete old lease");
    }
    {
        Fixture f;f.services.onEffects=[](auto&,auto&)->bool{throw std::runtime_error("explicit effect fault");};
        Check(f.Spawn()==Result::Failed&&f.pool.ActiveCount()==0,"service exception returns incomplete slot");
        const auto quota=f.Quota();Check(f.Spawn()==Result::Failed&&f.Quota()==quota,"failed factory does not repeat quota side effects");
        Require(f.pool.RestartRound()&&f.factory->Reset(),"failed transaction can reset after type cleanup");f.services.onEffects={};
        Require(f.Spawn()==Result::Spawned,"factory recovered after reset");
    }
    {
        Fixture f;f.onEngine=[&](auto stage,auto* ed){if(stage=="model")f.engine.DestroyPrivate(ed);};
        Check(f.Spawn()==Result::Failed&&f.pool.State()==CSOLMonsterPoolState::Failed,"model destruction marks real pool failed");
        Check(!f.factory->Reset(),"factory Reset refuses failed pool despite no valid active work");
        f.onEngine={};Require(f.pool.Shutdown()&&f.factory->Reset(),"shutdown permits safe cache release");
    }
}
}

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    try
    {
        const std::string_view group=argv[1];
        if(group=="--order")factory_test::Order();
        else if(group=="--replacement")factory_test::ReplacementCases();
        else if(group=="--lifecycle")factory_test::ReuseAndEarlyDeath();
        else if(group=="--cancellation")factory_test::Cancellation();
        else if(group=="--clearpoint")factory_test::ClearPointCases();
        else return 2;
    }
    catch(const std::exception& error){++death_events_test::failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"monster_factory checks="<<death_events_test::checks<<" failures="<<death_events_test::failures<<'\n';
    return death_events_test::failures?1:0;
}
