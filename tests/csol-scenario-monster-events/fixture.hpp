// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_monster_ordinary.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace model_events_test
{
using death_events_test::Check;
using death_events_test::Require;
using ConfigError = csol::scenario::ConfigError;
using Hook = IReGameHook_CBaseAnimating_ResetSequenceInfo;
constexpr float Frame = 1.0f / 30.0f;
bool Near(float left, float right);
struct Fixture;

struct NoClear final : CCSOLSpawnClearDeaths
{
    bool BeforeSpawnClear(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected occupancy clear"); }
    bool RequestSpawnClearDeath(CCSOLScenarioMonster&) override { throw std::runtime_error("unexpected occupancy death"); }
};
struct DeathServices final : CCSOLMonsterDeathServices
{
    std::vector<std::string>* log{};
    std::function<void()> onRules;
    std::shared_ptr<CCSOLScenarioPlayers> Players() const override { throw std::runtime_error("unexpected money registry"); }
    bool MoneyExtractor(CSOLMonsterDeathCall&, const EHANDLE&, bool&, ConfigError&) override { throw std::runtime_error("unexpected money extractor"); }
    bool RoomOption(CSOLMonsterDeathCall&, int&, ConfigError&) override { throw std::runtime_error("unexpected money room query"); }
    bool RulesDeath(CSOLMonsterDeathCall&, ConfigError&) override;
    bool PlayerKilledMonster(CSOLMonsterDeathCall&, ConfigError&) override { throw std::runtime_error("unexpected player reward"); }
    bool GoldenDeath(CSOLMonsterDeathCall&, ConfigError&) override;
};
// Explicit observers of still-unimplemented business services. Each call is
// recorded; an unexpected skill, damage or frozen-death service fails closed.
struct Services final : CCSOLOrdinaryMonsterServices
{
    Fixture& f;
    std::shared_ptr<CCSOLScenarioNavigation> navigation;
    std::vector<std::size_t> protectedSlots;
    bool stopped{}, suppressed{};
    int exclusions{};
    CSOLOrdinaryMonsterState* observed{};
    std::function<void()> onPin, onExcluded, onStopped;
    std::function<void(CSOLOrdinaryMonsterState&)> onSkill;
    std::function<bool(const EHANDLE&)> excluded;
    explicit Services(Fixture& fixture);
    bool OwnerCurrent() const override;
    CCSOLScenarioMap& Map() override;
    CCSOLMonsterPool& Pool() override;
    CCSOLMonsterDeathRequests& Deaths() override;
    CCSOLScenarioOccupancy& Occupancy() override;
    const CCSOLScenarioMessages& Messages() const override;
    std::shared_ptr<const CCSOLMonsterNavigation> PinNavigation() override;
    std::span<const std::size_t> ProtectedSlots() const override { return protectedSlots; }
    const bool& DamageSuppressed() const override { return suppressed; }
    bool LogicStopped() const override;
    bool PlayerTargetExcluded(CSOLMonsterSpawnCall&, const EHANDLE&, bool&, ConfigError&) override;
    bool UseSkill(CSOLMonsterSpawnCall&, CSOLOrdinaryMonsterState&, ConfigError&) override;
    bool ApplyContactImpulse(CSOLMonsterSpawnCall&, const EHANDLE&, const Vector&, ConfigError&) override;
    bool FrozenDeath(CSOLMonsterDeathCall&, ConfigError&) override;
};
struct FactoryServices final : CCSOLMonsterFactoryServices
{
    std::shared_ptr<Services> services;
    CCSOLScenarioMonster* last{};
    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateOrdinary(CSOLMonsterSpawnCall&, ConfigError&) override;
    std::unique_ptr<CCSOLMonsterSpawnBehavior> CreateBoss(CSOLMonsterSpawnCall&, std::int32_t, ConfigError&) override;
    std::span<const std::size_t> ProtectedSlots() const override { return {}; }
    bool ClearSpawnEffects(CSOLMonsterSpawnCall&, ConfigError&) override;
    bool ResolveSpawnOverlap(CSOLMonsterSpawnCall&, ConfigError&) override;
    bool RulesMonsterSpawned(CSOLMonsterSpawnCall&, ConfigError&) override;
};
struct Fixture : death_events_test::Fixture
{
    inline static Fixture* active{};
    std::vector<std::string> log;
    NoClear noClear;
    CCSOLScenarioOccupancy occupancy{pool, noClear};
    CCSOLMonsterModels models;
    DeathServices deathServices;
    std::unique_ptr<CCSOLMonsterDeathOwner> deathOwner;
    std::shared_ptr<Services> services;
    FactoryServices factoryServices;
    std::unique_ptr<CCSOLMonsterFactory> factory;
    struct Model {
        studiohdr_t header{};
        std::array<mstudioseqdesc_t, 35> sequence{};
        std::array<mstudioevent_t, 32> events{};
    } model;
    struct TraceCall { Vector from, to; int flags{}; edict_t* ignored{}; };
    struct Event { int id{}; std::string options; };
    std::vector<TraceCall> traceCalls;
    std::vector<Event> eventCalls;
    std::function<void()> onModel, onTrace, onRandom;
    std::function<void(const TraceCall&, TraceResult&)> traceResponse;
    std::function<void()> onEvent;
    std::function<void(Hook*, CBaseAnimating*)> onAnimationHook;
    bool hooked{}, nullModel{};
    int modelQueries{}, traces{}, randomLong{100}, randomCalls{};
    explicit Fixture(bool observeEvents = false);
    ~Fixture();
    CCSOLScenarioMonster& Spawn();
    CCSOLScenarioMonster& Monster() { return *factoryServices.last; }
    CCSOLOrdinaryMonsterBehavior& Behavior();
    CSOLMonsterSpawnState& SpawnState();
    CSOLMonsterSpawnCall Call();
    void Tick(float now);
    bool Reset();
    void HookAnimation();
    void ReleaseMap();
    CSOLOrdinaryMonsterState& CaptureState();
    void SetEvents(std::initializer_list<std::pair<int, int>>, int sequence = 1);
    bool Dispatch(bool (*ownerValid)(const void*) = nullptr, const void* owner = nullptr);
    static edict_t* CreateEventMonster(int);
    static void* ModelQuery(edict_t*);
    static void Trace(const float*, const float*, int, edict_t*, TraceResult*);
    static void AnimationHook(Hook*, CBaseAnimating*);
};
// Fixture registration identity, with no callback or assumed success.
inline bool Services::OwnerCurrent() const { return f.services.get() == this; }
void ScanCases();
void ValidationCases();
void CancellationCases();
void OwnerIntegrationCases();
}
