// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include <deque>
#include <list>

class CSOLMonsterSpawnCall;
class CCSOLScenarioMessages;

struct CSOLMonsterSpeedLimit
{
    int id{};
    float until{}, speed{}, gravity{-9999.0f};
    int sourceTag{}, group{-1};
};

struct CSOLMonsterVelocityEffect
{
    int id{};
    float speed{}, vertical{}, until{};
    int priority{};
    float initialSpeed{};
};

// Entity lifetime, independent of replacement of its ordinary/Boss behavior.
// Pool reuse clears the legacy speed ring, not the separate velocity list or
// timed speed boost. Their deadlines retain their original lifetime.
struct CSOLMonsterSpeedState
{
    float baseSpeed{};
    double boostUntil{}, boostAdd{1.0};
    std::deque<CSOLMonsterSpeedLimit> limits;
    std::list<CSOLMonsterVelocityEffect> effects;
    float recovery{1.0f};
    // Original host+1378. The legacy name predates the part10 consumer:
    // this deadline blocks nearby-monster healing, not motion/animation.
    // The normal motion pass clears it on expiry; pool reuse preserves it.
    double impulseProtectionUntil{};
    Vector2D impulse{};
    float impulseUntil{};
    bool clearingLimits{};

    bool AddLimit(int id, float duration, float speed, float gravity, int sourceTag, float now);
    // Clearing limits uses CSOL_ClearMonsterSpeedLimits so per-ID removal
    // notifications accompany the server-side state changes.
    bool AddEffect(int id, float speed, float vertical, float duration, int priority,
        const Vector& currentVelocity, float now);
    void RemoveEffect(int id);
    bool UpdateMaxSpeed(entvars_t& vars, float now);
    // Mode15 ordinary-monster branch. The scalar pass and the vector pass
    // deliberately have different equal-priority behavior.
    bool ApplyVelocityEffects(const Vector& input, float maxSpeed, float now, Vector& output);
};

// Factory use, after other spawn effects and before SET_ORIGIN. Invalidates
// each ring record, sends its original removal, then clears the ring. Does
// not clear the independent velocity-effect list or timed speed boost.
bool CSOL_ClearMonsterSpeedLimits(CSOLMonsterSpawnCall& call, const CCSOLScenarioMessages& messages);
