// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "ballistics.hpp"

#include <cmath>

namespace guest::cof
{
namespace
{
constexpr int PistolDamageBits = DMG_BULLET | DMG_NEVERGIB;
constexpr float PistolDistance = 8192.0f;

bool Finite(const Vector& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Host adaptation: nested engine/ReAPI calls must not destroy a caller's
// pending damage transaction. The original CoF register was process-global.
class DamageRegister
{
public:
    DamageRegister() noexcept : previous_(gMultiDamage) {}
    ~DamageRegister() { gMultiDamage = previous_; }
private:
    MULTIDAMAGE previous_;
};

struct Owner
{
    EntityHandle<CBaseEntity> handle;
    const CBaseEntity* expected;
};

bool Current(const BallisticsCall& call, const Owner& owner) noexcept
{
    return call.current(call.context) && owner.handle.Get<CBaseEntity>() == owner.expected;
}

// Original 1011AA30 uses a double midpoint then rounds once to float.
float WaterLevel(const Vector& position, float low, float high,
    const BallisticsCall& call, const Owner& owner, bool& valid) noexcept
{
    Vector point = position;
    point.z = low;
    const int lowContents = UTIL_PointContents(point);
    if (!(valid = Current(call, owner))) return low;
    if (lowContents != CONTENTS_WATER) return low;
    point.z = high;
    const int highContents = UTIL_PointContents(point);
    if (!(valid = Current(call, owner))) return low;
    if (highContents == CONTENTS_WATER) return high;
    float difference = high - low;
    while (difference > 1.0f)
    {
        point.z = static_cast<float>(static_cast<double>(low) + static_cast<double>(difference) * 0.5);
        const int contents = UTIL_PointContents(point);
        if (!(valid = Current(call, owner))) return low;
        if (contents == CONTENTS_WATER) low = point.z;
        else high = point.z;
        difference = high - low;
    }
    return point.z;
}

void WriteVector(const Vector& value) noexcept
{
    WRITE_COORD(value.x);
    WRITE_COORD(value.y);
    WRITE_COORD(value.z);
}

// Once a packet begins, always finish it. Hook cancellation is observed after
// MESSAGE_END, before any following effect/damage callback.
void WorldImpact(const TraceResult& trace, const BallisticsEffects& effects) noexcept
{
    MESSAGE_BEGIN(MSG_BROADCAST, effects.customDecalMessage);
    WriteVector(trace.vecEndPos);
    WriteVector(trace.vecPlaneNormal);
    WRITE_BYTE(0);
    WRITE_STRING("shot");
    MESSAGE_END();
}

bool BubbleTrail(const Vector& source, const TraceResult& trace,
    const BallisticsEffects& effects, const BallisticsCall& call,
    const Owner& owner) noexcept
{
    bool valid = true;
    float height = WaterLevel(source, source.z, source.z + 256.0f, call, owner, valid) - source.z;
    if (!valid) return false;
    if (height < 8.0f)
    {
        const float endHeight = WaterLevel(trace.vecEndPos, trace.vecEndPos.z,
            trace.vecEndPos.z + 256.0f, call, owner, valid) - trace.vecEndPos.z;
        if (!valid) return false;
        if (endHeight < 8.0f) return true;
        height = (trace.vecEndPos.z + endHeight) - source.z;
    }
    const float travelled = trace.flFraction * PistolDistance;
    const int count = static_cast<int>(static_cast<double>(travelled) * (1.0 / 64.0));
    MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
    WRITE_BYTE(TE_BUBBLETRAIL);
    WriteVector(source);
    WriteVector(trace.vecEndPos);
    WRITE_COORD(height);
    WRITE_SHORT(effects.bubbleModel);
    WRITE_BYTE(count > 255 ? 255 : count);
    WRITE_COORD(8.0f);
    MESSAGE_END();
    return Current(call, owner);
}
}

BallisticsStatus CheckPistolBallistics(const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call) noexcept
{
    if (shot.sourceMode != PistolSource::NormalMuzzle)
        return BallisticsStatus::UnsupportedSource;
    if (!call.current || !Finite(shot.source) || !Finite(shot.direction) ||
        !Finite(shot.spread) || !Finite(shot.right) || !Finite(shot.up) || shot.damage < 0 ||
        shot.playerHits != PistolPlayerHits::OriginalCooperativeSkipPlayers)
        return BallisticsStatus::InvalidInput;
    if (effects.customDecalMessage < 64 || effects.customDecalMessage > 255 ||
        effects.bubbleModel <= 0 || effects.bubbleModel > 32767)
        return BallisticsStatus::MissingEffects;
    return call.current(call.context) ? BallisticsStatus::Ready : BallisticsStatus::Cancelled;
}

BallisticsResult FirePistolBallistics(CBaseEntity& shooter, const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call) noexcept
{
    const auto ready = CheckPistolBallistics(shot, effects, call);
    if (ready != BallisticsStatus::Ready) return {ready, {0, 0, 0}};
    const Owner owner{EntityHandle<CBaseEntity>(&shooter), &shooter};
    if (!Current(call, owner)) return {BallisticsStatus::Cancelled, {0, 0, 0}};
    auto* const inflictor = shooter.pev;
    auto* const shooterEdict = shooter.edict();
    if (!Current(call, owner)) return {BallisticsStatus::Cancelled, {0, 0, 0}};

    Vector spread = shot.spread;
    if (shot.aiming)
    {
        spread.x *= 0.5f;
        spread.y *= 0.5f;
        spread.z *= 0.5f;
    }
    DamageRegister transaction;
    ClearMultiDamage();
    if (!Current(call, owner)) return {BallisticsStatus::Cancelled, {0, 0, 0}};
    gMultiDamage.type = PistolDamageBits;

    const float xFirst = UTIL_SharedRandomFloat(shot.seed + 2u, -0.5f, 0.5f);
    const float xSecond = UTIL_SharedRandomFloat(shot.seed + 1u, -0.5f, 0.5f);
    const float yFirst = UTIL_SharedRandomFloat(shot.seed + 4u, -0.5f, 0.5f);
    const float ySecond = UTIL_SharedRandomFloat(shot.seed + 3u, -0.5f, 0.5f);
    const float x = xFirst + xSecond;
    const float y = yFirst + ySecond;
    const float horizontal = x * spread.x;
    const float vertical = y * spread.y;
    const Vector direction{
        (shot.direction.x + shot.right.x * horizontal) + shot.up.x * vertical,
        (shot.direction.y + shot.right.y * horizontal) + shot.up.y * vertical,
        (shot.direction.z + shot.right.z * horizontal) + shot.up.z * vertical
    };
    const Vector end{
        shot.source.x + direction.x * PistolDistance,
        shot.source.y + direction.y * PistolDistance,
        shot.source.z + direction.z * PistolDistance
    };
    const Vector result{horizontal, vertical, 0};
    TraceResult trace{};
    UTIL_TraceLine(shot.source, end, dont_ignore_monsters, shooterEdict, &trace);
    if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};

    // CoF first retraces past a player, then gives that original player direct
    // damage when MyMonsterPointer is non-null. Two further players are skipped.
    for (int skipped = 0; skipped < 3 && trace.flFraction != 1.0f; ++skipped)
    {
        auto* const target = CBaseEntity::Instance(trace.pHit);
        if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};
        if (!target) break;
        const EntityHandle<CBaseEntity> targetHandle(target);
        const bool player = target->IsPlayer() != FALSE;
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target)
            return {BallisticsStatus::Cancelled, result};
        if (!player) break;
        if (targetHandle.Get<CBaseEntity>() != target) return {BallisticsStatus::Cancelled, result};
        auto* const skip = target->edict();
        if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target)
            return {BallisticsStatus::Cancelled, result};
        const Vector start = trace.vecEndPos;
        UTIL_TraceLine(start, end, dont_ignore_monsters, skip, &trace);
        if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};
        if (skipped == 0)
        {
            if (targetHandle.Get<CBaseEntity>() != target) return {BallisticsStatus::Cancelled, result};
            const bool monster = target->MyMonsterPointer() != nullptr;
            if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target)
                return {BallisticsStatus::Cancelled, result};
            if (monster)
            {
                target->TakeDamage(inflictor, inflictor, static_cast<float>(shot.damage), PistolDamageBits);
                if (!Current(call, owner) || targetHandle.Get<CBaseEntity>() != target)
                    return {BallisticsStatus::Cancelled, result};
            }
        }
    }

    if (trace.flFraction != 1.0f)
    {
        auto* const target = CBaseEntity::Instance(trace.pHit);
        if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};
        const EntityHandle<CBaseEntity> targetHandle(target);
        if (target && shot.damage != 0)
        {
            target->TraceAttack(inflictor, static_cast<float>(shot.damage), direction, &trace, PistolDamageBits);
            if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};
        }
        if (target && targetHandle.Get<CBaseEntity>() != target) return {BallisticsStatus::Cancelled, result};
        // Original customdecal only supports world BSP, not brush entities.
        const int hitIndex = trace.pHit ? ENTINDEX(trace.pHit) : 0;
        if (!Current(call, owner) || (target && targetHandle.Get<CBaseEntity>() != target))
            return {BallisticsStatus::Cancelled, result};
        if (hitIndex == 0 && target && target->pev->solid == SOLID_BSP && !trace.fInWater)
        {
            WorldImpact(trace, effects);
            if (!Current(call, owner)) return {BallisticsStatus::Cancelled, result};
        }
    }
    if (!BubbleTrail(shot.source, trace, effects, call, owner))
        return {BallisticsStatus::Cancelled, result};
    ApplyMultiDamage(inflictor, inflictor);
    return {Current(call, owner) ? BallisticsStatus::Fired : BallisticsStatus::Cancelled, result};
}
}
