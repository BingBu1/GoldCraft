// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_oberon.h"
#include "scenario_oberon_attacks.h"
#include "scenario_oberon_projectile.h"
#include "scenario_oberon_resources.h"
#include "../monsters/scenario_monster_services.h"
#include "../monsters/scenario_monster_damage.h"
#include "../monsters/scenario_monster_effects.h"
#include "../core/scenario_gameplay.h"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include "../core/game_rules_gate.h"
#include "hostage/hostage.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#pragma clang fp contract(off)
namespace
{
constexpr float FrameInterval = 1.0f / 30.0f;
void Start(csol::scenario::BossSkillTimer& clock, float duration)
{ clock.duration = duration; clock.deadline = gpGlobals->time + duration; clock.startedAt = gpGlobals->time; }
float Square3(const Vector& v) { return (v.y * v.y + v.x * v.x) + v.z * v.z; }
bool Finite(const Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Vector Feet(const CCSOLScenarioMonster& monster)
{
    Vector feet = monster.pev->origin;
    if (const auto* appearance = csol::scenario::FindMonsterAppearance(monster.Type()->appearance))
        feet.z += appearance->feetZ;
    return feet;
}
}

CCSOLOberonServices::CCSOLOberonServices(std::shared_ptr<CCSOLScenarioRuntime> runtime,
    std::shared_ptr<CCSOLScenarioMonsterServices> ordinary)
    : m_runtime(runtime), m_ordinary(ordinary), m_session(runtime->session),
      m_knives(std::make_shared<CCSOLOberonKnifeEffects>()) {}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLOberonServices::Runtime() const
{
    const auto runtime = m_runtime.lock();
    const auto ordinary = m_ordinary.lock();
    return runtime && !runtime->Closing() && runtime->session == m_session && ordinary &&
        runtime->monsterServices == ordinary && ordinary->OwnerCurrent() ? runtime : nullptr;
}
std::shared_ptr<CCSOLScenarioMonsterServices> CCSOLOberonServices::Ordinary() const
{ return Runtime() ? m_ordinary.lock() : nullptr; }
bool CCSOLOberonServices::OwnerCurrent() const { return Runtime() != nullptr; }
bool CCSOLOberonServices::Initialize(csol::scenario::ConfigError& error)
{ return OwnerCurrent() && m_knives->Initialize(m_session, error) && OwnerCurrent(); }
const CCSOLScenarioMessages& CCSOLOberonServices::BossMessages() const { return m_session->Messages(); }
bool CCSOLOberonServices::RulesSkillStarted(CSOLMonsterSpawnCall&, csol::scenario::ConfigError& error)
{
    // Original rules354 has no failure return. Its directory+2C producer is
    // not yet proven, so report this visual omission without freezing SK AI.
    error.message = "original rules354 permission directory is not yet implemented";
    return false;
}

CCSOLOberonBehavior::CCSOLOberonBehavior(std::shared_ptr<CCSOLOberonServices> services,
    std::shared_ptr<CCSOLOrdinaryMonsterDamage> damage, std::shared_ptr<csol::scenario::BossSkills> skills)
    : m_services(services), m_damage(std::move(damage)), m_skills(std::move(skills)),
      m_control(std::make_shared<CCSOLMonsterBossControl>(m_skills, services))
{
    // MP113D65AE ->113E4940, before any Oberon skill queries a room.
    const auto runtime = services ? services->Runtime() : nullptr;
    if (!runtime || !runtime->session ||
        !runtime->session->BossRooms().ControllerConstructed(runtime->session->Map().EntityAccess()))
        throw std::runtime_error("Oberon room cache requires its live session");
}

CCSOLScenarioMonster* CCSOLOberonBehavior::Current(CSOLMonsterSpawnCall& call, const StatePtr& state) const
{
    const auto services = m_services.lock();
    if (!services || !services->OwnerCurrent() || !state || m_state != state ||
        !state->control || state->control != m_control->State()) return nullptr;
    auto* monster = call.Current();
    return monster && monster->pev->deadflag != DEAD_DEAD &&
        monster->Behavior() == this && state->control->spawn &&
        monster->Pool()->Borrowed(*monster) && monster->Pool()->Lease(*monster) == state->control->lease &&
        state->control->spawn->lease == state->control->lease ? monster : nullptr;
}
bool CCSOLOberonBehavior::Step(CSOLMonsterSpawnCall& call, const StatePtr& state, bool result, const char* failure)
{
    if (!Current(call, state)) return false;
    if (!result && state->error.message.empty()) state->error.message = failure;
    return result && state->error.message.empty();
}
bool CCSOLOberonBehavior::ResetForSpawn(CSOLMonsterSpawnCall& input,
    CSOLMonsterSpawnState& spawn, csol::scenario::ConfigError& error)
{
    const auto services = m_services.lock();
    const auto ordinary = services ? services->Ordinary() : nullptr;
    auto* monster = input.Current();
    if (!ordinary || !monster || monster->Behavior() != this || !input.BelongsTo(ordinary->Map())) return false;
    CSOLMonsterSpawnCall call(ordinary->Map(), ordinary->Pool(), ordinary->Deaths(), *monster, this, services);
    auto navigation = ordinary->PinNavigation();
    if (!services->OwnerCurrent() || !call.Current()) return false;
    if (!navigation || !navigation->World().Ready())
    { error.message = "Oberon requires the actual loaded navigation world"; return false; }
    if (!m_control->Reset(call, spawn, error)) return false;
    auto state = std::make_shared<State>();
    state->control = m_control->State();
    state->navigation = std::move(navigation);
    state->control->position.worldGeneration = state->navigation->World().Generation();
    m_state = state;
    return Current(call, state) != nullptr;
}
bool CCSOLOberonBehavior::MoveTo(CSOLMonsterSpawnCall& call, const StatePtr& state, const Vector& goal)
{
    auto* monster = Current(call, state);
    if (!monster || !Finite(goal) || state->control->position.startingJump) return false;
    auto& control = *state->control;
    auto& position = control.position;
    const Vector change = goal - position.destination;
    if (change.z < 36 && Square3(change) < 90000 &&
        Square3(monster->pev->origin - position.destination) > 202500) return true;
    position.destination = goal;
    position.follower.Reset();
    ++position.updateSerial;
    if (!control.knownGoodValid)
    {
        control.knownGoodValid = true;
        if (!(gpGlobals->time <= position.jumpUntil) && !position.motion.crouching)
        {
            if (gpGlobals->time > control.knownGoodClock.deadline)
            {
                Start(control.knownGoodClock, 1);
                control.previousKnownGood = control.knownGood;
                control.previousKnownGoodValid = true;
            }
            control.knownGood = Feet(*monster);
        }
    }
    return true;
}
bool CCSOLOberonBehavior::Stop(CSOLMonsterSpawnCall& call, const StatePtr& state)
{
    auto* monster = Current(call, state);
    if (!monster || !MoveTo(call, state, Feet(*monster))) return false;
    monster->pev->velocity = g_vecZero;
    state->control->position.motion.moveType = std::min(0, state->control->position.motion.maxMoveType);
    return true;
}
bool CCSOLOberonBehavior::FindPath(CSOLMonsterSpawnCall& call, const StatePtr& state)
{
    auto& control = *state->control;
    if (!control.path.Elapsed(gpGlobals->time)) return true;
    Start(control.path, 0.6f);
    auto* target = control.target.Get<CBaseEntity>();
    if (!target) return true;
    const bool player = target->IsPlayer() != FALSE;
    auto* monster = Current(call, state);
    if (!monster) return false;
    target = control.target.Get<CBaseEntity>();
    if (!target) return true;
    Vector goal = target->pev->origin;
    if (player && monster->pev->maxspeed >= 300 && monster->HostState().activeSkill != 1)
    {
        const float scale = (monster->pev->maxspeed - 300) * std::bit_cast<float>(std::uint32_t{0x3c360b61});
        goal += target->pev->velocity * (scale * 0.2f + 0.3f);
    }
    return MoveTo(call, state, goal);
}
bool CCSOLOberonBehavior::Debris(CSOLMonsterSpawnCall& call, const StatePtr& state, bool landing)
{
    const int model = MODEL_INDEX(CSOLOberonDebrisModel);
    auto* monster = Current(call, state);
    const auto services = m_services.lock();
    if (!monster || !services) return false;
    if (model <= 0) return Step(call, state, false, "Oberon concrete debris is not precached");
    const auto forward = csol::scenario::oberon::Forward(monster->pev->angles.y);
    const auto pitch = csol::scenario::oberon::Forward(monster->pev->angles.x);
    Vector origin(monster->pev->origin.x + (forward[0] * pitch[0]) * 200.0f,
        monster->pev->origin.y + (forward[1] * pitch[0]) * 200.0f,
        landing ? monster->pev->origin.z - 100.0f : monster->pev->origin.z - pitch[1] * 200.0f);
    const Vector size = landing ? monster->pev->size : Vector(60, 60, 20);
    return Step(call, state, services->BossMessages().SendOberonDebris(origin, size, landing, model),
        "Oberon debris message failed");
}
bool CCSOLOberonBehavior::Effects(CSOLMonsterSpawnCall& call, const StatePtr& state,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime)
{
    auto& control = *state->control;
    if (!control.row || !control.effect.Elapsed(gpGlobals->time)) return true;
    const auto row = m_skills->Table().rows[*control.row];
    if (row.type == 5) return m_control->LaunchOberonLeap(call, state->error);
    Start(control.effect, 99999);
    if (row.type == 3 || row.type == 4)
    {
        if (Current(call, state)->HostState().transformed)
        {
            const auto services = m_services.lock();
            if (!services || !Step(call, state, services->Knives()->Play(call, row.type, state->error),
                "Oberon knife effect failed")) return false;
        }
        else if (row.type == 3 && !Debris(call, state, false)) return false;
        return Step(call, state, CSOL_OberonSector(runtime, call, row, state->error), "Oberon sector attack failed");
    }
    else if (row.type == 6)
    {
        if (!Step(call, state, CSOL_OberonCylinder(runtime, call, row, 0, false, state->error),
            "Oberon black-hole attack failed")) return false;
    }
    else if (row.type == 7)
    {
        if (control.repeatCount) { Start(control.effect, 3); --control.repeatCount; }
        return Step(call, state, CCSOLOberonProjectile::CreateBatch(runtime, call, row, state->error),
            "Oberon bombing batch failed");
    }
    if (row.type == 6 && Current(call, state)->HostState().transformed)
    {
        const auto services = m_services.lock();
        if (!services) return false;
        return Step(call, state, services->Knives()->Play(call, row.type, state->error), "Oberon knife effect failed");
    }
    return true;
}
bool CCSOLOberonBehavior::Landing(CSOLMonsterSpawnCall& call, const StatePtr& state,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime)
{
    auto* monster = Current(call, state);
    if (!monster) return false;
    auto& control = *state->control;
    if (!control.row || !(monster->pev->origin.z < control.launchOriginZ + 196.84999084472656f) ||
        !(monster->pev->velocity.z <= 0)) return true;
    const float launchZ = control.launchOriginZ;
    control.launchOriginZ = -999999;
    control.clocks.end.deadline = control.clocks.end.startedAt = -1;
    const auto row = m_skills->Table().rows[*control.row];
    if (row.type != 5) return true;
    if (!Step(call, state, CSOL_OberonCylinder(runtime, call, row, launchZ, true, state->error),
        "Oberon landing attack failed") || !Debris(call, state, true)) return false;
    Start(control.movement, 2);
    if (!Current(call, state)->Animation().LockUntil(gpGlobals->time + 2)) return false;
    Start(control.clocks.end, 2);
    Start(control.clocks.selection, 2);
    return true;
}
bool CCSOLOberonBehavior::UpdatePosition(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& ordinary)
{
    const auto services = m_services.lock();
    const auto runtime = services ? services->Runtime() : nullptr;
    auto* monster = Current(call, state);
    if (!runtime || !monster) return false;
    auto& control = *state->control;
    if (control.movement.Elapsed(gpGlobals->time))
    {
        const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
        const auto navigation = state->navigation;
        if (!appearance || navigation->World().Generation() != control.position.worldGeneration) return false;
        const auto result = CSOL_UpdateOrdinaryMonsterPosition(call, navigation->World(), ordinary.Occupancy(),
            *appearance, control.position, FrameInterval, monster->HostState().moveAfter,
            control.avoidance.deadline, navigation->ClimberLadders());
        monster = Current(call, state);
        if (!monster || result == CSOLMonsterPositionResult::Cancelled) return false;
        if (result == CSOLMonsterPositionResult::Failed) return false;
        if (result == CSOLMonsterPositionResult::NavigationExpired) monster->HostState().navigationExpired = true;
        if (monster->HostState().transformed) monster->pev->maxspeed = 300;
    }
    return Landing(call, state, runtime);
}
bool CCSOLOberonBehavior::UpdateBehavior(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& ordinary)
{
    auto* monster = Current(call, state);
    if (!monster || monster->pev->deadflag == DEAD_DEAD || cv_hostage_stop.value > 0) return monster != nullptr;
    const auto services = m_services.lock();
    const auto runtime = services ? services->Runtime() : nullptr;
    if (!runtime || !runtime->gameplay) return false;
    if (ordinary.LogicStopped()) return Current(call, state) != nullptr;
    auto& control = *state->control;
    control.deltaX = monster->pev->origin.x - state->previousOrigin.x;
    control.deltaY = monster->pev->origin.y - state->previousOrigin.y;
    state->previousOrigin = monster->pev->origin;
    if (control.clocks.target.Elapsed(gpGlobals->time))
    {
        state->jitter = float(std::rand() % 30) * FrameInterval;
        Start(control.clocks.target, 3.0f + state->jitter);
        if (!SelectTarget(call, state, ordinary)) return false;
    }
    if (control.clocks.selection.Elapsed(gpGlobals->time))
    {
        Start(control.clocks.selection, 2);
        Start(control.clocks.end, 99999);
        if (!Step(call, state, m_control->Select(call, state->error), "Oberon skill selection failed")) return false;
    }
    if (state->interrupt && !Step(call, state, m_control->Interrupt(call, state->interrupt, state->error),
        "Oberon interruption failed")) return false;
    const auto begin = m_control->TryBeginOberon(call, runtime->session, runtime->gameplay->Factory(), state->error);
    if (begin == CSOLBossBeginResult::Cancelled) return false;
    if (!Step(call, state, begin != CSOLBossBeginResult::Failed, "Oberon skill begin failed") ||
        !Effects(call, state, runtime) ||
        !Step(call, state, m_control->Finish(call, state->error), "Oberon skill finish failed") ||
        !FindPath(call, state)) return false;
    monster = Current(call, state);
    if (!monster) return false;
    if (Square3(control.position.destination - Feet(*monster)) < 400.0f) return Stop(call, state);
    control.position.motion.moveType = control.position.motion.maxMoveType;
    struct Reset { CCSOLOberonBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; } reset{this, &call, state};
    const bool animated = monster->Animation().UpdateLocomotionGuarded(*monster, control.spawn->sequences,
        control.spawn->skill, control.deltaX, control.deltaY, gpGlobals->time,
        control.position.motion.climbSequence, [](void* value)
        {
            const auto& c = *static_cast<Reset*>(value);
            return CSOL_ResetMonsterSequenceInfo(*c.call, [](const void* value)
            { const auto& c = *static_cast<const Reset*>(value); return c.owner->Current(*c.call, c.state) != nullptr; }, &c);
        }, &reset);
    if (!Step(call, state, animated, "Oberon locomotion animation failed")) return false;
    const bool alive = Current(call, state)->IsAlive() != FALSE;
    monster = Current(call, state);
    if (!monster) return false;
    if (alive && gpGlobals->time > control.solidAfter) monster->pev->solid = SOLID_SLIDEBOX;
    return true;
}
bool CCSOLOberonBehavior::RequestInterrupt(CSOLMonsterSpawnCall& call, std::int16_t cause)
{
    if (!Current(call, m_state)) return false;
    m_state->interrupt = cause;
    return true;
}
bool CCSOLOberonBehavior::ResolveHitDamage(CSOLMonsterDamageCall& call, int weapon,
    csol::scenario::ConfigError& error)
{ return CSOL_ResolveOberonHitDamage(call, weapon, error); }
bool CCSOLOberonBehavior::DamageFlinch(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error)
{
    // Both original v164 slots point to the common flinch routine, but the
    // caller excludes health-summary appearances and type IDs above29999.
    // This behavior is restricted to30200, so no damage call may reach it.
    if (Current(call, m_state)) error.message = "Oberon must be excluded by the original normal flinch gate";
    return false;
}


bool CCSOLOberonBehavior::ValidateTarget(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    auto* target = state->control->target.Get<CBaseEntity>();
    if (!target) return true;
    const bool player = target->IsPlayer() != FALSE;
    if (!Current(call, state)) return false;
    target = state->control->target.Get<CBaseEntity>();
    if (!target) return true;
    bool excluded{};
    if (player)
    {
        // Capture flags before the feature callback, matching the original.
        const bool dormant = static_cast<std::uint32_t>(target->pev->flags) == 0x80000000u;
        if (!Step(call, state, services.PlayerTargetExcluded(call, state->control->target,
            excluded, state->error), "Oberon player target feature query failed")) return false;
        excluded = excluded || dormant;
    }
    else excluded = (target->pev->effects & EF_NODRAW) != 0;
    if (excluded)
    {
        state->control->target = nullptr;
        state->distantTarget = nullptr; // Oberon v144
    }
    return true;
}


bool CCSOLOberonBehavior::SelectTarget(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32 ||
        !g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnRandomLong ||
        !g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnTraceLine)
        return Step(call, state, false, "Oberon target engine services are unavailable");
    std::array<EHANDLE, 32> candidates, nearby;
    std::size_t count{}, closeCount{};
    float closestDistance = 9000000.0f;
    EHANDLE closest;
    const int clientCount = gpGlobals->maxClients;
    for (int index = 1; index <= clientCount; ++index)
    {
        auto* edict = INDEXENT(index);
        auto* monster = Current(call, state);
        if (!monster) return false;
        if (!edict || edict->free) continue;
        auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(edict));
        if (!player || static_cast<int>(player->m_iTeam) == monster->pev->team ||
            static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) continue;
        EHANDLE handle(player);
        const bool alive = player->IsAlive() != FALSE;
        if (!Current(call, state)) return false;
        if (!alive || !handle.Get<CBaseEntity>()) continue;
        bool excluded{};
        if (!Step(call, state, services.PlayerTargetExcluded(call, handle, excluded, state->error),
            "Oberon candidate feature query failed")) return false;
        if (excluded) continue;
        auto* candidate = handle.Get<CBaseEntity>();
        monster = Current(call, state);
        if (!monster) return false;
        if (!candidate) continue;
        const float squared = Square3(monster->pev->origin - candidate->pev->origin);
        if (!std::isfinite(squared))
            return Step(call, state, false, "Oberon target position is not finite");
        candidates[count++] = handle;
        if (squared < closestDistance) { closestDistance = squared; closest = handle; }
        if (squared < 55799.88671875f) nearby[closeCount++] = handle;
    }
    // The engine RNG stream is separate from the CRT stream below. This
    // check happens even when the player candidate list is empty.
    const auto roll = RANDOM_LONG(0, 100);
    if (!Current(call, state)) return false;
    if (roll <= 15)
    {
        auto* follower = UTIL_FindEntityByClassname(nullptr, "zs_follower");
        if (!Current(call, state)) return false;
        if (follower && UTIL_IsValidEntity(follower->edict()))
        {
            EHANDLE handle(follower);
            const bool alive = follower->IsAlive() != FALSE;
            auto* monster = Current(call, state);
            if (!monster) return false;
            follower = handle.Get<CBaseEntity>();
            if (alive && follower)
            {
                const Vector start = monster->pev->origin, end = follower->pev->origin;
                TraceResult trace{};
                UTIL_TraceLine(start, end, dont_ignore_monsters, monster->edict(), &trace);
                if (!Current(call, state)) return false;
                // MP 113ECCFC takes the follower branch for a non-unit
                // fraction (including unordered), not a clear trace.
                if (trace.flFraction != 1.0f && handle.Get<CBaseEntity>())
                { state->control->target = handle; return true; }
            }
        }
    }
    if (count && static_cast<double>(closestDistance) > 619998.73046875)
    {
        // EHANDLE replaces the original raw cache; a removed cached edict
        // cannot become a dangling pointer or refer to a new occupant.
        if (!state->distantTarget.Get<CBaseEntity>())
            state->distantTarget = candidates[static_cast<std::size_t>(std::rand()) % count];
        state->control->target = state->distantTarget;
        return true;
    }
    state->distantTarget = nullptr;
    if (closestDistance > 6199.9873046875f && closeCount >= 3)
    {
        state->control->target = nearby[static_cast<std::size_t>(std::rand()) % closeCount];
        return true;
    }
    auto* monster = Current(call, state);
    if (!monster) return false;
    const auto policy = monster->HostState().targetPolicy;
    const auto type = monster->TypeId();
    auto* previous = state->control->target.Get<CBaseEntity>();
    if (previous && (policy == 100 || (policy == 200 && (type < 500 || type > 29999))))
    {
        const EHANDLE handle(previous);
        bool eligible = true;
        if (policy == 200)
        {
            eligible = previous->IsPlayer() != FALSE;
            if (!Current(call, state)) return false;
            previous = handle.Get<CBaseEntity>();
        }
        if (previous && eligible)
        {
            const bool alive = previous->IsAlive() != FALSE;
            if (!Current(call, state)) return false;
            previous = handle.Get<CBaseEntity>();
            if (alive && previous)
            {
                if (policy == 200)
                {
                    const bool stillPlayer = previous->IsPlayer() != FALSE;
                    if (!Current(call, state)) return false;
                    previous = handle.Get<CBaseEntity>();
                    if (!previous) { state->control->target = nullptr; return true; }
                    if (stillPlayer && previous->pev->rendermode == kRenderTransAlpha && previous->pev->renderamt < 1.0f)
                        state->control->target = nullptr;
                }
                return true;
            }
        }
    }
    state->control->target = closest;
    return true;
}


bool CCSOLOberonBehavior::UpdateStatusTick(CSOLMonsterSpawnCall& call, bool& elapsed)
{
    elapsed = false;
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    if (!services || !services->OwnerCurrent()) return false;
    auto state = m_state;
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    auto& status = monster->HostState();
    if (!(gpGlobals->time >= status.statusUpdateAt)) return true;
    elapsed = true;
    status.statusUpdateAt = gpGlobals->time + 1.0f;
    if (monster->pev->health > 0 && status.damageScaleCount != 0)
    {
        status.damageScaleCount = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(status.damageScaleCount) - 1u);
        monster->pev->health -= status.nonlethalDamage;
        if (!(monster->pev->health > 0)) monster->pev->health = 1;
    }
    else if (status.damageScaleCount <= 0)
    {
        status.nonlethalDamage = 0;
        status.damageScaleCount = 0;
        status.damageScale = 1.0f;
    }
    return true;
}


bool CCSOLOberonBehavior::UpdateAmbientAndEffects(CSOLMonsterSpawnCall& call,
    const StatePtr& state, CCSOLOrdinaryMonsterServices& services)
{
    #pragma clang fp contract(off)
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!Step(call, state, appearance != nullptr, "Oberon ambient appearance is unavailable")) return false;
    if (!appearance->ambientSound.empty() && gpGlobals->time > state->control->spawn->ambientAt)
    {
        if (!Step(call, state, g_engfuncs.pfnEmitSound != nullptr, "Oberon ambient sound service is unavailable"))
            return false;
        state->control->spawn->ambientAt = (gpGlobals->time + 7.0f) + static_cast<float>(std::rand() % 15);
        EMIT_SOUND_DYN(monster->edict(), CHAN_BODY, appearance->ambientSound.data(),
            1.0f, ATTN_NORM, 0, PITCH_NORM);
        if (!Current(call, state)) return false;
    }
    bool elapsed{};
    if (!Step(call, state, UpdateStatusTick(call, elapsed), "Oberon status update failed")) return false;
    struct Capture { CCSOLOberonBehavior* owner; CSOLMonsterSpawnCall& call; StatePtr state; };
    const Capture capture{this, call, state};
    const CSOLMonsterEffectGuard guard{[](const void* value)
    {
        const auto& scope = *static_cast<const Capture*>(value);
        return scope.owner->Current(scope.call, scope.state);
    }, &capture};
    monster = Current(call, state);
    if (!monster) return false;
    auto effects = monster->DamageEffects();
    if (!Step(call, state, effects != nullptr, "Oberon damage effects are unavailable")) return false;
    if (elapsed && !Step(call, state, effects->Tick(call, guard, state->error), "Oberon DOT update failed"))
        return false;
    return Step(call, state, CSOL_UpdateMonsterGait(call, state->error, [](const void* value)
    {
        const auto& scope = *static_cast<const Capture*>(value);
        return scope.owner->Current(scope.call, scope.state) != nullptr;
    }, &capture), "Oberon gait update failed");
}


BOOL CCSOLOberonBehavior::TakeDamage(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, float damage, int damageType)
{
    return TakeDamageWithContext(monster, inflictor, attacker, damage, damageType, {});
}


BOOL CCSOLOberonBehavior::TakeDamageWithContext(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, float damage, int damageType,
    const CSOLMonsterDeathContext& damageContext)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    auto state = m_state;
    if (!services || !services->OwnerCurrent() || !state) return FALSE;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), monster, this, services);
    if (!Current(call, state)) return FALSE;
    auto owner = m_damage;
    if (!owner)
    {
        if (state->error.message.empty()) state->error.message = "Oberon damage owner is not installed";
        return FALSE;
    }
    struct DamageContext { CCSOLOberonBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
    const DamageContext context{this, &call, state};
    return owner->Apply(call, inflictor, attacker, damage, damageType, state->error,
        [](const void* pointer)
        {
            const auto& context = *static_cast<const DamageContext*>(pointer);
            return context.owner->Current(*context.call, context.state) != nullptr;
        }, &context, damageContext);
}


void CCSOLOberonBehavior::Killed(CCSOLScenarioMonster& monster, entvars_t* attacker, int)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    auto state = m_state;
    if (!services || !services->OwnerCurrent() || !state) return;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), monster, this, services);
    if (!Current(call, state)) return;
    CSOLMonsterDeathArguments arguments;
    if (attacker) arguments.attacker = CBaseEntity::Instance(attacker);
    services->Deaths().Request(monster, arguments);
}


CSOLMonsterDeathParameters CCSOLOberonBehavior::DeathParameters() const
{
    if (!m_state || !m_state->control->spawn) return {};
    return {m_state->control->spawn->attributes.bonusMoney, m_state->control->spawn->skill, m_state->control->spawn->shortDeath};
}


bool CCSOLOberonBehavior::BeforeDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    if (!services || !services->OwnerCurrent()) return false;
    auto* monster = call.Current();
    if (!monster) return false;
    return !(monster->HostState().frozenRemaining > 0) || services->FrozenDeath(call, error);
}


bool CCSOLOberonBehavior::AdjustDeathDuration(CSOLMonsterDeathCall& call,
    float&, csol::scenario::ConfigError&)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    if (!services || !services->OwnerCurrent()) return false;
    return call.Current() != nullptr; // Ordinary override returns the input unchanged.
}


bool CCSOLOberonBehavior::PostDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError&)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    if (!services || !services->OwnerCurrent()) return false;
    return call.Current() != nullptr;
}

bool CCSOLOberonBehavior::SelectDeathSequence(CSOLMonsterDeathCall& call,
    int& sequence, csol::scenario::ConfigError& error)
{
    const auto services = m_services.lock();
    const auto state = m_state;
    auto* monster = call.Current();
    if (!services || !services->OwnerCurrent() || !state || !monster || monster->Behavior() != this) return false;
    if (!g_engfuncs.pfnGetModelPtr)
    { error.message = "Oberon death model query is unavailable"; return false; }
    void* model = GET_MODEL_PTR(monster->edict());
    if (!services->OwnerCurrent() || m_state != state || call.Current() != monster) return false;
    sequence = ::LookupSequence(model, "death");
    if (sequence == -1) sequence = state->control->spawn->sequences[csol::scenario::MonsterSequence::Gutshot];
    return true;
}


void CCSOLOberonBehavior::Think(CCSOLScenarioMonster& entity)
{
    const auto provider = m_services.lock();
    auto services = provider ? provider->Ordinary() : nullptr;
    auto state = m_state;
    if (!services || !services->OwnerCurrent() || !state || state->thinking || !state->error.message.empty() || !gpGlobals) return;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), entity, this, services);
    auto* monster = Current(call, state);
    if (!monster) return;
    struct Scope { bool& busy; Scope(bool& value) : busy(value) { busy = true; } ~Scope() { busy = false; } } scope(state->thinking);
    try
    {
        monster->pev->nextthink = gpGlobals->time + FrameInterval;
        if (!ValidateTarget(call, state, *services)) return;
        monster = Current(call, state);
        if (!monster || (monster->pev->effects & EF_NODRAW)) return;
        auto& host = monster->HostState();
        if (host.frozenRemaining > 0)
        {
            host.frozenRemaining = std::max(0.0f, host.frozenRemaining - FrameInterval);
            if (host.frozenRemaining <= 0)
            {
                monster->pev->rendercolor = Vector(255, 255, 255);
                struct ResetContext { CCSOLOberonBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
                ResetContext context{this, &call, state};
                const bool reset = CSOL_ResetMonsterSequenceInfo(call, [](const void* pointer)
                {
                    const auto& context = *static_cast<const ResetContext*>(pointer);
                    return context.owner->Current(*context.call, context.state) != nullptr;
                }, &context);
                if (!Step(call, state, reset, "Oberon thaw sequence reset failed")) return;
            }
        }
        if (gpGlobals->time > state->control->spawn->animationAt)
        {
            monster = Current(call, state);
            if (monster->HostState().frozenRemaining > 0) monster->pev->framerate = 0;
            else monster->StudioFrameAdvance(0);
            // CSO and the stock SDK both use a fixed .1 event window, even
            // while frozen. CMonster's default event handler is empty.
            struct EventContext { CCSOLOberonBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
            EventContext context{this, &call, state};
            const bool dispatched = CSOL_DispatchMonsterAnimationEvents(call, [](const void* pointer)
            {
                const auto& context = *static_cast<const EventContext*>(pointer);
                return context.owner->Current(*context.call, context.state) != nullptr;
            }, &context);
            if (!Step(call, state, dispatched,
                "Oberon animation events failed")) return;
            state->control->spawn->animationAt = gpGlobals->time + FrameInterval;
        }
        monster = Current(call, state);
        if (!monster) return;
        if (monster->HostState().frozenRemaining <= 0)
        {
            const auto behavior = services->Deaths().BeginBehavior(*monster);
            if (!Step(call, state, behavior.has_value(), "Oberon behavior death scope failed")) return;
            bool moved = true;
            bool cancelled = false;
            try
            {
                if (monster->pev->deadflag != DEAD_DEAD)
                    moved = UpdatePosition(call, state, *services);
                cancelled = Current(call, state) == nullptr;
            }
            catch (...)
            {
                if (Current(call, state)) state->error.message = "Oberon position callback threw";
                moved = false;
            }
            const bool ended = services->Deaths().EndBehavior(*behavior);
            if (cancelled) return;
            if (!Step(call, state, ended && moved, "Oberon position update failed")) return;
        }
        if (state->control->spawn->updateAt > gpGlobals->time) return;
        state->control->spawn->updateAt = gpGlobals->time + 0.1f;
        monster = Current(call, state);
        if (!monster) return;
        if (monster->HostState().frozenRemaining <= 0 && !UpdateBehavior(call, state, *services)) return;
        UpdateAmbientAndEffects(call, state, *services);
    }
    catch (...)
    {
        if (Current(call, state)) state->error.message = "Oberon Think callback threw";
    }
}
