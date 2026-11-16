// SPDX-License-Identifier: GPL-3.0-or-later
#include "g43_frame.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
G43FrameResult AdvanceG43DerivedFrame(MagazineActionState& state, float& deadline,
    const G43FrameHost& host) noexcept
{
    if (!host.current || !host.time || !host.owner || !host.event || !host.duration)
        return G43FrameResult::MissingHost;
    if (!host.current(host.context)) return G43FrameResult::Cancelled;
    if (state.special || state.action < 0 || (state.action > 2 && state.action != 5))
        return G43FrameResult::UnsupportedState;
    if (!std::isfinite(host.time(host.context)) || !std::isfinite(state.primary) ||
        !std::isfinite(state.secondary) || !std::isfinite(state.extra) || !std::isfinite(deadline))
        return G43FrameResult::InvalidInput;
    G43FrameResult result = G43FrameResult::ConsumerRejected;
    const auto current = [&]() noexcept
    {
        if (host.current(host.context)) return true;
        result = G43FrameResult::Cancelled; return false;
    };
    const auto read = [&](G43FrameOwner& owner) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.owner(host.context, owner);
        if (!current() || !accepted) return false;
        if (std::isfinite(owner.velocityX) && std::isfinite(owner.velocityY) &&
            std::isfinite(owner.pendingGateTime)) return true;
        result = G43FrameResult::InvalidInput; return false;
    };
    const auto emit = [&](G43FrameEvent event, int sequence = 0) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.event(host.context, event, sequence);
        return current() && accepted;
    };
    const auto animation = [&](int sequence, float& duration) noexcept
    {
        if (!emit(G43FrameEvent::ViewAnimation, sequence)) return false;
        const bool accepted = host.duration(host.context, duration);
        if (!current() || !accepted) return false;
        if (std::isfinite(duration) && duration >= 0) return true;
        result = G43FrameResult::InvalidInput; return false;
    };
    const auto time = [&]() noexcept { return host.time(host.context); };
    const auto speed = [](const G43FrameOwner& owner) noexcept
    {
        const float squared = owner.velocityX * owner.velocityX + owner.velocityY * owner.velocityY;
        return static_cast<float>(std::sqrt(double(squared)));
    };
    if (state.action == 2 && !state.loading) state.action = 0;
    G43FrameOwner owner;
    if (!read(owner)) return result;
    const float pendingTime = time();
    if (state.pendingReload && !owner.pendingGate && owner.pendingGateTime <= pendingTime &&
        state.primary <= pendingTime && state.secondary <= pendingTime && state.extra <= pendingTime && !state.action)
    {
        if (state.aiming)
        {
            state.aiming = false;
            if (!emit(G43FrameEvent::ExitAim)) return result;
        }
        else
        {
            state.pendingReload = false;
            if (!emit(G43FrameEvent::Reload)) return result;
        }
        if (!read(owner)) return result;
    }
    if (state.special) return G43FrameResult::UnsupportedState;
    if (!state.action && time() > state.primary && time() > state.secondary && time() > state.extra &&
        !owner.toggleAim && state.aiming && !((owner.pressed | owner.released | owner.held) & 0x40U))
    {
        state.aiming = false;
        if (!emit(G43FrameEvent::ExitAim) || !read(owner)) return result;
    }
    const float entrySpeed = speed(owner);
    if (state.loading || state.action == 2 || time() <= state.primary) return G43FrameResult::Applied;
    float duration;
    if (owner.condition3AA && !state.aiming && time() > state.extra)
    {
        if (!state.action)
        {
            state.action = 5;
            if (!animation(state.clip ? 34 : 35, duration)) return result;
            state.primary = static_cast<float>(double(duration) * .5 + double(time()));
            deadline = duration + time();
        }
    }
    else if (state.action == 5)
    {
        if (!animation(state.clip ? 32 : 33, duration)) return result;
        state.primary = duration + time();
        state.idle = duration + time(); state.action = 0;
    }
    if (!read(owner)) return result;
    if (entrySpeed > 0 && (owner.flags & 0x40000U) && state.action != 5 && state.action != 1)
    {
        state.action = 1;
        if (!animation(state.clip ? 34 : 35, duration)) return result;
        state.primary = static_cast<float>(double(duration) * .5 + double(time()));
        deadline = duration + time();
    }
    if (!read(owner)) return result;
    if ((owner.flags & 0x40000U) && entrySpeed > 0 && state.action == 1 && time() > deadline && speed(owner) != 0)
    {
        if (!animation(state.clip ? 30 : 31, duration)) return result;
        deadline = duration + time();
    }
    if (!read(owner)) return result;
    if ((!(owner.flags & 0x40000U) || entrySpeed <= 0) && time() > deadline && state.action == 1)
    {
        if (!animation(state.clip ? 32 : 33, duration)) return result;
        state.primary = duration + time();
        state.idle = duration + time(); state.action = 0;
    }
    return G43FrameResult::Applied;
}
}
