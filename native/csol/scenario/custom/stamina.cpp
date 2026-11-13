// SPDX-License-Identifier: GPL-3.0-or-later
#include "stamina.hpp"
#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)

namespace csol::scenario::custom
{
namespace
{
bool Playing(Phase phase) noexcept
{ return phase == Phase::Noon || phase == Phase::Dusk || phase == Phase::Night; }
float Capacity(const StaminaConfig& config, Phase phase) noexcept
{ return phase == Phase::Dusk ? config.maximum * config.duskMaximum : config.maximum; }
}
bool Valid(const StaminaConfig& value) noexcept
{
    const float positive[]{value.maximum,
        value.duskMaximum, value.exhaustionDelay,
        value.walkReference, value.slowReference, value.sprintReference};
    for (const float number : positive) if (!std::isfinite(number) || number <= 0) return false;
    const float costs[]{value.sprintEntry, value.sprintDrain, value.jumpCost, value.kickCost,
        value.recovery, value.duckRecovery, value.duskRecovery};
    for (const float number : costs) if (!std::isfinite(number) || number < 0) return false;
    return value.duskMaximum >= 1 &&
        std::isfinite(value.maximum * value.duskMaximum) &&
        std::isfinite(value.duckRecovery * value.duskRecovery) &&
        std::isfinite(value.recovery * value.duskRecovery) &&
        std::isfinite(value.drainInterval) && value.drainInterval > 0 &&
        std::isfinite(value.recoveryInterval) && value.recoveryInterval > 0;
}
bool StaminaState::Begin(LifeKey key, Phase phase, float now, StaminaConfig config) noexcept
{
    if (!key.Valid() || !Playing(phase) || !std::isfinite(now) || !Valid(config)) return false;
    // A duplicate preparation (including a weapon switch) cannot refill life.
    if ((key_.Valid() && key.round == key_.round && key.life <= key_.life) ||
        (retired_.Valid() && key.round == retired_.round && key.life <= retired_.life)) return false;
    key_ = key; config_ = config; phase_ = phase;
    maximum_ = amount_ = Capacity(config_, phase); cycle_ = now_ = now;
    command_ = 0; sprinting_ = exhausted_ = slow_ = false;
    return true;
}
StaminaView StaminaState::Current() const noexcept
{
    const bool infinite = phase_ == Phase::Noon;
    return {amount_, maximum_, sprinting_ ? config_.sprintReference :
        (slow_ || exhausted_ ? config_.slowReference : config_.walkReference), cycle_,
        infinite, sprinting_, exhausted_, infinite || amount_ >= config_.kickCost, key_.Valid()};
}
std::optional<StaminaView> StaminaState::View(LifeKey key) const noexcept
{ return key_.Valid() && key == key_ ? std::optional{Current()} : std::nullopt; }
bool StaminaState::CanKick(LifeKey key) const noexcept
{ const auto value = View(key); return value && value->kickAvailable; }
bool StaminaState::DebitKick(LifeKey key) noexcept
{
    if (!CanKick(key)) return false;
    if (phase_ != Phase::Noon) amount_ -= config_.kickCost;
    return true;
}
bool StaminaState::Configure(LifeKey key, StaminaConfig config, Phase phase) noexcept
{
    if (!key_.Valid() || key != key_ || !Playing(phase) || !Valid(config)) return false;
    config_ = config; phase_ = phase; maximum_ = Capacity(config_, phase);
    amount_ = std::min(amount_, maximum_); return true;
}
std::optional<StaminaView> StaminaState::Step(const StaminaInput& input) noexcept
{
    if (!key_.Valid() || input.key != key_ || !input.command || input.command <= command_ ||
        !std::isfinite(input.now) || input.now < now_ || !Playing(input.phase) ||
        input.phase < phase_ || input.waterLevel < 0 || input.waterLevel > 3) return {};
    if (input.dead)
    {
        auto value = Current(); value.alive = value.sprinting = value.kickAvailable = false;
        Reset(); return value;
    }
    const float maximum = Capacity(config_, input.phase);
    // Validate all deadlines before mutating state. Original cycle arithmetic
    // converts float now to double, adds the double interval, then rounds once.
    const float drainCycle = static_cast<float>(static_cast<double>(input.now) + config_.drainInterval);
    const float recoveryCycle = static_cast<float>(static_cast<double>(input.now) + config_.recoveryInterval);
    const float exhaustionCycle = input.now + config_.exhaustionDelay;
    if (!std::isfinite(drainCycle) || !std::isfinite(recoveryCycle) || !std::isfinite(exhaustionCycle)) return {};
    command_ = input.command; now_ = input.now; phase_ = input.phase;
    maximum_ = maximum; amount_ = std::min(amount_, maximum_); slow_ = input.slow;
    const bool infinite = phase_ == Phase::Noon;
    // Jump is before the original PostThink stamina producer. Kick is an
    // explicit host accepted-action debit at the same profile boundary.
    if (!infinite)
    {
        if (input.acceptedJump) amount_ -= config_.jumpCost;
        if (input.acceptedKick) amount_ -= config_.kickCost;
    }
    const bool eligible = input.wantsSprint && input.moving && !input.backward &&
        !input.reloading && !input.aiming && !input.ladder && !input.ducked && input.waterLevel != 2;
    if (!sprinting_ && eligible && amount_ >= 1 && !exhausted_)
    {
        sprinting_ = true;
        if (!infinite) amount_ -= config_.sprintEntry;
    }
    if (sprinting_ && (!eligible || amount_ == 0 || exhausted_)) sprinting_ = false;
    // Exactly one original cycle per accepted command, even after a long gap.
    if (input.now > cycle_ && amount_ > 0)
    {
        if (sprinting_ && input.moving)
        {
            if (!infinite) amount_ -= config_.sprintDrain;
            cycle_ = drainCycle;
        }
        else
        {
            float recovery = input.ducked ? config_.duckRecovery : config_.recovery;
            if (phase_ == Phase::Dusk) recovery *= config_.duskRecovery;
            amount_ += recovery; cycle_ = recoveryCycle;
        }
    }
    if (input.now > cycle_ && amount_ == 0)
    {
        amount_ = 0.1f; exhausted_ = true; sprinting_ = false; cycle_ = exhaustionCycle;
    }
    if (exhausted_ && amount_ >= 1) exhausted_ = false;
    amount_ = std::clamp(amount_, 0.0f, maximum_);
    return Current();
}
bool StaminaState::Retire(LifeKey key) noexcept
{ if (!key_.Valid() || key != key_) return false; Reset(); return true; }
void StaminaState::Reset() noexcept
{
    if (key_.Valid()) retired_ = key_;
    key_ = {}; command_ = 0; sprinting_ = exhausted_ = slow_ = false;
}
}
