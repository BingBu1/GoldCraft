// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"

namespace csol::scenario::custom
{
struct LifeKey
{
    RoundKey round;
    std::uint64_t life{};
    bool Valid() const noexcept { return round.Valid() && life; }
    bool operator==(const LifeKey&) const = default;
};
struct StaminaConfig
{
    float maximum{100.0f}, sprintEntry{5.0f}, sprintDrain{2.0f};
    float recovery{1.0f}, duckRecovery{2.0f}, jumpCost{15.0f};
    // Product defaults, not original CSOL/CoF constants. Dusk changes capacity
    // and recovery amount; it does not shorten the original cycle interval.
    float kickCost{10.0f}, duskMaximum{1.5f}, duskRecovery{1.25f};
    double drainInterval{0.1}, recoveryInterval{0.25};
    float exhaustionDelay{5.0f};
    // Original CoF skill.cfg references; no final host PM speed is promised.
    float walkReference{75.0f}, slowReference{45.0f}, sprintReference{125.0f};
};
bool Valid(const StaminaConfig&) noexcept;
struct StaminaInput
{
    LifeKey key;
    std::uint64_t command{};
    float now{};
    Phase phase{Phase::Noon};
    bool wantsSprint{}, moving{}, backward{}, reloading{}, aiming{}, ladder{}, ducked{}, slow{};
    int waterLevel{};
    bool dead{}, acceptedJump{}, acceptedKick{};
};
struct StaminaView
{
    float amount{}, maximum{}, speedReference{}, nextCycle{};
    bool infinite{}, sprinting{}, exhausted{}, kickAvailable{}, alive{};
};

// Pure host profile. The real authoritative callbacks supply accepted events;
// key presses, weapon changes and client claims cannot refill or debit it.
class StaminaState
{
public:
    bool Begin(LifeKey key, Phase phase, float now, StaminaConfig config = {}) noexcept;
    std::optional<StaminaView> Step(const StaminaInput&) noexcept;
    std::optional<StaminaView> View(LifeKey key) const noexcept;
    bool CanKick(LifeKey key) const noexcept;
    // Accepted host action only, independent of PostThink recovery cadence.
    bool DebitKick(LifeKey key) noexcept;
    // Original accepted Jump debit precedes PostThink's recovery and clamp.
    // A low balance never rejects the native jump or clamps it prematurely.
    bool DebitJump(LifeKey key) noexcept;
    bool Configure(LifeKey key, StaminaConfig config, Phase phase) noexcept;
    bool Retire(LifeKey key) noexcept;
    void Reset() noexcept;
private:
    StaminaView Current() const noexcept;
    LifeKey key_{};
    LifeKey retired_{};
    StaminaConfig config_{};
    Phase phase_{Phase::Noon};
    std::uint64_t command_{};
    float amount_{}, maximum_{}, cycle_{}, now_{};
    bool sprinting_{}, exhausted_{}, slow_{};
};
}
