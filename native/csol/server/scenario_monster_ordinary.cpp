// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_ordinary.h"
#include "scenario_monster_damage.h"
#include "scenario_monster_effects.h"
#include "../scenario/monster_death.hpp"
#include "hostage/hostage.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr float FrameInterval = 1.0f / 30.0f;
bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float Square3(const Vector& value)
{
    return (value.y * value.y + value.x * value.x) + value.z * value.z;
}
void StartClock(CSOLMonsterInterval& clock, float interval)
{
    clock.interval = interval;
    clock.deadline = gpGlobals->time + interval;
    clock.stamp = gpGlobals->time;
}
Vector Feet(const CCSOLScenarioMonster& monster)
{
    Vector feet = monster.pev->origin;
    if (const auto* appearance = csol::scenario::FindMonsterAppearance(monster.Type()->appearance))
        feet.z += appearance->feetZ;
    return feet;
}
}

bool CCSOLOrdinaryMonsterBehavior::UseSkill(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    #pragma clang fp contract(off)
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    const auto skill = state->spawn->skill;
    if (skill != 1 && skill != 2)
        return services.UseSkill(call, *state, state->error);
    // 113F0970, mode15. These first-wave skills update their own active and
    // reuse clocks. They do NOT assign enhancedUntil: the ordinary D0 tail
    // still restores the material/speed using its independent host deadline.
    float duration{}, cooldown{};
    if (skill == 1)
    {
        monster->pev->maxspeed = std::min(state->spawn->attributes.speed + 100.0f, 390.0f);
        monster->pev->rendermode = kRenderNormal;
        monster->pev->rendercolor = Vector(255, 0, 0);
        duration = 5.0f;
        cooldown = 10.0f;
    }
    else
    {
        monster->pev->maxspeed = std::max(state->spawn->attributes.speed - 100.0f, 100.0f);
        monster->pev->rendermode = kRenderTransAlpha;
        monster->pev->renderfx = kRenderFxNone;
        monster->pev->renderamt = 35;
        duration = 7.0f;
        cooldown = 15.0f;
    }
    // Match the separate multiply/add and deadline writes in the original
    // float32 path; the live build must not contract these into an FMA.
    const float jitter = state->jitter * 5.0f;
    StartClock(state->skillActive, duration);
    StartClock(state->skill, cooldown + jitter);
    return Current(call, state) != nullptr;
}

bool CCSOLOrdinaryMonsterBehavior::UpdateSkill(CSOLMonsterSpawnCall& call, const StatePtr& state)
{
    #pragma clang fp contract(off)
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    // 113F03D0: an SK row and an unexpired (or unordered) secondary clock
    // both leave the entire state untouched. This is not the frozen gate.
    if (monster->HostState().hasSkillRow || !(gpGlobals->time > state->secondarySkill.deadline))
        return true;
    StartClock(state->secondarySkill, 99999.0f);
    if (monster->HostState().activeSkill != 9) return true;
    auto* target = state->target.Get<CBaseEntity>();
    if (!Current(call, state)) return false;
    if (!target) return true;

    const Vector delta = target->pev->origin - monster->pev->origin;
    // The original normalizes in 3D before using XY: float squares/sum,
    // double sqrt narrowed to float, then a single float reciprocal.
    const float square = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(square)));
    Vector direction(0, 0, 1);
    if (length != 0.0f)
    {
        const float inverse = 1.0f / length;
        direction = Vector(delta.x * inverse, delta.y * inverse, delta.z * inverse);
    }
    monster->pev->velocity = Vector(direction.x * 1200.0f, direction.y * 1200.0f, 120.0f);
    state->position.updateInterval = 0.5f;
    state->position.updateAfter = gpGlobals->time + 0.5f;
    state->updateStamp = gpGlobals->time;
    state->secondaryUpdateInterval = 0.5f;
    state->secondaryUpdateAfter = gpGlobals->time + 0.5f;
    state->secondaryUpdateStamp = gpGlobals->time;
    // Slot+10 is attackDamage; speed is +1C. Touch consumes this separately
    // from both motion clocks. Do not clear activeSkill or skillActive here.
    state->skillCollisionDamage = state->spawn->attributes.attackDamage * 3.0f;
    return Current(call, state) != nullptr;
}

CCSOLScenarioMonster* CCSOLOrdinaryMonsterBehavior::Current(
    CSOLMonsterSpawnCall& call, const StatePtr& state) const
{
    auto* monster = call.Current();
    if (!monster || !state || state != m_state || !state->spawn || state->spawn->lease != state->lease)
        return nullptr;
    return monster && monster->Behavior() == this && monster->Pool()->Borrowed(*monster) &&
        monster->Pool()->Lease(*monster) == state->lease ? monster : nullptr;
}

bool CCSOLOrdinaryMonsterBehavior::Step(CSOLMonsterSpawnCall& call,
    const StatePtr& state, bool ok, const char* failure)
{
    if (!Current(call, state)) return false;
    if (!ok && state->error.message.empty()) state->error.message = failure;
    return ok && state->error.message.empty();
}

bool CCSOLOrdinaryMonsterBehavior::MoveTo(CSOLMonsterSpawnCall& call,
    State& state, const Vector& goal)
{
    auto* monster = call.Current();
    if (!monster || !gpGlobals || !Finite(goal)) return false;
    auto& position = state.position;
    // A sequence hook cannot cancel or mutate the valid Jump transaction.
    if (position.startingJump) return false;
    const Vector change = goal - position.destination;
    if (change.z < 36.0f && Square3(change) < 90000.0f &&
        Square3(monster->pev->origin - position.destination) > 202500.0f)
        return true;
    position.destination = goal;
    position.follower.Reset();
    ++position.updateSerial;
    if (!state.knownGoodValid)
    {
        // The original sets valid BEFORE SetKnownGoodPosition, so the first
        // call may move the constructor's zero vector into history.
        state.knownGoodValid = true;
        if (!(gpGlobals->time <= position.jumpUntil) && !position.motion.crouching)
        {
            if (gpGlobals->time > state.knownGoodClock.deadline)
            {
                StartClock(state.knownGoodClock, 1.0f);
                state.previousKnownGood = state.knownGood;
                state.previousKnownGoodValid = true;
            }
            state.knownGood = Feet(*monster);
        }
    }
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::Stop(CSOLMonsterSpawnCall& call, State& state)
{
    auto* monster = call.Current();
    if (!monster || !MoveTo(call, state, Feet(*monster))) return false;
    monster->pev->velocity = g_vecZero;
    state.position.motion.moveType = std::min(0, state.position.motion.maxMoveType);
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::ResetMotion(CSOLMonsterSpawnCall& call, State& state)
{
    if (!Stop(call, state)) return false;
    state.position.destination.x = -99999.0f;
    state.position.motion.velocity = Vector2D(0, 0);
    state.deltaX = state.deltaY = state.motionRemainder = 0;
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::MoveTo(CSOLMonsterSpawnCall& call, const Vector& goal)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto state = m_state;
    return Current(call, state) && MoveTo(call, *state, goal);
}

bool CCSOLOrdinaryMonsterBehavior::Stop(CSOLMonsterSpawnCall& call)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto state = m_state;
    return Current(call, state) && Stop(call, *state);
}

bool CCSOLOrdinaryMonsterBehavior::Jump(CSOLMonsterSpawnCall& call)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto state = m_state;
    auto* monster = Current(call, state);
    if (!monster) return false;
    CSOLMonsterSpawnCall bound(services->Map(), services->Pool(), services->Deaths(), *monster, this);
    return CSOL_PrepareMonsterJumpDirection(bound, state->position) &&
        CSOL_StartMonsterJump(bound, state->position, state->spawn->sequences) && Current(call, state);
}

bool CCSOLOrdinaryMonsterBehavior::Flinch(CSOLMonsterSpawnCall& call)
{
    auto services = m_services.lock();
    if (!services) return false;
    const auto state = m_state;
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals || !std::isfinite(gpGlobals->time)) return false;
    const auto damageable = [](const CCSOLScenarioMonster* entity)
    {
        return entity && entity->pev->deadflag == DEAD_NO && entity->pev->takedamage != DAMAGE_NO &&
            !(entity->pev->effects & EF_NODRAW) && !(entity->pev->flags & FL_KILLME);
    };
    if (!damageable(monster)) return false;
    // MP 113EE380 sets the locomotion lock even if the model has no flinch
    // sequence. It does not choose by hitgroup or extend an existing lock.
    if (!monster->Animation().LockUntil(gpGlobals->time + 0.1f)) return false;
    using Sequence = csol::scenario::MonsterSequence;
    const int sequence = state->spawn->sequences[std::rand() % 2 ? Sequence::GutFlinch : Sequence::HeadFlinch];
    if (sequence < 0) return true;
    struct Reset
    {
        CCSOLOrdinaryMonsterBehavior* owner;
        CSOLMonsterSpawnCall* call;
        StatePtr state;
        bool (*damageable)(const CCSOLScenarioMonster*);
        static bool Valid(const void* context)
        {
            const auto& self = *static_cast<const Reset*>(context);
            return self.damageable(self.owner->Current(*self.call, self.state));
        }
        static bool Run(void* context)
        {
            auto& self = *static_cast<Reset*>(context);
            return CSOL_ResetMonsterSequenceInfo(*self.call, Valid, context);
        }
    } reset{this, &call, state, damageable};
    const bool changed = monster->Animation().SetSequenceGuarded(*monster, sequence, true, Reset::Run, &reset);
    if (!damageable(Current(call, state)) || !changed) return false;
    // This is the behavior's XY accumulator, not the engine velocity. The
    // normal motion pass later consumes it with friction and effect services.
    state->position.motion.velocity.x *= 0.5f;
    state->position.motion.velocity.y *= 0.5f;
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::UpdateStatusTick(CSOLMonsterSpawnCall& call, bool& elapsed)
{
    elapsed = false;
    auto services = m_services.lock();
    if (!services) return false;
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

bool CCSOLOrdinaryMonsterBehavior::UpdateAmbientAndEffects(CSOLMonsterSpawnCall& call,
    const StatePtr& state, CCSOLOrdinaryMonsterServices& services)
{
    #pragma clang fp contract(off)
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals) return false;
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!Step(call, state, appearance != nullptr, "ordinary ambient appearance is unavailable")) return false;
    if (!appearance->ambientSound.empty() && gpGlobals->time > state->spawn->ambientAt)
    {
        if (!Step(call, state, g_engfuncs.pfnEmitSound != nullptr, "ordinary ambient sound service is unavailable"))
            return false;
        state->spawn->ambientAt = (gpGlobals->time + 7.0f) + static_cast<float>(std::rand() % 15);
        EMIT_SOUND_DYN(monster->edict(), CHAN_BODY, appearance->ambientSound.data(),
            1.0f, ATTN_NORM, 0, PITCH_NORM);
        if (!Current(call, state)) return false;
    }
    bool elapsed{};
    if (!Step(call, state, UpdateStatusTick(call, elapsed), "ordinary status update failed")) return false;
    struct Capture { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall& call; StatePtr state; };
    const Capture capture{this, call, state};
    const CSOLMonsterEffectGuard guard{[](const void* value)
    {
        const auto& scope = *static_cast<const Capture*>(value);
        return scope.owner->Current(scope.call, scope.state);
    }, &capture};
    monster = Current(call, state);
    if (!monster) return false;
    auto effects = monster->DamageEffects();
    if (!Step(call, state, effects != nullptr, "ordinary damage effects are unavailable")) return false;
    if (elapsed && !Step(call, state, effects->Tick(call, guard, state->error), "ordinary DOT update failed"))
        return false;
    return Step(call, state, CSOL_UpdateMonsterGait(call, state->error, [](const void* value)
    {
        const auto& scope = *static_cast<const Capture*>(value);
        return scope.owner->Current(scope.call, scope.state) != nullptr;
    }, &capture), "ordinary gait update failed");
}

bool CCSOLOrdinaryMonsterBehavior::ResetForSpawn(CSOLMonsterSpawnCall& call,
    CSOLMonsterSpawnState& spawn, csol::scenario::ConfigError& error)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto* monster = call.Current();
    if (!monster || !gpGlobals || monster->Behavior() != this ||
        monster->Type()->skill >= 1000 || monster->Type()->propensity != 0 ||
        !std::isfinite(gpGlobals->time))
    { error.message = "ordinary behavior requires a valid propensity0 spawn"; return false; }
    if (m_state && m_state->position.startingJump)
    { error.message = "ordinary reset reentered a Jump animation transaction"; return false; }

    // Keep old state alive on its caller's stack. Reuse preserves original
    // history; a nested reset cancels old navigation before cloning it.
    if (m_state && m_state->position.updating)
    {
        ++m_state->position.updateSerial;
        m_state->position.follower.Reset();
    }
    auto state = m_state ? std::make_shared<State>(*m_state) : std::make_shared<State>();
    state->thinking = false;
    state->position.updating = state->position.startingJump = false;
    state->spawn = spawn.weak_from_this().lock();
    if (!state->spawn || state->spawn.get() != &spawn)
    { error.message = "ordinary behavior needs factory-owned spawn state"; return false; }
    state->lease = spawn.lease;
    state->error = {};
    // Paths contain the previous borrow's checked SpawnCall. Keep behavior
    // history, but never let that route become the new lease's live route.
    state->position.follower.Reset();
    const auto previous = m_state;
    auto navigation = services->PinNavigation();
    // PinNavigation may enter map code. Do not replace a newer reset.
    if (m_state != previous || call.Current() != monster) return false;
    if (!navigation)
    { error.message = "ordinary behavior needs an owned, initialized NAV world"; return false; }
    const auto& world = navigation->World();
    const auto generation = world.Generation();
    if (m_state != previous || call.Current() != monster) return false;
    if (!generation || !world.Ready())
    { error.message = "ordinary behavior needs an initialized NAV world"; return false; }
    if (state->navigation != navigation ||
        state->position.worldGeneration != generation)
    {
        state->position.follower.Reset();
        state->position.lastArea = nullptr;
        state->position.worldGeneration = generation;
    }
    state->navigation = std::move(navigation);
    m_state = state;
    state->target = nullptr;
    monster->HostState().enhancedUntil = -1.0f;
    state->distantTarget = nullptr;
    StartClock(state->skill, static_cast<float>(5 + std::rand() % 3));
    state->obstacleBlocked = false;
    if (!ResetMotion(call, *state)) return false;
    state->position.updateAfter = state->updateStamp = -1;
    state->secondaryUpdateAfter = state->secondaryUpdateStamp = -1;
    state->findPath.deadline = state->findPath.stamp = -1;
    state->findTarget.deadline = state->findTarget.stamp = -1;
    StartClock(state->secondarySkill, 99999.0f);
    StartClock(state->skillPhase, 99999.0f);
    StartClock(state->skillRecovery, 99999.0f);
    monster->Animation().LockUntil(0);
    state->lastActionAt = std::bit_cast<float>(std::uint32_t{0xc97423f0});
    state->solidAfter = 0;
    state->position.missingAreaUntil = -1;
    if (monster->Type()->appearance == "zombiedog")
    {
        if (!g_engfuncs.pfnEmitSound)
        { error.message = "ordinary spawn howl sound service is unavailable"; return false; }
        EMIT_SOUND_DYN(monster->edict(), CHAN_BODY, "zombi/zombiedog_howls.wav",
            1.0f, ATTN_NORM, 0, PITCH_NORM);
    }
    if (!Current(call, state)) return false;
    state->actionState = 0;
    state->canSkill = state->canAttack = false;
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::ValidateTarget(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    auto* target = state->target.Get<CBaseEntity>();
    if (!target) return true;
    const bool player = target->IsPlayer() != FALSE;
    if (!Current(call, state)) return false;
    target = state->target.Get<CBaseEntity>();
    if (!target) return true;
    bool excluded{};
    if (player)
    {
        // Capture flags before the feature callback, matching the original.
        const bool dormant = static_cast<std::uint32_t>(target->pev->flags) == 0x80000000u;
        if (!Step(call, state, services.PlayerTargetExcluded(call, state->target,
            excluded, state->error), "ordinary player target feature query failed")) return false;
        excluded = excluded || dormant;
    }
    else excluded = (target->pev->effects & EF_NODRAW) != 0;
    if (excluded)
    {
        state->target = nullptr;
        state->distantTarget = nullptr; // ordinary v144
    }
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::FindPath(CSOLMonsterSpawnCall& call,
    const StatePtr& state, bool& acted)
{
    acted = false;
    if (!(gpGlobals->time > state->findPath.deadline)) return true;
    StartClock(state->findPath, 0.6f);
    acted = true;
    if (state->specialTarget) return MoveTo(call, *state, state->specialGoal);
    auto* target = state->target.Get<CBaseEntity>();
    if (!target) return true;
    const bool player = target->IsPlayer() != FALSE;
    auto* monster = Current(call, state);
    if (!monster) return false;
    target = state->target.Get<CBaseEntity>();
    if (!target) return true;
    Vector goal = target->pev->origin;
    if (player && monster->pev->maxspeed >= 300.0f && monster->HostState().activeSkill != 1)
    {
        const float scale = (monster->pev->maxspeed - 300.0f) *
            std::bit_cast<float>(std::uint32_t{0x3c360b61});
        const float lead = scale * 0.2f + 0.3f;
        goal += target->pev->velocity * lead;
    }
    if (!MoveTo(call, *state, goal)) return false;
    if (state->spawn->skill == 6 &&
        static_cast<double>(Square3(monster->pev->origin - target->pev->origin)) < 75949.84448242188)
    {
        acted = false;
        return ResetMotion(call, *state);
    }
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::SelectTarget(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32 ||
        !g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnRandomLong ||
        !g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnTraceLine)
        return Step(call, state, false, "ordinary target engine services are unavailable");
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
            "ordinary candidate feature query failed")) return false;
        if (excluded) continue;
        auto* candidate = handle.Get<CBaseEntity>();
        monster = Current(call, state);
        if (!monster) return false;
        if (!candidate) continue;
        const float squared = Square3(monster->pev->origin - candidate->pev->origin);
        if (!std::isfinite(squared))
            return Step(call, state, false, "ordinary target position is not finite");
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
                if (trace.flFraction == 1.0f && handle.Get<CBaseEntity>())
                { state->target = handle; return true; }
            }
        }
    }
    if (count && static_cast<double>(closestDistance) > 619998.73046875)
    {
        // EHANDLE replaces the original raw cache; a removed cached edict
        // cannot become a dangling pointer or refer to a new occupant.
        if (!state->distantTarget.Get<CBaseEntity>())
            state->distantTarget = candidates[static_cast<std::size_t>(std::rand()) % count];
        state->target = state->distantTarget;
        return true;
    }
    state->distantTarget = nullptr;
    if (closestDistance > 6199.9873046875f && closeCount >= 3)
    {
        state->target = nearby[static_cast<std::size_t>(std::rand()) % closeCount];
        return true;
    }
    auto* monster = Current(call, state);
    if (!monster) return false;
    const auto policy = monster->HostState().targetPolicy;
    const auto type = monster->TypeId();
    auto* previous = state->target.Get<CBaseEntity>();
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
                    if (!previous) { state->target = nullptr; return true; }
                    if (stillPlayer && previous->pev->rendermode == kRenderTransAlpha && previous->pev->renderamt < 1.0f)
                        state->target = nullptr;
                }
                return true;
            }
        }
    }
    state->target = closest;
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::RunTree(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    const bool stopped = services.LogicStopped();
    if (!Current(call, state)) return false;
    if (stopped) return true;
    state->jitter = static_cast<float>(std::rand() % 30) * FrameInterval;
    const bool& suppressed = services.DamageSuppressed();
    if (!Current(call, state)) return false;
    state->canSkill = state->spawn->skill != 0 && !suppressed &&
        (gpGlobals->time > state->skill.deadline || gpGlobals->time > state->secondarySkill.deadline);
    state->canAttack = gpGlobals->time > state->melee.attack.deadline;
    if (state->canSkill)
    {
        const bool used = gpGlobals->time > state->skill.deadline;
        if (used)
        {
            Current(call, state)->HostState().activeSkill = state->spawn->skill;
            if (!Step(call, state, UseSkill(call, state, services),
                "ordinary skill execution failed")) return false;
        }
        if (!Step(call, state, UpdateSkill(call, state),
            "ordinary skill update failed")) return false;
        if (used) return true;
    }
    if (state->canAttack)
    {
        struct MeleeContext { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
        MeleeContext context{this, &call, state};
        const auto result = CSOL_TryOrdinaryMonsterMelee(call, *state->spawn, state->target,
            Current(call, state)->HostState().enhancedUntil, suppressed,
            state->deltaX, state->deltaY, state->melee, [](const void* pointer)
            {
                const auto& context = *static_cast<const MeleeContext*>(pointer);
                return context.owner->Current(*context.call, context.state) != nullptr;
            }, &context);
        if (result == CSOLMonsterMeleeResult::Cancelled) return false;
        if (!Step(call, state, result != CSOLMonsterMeleeResult::Failed, "ordinary melee failed")) return false;
        if (result == CSOLMonsterMeleeResult::Hit) return true;
    }
    if (!state->specialTarget && gpGlobals->time > state->findTarget.deadline)
    {
        StartClock(state->findTarget, 3.0f + state->jitter);
        if (!SelectTarget(call, state, services)) return false;
        if (state->spawn->skill == 7)
        {
            const auto* target = state->target.Get<CBaseEntity>();
            const auto* monster = Current(call, state);
            if (!monster) return false;
            if (target && Square3(target->pev->origin - monster->pev->origin) > 24799.94921875f)
            {
                // Original target-update uses TrackPath's saved goal, not
                // the newly selected enemy or the MoveTo destination.
                if (!CSOL_PrepareMonsterJumpDirection(call, state->position)) return false;
                CSOL_StartMonsterJump(call, state->position, state->spawn->sequences);
                if (!Current(call, state)) return false;
                // A still-active Jump cooldown is an ordinary ignored return.
            }
        }
    }
    if (state->specialTarget || state->target.Get<CBaseEntity>())
    {
        bool acted{};
        if (!FindPath(call, state, acted)) return false;
        if (acted) return true;
    }
    if (!CheckObstacles(call, state)) return false;
    auto* monster = Current(call, state);
    if (!monster) return false;
    if (Square3(state->position.destination - Feet(*monster)) < 400.0f)
        return Stop(call, *state);
    state->position.motion.moveType = state->position.motion.maxMoveType;
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::UpdateBehavior(CSOLMonsterSpawnCall& call, const StatePtr& state,
    CCSOLOrdinaryMonsterServices& services)
{
    auto* monster = Current(call, state);
    if (!monster || monster->pev->deadflag == DEAD_DEAD) return true;
    // The original cvar suppresses D0 only; D4 has already run this tick.
    if (cv_hostage_stop.value > 0) return true;
    state->deltaX = monster->pev->origin.x - state->previousOrigin.x;
    state->deltaY = monster->pev->origin.y - state->previousOrigin.y;
    state->previousOrigin = monster->pev->origin;
    if (!RunTree(call, state, services)) return false;
    monster = Current(call, state);
    if (!monster) return false;
    struct ResetContext { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
    ResetContext context{this, &call, state};
    if (!Step(call, state, monster->Animation().UpdateLocomotionGuarded(*monster,
        state->spawn->sequences, state->spawn->skill, state->deltaX, state->deltaY,
        gpGlobals->time, state->position.motion.climbSequence, [](void* pointer)
        {
            auto& context = *static_cast<ResetContext*>(pointer);
            return CSOL_ResetMonsterSequenceInfo(*context.call, [](const void* pointer)
            {
                const auto& context = *static_cast<const ResetContext*>(pointer);
                return context.owner->Current(*context.call, context.state) != nullptr;
            }, &context);
        }, &context), "ordinary locomotion animation failed")) return false;
    monster = Current(call, state);
    if (gpGlobals->time > monster->HostState().enhancedUntil)
    {
        monster->pev->maxspeed = state->spawn->attributes.speed;
        const auto& type = *monster->Type();
        monster->pev->rendercolor = Vector(type.color.data());
        monster->pev->rendermode = static_cast<int>(type.column21[0]);
        monster->pev->renderfx = static_cast<int>(type.column21[1]);
        monster->pev->renderamt = type.column21[2];
    }
    const bool alive = monster->IsAlive() != FALSE;
    monster = Current(call, state);
    if (!monster) return false;
    if (alive && gpGlobals->time > state->solidAfter) monster->pev->solid = SOLID_SLIDEBOX;
    return true;
}

void CCSOLOrdinaryMonsterBehavior::Think(CCSOLScenarioMonster& entity)
{
    auto services = m_services.lock();
    auto state = m_state;
    if (!services || !state || state->thinking || !state->error.message.empty() || !gpGlobals) return;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), entity, this);
    auto* monster = Current(call, state);
    if (!monster) return;
    struct Scope { bool& busy; Scope(bool& value) : busy(value) { busy = true; } ~Scope() { busy = false; } } scope(state->thinking);
    try
    {
        if (monster->HostState().navigationExpired && monster->Type()->skill < 1000 &&
            monster->Type()->eventClass <= 149)
        {
            const auto protectedSlots = services->ProtectedSlots();
            if (!Current(call, state)) return;
            if (std::find(protectedSlots.begin(), protectedSlots.end(), monster->Slot()) == protectedSlots.end())
            {
                CSOLMonsterDeathArguments arguments;
                arguments.suppressSound = true;
                services->Deaths().Request(*monster, arguments);
                return; // Original death precedes the nextthink write.
            }
        }
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
                struct ResetContext { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
                ResetContext context{this, &call, state};
                const bool reset = CSOL_ResetMonsterSequenceInfo(call, [](const void* pointer)
                {
                    const auto& context = *static_cast<const ResetContext*>(pointer);
                    return context.owner->Current(*context.call, context.state) != nullptr;
                }, &context);
                if (!Step(call, state, reset, "ordinary thaw sequence reset failed")) return;
            }
        }
        if (gpGlobals->time > state->spawn->animationAt)
        {
            monster = Current(call, state);
            if (monster->HostState().frozenRemaining > 0) monster->pev->framerate = 0;
            else monster->StudioFrameAdvance(0);
            // CSO and the stock SDK both use a fixed .1 event window, even
            // while frozen. CMonster's default event handler is empty.
            struct EventContext { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
            EventContext context{this, &call, state};
            const bool dispatched = CSOL_DispatchMonsterAnimationEvents(call, [](const void* pointer)
            {
                const auto& context = *static_cast<const EventContext*>(pointer);
                return context.owner->Current(*context.call, context.state) != nullptr;
            }, &context);
            if (!Step(call, state, dispatched,
                "ordinary animation events failed")) return;
            state->spawn->animationAt = gpGlobals->time + FrameInterval;
        }
        monster = Current(call, state);
        if (!monster) return;
        if (monster->HostState().frozenRemaining <= 0)
        {
            const auto behavior = services->Deaths().BeginBehavior(*monster);
            if (!Step(call, state, behavior.has_value(), "ordinary behavior death scope failed")) return;
            bool moved = true;
            bool cancelled = false;
            try
            {
                if (monster->pev->deadflag != DEAD_DEAD)
                {
                    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
                    if (!appearance) moved = false;
                    else
                    {
                        auto navigation = state->navigation;
                        const auto& world = navigation->World();
                        auto& occupancy = services->Occupancy();
                        const auto ladders = navigation->ClimberLadders();
                        monster = Current(call, state);
                        if (!monster || world.Generation() != state->position.worldGeneration)
                            cancelled = true;
                        else
                        {
                            const auto result = CSOL_UpdateOrdinaryMonsterPosition(call, world,
                                occupancy, *appearance, state->position, FrameInterval,
                                monster->HostState().moveAfter, state->melee.avoidance.deadline, ladders);
                            if (Current(call, state) && result == CSOLMonsterPositionResult::NavigationExpired)
                                Current(call, state)->HostState().navigationExpired = true;
                            moved = result != CSOLMonsterPositionResult::Failed;
                            cancelled = result == CSOLMonsterPositionResult::Cancelled;
                        }
                    }
                }
            }
            catch (...)
            {
                if (Current(call, state)) state->error.message = "ordinary position callback threw";
                moved = false;
            }
            const bool ended = services->Deaths().EndBehavior(*behavior);
            if (cancelled) return;
            if (!Step(call, state, ended && moved, "ordinary position update failed")) return;
        }
        if (state->spawn->updateAt > gpGlobals->time) return;
        state->spawn->updateAt = gpGlobals->time + 0.1f;
        monster = Current(call, state);
        if (!monster) return;
        if (monster->HostState().frozenRemaining <= 0 && !UpdateBehavior(call, state, *services)) return;
        UpdateAmbientAndEffects(call, state, *services);
    }
    catch (...)
    {
        if (Current(call, state)) state->error.message = "ordinary Think callback threw";
    }
}

void CCSOLOrdinaryMonsterBehavior::Touch(CCSOLScenarioMonster& entity, CBaseEntity* other)
{
    #pragma clang fp contract(off)
    auto services = m_services.lock();
    auto state = m_state;
    if (!other || !services || !state || !gpGlobals || !state->error.message.empty()) return;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), entity, this);
    const EHANDLE target(other);
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        auto* monster = Current(call, state);
        return monster && target.Get<CBaseEntity>() ? monster : nullptr;
    };
    auto* monster = current();
    if (!monster) return;
    const auto skill = monster->HostState().activeSkill;
    if (skill != 9 && skill != 12) return; // Original mode15 ordinary early return.
    const auto enemyPlayer = [&](CBaseEntity& receiver)
    {
        // Native CS keeps player team in m_iTeam, not entvars.team.
        const auto* player = dynamic_cast<CBasePlayer*>(&receiver);
        return (player ? static_cast<int>(player->m_iTeam) : receiver.pev->team) != monster->pev->team;
    };
    try
    {
        if (skill == 9)
        {
            const bool player = other->IsPlayer() != FALSE;
            if (!current()) return;
            if (!player)
            {
                const int classification = target.Get<CBaseEntity>()->Classify();
                if (!current() || classification != 17) return;
            }
            // Original COMISS/JA skips only ordered greater, including the
            // deadline itself and unordered input in the active branch.
            if (gpGlobals->time > state->secondaryUpdateAfter) return;
            other = target.Get<CBaseEntity>();
            const bool nowPlayer = other->IsPlayer() != FALSE;
            monster = current();
            if (!monster) return;
            other = target.Get<CBaseEntity>();
            if (nowPlayer && enemyPlayer(*other))
            {
                other->TakeDamage(monster->pev, monster->pev, state->skillCollisionDamage, DMG_BULLET);
                if (!current()) return;
            }
            const auto& facing = state->position.motion.facingDirection;
            const Vector requested(500.0f * facing.x, 500.0f * facing.y, 300.0f);
            if (!Step(call, state, services->ApplyContactImpulse(call, target, requested, state->error),
                    "ordinary contact impulse service failed")) return;
            monster = current();
            if (!monster) return;
            // Re-read the saved direction after the helper's rule callbacks.
            // A rejected target impulse still reaches this host-velocity tail.
            monster->pev->velocity.x = state->position.motion.facingDirection.x * 500.0f;
            monster->pev->velocity.y = state->position.motion.facingDirection.y * 500.0f;
            return; // Skill9 preserves both contact clocks and vertical speed.
        }
        if (gpGlobals->time > state->secondaryUpdateAfter) return;
        const bool player = other->IsPlayer() != FALSE;
        monster = current();
        if (!monster || !player) return;
        other = target.Get<CBaseEntity>();
        if (!enemyPlayer(*other)) return;
        other->TakeDamage(monster->pev, monster->pev, state->skillCollisionDamage, DMG_BULLET);
        if (!current()) return;
        const auto& messages = services->Messages();
        if (!current()) return;
        const bool sent = messages.SendMonsterContactEffect(target.Get<CBaseEntity>()->edict());
        if (!current() || !Step(call, state, sent, "ordinary contact effect packet failed")) return;
        // Only skill12 performs this reset, after the complete packet.
        state->secondaryUpdateAfter = state->secondaryUpdateStamp = -1.0f;
    }
    catch (...)
    {
        if (Current(call, state)) state->error.message = "ordinary Touch callback threw";
    }
}

BOOL CCSOLOrdinaryMonsterBehavior::TakeDamage(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, float damage, int damageType)
{
    return TakeDamageWithContext(monster, inflictor, attacker, damage, damageType, {});
}

BOOL CCSOLOrdinaryMonsterBehavior::TakeDamageWithContext(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, float damage, int damageType,
    const CSOLMonsterDeathContext& damageContext)
{
    auto services = m_services.lock();
    auto state = m_state;
    if (!services || !state) return FALSE;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), monster, this);
    if (!Current(call, state)) return FALSE;
    auto owner = m_damage;
    if (!owner)
    {
        if (state->error.message.empty()) state->error.message = "ordinary damage owner is not installed";
        return FALSE;
    }
    struct DamageContext { CCSOLOrdinaryMonsterBehavior* owner; CSOLMonsterSpawnCall* call; StatePtr state; };
    const DamageContext context{this, &call, state};
    return owner->Apply(call, inflictor, attacker, damage, damageType, state->error,
        [](const void* pointer)
        {
            const auto& context = *static_cast<const DamageContext*>(pointer);
            return context.owner->Current(*context.call, context.state) != nullptr;
        }, &context, damageContext);
}

void CCSOLOrdinaryMonsterBehavior::Killed(CCSOLScenarioMonster& monster, entvars_t* attacker, int)
{
    auto services = m_services.lock();
    auto state = m_state;
    if (!services || !state) return;
    CSOLMonsterSpawnCall call(services->Map(), services->Pool(), services->Deaths(), monster, this);
    if (!Current(call, state)) return;
    CSOLMonsterDeathArguments arguments;
    if (attacker) arguments.attacker = CBaseEntity::Instance(attacker);
    services->Deaths().Request(monster, arguments);
}

CSOLMonsterDeathParameters CCSOLOrdinaryMonsterBehavior::DeathParameters() const
{
    if (!m_state || !m_state->spawn) return {};
    return {m_state->spawn->attributes.bonusMoney, m_state->spawn->skill, m_state->spawn->shortDeath};
}

bool CCSOLOrdinaryMonsterBehavior::BeforeDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto* monster = call.Current();
    if (!monster) return false;
    return !(monster->HostState().frozenRemaining > 0) || services->FrozenDeath(call, error);
}

bool CCSOLOrdinaryMonsterBehavior::SelectDeathSequence(CSOLMonsterDeathCall& call,
    int& sequence, csol::scenario::ConfigError&)
{
    auto services = m_services.lock();
    if (!services) return false;
    auto* monster = call.Current();
    if (!monster || !m_state || !m_state->spawn) return false;
    sequence = csol::scenario::SelectOrdinaryMonsterDeathSequence(
        &m_state->spawn->sequences, monster->HostState().lastHitgroup);
    return true;
}

bool CCSOLOrdinaryMonsterBehavior::AdjustDeathDuration(CSOLMonsterDeathCall& call,
    float&, csol::scenario::ConfigError&)
{
    auto services = m_services.lock();
    if (!services) return false;
    return call.Current() != nullptr; // Ordinary override returns the input unchanged.
}

bool CCSOLOrdinaryMonsterBehavior::PostDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError&)
{
    auto services = m_services.lock();
    if (!services) return false;
    return call.Current() != nullptr; // Ordinary v128=10611B90 is ret8.
}
