// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "standalone_stamina.hpp"
#include "standalone_movement.hpp"
#include "stamina.hpp" // Shared life/view contracts; the legacy CoF state is unused.

namespace csol::scenario::custom
{
struct StandaloneStaminaConfig
{
    StandaloneRecoveryConfig recovery;
    float jumpCost{15}, kickCost{10}, duskMaximum{1.5f}, duskRecovery{1.25f};
    float runCostPerUnit{.1f};
    // Explicit custom linear-per-second override; absent uses original
    // StandAlone's accelerating recovery, never the former CoF period.
    std::optional<float> recoveryPerSecond;
};
inline bool Valid(const StandaloneStaminaConfig& config) noexcept
{
    auto recovery=config.recovery;
    recovery.lowThreshold=std::min(recovery.lowThreshold,recovery.maximum);
    return Valid(recovery) && std::isfinite(config.jumpCost) && config.jumpCost >= 0 &&
        std::isfinite(config.runCostPerUnit) && config.runCostPerUnit >= 0 &&
        std::isfinite(config.kickCost) && config.kickCost >= 0 &&
        std::isfinite(config.duskMaximum) && config.duskMaximum >= 1 &&
        std::isfinite(config.duskRecovery) && config.duskRecovery >= 0 &&
        std::isfinite(config.recovery.maximum * config.duskMaximum) &&
        std::isfinite(config.recovery.acceleration * config.duskRecovery) &&
        (!config.recoveryPerSecond || (std::isfinite(*config.recoveryPerSecond) &&
            *config.recoveryPerSecond >= 0 && std::isfinite(*config.recoveryPerSecond * config.duskRecovery)));
}
class StandaloneStaminaState final
{
public:
    bool Begin(LifeKey key, Phase phase, float now, StandaloneStaminaConfig config = {}) noexcept
    {
        if (!key.Valid() || !Playing(phase) || !std::isfinite(now) || !Valid(config) ||
            (key_.Valid() && key.round == key_.round && key.life <= key_.life) ||
            (retired_.Valid() && key.round == retired_.round && key.life <= retired_.life)) return false;
        const float deadline = now + config.recovery.delay;
        if (!std::isfinite(deadline)) return false;
        key_ = key; config_ = config; phase_ = phase; now_ = now; command_ = 0;
        recovery_ = {}; recovery_.amount = recovery_.observed = Capacity();
        recovery_.wait = config.recovery.delay; recovery_.deadline = deadline; recovery_.started = now;
        run_ = {};
        depleted_ = recoveryVoice_ = stepped_ = false;
        return true;
    }
    bool Configure(LifeKey key, const StandaloneStaminaConfig& config, Phase phase) noexcept
    {
        if (!Current(key) || !Playing(phase) || !Valid(config)) return false;
        config_ = config; phase_ = phase;
        recovery_.amount = std::min(recovery_.amount, Capacity());
        recovery_.observed = std::min(recovery_.observed, Capacity());
        return true;
    }
    std::optional<StaminaView> View(LifeKey key) const noexcept
    {
        if (!Current(key)) return {};
        const bool infinite = phase_ == Phase::Noon;
        return StaminaView{recovery_.amount, Capacity(), 0, recovery_.deadline,
            infinite, run_.running, !infinite && depleted_, infinite || recovery_.amount >= config_.kickCost, true};
    }
    bool CanKick(LifeKey key) const noexcept
    { const auto view = View(key); return view && view->kickAvailable; }
    bool CanJump(LifeKey key) const noexcept
    { return Current(key) && CanAffordStandaloneJump(recovery_.amount, config_.jumpCost, phase_ == Phase::Noon); }
    bool DebitKick(LifeKey key) noexcept
    { return CanKick(key) && Debit(config_.kickCost); }
    bool DebitJump(LifeKey key) noexcept
    { return CanJump(key) && Debit(config_.jumpCost); }
    // Explicit host action, sharing this ledger and its normal recovery/HUD.
    // CoF permits a positive balance below its cost; saturating that accepted
    // debit keeps the native CSOL ledger nonnegative (human choice2026-11-16).
    bool DebitExternalAction(LifeKey key, float expected, float cost) noexcept
    {
        if (!Current(key) || !std::isfinite(expected) || !(expected > 0) ||
            expected != recovery_.amount || !std::isfinite(cost) || !(cost > 0)) return false;
        if (phase_ != Phase::Noon)
        {
            recovery_.amount = std::max(expected - cost, 0.0f);
            if (recovery_.amount == 0) depleted_ = true;
        }
        return true;
    }
    bool RequestRun(LifeKey key, bool requested) noexcept
    {
        if (!Current(key)) return false;
        run_.requested = requested;
        return true;
    }
    std::optional<float> RunDisplacement(LifeKey key, const std::array<float, 3>& origin, bool ducking) noexcept
    {
        if (!Current(key)) return {};
        const bool infinite = phase_ == Phase::Noon;
        float amount = infinite ? Capacity() : recovery_.amount;
        const auto distance = AdvanceStandaloneRunDisplacement(run_, amount, origin, ducking,
            infinite ? 0 : config_.runCostPerUnit);
        if (!distance) return {};
        if (!infinite)
        {
            recovery_.amount = amount;
            if (amount == 0) depleted_ = true;
        }
        return distance;
    }
    bool Running(LifeKey key) const noexcept { return Current(key) && run_.running; }
    std::optional<StaminaView> Step(LifeKey key, std::uint64_t command, float now, float frameTime) noexcept
    {
        if (!Current(key) || !command || command <= command_ || !std::isfinite(now) || now < now_ ||
            !std::isfinite(frameTime) || frameTime < 0) return {};
        if (stepped_ && now == now_ && recovery_.amount == recovery_.observed)
        {
            command_ = command; recoveryVoice_ = false;
            return View(key);
        }
        auto next = recovery_;
        auto profile = config_.recovery;
        profile.maximum = Capacity(); profile.lowThreshold = std::min(profile.lowThreshold, profile.maximum);
        const float multiplier = phase_ == Phase::Dusk ? config_.duskRecovery : 1;
        profile.acceleration = config_.recoveryPerSecond ? 0 : profile.acceleration * multiplier;
        const auto events = AdvanceStandaloneRecovery(next, profile, now, frameTime);
        if (!events) return {};
        if (phase_ == Phase::Noon)
        {
            // Infinite is an authoritative product phase, not a refill.
            next = recovery_; next.lowSound = false;
        }
        else if (config_.recoveryPerSecond && now > next.deadline)
        {
            const float amount = recovery_.amount + frameTime * *config_.recoveryPerSecond * multiplier;
            if (!std::isfinite(amount)) return {};
            next.amount = next.observed = std::min(amount, profile.maximum);
            next.accumulated = 0;
            if(events->recovered && next.amount==recovery_.amount) next.lowSound=true;
        }
        recoveryVoice_ = phase_ != Phase::Noon && depleted_ && next.amount>recovery_.amount;
        if (recoveryVoice_) depleted_ = false;
        recovery_ = next; now_ = now; command_ = command; stepped_ = true;
        return View(key);
    }
    bool RecoveryVoice() const noexcept { return recoveryVoice_; }
    bool Retire(LifeKey key) noexcept
    { if (!Current(key)) return false; Reset(); return true; }
    void Reset() noexcept
    { if (key_.Valid()) retired_ = key_; key_ = {}; command_ = 0; run_ = {}; depleted_ = recoveryVoice_ = stepped_ = false; }
private:
    bool Current(LifeKey key) const noexcept { return key_.Valid() && key_ == key; }
    static bool Playing(Phase phase) noexcept
    { return phase == Phase::Noon || phase == Phase::Dusk || phase == Phase::Night; }
    float Capacity() const noexcept
    { return config_.recovery.maximum * (phase_ == Phase::Dusk ? config_.duskMaximum : 1); }
    bool Debit(float cost) noexcept
    {
        if (phase_ != Phase::Noon)
        {
            recovery_.amount -= cost;
            if (recovery_.amount == 0) depleted_ = true;
        }
        return true;
    }
    LifeKey key_, retired_;
    StandaloneStaminaConfig config_;
    StandaloneRecoveryState recovery_;
    StandaloneRunState run_;
    Phase phase_{Phase::Night};
    float now_{};
    std::uint64_t command_{};
    bool depleted_{}, recoveryVoice_{}, stepped_{};
};
}
