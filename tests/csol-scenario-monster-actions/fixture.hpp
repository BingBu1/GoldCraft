// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-death-events/fixture.hpp"
#include "../../native/csol/server/scenario_monster_follow.h"
#include "../../native/csol/server/scenario_monster_melee.h"
#include "../../native/csol/server/scenario_monster_heading.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace actions_test
{
using death_events_test::Check;
using death_events_test::Require;
using Hook = IReGameHook_CBaseAnimating_ResetSequenceInfo;
bool Equal(const Vector&, const Vector&);
bool Clock(const CSOLMonsterInterval&, float interval, float deadline, float stamp);
struct Damage
{
    entvars_t* inflictor{};
    entvars_t* attacker{};
    float amount{};
    int flags{};
};
// Exact virtual call receiver/observer, not an emulation of player health,
// armor, infection, kills or game rules damage. Those require live validation.
struct Victim final : CBaseEntity
{
    bool player{};
    int isPlayerCalls{};
    std::vector<Damage> damage;
    std::function<void()> onIsPlayer, onDamage;
    BOOL IsPlayer() override;
    BOOL TakeDamage(entvars_t*, entvars_t*, float, int) override;
};
struct TraceCall { Vector from, to; int flags{}; edict_t* ignore{}; };
struct Fixture : death_events_test::Fixture
{
    inline static Fixture* active{};
    CCSOLScenarioMonster* monster{};
    std::optional<CSOLMonsterSpawnCall> call;
    CSOLMonsterSpawnState state;
    CSOLMonsterMeleeClocks clocks;
    float boostUntil{};
    bool suppressed{}, hooked{};
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t,8> sequence{}; } model;
    int queries{};
    std::function<void()> onModel;
    std::function<void(Hook*,CBaseAnimating*)> onAnimation;
    std::function<void(const TraceCall&,TraceResult&)> traceResponse;
    std::vector<TraceCall> traces;
    CCSOLScenarioNavWorld world;
    std::vector<std::unique_ptr<CNavArea>> areaStorage;
    std::vector<CNavArea*> areas;
    csol::scenario::MonsterAppearance appearance;
    Fixture();
    ~Fixture();
    Victim& Target(int slot=500);
    CNavArea* Flat(float x,float y,float z=0,float half=40);
    void Grid();
    void ClearGraph();
    void ReborrowSameSlot();
    void HookAnimation();
    void ClearTraces();
    CSOLMonsterMeleeResult Attack(const EHANDLE&,float dx=0,float dy=0);
    static void* ModelQuery(edict_t*);
    static void AnimationHook(Hook*,CBaseAnimating*);
    static void Trace(const float*,const float*,int,edict_t*,TraceResult*);
};
void HeadingCases();
void MeleeCases();
void MeleeCancellationCases();
void FollowCases();
void FollowCancellationCases();
void MotionCases();
}
