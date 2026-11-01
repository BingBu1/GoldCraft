// SPDX-License-Identifier: GPL-3.0-or-later
#include "control.hpp"
#include "catalog.hpp"
#include <algorithm>
#include <cmath>

namespace csol::scenario::skills
{
bool Ready(const SkillTraits& traits, const ActivationState& state,
    ActivationHost& host, bool& ready, ConfigError& error)
{
    ready = false;
    if (!host.Current() || !error.message.empty()) return false;
    const auto step = [&](bool result)
    { return host.Current() && result && error.message.empty(); };
    bool blocked{};
    if (!step(host.FrozenWrathBlocks(blocked, error))) return false;
    if (blocked) return true;
    bool alive{};
    if (!step(host.PlayerAlive(alive, error))) return false;
    if (!alive) return true;
    if (traits.damageProgress) ready = state.progress >= state.requiredProgress;
    else
    {
        float now{};
        if (!step(host.ReadTime(now, error))) return false;
        ready = now >= state.readyAt;
    }
    // Type21's v0C wrapper invokes the common readiness first, then its actual
    // scenario-target owner. An absent target is refusal, not a fatal error.
    if (ready && traits.requiresScenarioTarget)
        if (!step(host.ScenarioTargetAvailable(ready, error))) return false;
    return true;
}

ActivationResult Activate(const SkillTraits& traits, ActivationState& state,
    ActivationHost& host, ConfigError& error)
{
    #pragma clang fp contract(off)
    using Result = ActivationResult;
    const auto failure = [&]()
    {
        if (!host.Current()) return Result::Cancelled;
        if (error.message.empty()) error.message = "scenario character activation host failed";
        return Result::Failed;
    };
    const auto step = [&](bool result)
    { return host.Current() && result && error.message.empty(); };
    bool ready{};
    if (!Ready(traits, state, host, ready, error)) return failure();
    if (!ready)
    {
        if (!traits.automaticTrigger && !step(host.NotifyNotReady(error))) return failure();
        return Result::NotReady;
    }
    if (traits.requiresCharacterAction)
    {
        bool started{};
        if (!step(host.StartCharacterAction5(started, error))) return failure();
        if (!started) return Result::ActionRefused;
    }
    bool prepared{};
    if (!step(host.PrepareEffects(state, prepared, error))) return failure();
    if (!prepared) return Result::PreparationRefused;
    if (traits.requiresCharacterAction && state.actionConstraint > 0)
        if (!step(host.AddActionConstraint33(state.actionConstraint, error))) return failure();
    float now{};
    if (!step(host.ReadTime(now, error))) return failure();
    const float requested = state.cooldown + now;
    float readyAt{};
    if (!step(host.AdjustCooldown(requested, readyAt, error))) return failure();
    if (!std::isfinite(readyAt))
    { error.message = "scenario character cooldown is not finite"; return failure(); }
    state.readyAt = readyAt;
    if (!step(host.ReadTime(now, error))) return failure();
    const float pendingAt = state.delay + now;
    if (!std::isfinite(pendingAt))
    { error.message = "scenario character effect deadline is not finite"; return failure(); }
    state.effects.pendingAt = pendingAt;
    int modelId{};
    if (!step(host.PlayerModelId(modelId, error))) return failure();
    if ((modelId != 121 && modelId != 123 && modelId != 131) || traits.automaticTrigger)
        if (!step(host.PlayActivationVoice(error))) return failure();
    float value{};
    if (traits.damageProgress) state.progress = 0;
    else
    {
        if (!step(host.ReadTime(now, error))) return failure();
        value = state.readyAt - now;
    }
    if (!step(host.PublishActivation(traits.subtype, traits.damageProgress, value, error))) return failure();
    return Result::Started;
}

bool Update(ActivationState& state, ActivationHost& host, ConfigError& error)
{
    return CSOL_UpdateScenarioSkillEffects(state.effects, host, error);
}

float AdjustCooldown(float now, float requestedReadyAt, float reduction)
{
    #pragma clang fp contract(off)
    const float remaining = requestedReadyAt - now;
    const float scale = 1.0f - std::min(1.0f, reduction);
    return remaining * scale + now;
}
}
