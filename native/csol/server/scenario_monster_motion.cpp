// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_motion.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace
{
CCSOLScenarioMonster* Active(CSOLMonsterSpawnCall& call)
{
    auto* monster = call.Current();
    return monster && monster->pev->deadflag != DEAD_DEAD &&
        monster->Pool()->Borrowed(*monster) ? monster : nullptr;
}
bool Finite(const Vector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float Length2(const Vector2D& v)
{
    const float squared = v.y * v.y + v.x * v.x;
    return static_cast<float>(std::sqrt(static_cast<double>(squared)));
}
float InverseLength(float squared)
{
    const float estimate = std::bit_cast<float>(std::uint32_t{0x5f3759d5} -
        (std::bit_cast<std::uint32_t>(squared) >> 1));
    const float half = squared * 0.5f;
    const float first = half * estimate;
    const float second = first * estimate;
    return (1.5f - second) * estimate;
}
}

CSOLMonsterMotionResult CSOL_CommitMonsterMotion(CSOLMonsterSpawnCall& call,
    CCSOLScenarioOccupancy& occupancy, const csol::scenario::MonsterAppearance& appearance,
    CSOLMonsterRouteMotion& motion, float deltaTime, float avoidanceUntil, int& moveFlags)
{
    using Result = CSOLMonsterMotionResult;
    auto* monster = Active(call);
    if (!monster) return Result::Cancelled;
    if (!gpGlobals || !monster->Type() || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(deltaTime) || deltaTime < 0 || !std::isfinite(avoidanceUntil) ||
        !std::isfinite(motion.velocity.x) || !std::isfinite(motion.velocity.y) ||
        !std::isfinite(appearance.centroidZ)) return Result::Failed;
    auto& effects = monster->SpeedState();
    const float now = gpGlobals->time;
    if (!std::isfinite(effects.impulseProtectionUntil) || !std::isfinite(effects.impulseUntil) ||
        !std::isfinite(effects.recovery) || !Finite(monster->pev->velocity)) return Result::Failed;
    if (effects.impulseProtectionUntil > 0 && static_cast<double>(now) >= effects.impulseProtectionUntil)
        effects.impulseProtectionUntil = 0;

    Vector2D planar = motion.velocity;
    const auto& type = *monster->Type();
    // Original category getters: bosses and special event entities cannot
    // take this ordinary forced-XY shortcut. Equality ends the impulse.
    if (type.skill < 1000 && type.eventClass <= 149 && effects.impulseUntil > now)
        planar = effects.impulse;
    else
    {
        planar.x -= (planar.x * 3.0f) * deltaTime;
        planar.y -= (planar.y * 3.0f) * deltaTime;
        const float speed = Length2(planar);
        if (!std::isfinite(speed)) return Result::Failed;
        if (speed > 0) { planar.x /= speed; planar.y /= speed; }
        else planar = Vector2D(1, 0);
        if (!effects.UpdateMaxSpeed(*monster->pev, now) || !std::isfinite(monster->pev->maxspeed))
            return Result::Failed;
        const float limited = std::min(monster->pev->maxspeed, speed);
        planar.x *= limited;
        planar.y *= limited;
    }
    if (!std::isfinite(planar.x) || !std::isfinite(planar.y)) return Result::Failed;

    if (now > avoidanceUntil)
    {
        std::array<CCSOLScenarioOccupancy::Neighbor, CCSOLMonsterPool::Capacity> neighbors{};
        std::size_t count{};
        if (!occupancy.MovementNeighbors(monster->pev->origin, neighbors, count)) return Result::Failed;
        // The spatial query performs no engine callbacks. Preserve its chain
        // order; self/dead filtering belongs here, not to the occupancy layer.
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!call.SameSession() || Active(call) != monster) return Result::Cancelled;
            auto* neighbor = occupancy.ResolveNeighbor(neighbors[i]);
            if (!neighbor || neighbor == monster || neighbor->pev->deadflag == DEAD_DEAD) continue;
            const Vector centroid = monster->pev->origin + Vector(0, 0, appearance.centroidZ);
            const Vector delta = neighbor->pev->origin - centroid;
            if (!Finite(delta)) return Result::Failed;
            const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
            if (!(squared < 6199.9873046875f)) continue;
            const float inverse = InverseLength(squared);
            if (inverse == 0) continue;
            const float amount = (1.0f / inverse) - 78.73999786376953f;
            planar.x += amount * (delta.x * inverse);
            planar.y += amount * (delta.y * inverse);
        }
    }
    // Avoidance deliberately follows the speed clamp. Re-clamping here would
    // remove the original separation force in crowds.
    if (!std::isfinite(planar.x) || !std::isfinite(planar.y)) return Result::Failed;
    Vector finalVelocity(planar.x, planar.y, monster->pev->velocity.z);
    float recovery = effects.recovery;
    if (recovery < 1.0f)
    {
        // MP double at 117BBDE0 is 0.01. It is per update, not per second,
        // and is allowed to cross 1.0 on the final recovery update.
        recovery = static_cast<float>(static_cast<double>(recovery) + 0.01);
        finalVelocity *= recovery;
    }
    Vector adjusted;
    if (!effects.ApplyVelocityEffects(finalVelocity, monster->pev->maxspeed, now, adjusted))
        return Result::Failed;
    if (Active(call) != monster) return Result::Cancelled;
    motion.velocity = planar;
    moveFlags = 0;
    effects.recovery = recovery;
    monster->pev->velocity = adjusted;
    return Result::Applied;
}

CSOLMonsterMotionStep CSOL_AdvanceMonsterMotion(CSOLMonsterSpawnCall& call,
    CCSOLMonsterPathFollower& follower, const CCSOLScenarioNavWorld& world,
    CCSOLScenarioOccupancy& occupancy, const csol::scenario::MonsterAppearance& appearance,
    CNavArea* lastArea, CSOLMonsterRouteMotion& motion, float deltaTime,
    std::int16_t skill, bool avoidObstacles, float avoidanceUntil, int& moveFlags)
{
    CSOLMonsterMotionStep result;
    const auto generation = world.Generation();
    auto* owner = Active(call);
    if (!owner) return result;
    result.follow = follower.Advance(call, world, appearance, lastArea, motion,
        deltaTime, skill, avoidObstacles);
    if (generation != world.Generation() || Active(call) != owner ||
        result.follow == CSOLMonsterFollowResult::Cancelled) return result;
    if (result.follow == CSOLMonsterFollowResult::Failed)
    { result.motion = CSOLMonsterMotionResult::Failed; return result; }
    result.motion = CSOL_CommitMonsterMotion(call, occupancy, appearance, motion,
        deltaTime, avoidanceUntil, moveFlags);
    return result;
}
