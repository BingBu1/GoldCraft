// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_speed.h"
#include "scenario_monster_factory.h"
#include "scenario_messages.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
float Length2(const Vector& value)
{
    const float squared = value.y * value.y + value.x * value.x;
    return static_cast<float>(std::sqrt(static_cast<double>(squared)));
}
bool Finite(const Vector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

bool CSOLMonsterSpeedState::AddLimit(int id, float duration, float speed,
    float gravity, int sourceTag, float now)
{
    if (clearingLimits || !(duration > 0) || !std::isfinite(duration) || !std::isfinite(speed) ||
        !std::isfinite(gravity) || !std::isfinite(now)) return false;
    const float until = now + duration;
    if (!std::isfinite(until)) return false;
    constexpr std::array group5{4, 5, 18, 20, 22, 23, 25, 26, 33, 34, 35, 37, 43, 47, 49, 50, 51, 52, 61};
    constexpr std::array group3{72, 86, 119, 137};
    const int group = std::find(group5.begin(), group5.end(), id) != group5.end() ? 5 :
        std::find(group3.begin(), group3.end(), id) != group3.end() ? 3 : -1;
    const CSOLMonsterSpeedLimit replacement{id, until, speed, gravity, sourceTag, group};
    for (auto& limit : limits)
    {
        if (limit.id != id || !((sourceTag <= 0 && limit.sourceTag <= 0) || sourceTag == limit.sourceTag))
            continue;
        if (!(until > limit.until)) return false;
        limit = replacement;
        return true;
    }
    limits.push_back(replacement);
    return true;
}

bool CSOLMonsterSpeedState::UpdateMaxSpeed(entvars_t& vars, float now)
{
    if (!std::isfinite(now) || !std::isfinite(baseSpeed) ||
        !std::isfinite(boostUntil) || !std::isfinite(boostAdd)) return false;
    if (limits.empty() && boostUntil == 0.0) return true;
    float speed = baseSpeed;
    if (boostUntil > static_cast<double>(now))
        speed = static_cast<float>((boostAdd + 1.0) * static_cast<double>(speed));
    else if (boostUntil != 0.0)
    {
        boostUntil = 0.0;
        boostAdd = 1.0;
        // This early return preserves the previous maxspeed in the original.
        if (limits.empty()) return true;
    }
    if (!std::isfinite(speed)) return false;
    for (const auto& limit : limits)
        if (!std::isfinite(limit.until) || !std::isfinite(limit.speed) ||
            !std::isfinite(limit.gravity)) return false;
    // The old ring only expires its ends. An expired middle record survives
    // until an end reaches it; this is not a general deadline filter.
    while (!limits.empty())
    {
        if (now > limits.front().until) limits.pop_front();
        else if (now > limits.back().until) limits.pop_back();
        else break;
    }
    float gravity = -9999.0f;
    for (const auto& limit : limits)
        if (speed >= limit.speed) { speed = limit.speed; gravity = limit.gravity; }
    if (gravity != -9999.0f) vars.gravity = gravity;
    vars.maxspeed = speed;
    return true;
}

bool CSOLMonsterSpeedState::AddEffect(int id, float speed, float vertical, float duration,
    int priority, const Vector& currentVelocity, float now)
{
    if (!std::isfinite(speed) || !std::isfinite(vertical) || !std::isfinite(duration) ||
        !std::isfinite(now)) return false;
    const float until = now + duration;
    if (!std::isfinite(until)) return false;
    for (auto& effect : effects)
    {
        if (effect.id != id) continue;
        // This list permits shortening a deadline and preserves the speed
        // captured on first insertion, unlike the separate legacy ring.
        effect.speed = speed;
        effect.vertical = vertical;
        effect.until = until;
        effect.priority = priority;
        return true;
    }
    const float initialSpeed = Length2(currentVelocity);
    if (!std::isfinite(initialSpeed)) return false;
    effects.push_back({id, speed, vertical, until, priority, initialSpeed});
    return true;
}

void CSOLMonsterSpeedState::RemoveEffect(int id)
{
    effects.remove_if([id](const auto& effect) { return effect.id == id; });
}

bool CSOLMonsterSpeedState::ApplyVelocityEffects(const Vector& input, float maxSpeed,
    float now, Vector& output)
{
    if (!Finite(input) || !std::isfinite(maxSpeed) || !std::isfinite(now)) return false;
    for (const auto& effect : effects)
        if (!std::isfinite(effect.speed) || !std::isfinite(effect.vertical) ||
            !std::isfinite(effect.until)) return false;

    // Original 1085CCB0 runs first and expires nodes even though its returned
    // scalar is not consumed by the non-player 1085CEB0 branch.
    float scalar = std::fabs(maxSpeed);
    int priority = -1;
    for (auto it = effects.begin(); it != effects.end(); )
    {
        if (now >= it->until) { it = effects.erase(it); continue; }
        if (priority <= it->priority)
        {
            priority = std::max(priority, it->priority);
            if (scalar > it->speed) scalar = it->speed;
        }
        ++it;
    }
    if (maxSpeed < 0) scalar = -scalar;
    (void)scalar;
    if (effects.empty()) { output = input; return true; }

    float cap = Length2(input), vertical{};
    if (!std::isfinite(cap)) return false;
    priority = -1;
    for (auto it = effects.begin(); it != effects.end(); )
    {
        if (now >= it->until) { it = effects.erase(it); continue; }
        // Unlike the scalar pass, a later equal-priority record is skipped.
        if (priority < it->priority)
        {
            priority = it->priority;
            cap = std::min(it->speed, cap);
            vertical = std::min(it->vertical, vertical);
        }
        ++it;
    }
    const float length = Length2(input);
    Vector result(0, 0, input.z + vertical);
    if (length != 0)
    {
        const float inverse = 1.0f / length;
        result.x = (input.x * inverse) * cap;
        result.y = (input.y * inverse) * cap;
    }
    if (!Finite(result)) return false;
    output = result;
    return true;
}

bool CSOL_ClearMonsterSpeedLimits(CSOLMonsterSpawnCall& call, const CCSOLScenarioMessages& messages)
{
    auto* owner = call.Current();
    if (!owner || owner->SpeedState().clearingLimits) return false;
    owner->SpeedState().clearingLimits = true;
    const auto finish = [&](bool success)
    {
        // Do not retain a reference into an entity across a message/engine
        // callback: the old map or this pool lease may have been destroyed.
        auto* current = call.Current();
        if (current != owner) return false;
        current->SpeedState().clearingLimits = false;
        return success;
    };
    try
    {
        const auto count = owner->SpeedState().limits.size();
        for (std::size_t index = 0; index < count; ++index)
        {
            auto* current = call.Current();
            if (current != owner) return false;
            auto& limits = current->SpeedState().limits;
            if (limits.size() != count) return finish(false);
            limits[index].until = -1.0f;
            const int id = limits[index].id;
            const char* model = id == 28 || id == 57 ? "models/ef_m95tiger_nethit.mdl" :
                id == 38 ? "models/ef_m95tigerm_nethit.mdl" : nullptr;
            int effect = id == 29 ? 0x56 : id == 39 ? 0x5b : 0;
            if (model)
            {
                if (!g_engfuncs.pfnModelIndex) return finish(false);
                effect = MODEL_INDEX(model);
                if (call.Current() != owner) return false;
                // Original does not send a model-based removal for index0.
                if (effect == 0) continue;
            }
            else if (!effect) continue;
            if (!g_engfuncs.pfnIndexOfEdict) return finish(false);
            const int entity = ENTINDEX(owner->edict());
            if (call.Current() != owner) return false;
            const bool sent = messages.SendMonsterEffectRemoval(entity, effect, model == nullptr);
            if (call.Current() != owner) return false;
            if (!sent) return finish(false);
        }
        auto* current = call.Current();
        if (current != owner || current->SpeedState().limits.size() != count) return finish(false);
        current->SpeedState().limits.clear();
        return finish(true);
    }
    catch (...) { return finish(false); }
}
