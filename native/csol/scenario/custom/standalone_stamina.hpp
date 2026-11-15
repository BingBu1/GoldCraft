// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace csol::scenario::custom
{
// Original CSOL MP39f962f3: recovery108593D0, reset10858D60,
// configured defaults100CF770/100CF860/100CF950. No movement owner here.
struct StandaloneRecoveryConfig
{
    float maximum{100.0f}, delay{3.0f}, acceleration{0.1f}, lowThreshold{11.0f};
};
struct StandaloneRecoveryState
{
    float amount{100.0f}, observed{100.0f}, accumulated{};
    float wait{3.0f}, deadline{}, started{};
    bool lowSound{};
};
struct StandaloneRecoveryEvents
{
    bool low{}, recovered{};
};
inline bool Valid(const StandaloneRecoveryConfig& config) noexcept
{
    return std::isfinite(config.maximum) && config.maximum > 0 &&
        std::isfinite(config.delay) && config.delay >= 0 &&
        std::isfinite(config.acceleration) && config.acceleration >= 0 &&
        std::isfinite(config.lowThreshold) && config.lowThreshold >= 0 &&
        config.lowThreshold <= config.maximum;
}
inline std::optional<StandaloneRecoveryEvents> AdvanceStandaloneRecovery(
    StandaloneRecoveryState& state, const StandaloneRecoveryConfig& config,
    float now, float frameTime) noexcept
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if (!Valid(config) || !std::isfinite(now) || !std::isfinite(frameTime) || frameTime < 0 ||
        !std::isfinite(state.amount) || state.amount < 0 || state.amount > config.maximum ||
        !std::isfinite(state.observed) || state.observed < 0 || state.observed > config.maximum ||
        !std::isfinite(state.accumulated) || state.accumulated < 0 ||
        !std::isfinite(state.wait) || !std::isfinite(state.deadline) || !std::isfinite(state.started)) return {};
    // Validate on stack before committing; rejected host input cannot leave a
    // partially changed recovery timer or consume its one-shot audio edge.
    auto next = state;
    StandaloneRecoveryEvents events;
    if (next.observed != next.amount)
    {
        next.accumulated = 0;
        next.observed = next.amount;
        next.wait = config.delay;
        next.deadline = now + config.delay;
        next.started = now;
        if (!std::isfinite(next.deadline)) return {};
        if (next.amount < config.lowThreshold && !next.lowSound)
        {
            next.lowSound = true;
            events.low = true;
        }
    }
    if (next.amount < config.maximum && next.observed == next.amount && now > next.deadline)
    {
        next.accumulated += frameTime * config.acceleration;
        const float recovered = next.amount + next.accumulated;
        if (!std::isfinite(next.accumulated) || !std::isfinite(recovered)) return {};
        next.amount = std::min(recovered, config.maximum);
        next.observed = next.amount;
        if (next.lowSound)
        {
            next.lowSound = false;
            events.recovered = true;
        }
    }
    state = next;
    return events;
}
// Human rule2026-11-15; the original jump default18 is deliberately replaced
// by configured cost (human selected15). This is not an original-CoF rule.
inline bool CanAffordStandaloneJump(float amount, float cost, bool infinite) noexcept
{
    return std::isfinite(amount) && amount >= 0 && std::isfinite(cost) && cost >= 0 &&
        (infinite || amount >= cost);
}
}
