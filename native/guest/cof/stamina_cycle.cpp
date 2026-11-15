// SPDX-License-Identifier: GPL-3.0-or-later
#include "stamina_cycle.hpp"
#include <cmath>
#include <limits>

namespace guest::cof
{
namespace
{
constexpr std::uint32_t Sprint = 0x40000U, Exhausted = 0x80000U, Duck = 0x4000U;
bool Prepared(const StaminaCycleHost& h) noexcept
{ return h.current && h.clock && h.multiplayer && h.random && h.voice && h.hud; }
StaminaCycleResult Failure(const StaminaCycleHost& h) noexcept
{ return h.current(h.context) ? StaminaCycleResult::MissingBackend : StaminaCycleResult::Cancelled; }
bool Clock(const StaminaCycleHost& h, float& value) noexcept
{ value = h.clock(h.context); return std::isfinite(value); }
int Truncate(float value) noexcept
{
    // Original cvttss2si, including its defined indefinite result. Message
    // callbacks may legitimately change the amount after the earlier clamp.
    return value >= -2147483648.F && value < 2147483648.F ? static_cast<int>(value) :
        (std::numeric_limits<int>::min)();
}
bool Hud(const StaminaCycleHost& h, const StaminaHudEvent& event) noexcept
{ return h.hud(h.context, event) && h.current(h.context); }
}

StaminaCycleResult SpawnStaminaCycle(StaminaCycleState& state, const StaminaCycleHost& h) noexcept
{
    if (!Prepared(h)) return StaminaCycleResult::MissingBackend;
    if (!h.current(h.context)) return StaminaCycleResult::Cancelled;
    float now{}; if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
    state.amount = 100; state.nextCycle = state.auxiliaryTime = now;
    if (!Hud(h, {StaminaHudKind::Begin, 8}) ||
        !Hud(h, {StaminaHudKind::Byte, Truncate(state.amount)}) ||
        !Hud(h, {StaminaHudKind::Coord, 0, 0}) || !Hud(h, {StaminaHudKind::End})) return Failure(h);
    // Original Spawn does not update the prior-sent cache or clear430 here.
    return StaminaCycleResult::Applied;
}

StaminaCycleResult AdvanceStaminaCycle(StaminaCycleState& state, StaminaCycleOwner& owner,
                                     const StaminaCycleHost& h) noexcept
{
    if (!Prepared(h)) return StaminaCycleResult::MissingBackend;
    if (!h.current(h.context)) return StaminaCycleResult::Cancelled;
    if (!std::isfinite(state.amount) || !std::isfinite(state.lastSent) ||
        !std::isfinite(state.nextCycle) || !std::isfinite(state.voiceDeadline) ||
        !std::isfinite(owner.horizontalSpeed) || !std::isfinite(owner.voiceVariant))
        return StaminaCycleResult::InvalidInput;
    float now{}; if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
    float next = state.nextCycle;
    if (now > next && state.amount > 0)
    {
        if (((owner.flags & Sprint) && owner.horizontalSpeed != 0) || owner.alternateDrain)
        {
            if (!owner.infinite) state.amount -= owner.alternateDrain ? 1.F : 2.F;
            if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
            next = static_cast<float>(double(now) + (owner.alteredCycle ? .05 : .1));
        }
        else
        {
            state.amount += owner.flags & Duck ? 2.F : 1.F;
            if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
            next = static_cast<float>(double(now) + (owner.alteredCycle ? .75 : .25));
        }
        state.nextCycle = next;
    }
    if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
    if (now > next && state.amount == 0)
    {
        state.amount = .1F;
        if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
        state.exhaustionActionDeadline = now + 4.F;
        if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
        if (owner.waterLevel < 3 && now > state.voiceDeadline)
        {
            bool multi{};
            if (owner.rulesPresent && (!h.multiplayer(h.context, multi) || !h.current(h.context))) return Failure(h);
            StaminaVoice sample{};
            if (multi)
            {
                int value{};
                if (!h.random(h.context, 0, 1, value) || !h.current(h.context)) return Failure(h);
                sample = value == 1 ? StaminaVoice::Police1 : StaminaVoice::Police2;
            }
            else sample = owner.voiceVariant == 0 ? StaminaVoice::Player : StaminaVoice::Doctor;
            if (!h.voice(h.context, {sample, 2, 1, .8F, 0, 100}) || !h.current(h.context)) return Failure(h);
            if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
            state.voiceDeadline = now + 30.F;
        }
        if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
        state.nextCycle = now + (owner.alteredCycle ? 10.F : 5.F);
        owner.flags |= Exhausted;
        if (!Clock(h, now)) return StaminaCycleResult::InvalidInput;
        state.exhaustionAudioDeadline = now + 5.F;
    }
    if ((owner.flags & Exhausted) && state.amount >= 1) owner.flags &= ~Exhausted;
    if (state.amount < 0) state.amount = 0;
    if (state.amount > 100) state.amount = 100;
    if (state.lastSent != state.amount)
    {
        if (!Hud(h, {StaminaHudKind::Begin, 8}) ||
            !Hud(h, {StaminaHudKind::Byte, Truncate(state.amount)}) ||
            !Hud(h, {StaminaHudKind::Coord, 0, state.hudPulse == 0 ? 0.F : state.hudPulse}) ||
            !Hud(h, {StaminaHudKind::End})) return Failure(h);
        if (state.hudPulse != 0) state.hudPulse = 0;
        state.lastSent = state.amount;
    }
    return StaminaCycleResult::Applied;
}
}
