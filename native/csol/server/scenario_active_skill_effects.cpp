// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_active_skill_effects.h"

#include <cmath>
#include <limits>

bool CSOL_UpdateScenarioSkillEffects(CSOLScenarioSkillEffectTimers& timers,
    CSOLScenarioSkillEffectHost& host, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    if (!host.Current() || !error.message.empty()) return false;
    const auto step = [&](bool result)
    { return host.Current() && result && error.message.empty(); };

    bool constrained{};
    if (!step(host.HasActionConstraint33(constrained, error))) return false;
    if (constrained)
    {
        bool playing{};
        if (!step(host.CharacterActionPlaying(playing, error))) return false;
        if (!playing && !step(host.RemoveActionConstraint33(error))) return false;
    }

    bool playing{};
    if (!step(host.CharacterActionPlaying(playing, error))) return false;
    if (playing && timers.pendingAt > 0)
    {
        float now{};
        if (!step(host.ReadTime(now, error))) return false;
        if (now > timers.pendingAt)
        {
            if (!step(host.StartEffects(error))) return false;
            const float duration = timers.duration;
            timers.pendingAt = -1;
            if (!step(host.ReadTime(now, error))) return false;
            timers.activeUntil = duration + now;
        }
    }

    if (timers.pendingAt < 0)
    {
        float now{};
        if (!step(host.ReadTime(now, error))) return false;
        if (timers.activeUntil > now && !step(host.ActiveEffects(error))) return false;
    }
    if (timers.activeUntil > 0)
    {
        float now{};
        if (!step(host.ReadTime(now, error))) return false;
        if (now > timers.activeUntil)
        {
            if (!step(host.EndEffects(error))) return false;
            timers.activeUntil = -1;
        }
    }
    return step(host.UpdateEffects(error));
}

bool CSOL_InstallScenarioSkillHealing(CSOLScenarioSkillHealing& healing,
    float duration, float interval, float ratio,
    CSOLScenarioSkillHealingHost& host, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    if (!host.Current() || !error.message.empty()) return false;
    if (!std::isfinite(duration) || !std::isfinite(interval) || !std::isfinite(ratio))
    { error.message = "scenario skill healing parameters are not finite"; return false; }
    float now{};
    if (!host.ReadTime(now, error) || !host.Current() || !error.message.empty()) return false;
    if (!std::isfinite(now + duration))
    { error.message = "scenario skill healing expiry is not finite"; return false; }
    healing.expires = duration + now;
    healing.interval = interval;
    if (!host.ReadTime(now, error) || !host.Current() || !error.message.empty()) return false;
    if (!std::isfinite(interval + now))
    { error.message = "scenario skill healing pulse deadline is not finite"; return false; }
    healing.nextAt = interval + now;
    healing.ratio = ratio;
    return true;
}

bool CSOL_UpdateScenarioSkillHealing(CSOLScenarioSkillHealing& healing,
    CSOLScenarioSkillHealingHost& host, bool& applied,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    applied = false;
    if (!host.Current() || !error.message.empty()) return false;
    // Original CVTPS2PD/COMISD against 117D3810, exactly DBL_EPSILON.
    if (!(static_cast<double>(healing.nextAt) >= std::numeric_limits<double>::epsilon()))
        return true;
    float now{};
    if (!host.ReadTime(now, error) || !host.Current() || !error.message.empty()) return false;
    if (!(now >= healing.nextAt) || !(healing.expires > now)) return true;

    float health{}, maximum{};
    if (!host.ReadHealth(health, maximum, error) || !host.Current() || !error.message.empty())
        return false;
    const float ratio = healing.ratio + 0.0f; // Original ADDSS 1178F584.
    const float amount = ratio * maximum;
    const float updated = amount + health;
    if (!host.WriteHealth(updated, error) || !host.Current() || !error.message.empty()) return false;
    applied = true;
    if (!host.ReadHealth(health, maximum, error) || !host.Current() || !error.message.empty())
        return false;
    if (health > maximum &&
        (!host.WriteHealth(maximum, error) || !host.Current() || !error.message.empty())) return false;
    healing.nextAt = healing.interval + healing.nextAt;
    return host.NotifyRecoveryPulse(error) && host.Current() && error.message.empty();
}
