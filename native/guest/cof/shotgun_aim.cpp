// SPDX-License-Identifier: GPL-3.0-or-later
#include "shotgun_aim.hpp"

#include <bit>
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
struct Call
{
    const ShotgunAimHost& host;
    ShotgunControlResult result{ShotgunControlResult::ConsumerRejected};
    ShotgunControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event || !host.duration)
            return ShotgunControlResult::MissingHost;
        return host.current(host.context) ? ShotgunControlResult::Applied : ShotgunControlResult::Cancelled;
    }
    template<class F> bool Run(F&& callback) noexcept
    {
        if (!host.current(host.context)) { result = ShotgunControlResult::Cancelled; return false; }
        const bool accepted = callback();
        if (!host.current(host.context)) { result = ShotgunControlResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const ShotgunAimEvent& event) noexcept
    { return Run([&]() noexcept { return host.event(host.context, event); }); }
    float Time() const noexcept { return host.time(host.context); }
    bool Duration(float& duration) noexcept
    {
        if (!Run([&]() noexcept { return host.duration(host.context, duration); })) return false;
        if (std::isfinite(duration) && duration >= 0) return true;
        result = ShotgunControlResult::MissingSequence; return false;
    }
};
}

ShotgunControlResult ApplyShotgunAim(ShotgunActionState& state, bool enabled,
    const ShotgunAimMetadata& metadata, const ShotgunAimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    if (!std::isfinite(call.Time()) || !std::isfinite(metadata.offset[0]) ||
        !std::isfinite(metadata.offset[1]) || !std::isfinite(metadata.offset[2]))
        return ShotgunControlResult::InvalidInput;
    ShotgunAimEvent presentation{ShotgunAimEventKind::Presentation};
    presentation.offset = enabled ? metadata.offset : std::array<float, 3>{};
    presentation.value = enabled ? metadata.zoom : 0;
    presentation.enabled = enabled;
    if (!call.Emit(presentation)) return call.result;
    ShotgunAimEvent zoom{ShotgunAimEventKind::CameraZoom}; zoom.amount = 350;
    if (!call.Emit(zoom)) return call.result;
    ShotgunAimEvent fov{ShotgunAimEventKind::PlayerFov}; fov.amount = enabled ? float(metadata.zoom) : 90.F;
    if (!call.Emit(fov)) return call.result;
    ShotgunAimEvent view{ShotgunAimEventKind::ViewAnimation}; view.value = enabled ? 14 : 17;
    if (!call.Emit(view)) return call.result;
    state.aiming = enabled;
    state.primary = state.secondary = state.extra = static_cast<float>(double(call.Time()) + .3);
    float duration{};
    if (!call.Duration(duration)) return call.result;
    state.idle = duration + call.Time();
    return ShotgunControlResult::Applied;
}

ShotgunControlResult ToggleShotgunAim(ShotgunActionState& state, bool pressed, bool toggleMode,
    const ShotgunAimMetadata& metadata, const ShotgunAimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) || !std::isfinite(state.secondary) ||
        !std::isfinite(state.extra)) return ShotgunControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.extra > now ||
        state.action || state.special || !pressed || (!toggleMode && state.aiming))
        return ShotgunControlResult::Ineligible;
    state.aiming = !state.aiming;
    return ApplyShotgunAim(state, state.aiming, metadata, host);
}

ShotgunControlResult ShotgunIdle(ShotgunActionState& state, const ShotgunAimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    if (!host.autoAim) return ShotgunControlResult::MissingHost;
    if (!call.Run([&]() noexcept { return host.autoAim(host.context,
        std::bit_cast<float>(std::uint32_t{0x3DB27EB6})); })) return call.result;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.idle) || !std::isfinite(state.primary) ||
        !std::isfinite(state.secondary)) return ShotgunControlResult::InvalidInput;
    if (state.idle > now || state.primary > now || state.secondary > now || state.action || state.special)
        return ShotgunControlResult::Ineligible;
    ShotgunAimEvent view{ShotgunAimEventKind::ViewAnimation}; view.value = state.aiming ? 15 : 0;
    if (!call.Emit(view)) return call.result;
    float duration{};
    if (!call.Duration(duration)) return call.result;
    state.idle = duration + call.Time();
    return ShotgunControlResult::Applied;
}
}
