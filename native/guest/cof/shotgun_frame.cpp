// SPDX-License-Identifier: GPL-3.0-or-later
#include "shotgun_frame.hpp"
#include "shotgun_shot.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
ShotgunControlResult AdvanceShotgunDerivedFrame(ShotgunShotState& shot, float& deadline,
    const ShotgunFrameHost& host) noexcept
{
    if (!host.current || !host.time || !host.owner || !host.event || !host.duration || !host.control)
        return ShotgunControlResult::MissingHost;
    if (!host.current(host.context)) return ShotgunControlResult::Cancelled;
    auto& state = shot.action;
    if (state.special || state.action < 0 || (state.action > 2 && state.action != 5))
        return ShotgunControlResult::UnsupportedState;
    const auto time = [&]() noexcept { return host.time(host.context); };
    if (!std::isfinite(time()) || !std::isfinite(state.primary) || !std::isfinite(state.secondary) ||
        !std::isfinite(state.extra) || !std::isfinite(state.idle) || !std::isfinite(deadline))
        return ShotgunControlResult::InvalidInput;
    ShotgunControlResult result = ShotgunControlResult::ConsumerRejected;
    const auto current = [&]() noexcept
    {
        if (host.current(host.context)) return true;
        result = ShotgunControlResult::Cancelled; return false;
    };
    const auto read = [&](ShotgunFrameOwner& owner) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.owner(host.context, owner);
        if (!current() || !accepted) return false;
        if (std::isfinite(owner.velocityX) && std::isfinite(owner.velocityY) &&
            std::isfinite(owner.pendingGateTime)) return true;
        result = ShotgunControlResult::InvalidInput; return false;
    };
    const auto emit = [&](ShotgunFrameEvent event, int sequence = 0) noexcept
    {
        if (!current()) return false;
        const bool accepted = host.event(host.context, event, sequence);
        return current() && accepted;
    };
    const auto animation = [&](int sequence, float& duration) noexcept
    {
        if (!emit(ShotgunFrameEvent::ViewAnimation, sequence)) return false;
        const bool accepted = host.duration(host.context, duration);
        if (!current() || !accepted) return false;
        if (std::isfinite(duration) && duration >= 0) return true;
        result = ShotgunControlResult::MissingSequence; return false;
    };
    const auto speed = [](const ShotgunFrameOwner& owner) noexcept
    {
        const float squared = owner.velocityX * owner.velocityX + owner.velocityY * owner.velocityY;
        return static_cast<float>(std::sqrt(double(squared)));
    };
    ShotgunFrameOwner owner;
    if (!read(owner)) return result;
    ObserveShotgunReloadAttack(state, (owner.pressed & 1U) != 0);
    const float pendingTime = time();
    if (state.pendingReload && !state.reloadStage && !owner.pendingGate &&
        owner.pendingGateTime <= pendingTime && state.primary <= pendingTime &&
        state.secondary <= pendingTime && state.extra <= pendingTime && !state.action && !state.special)
    {
        if (state.aiming)
        {
            state.aiming = false;
            if (!emit(ShotgunFrameEvent::ExitAim)) return result;
        }
        else
        {
            state.pendingReload = false;
            if (!emit(ShotgunFrameEvent::Reload)) return result;
        }
    }
    if (time() > state.idle && state.reloadStage)
    {
        if (!state.reloadAbort && state.clip != 5 && state.reserve)
        {
            if (!emit(ShotgunFrameEvent::Reload)) return result;
        }
        else
        {
            const ShotgunControlHost controls{host.context, host.current, host.time, host.control};
            const auto finished = AdvanceShotgunReload(state, controls);
            if (finished != ShotgunControlResult::Applied) return finished;
        }
    }
    if (!read(owner)) return result;
    const float entrySpeed = speed(owner);
    if (!shot.shotGate && !state.special && state.action != 2 && time() > state.primary)
    {
        float duration;
        if (owner.condition3AA && !state.aiming && time() > state.extra)
        {
            if (!state.action)
            {
                state.action = 5;
                if (!animation(8, duration)) return result;
                state.primary = static_cast<float>(double(duration) * .5 + double(time()));
                deadline = duration + time();
            }
        }
        else if (state.action == 5)
        {
            if (!animation(10, duration)) return result;
            state.primary = duration + time();
            const float idle = duration + time();
            state.action = 0; state.idle = idle;
        }
        if (!read(owner)) return result;
        if (entrySpeed > 0 && (owner.flags & 0x40000U) && state.action != 5 && state.action != 1)
        {
            state.action = 1;
            if (!animation(8, duration)) return result;
            state.primary = static_cast<float>(double(duration) * .5 + double(time()));
            deadline = duration + time();
        }
        if (!read(owner)) return result;
        if ((owner.flags & 0x40000U) && entrySpeed > 0 && state.action == 1 && time() > deadline && speed(owner) != 0)
        {
            if (!animation(9, duration)) return result;
            deadline = duration + time();
        }
        if (!read(owner)) return result;
        if ((!(owner.flags & 0x40000U) || entrySpeed <= 0) && time() > deadline && state.action == 1)
        {
            if (!animation(10, duration)) return result;
            state.primary = duration + time();
            state.action = 0;
        }
    }
    return emit(ShotgunFrameEvent::CommonFrame) ? ShotgunControlResult::Applied : result;
}
}
