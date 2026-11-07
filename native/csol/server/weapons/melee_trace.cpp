// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "melee_trace.h"
#include <cmath>

bool CSOL_TraceMeleeTarget(CBaseEntity& target, CBasePlayer& attacker,
    const Vector& origin, const Vector& forward, const Vector& horizontalForward,
    float distance, float halfAngleCosine, CSOLMeleeBodyTarget bodyTarget,
    TraceResult& result)
{
    // MP 39f962f3, 10CBE320..10CBE6AA. Brush entities require a direct
    // human-hull hit. Non-brush targets can use the horizontal sector fallback.
    Vector start = origin;
    const Vector end = origin + forward * distance;
    if (target.IsBSPModel()) {
        UTIL_TraceHull(start, end, dont_ignore_monsters, human_hull, attacker.edict(), &result);
        return result.flFraction < 1 && result.pHit == target.edict();
    }

    bool hullHit = false;
    UTIL_TraceHull(start, end, dont_ignore_monsters, point_hull, attacker.edict(), &result);
    if (result.flFraction < 1 && result.pHit == target.edict()) {
        hullHit = true;
        TraceResult line{};
        UTIL_TraceLine(start, end, dont_ignore_monsters, attacker.edict(), &line);
        if (!(line.flFraction < 1) || !line.pHit) return hullHit;
        if (line.pHit == target.edict()) result = line;
        return hullHit;
    }

    if (!bodyTarget) return false;
    const Vector body = bodyTarget(target, origin, distance);
    const float dx = body.x - start.x;
    const float dy = body.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float x = length != 0 ? dx / length : 0;
    const float y = length != 0 ? dy / length : 0;
    if (halfAngleCosine > x * horizontalForward.x + y * horizontalForward.y) return false;

    for (int attempts = 5; attempts > 0; --attempts) {
        UTIL_TraceLine(start, body, dont_ignore_monsters, attacker.edict(), &result);
        if (!result.pHit) return hullHit;
        CBaseEntity* hit = CBaseEntity::Instance(result.pHit);
        if (!hit) return hullHit;
        if (hit == &target) return true;
        if (result.flFraction == 1) {
            // The native trace can end without a solid collision while the
            // selected damageable entity still owns the endpoint.
            result.pHit = target.edict();
            result.vecEndPos = body;
            result.iHitgroup = HITGROUP_GENERIC;
            return true;
        }
        if (hit->IsBSPModel()) return hullHit;
        start = result.vecEndPos + forward * 5;
    }
    return hullHit;
}

bool CSOL_MeleeBackstabFacing(const Vector& targetOrigin, const Vector& attackerOrigin,
    const Vector& targetForward)
{
    // MP 110B56EC..110B57BF. The original projects the displacement onto XY
    // but retains the target's full forward XY (including its pitch).
    float x = targetOrigin.x - attackerOrigin.x;
    float y = targetOrigin.y - attackerOrigin.y;
    const float length = std::sqrt(x * x + y * y);
    if (length > 0) { x /= length; y /= length; }
    else { x = 1; y = 0; }
    return x * targetForward.x + y * targetForward.y > .8f;
}
