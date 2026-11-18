// SPDX-License-Identifier: GPL-3.0-or-later
#include "famas_frame.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
FamasFrameResult AdvanceFamasDerivedFrame(MagazineActionState& state, float& deadline,
    const FamasFrameHost& host) noexcept
{
    if (!host.current || !host.time || !host.owner) return FamasFrameResult::MissingHost;
    if (!host.current(host.context)) return FamasFrameResult::Cancelled;
    if (state.special || state.action < 0 || (state.action > 2 && state.action != 5))
        return FamasFrameResult::UnsupportedState;
    if (!std::isfinite(host.time(host.context)) || !std::isfinite(state.primary) ||
        !std::isfinite(state.secondary) || !std::isfinite(state.extra) || !std::isfinite(deadline))
        return FamasFrameResult::InvalidInput;
    FamasFrameResult result = FamasFrameResult::InputUnavailable;
    bool partial{};
    const auto finish = [&]() noexcept {
        return partial ? FamasFrameResult::AppliedWithoutPresentation : FamasFrameResult::Applied;
    };
    const auto current = [&]() noexcept {
        if (host.current(host.context)) return true;
        result = FamasFrameResult::Cancelled; return false;
    };
    const auto read = [&](FamasFrameOwner& owner) noexcept {
        if (!current()) return false;
        const bool accepted = host.owner(host.context, owner);
        return current() && accepted;
    };
    const auto emit = [&](FamasFrameEvent event, int sequence = 0) noexcept {
        if (!current()) return false;
        const bool accepted = host.event && host.event(host.context, event, sequence);
        if (!current()) return false;
        partial |= !accepted;
        return accepted;
    };
    const auto animation = [&](int sequence, float& duration) noexcept {
        emit(FamasFrameEvent::ViewAnimation, sequence);
        if (!current()) return false;
        const bool known = host.duration && host.duration(host.context, duration);
        if (!current()) return false;
        if (known && std::isfinite(duration) && duration >= 0) return true;
        partial = true; return false;
    };
    const auto recoverAction = [&](int proposed, int restored) noexcept {
        if (state.action == proposed) state.action = restored;
    };
    const auto time = [&]() noexcept { return host.time(host.context); };
    const auto speed = [](const FamasFrameOwner& owner) noexcept {
        const float squared = owner.velocityX * owner.velocityX + owner.velocityY * owner.velocityY;
        return static_cast<float>(std::sqrt(double(squared)));
    };
    if (state.action == 2 && !state.loading) state.action = 0;
    FamasFrameOwner owner;
    if (!read(owner)) return result;
    if (state.pendingReload && !owner.pendingGate)
    {
        if (!std::isfinite(owner.pendingGateTime)) return FamasFrameResult::InvalidInput;
        const float pendingTime = time();
        if (owner.pendingGateTime <= pendingTime && state.primary <= pendingTime &&
            state.secondary <= pendingTime && state.extra <= pendingTime && !state.action)
        {
            if (state.aiming)
            {
                state.aiming = false;
                if (!emit(FamasFrameEvent::ExitAim))
                {
                    if (!current()) return result;
                    state.aiming = true; return finish();
                }
            }
            else
            {
                state.pendingReload = false;
                if (!emit(FamasFrameEvent::Reload))
                {
                    if (!current()) return result;
                    state.pendingReload = true; return finish();
                }
            }
            if (!read(owner)) return result;
        }
    }
    if (!std::isfinite(owner.velocityX) || !std::isfinite(owner.velocityY)) return FamasFrameResult::InvalidInput;
    const float entrySpeed = speed(owner);
    if (state.loading || state.special || state.action == 2 || time() <= state.primary) return finish();
    float duration{};
    if (owner.condition3AA && !state.aiming && time() > state.extra)
    {
        if (!state.action)
        {
            state.action = 5;
            if (!animation(22, duration))
            {
                if (!current()) return result;
                recoverAction(5, 0); return finish();
            }
            state.primary = static_cast<float>(double(duration) * .5 + double(time()));
            deadline = duration + time();
        }
    }
    else if (state.action == 5)
    {
        if (!animation(23, duration))
        {
            if (!current()) return result;
            recoverAction(5, 0); return finish();
        }
        state.primary = duration + time();
        state.action = 0; state.idle = duration + time();
    }
    if (!read(owner)) return result;
    if (entrySpeed > 0 && (owner.flags & 0x40000U) && state.action != 5 && state.action != 1)
    {
        const int previousAction = state.action;
        state.action = 1;
        if (!animation(8, duration))
        {
            if (!current()) return result;
            recoverAction(1, previousAction); return finish();
        }
        state.primary = static_cast<float>(double(duration) * .5 + double(time()));
        deadline = duration + time();
    }
    if (!read(owner)) return result;
    if ((owner.flags & 0x40000U) && entrySpeed > 0 && state.action == 1 && time() > deadline && speed(owner) != 0)
    {
        if (!animation(9, duration))
        {
            if (!current()) return result;
            return finish();
        }
        deadline = duration + time();
    }
    if (!read(owner)) return result;
    if ((!(owner.flags & 0x40000U) || entrySpeed <= 0) && time() > deadline && state.action == 1)
    {
        // MP1003BAD9 discards the actual duration; the stop clocks are constants.
        animation(10, duration);
        if (!current()) return result;
        state.primary = static_cast<float>(double(time()) + .33);
        state.action = 0;
        state.idle = static_cast<float>(double(time()) + .35);
    }
    return finish();
}
}
