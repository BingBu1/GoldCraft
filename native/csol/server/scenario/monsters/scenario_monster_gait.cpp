// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_gait.h"
#include "scenario_monster_factory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

#pragma clang fp contract(off)

namespace
{
float AngleDifference(float value)
{
    value = static_cast<float>(std::fmod(static_cast<double>(value), 360.0));
    if (value > 180.0f) value -= 360.0f;
    else if (value < -180.0f) value += 360.0f;
    return value;
}

bool WholeCycles(float value, int period, float& remainder)
{
    const float quotient = value / static_cast<float>(period);
    // Reject malformed state before float-to-int or integer multiplication
    // can overflow. Normal model frames/angles retain the original int cycle.
    if (!std::isfinite(quotient) || static_cast<double>(quotient) < INT_MIN ||
        static_cast<double>(quotient) > INT_MAX) return false;
    const auto cycles = static_cast<std::int64_t>(static_cast<int>(quotient)) * period;
    if (cycles < INT_MIN || cycles > INT_MAX) return false;
    remainder = value - static_cast<float>(static_cast<int>(cycles));
    return true;
}

bool Estimate(CSOLMonsterGaitState& gait, const entvars_t& vars, float dt)
{
    if (dt == 0) { gait.distance = 0; return true; }
    Vector displacement = vars.origin - gait.previousOrigin;
    gait.previousOrigin = vars.origin;
    const float squared = (displacement.x * displacement.x + displacement.y * displacement.y) +
        displacement.z * displacement.z;
    gait.distance = static_cast<float>(std::sqrt(static_cast<double>(squared)));
    if (!std::isfinite(gait.distance)) return false;
    if (dt <= 0 || gait.distance / dt < 5.0f)
    {
        gait.distance = 0;
        displacement.x = displacement.y = 0;
    }
    if (displacement.x != 0 || displacement.y != 0)
    {
        const float angle = static_cast<float>(std::atan2(static_cast<double>(displacement.y),
            static_cast<double>(displacement.x)));
        const float degrees = angle * 180.0f;
        gait.yaw = std::clamp(static_cast<float>(static_cast<double>(degrees) /
            std::numbers::pi), -180.0f, 180.0f);
    }
    else
    {
        const float raw = vars.angles.y - gait.yaw;
        float following{};
        if (!WholeCycles(raw, 360, following)) return false;
        if (following > 180.0f) following -= 360.0f;
        else if (following < -180.0f) following += 360.0f;
        const float difference = AngleDifference(raw);
        if (difference > -5.0f && difference < 5.0f) gait.turnRate = 0.05f;
        else if (difference < -89.0f || difference > 89.0f) gait.turnRate = 3.5f;
        const float scale = dt < 0.25f ? dt * gait.turnRate : dt;
        float step = scale * following;
        if (std::abs(static_cast<double>(step)) < 0.1) step = 0;
        if (!WholeCycles(gait.yaw + step, 360, gait.yaw)) return false;
        gait.distance = 0;
    }
    gait.estimatedYaw = gait.yaw;
    return true;
}
}

bool CSOL_UpdateMonsterGait(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        if (ownerValid && !ownerValid(ownerContext)) return nullptr;
        return call.Current();
    };
    auto* monster = current();
    if (!monster) return false;
    if (monster->pev->gaitsequence <= 0) return true; // No writes, including cached sequence.
    const auto fail = [&]() { error.message = "invalid ordinary monster gait state or model"; return false; };
    if (!gpGlobals || !std::isfinite(gpGlobals->frametime)) return fail();
    auto& gait = monster->GaitState();
    gait.sequence = monster->pev->gaitsequence;
    const float dt = std::clamp(gpGlobals->frametime, 0.0f, 1.0f);
    if (!Estimate(gait, *monster->pev, dt)) return fail();
    float difference = AngleDifference(monster->pev->angles.y - gait.yaw);
    if (!std::isfinite(difference)) return fail();
    if (gait.distance != 0)
    {
        if (difference > 120.0f)
        { difference -= 180.0f; gait.yaw -= 180.0f; gait.distance = -gait.distance; }
        else if (difference < -120.0f)
        { difference += 180.0f; gait.yaw += 180.0f; gait.distance = -gait.distance; }
    }
    const float mapped = std::clamp(static_cast<float>((static_cast<double>(difference) / 90.0) *
        128.0 + 127.0), 0.0f, 255.0f);
    gait.yawController = static_cast<float>(255.0 - static_cast<double>(mapped));
    monster->pev->controller[0] = static_cast<byte>(static_cast<int>(gait.yawController));
    float pitch = monster->pev->angles.x;
    if (pitch > 180.0f) pitch -= 360.0f;
    const float scaledPitch = pitch * 3.0f;
    if (!std::isfinite(scaledPitch) || static_cast<double>(scaledPitch) < INT_MIN ||
        static_cast<double>(scaledPitch) > INT_MAX) return fail();
    const int p = static_cast<int>(scaledPitch);
    const int controller = p <= -45 ? 255 : p >= 45 ? 0 :
        static_cast<int>(((45.0f - static_cast<float>(p)) * 255.0f) / 90.0f);
    gait.pitchController = static_cast<float>(controller);
    monster->pev->controller[1] = static_cast<byte>(controller);

    // The original queries the model only after updating both controllers.
    if (!g_engfuncs.pfnGetModelPtr) return fail();
    const int sequence = monster->pev->gaitsequence, model = monster->pev->modelindex;
    const auto* header = static_cast<studiohdr_t*>(GET_MODEL_PTR(monster->edict()));
    monster = current();
    if (!monster || monster->pev->gaitsequence != sequence || monster->pev->modelindex != model) return false;
    if (!header) return true;
    if (header->length < static_cast<int>(sizeof(studiohdr_t)) || header->seqindex < 0 ||
        header->seqindex > header->length || header->numseq <= sequence ||
        static_cast<std::size_t>(header->numseq) >
            static_cast<std::size_t>(header->length - header->seqindex) / sizeof(mstudioseqdesc_t)) return fail();
    const auto& description = reinterpret_cast<const mstudioseqdesc_t*>(
        reinterpret_cast<const std::byte*>(header) + header->seqindex)[sequence];
    if (description.numframes <= 0) return fail();
    const float frames = static_cast<float>(description.numframes);
    const float delta = description.linearmovement.x > 0 ?
        (gait.distance / description.linearmovement.x) * frames :
        (dt * description.fps) * monster->pev->framerate;
    if (!WholeCycles(gait.frame + delta, description.numframes, gait.frame)) return fail();
    if (gait.frame < 0) gait.frame += frames;
    return true; // Never writes the ordinary animation's pev.frame.
}
