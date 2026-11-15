// SPDX-License-Identifier: GPL-3.0-or-later
#include "m16_control.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
float AddDouble(float now, double delay) noexcept
{
    return static_cast<float>(double(now) + delay);
}
struct Call
{
    const M16ControlHost& host;
    M16ControlResult result{M16ControlResult::ConsumerRejected};
    M16ControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event) return M16ControlResult::MissingHost;
        return host.current(host.context) ? M16ControlResult::Applied : M16ControlResult::Cancelled;
    }
    bool Emit(M16ControlEventKind kind, int sequence = 0) noexcept
    {
        if (!host.current(host.context)) { result = M16ControlResult::Cancelled; return false; }
        const bool accepted = host.event(host.context, {kind, sequence});
        if (!host.current(host.context)) { result = M16ControlResult::Cancelled; return false; }
        return accepted;
    }
    float Time() const noexcept { return host.time(host.context); }
};
bool FiniteClocks(const MagazineActionState& action, float now) noexcept
{
    return std::isfinite(now) && std::isfinite(action.primary) && std::isfinite(action.secondary);
}
}

M16ControlResult M16PrimaryTrigger(M16ControlState& state, bool attackPressed,
    bool fireOnEmpty, const M16ControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != M16ControlResult::Applied) return ready;
    auto& action = state.action;
    const float now = call.Time();
    if (!FiniteClocks(action, now)) return M16ControlResult::InvalidInput;
    if (action.primary > now || action.secondary > now || action.action || action.special || !attackPressed)
        return M16ControlResult::Ineligible;
    if (action.burstShots >= 3) action.burstShots = 0;
    if (action.clip <= 0)
    {
        if (!fireOnEmpty)
            return call.Emit(M16ControlEventKind::Reload) ? M16ControlResult::Deferred : call.result;
        if (!call.Emit(M16ControlEventKind::EmptyClick)) return call.result;
        action.primary = action.secondary = AddDouble(call.Time(), .8);
        action.idle = AddDouble(call.Time(), .8);
        return M16ControlResult::Applied;
    }
    if (!call.Emit(M16ControlEventKind::Shoot)) return call.result;
    // The original reads the mode after Shoot; the per-shot consumer owns
    // ammo, effects and burst continuation, including underwater rejection.
    const double interval = state.burst ? .75 : .1;
    action.primary = AddDouble(call.Time(), interval);
    action.secondary = AddDouble(call.Time(), interval);
    action.idle = AddDouble(call.Time(), .66);
    return M16ControlResult::Applied;
}

M16ControlResult M16ToggleFireMode(M16ControlState& state, const M16ControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != M16ControlResult::Applied) return ready;
    auto& action = state.action;
    const float now = call.Time();
    if (!FiniteClocks(action, now)) return M16ControlResult::InvalidInput;
    if (action.primary > now || action.secondary > now || action.action || action.special)
        return M16ControlResult::Ineligible;
    const bool next = !state.burst;
    const int sequence = action.aiming ? (state.burst ? 33 : 32) : (state.burst ? 13 : 6);
    if (!call.Emit(M16ControlEventKind::ViewAnimation, sequence)) return call.result;
    state.burst = next;
    action.primary = action.secondary = call.Time() + 1.F;
    action.idle = call.Time() + 1.F;
    return M16ControlResult::Applied;
}

M16ControlResult StartM16MagazineReload(M16ControlState& state, const MagazineSequenceTimes& times,
    const M16ControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != M16ControlResult::Applied) return ready;
    auto& action = state.action;
    // Host adaptation shared by the other CoF guns: preserve skill grants.
    if (action.clip > 20 || action.reserve <= 0 || action.action == 1 || action.action > 2 ||
        action.special || action.pendingReload) return M16ControlResult::Ineligible;
    if (action.aiming)
    {
        action.pendingReload = true;
        return M16ControlResult::Deferred;
    }
    const float now = call.Time();
    if (!FiniteClocks(action, now) || action.clip < 0) return M16ControlResult::InvalidInput;
    if (action.primary > now || action.secondary > now || action.clip == 20)
        return M16ControlResult::Ineligible;
    const bool loaded = action.clip != 0;
    const int sequence = (state.burst ? 10 : 3) + !loaded;
    if (!(times.present & (std::uint64_t{1} << sequence)) ||
        !std::isfinite(times.duration[sequence]) || times.duration[sequence] < 0)
        return M16ControlResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return M16ControlResult::InvalidInput;
    action.action = 2;
    action.burstShots = 0;
    action.clip = 0;
    action.ownerAttack = action.primary = action.secondary = action.idle = deadline;
    if (!call.Emit(M16ControlEventKind::ViewAnimation, sequence)) return call.result;
    action.loading = true;
    if (!call.Emit(loaded ? M16ControlEventKind::PlayerReloadLoaded : M16ControlEventKind::PlayerReloadEmpty))
        return call.result;
    return M16ControlResult::Applied;
}

M16ControlResult AdvanceM16DerivedFrame(MagazineActionState& state, float& deadline,
    const M16FrameHost& host) noexcept
{
    if (!host.current || !host.time || !host.owner || !host.event || !host.duration)
        return M16ControlResult::MissingHost;
    if (!host.current(host.context)) return M16ControlResult::Cancelled;
    if (state.special || state.action < 0 || (state.action > 2 && state.action != 5))
        return M16ControlResult::UnsupportedState;
    M16ControlResult result = M16ControlResult::ConsumerRejected;
    const auto current = [&]() noexcept
    {
        if (host.current(host.context)) return true;
        result = M16ControlResult::Cancelled;
        return false;
    };
    const auto read = [&](M16FrameOwner& owner) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.owner(host.context, owner);
        return current() && accepted;
    };
    const auto emit = [&](M16FrameEvent event, int sequence = 0) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.event(host.context, event, sequence);
        return current() && accepted;
    };
    const auto animation = [&](int sequence, float& duration) noexcept
    {
        if (!emit(M16FrameEvent::ViewAnimation, sequence)) return false;
        const bool accepted = host.duration(host.context, duration);
        if (!current() || !accepted) return false;
        if (std::isfinite(duration) && duration >= 0) return true;
        result = M16ControlResult::InvalidInput;
        return false;
    };
    const auto time = [&]() noexcept { return host.time(host.context); };
    const auto speed = [](const M16FrameOwner& owner) noexcept
    {
        const float squared = owner.velocityX * owner.velocityX + owner.velocityY * owner.velocityY;
        return static_cast<float>(std::sqrt(double(squared)));
    };
    if (state.action == 2 && !state.loading) state.action = 0;
    M16FrameOwner owner;
    if (!read(owner)) return result;
    const float pendingTime = time();
    if (state.pendingReload && !owner.pendingGate && owner.pendingGateTime <= pendingTime &&
        state.primary <= pendingTime && state.secondary <= pendingTime && state.extra <= pendingTime &&
        !state.action)
    {
        if (state.aiming)
        {
            state.aiming = false;
            if (!emit(M16FrameEvent::ExitAim)) return result;
        }
        else
        {
            state.pendingReload = false;
            if (!emit(M16FrameEvent::Reload)) return result;
        }
        if (!read(owner)) return result;
    }
    const float entrySpeed = speed(owner);
    if (state.loading || state.special || state.action == 2 || time() <= state.primary)
        return M16ControlResult::Applied;
    float duration;
    if (owner.condition3AA && !state.aiming && time() > state.extra)
    {
        if (!state.action)
        {
            state.action = 5;
            if (!animation(14, duration)) return result;
            state.primary = static_cast<float>(double(duration) * .5 + double(time()));
            deadline = duration + time();
        }
    }
    else if (state.action == 5)
    {
        if (!animation(16, duration)) return result;
        state.primary = duration + time();
        state.idle = duration + time();
        state.action = 0;
    }
    if (!read(owner)) return result;
    if (entrySpeed > 0 && (owner.flags & 0x40000U) && state.action != 5 && state.action != 1)
    {
        state.action = 1;
        if (!animation(14, duration)) return result;
        state.primary = state.secondary = static_cast<float>(double(duration) * .5 + double(time()));
        deadline = duration + time();
        state.idle = duration * .5F + time();
    }
    if (!read(owner)) return result;
    if ((owner.flags & 0x40000U) && entrySpeed > 0 && state.action == 1 && time() > deadline &&
        speed(owner) != 0)
    {
        if (!animation(15, duration)) return result;
        deadline = duration + time();
    }
    if (!read(owner)) return result;
    if ((!(owner.flags & 0x40000U) || entrySpeed <= 0) && time() > deadline && state.action == 1)
    {
        if (!animation(16, duration)) return result;
        state.primary = state.secondary = duration + time();
        state.idle = duration + time();
        state.action = 0;
    }
    return M16ControlResult::Applied;
}
}
