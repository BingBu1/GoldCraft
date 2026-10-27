// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_position.h"
#include "scenario_monster_heading.h"

#include <bit>
#include <cmath>

namespace
{
bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float Square3(const Vector& value)
{
    return (value.y * value.y + value.x * value.x) + value.z * value.z;
}
CCSOLScenarioMonster* Active(CSOLMonsterSpawnCall& call)
{
    auto* entity = call.Current();
    return entity && entity->pev->deadflag != DEAD_DEAD &&
        entity->Pool()->Borrowed(*entity) ? entity : nullptr;
}
bool FacePoint(CCSOLScenarioMonster& monster, const Vector& feet,
    CSOLMonsterPositionState& state, float deltaTime)
{
    Vector delta = state.faceGoal - feet;
    delta.z = 0;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(Square3(delta))));
    if (!std::isfinite(length)) return false;
    const Vector direction = length > 0 ? delta / length : Vector(1, 0, 0);
    float cosine{}, sine{};
    if (!CSOL_MonsterHeading(state.motion.moveYaw, cosine, sine)) return false;
    const float forward = direction.x * cosine + direction.y * sine;
    float side = direction.x * -sine + direction.y * cosine;
    if (forward < 0) side = side < 0 ? -1.0f : 1.0f;
    else if (std::abs(side) < 0.05f) { state.faceRequested = false; return true; }
    const float yaw = static_cast<float>(static_cast<double>(deltaTime * 300.0f * side) + state.motion.moveYaw);
    if (!std::isfinite(yaw)) return false;
    monster.pev->angles.y = state.motion.moveYaw = yaw;
    state.motion.turnSide = side;
    return true;
}
}

CSOLMonsterPositionResult CSOL_UpdateOrdinaryMonsterPosition(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world, CCSOLScenarioOccupancy& occupancy,
    const csol::scenario::MonsterAppearance& appearance, CSOLMonsterPositionState& state,
    float deltaTime, float hostMoveAfter, float avoidanceUntil,
    std::span<const CNavLadder* const> climberLadders)
{
    using Result = CSOLMonsterPositionResult;
    auto* owner = Active(call);
    if (!owner) return Result::Cancelled;
    // During the sequence-reset hook in Jump, reject a nested update without
    // cancelling the valid animation transaction already in progress.
    if (state.startingJump) return Result::Cancelled;
    const auto serial = ++state.updateSerial;
    if (state.updating)
    {
        // Trace callbacks can reenter F4. Cancel at the follower's own query
        // boundary as well, before it writes any more pose or velocity.
        state.follower.Reset();
        return Result::Cancelled;
    }
    struct UpdateScope
    {
        bool& flag;
        explicit UpdateScope(bool& value) : flag(value) { flag = true; }
        ~UpdateScope() { flag = false; }
    } updateScope(state.updating);
    const auto generation = world.Generation();
    if (!generation || !gpGlobals || !owner->Type() || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(deltaTime) || deltaTime < 0 || !std::isfinite(hostMoveAfter) ||
        !std::isfinite(state.updateAfter) || !std::isfinite(state.missingAreaUntil) ||
        !std::isfinite(state.jumpUntil) || !std::isfinite(state.obstacleAfter)) return Result::Failed;
    if (state.worldGeneration && state.worldGeneration != generation) return Result::Cancelled;
    state.worldGeneration = generation;
    const float now = gpGlobals->time;
    if (!(now > state.updateAfter) || owner->Type()->speed == 0 || hostMoveAfter > now)
        return Result::Skipped;
    const auto current = [&] { return state.updateSerial == serial &&
        world.Generation() == generation && Active(call) == owner; };
    if (!Finite(owner->pev->origin)) return Result::Failed;
    // Original 1134A290 is inclusive XY overlap, without the SDK Contains
    // height/layer test. An unsuccessful lookup preserves the previous area.
    bool hasArea = state.lastArea && state.lastArea->IsOverlapping(&owner->pev->origin);
    if (!hasArea)
        if (auto* area = world.AreaAt(owner->pev->origin, 120.0f))
        { state.lastArea = area; hasArea = true; }
    if (hasArea) state.missingAreaUntil = -1.0f;
    else if (state.missingAreaUntil == -1.0f) state.missingAreaUntil = now + 10.0f;
    if (state.missingAreaUntil > 0 && now > state.missingAreaUntil)
        return Result::NavigationExpired; // Owner sets original host12DC flag.

    state.motion.jumping = now <= state.jumpUntil;
    if (state.motion.jumping)
    {
        if (!std::isfinite(state.jumpDirection.x) || !std::isfinite(state.jumpDirection.y) ||
            !Finite(owner->pev->velocity)) return Result::Failed;
        const bool longJump = owner->Type()->skill == 7;
        if (!state.jumpStarted)
        {
            owner->pev->velocity.z += longJump ? 500.0f : 300.0f;
            state.jumpStarted = true;
            state.leftGround = false;
        }
        else if (state.leftGround)
        {
            if (owner->pev->flags & FL_ONGROUND)
            {
                state.jumpUntil = state.jumpStamp = -1.0f;
                state.motion.jumping = false;
            }
        }
        else if (!(owner->pev->flags & FL_ONGROUND)) state.leftGround = true;
        const float speed = longJump ? 300.0f : 150.0f;
        owner->pev->velocity.x = speed * state.jumpDirection.x;
        owner->pev->velocity.y = speed * state.jumpDirection.y;
        // Even the landing update returns here, without friction or effects.
        return Result::Jumping;
    }

    const Vector feet = owner->pev->origin + Vector(0, 0, appearance.feetZ);
    if (!Finite(feet) || !Finite(state.destination)) return Result::Failed;
    if (state.faceRequested && (!Finite(state.faceGoal) || !FacePoint(*owner, feet, state, deltaTime)))
        return Result::Failed;
    const bool climber = owner->Type()->appearance == "zombieclimber";
    if (!(Square3(feet - state.destination) < 400.0f) || state.follower.HasRoute())
    {
        const auto tick = state.routeTick++;
        const Vector base = owner->pev->basevelocity;
        if (!Finite(base)) return Result::Failed;
        if ((tick & 1) && !state.follower.HasRoute() && state.destination.x != -99999.0f &&
            base.y * base.y + base.x * base.x < 100.0f)
        {
            const auto expectedRevision = state.follower.Revision() + 1;
            state.follower.Build(call, world, feet, state.destination, climber, climberLadders);
            if (!current() || state.follower.Revision() != expectedRevision) return Result::Cancelled;
        }
        if (state.destination.x != -99999.0f)
        {
            const auto follow = state.follower.Advance(call, world, appearance, state.lastArea,
                state.motion, deltaTime, owner->Type()->skill, gpGlobals->time > state.obstacleAfter);
            if (!current() || follow == CSOLMonsterFollowResult::Cancelled) return Result::Cancelled;
            if (follow == CSOLMonsterFollowResult::Failed) return Result::Failed;
        }
        if (state.motion.moveType == 0) state.follower.ResetStuck();
    }
    if (state.motion.climbSequence && climber)
    {
        const auto direction = state.follower.LadderDirection(call, world);
        if (direction) owner->pev->angles.y = static_cast<float>((static_cast<int>(*direction) + 1) % 4) * 90.0f;
        owner->pev->velocity.z = 150.0f;
    }
    const auto result = CSOL_CommitMonsterMotion(call, occupancy, appearance, state.motion,
        deltaTime, avoidanceUntil, state.moveFlags);
    return result == CSOLMonsterMotionResult::Applied ? Result::Applied :
        result == CSOLMonsterMotionResult::Cancelled ? Result::Cancelled : Result::Failed;
}

bool CSOL_PrepareMonsterJumpDirection(CSOLMonsterSpawnCall& call, CSOLMonsterPositionState& state)
{
    auto* owner = Active(call);
    if (!owner || state.startingJump || !Finite(state.motion.jumpGoal) || !Finite(owner->pev->origin)) return false;
    const Vector delta = state.motion.jumpGoal - owner->pev->origin;
    const float squared = delta.x * delta.x + delta.y * delta.y;
    if (!std::isfinite(squared)) return false;
    const float estimate = std::bit_cast<float>(std::uint32_t{0x5f3759d5} -
        (std::bit_cast<std::uint32_t>(squared) >> 1));
    const float half = squared * 0.5f;
    const float first = half * estimate;
    const float second = first * estimate;
    const float inverse = (1.5f - second) * estimate;
    state.jumpDirection = Vector2D(delta.x * inverse, delta.y * inverse);
    return std::isfinite(state.jumpDirection.x) && std::isfinite(state.jumpDirection.y);
}

bool CSOL_StartMonsterJump(CSOLMonsterSpawnCall& call, CSOLMonsterPositionState& state,
    const csol::scenario::MonsterSequences& sequences)
{
    auto* owner = Active(call);
    if (!owner || state.startingJump || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(state.jumpUntil) || !(gpGlobals->time > state.jumpUntil) ||
        !Finite(owner->pev->origin) || !Finite(owner->pev->velocity)) return false;
    const auto serial = ++state.updateSerial;
    if (state.updating)
    {
        state.follower.Reset();
        return false;
    }
    struct JumpScope
    {
        bool& flag;
        explicit JumpScope(bool& value) : flag(value) { flag = true; }
        ~JumpScope() { flag = false; }
    } jumpScope(state.startingJump);
    state.moveFlags |= 2;
    state.jumpStarted = false;
    const Vector speed = owner->pev->velocity;
    if ((speed.x * speed.x + speed.y * speed.y) + speed.z * speed.z < 1.0f)
        owner->pev->origin.z += 2.0f;
    state.jumpInterval = 2.0f;
    state.jumpUntil = gpGlobals->time + 2.0f;
    state.jumpStamp = gpGlobals->time;
    state.motion.jumping = true;
    const auto reset = [](void* context)
    {
        auto& currentCall = *static_cast<CSOLMonsterSpawnCall*>(context);
        return CSOL_ResetMonsterSequenceInfo(currentCall) && Active(currentCall);
    };
    if (!owner->Animation().SetSequenceGuarded(*owner,
        sequences[csol::scenario::MonsterSequence::Jump], true, reset, &call) ||
        state.updateSerial != serial || Active(call) != owner || !gpGlobals)
        return false;
    return owner->Animation().LockUntil(gpGlobals->time + 0.75f);
}
