// SPDX-License-Identifier: GPL-3.0-or-later
#include "g43_aim.hpp"

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
    const G43AimHost& host;
    G43ControlResult result{G43ControlResult::ConsumerRejected};
    G43ControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event || !host.duration) return G43ControlResult::MissingHost;
        return host.current(host.context) ? G43ControlResult::Applied : G43ControlResult::Cancelled;
    }
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = G43ControlResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = G43ControlResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const G43AimEvent& event) noexcept
    { return Run([&]() noexcept { return host.event(host.context, event); }); }
};
}

G43ControlResult ApplyG43Aim(MagazineActionState& state, bool enabled,
    const G43AimMetadata& metadata, const G43AimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != G43ControlResult::Applied) return ready;
    if (!std::isfinite(host.time(host.context)) || !std::isfinite(metadata.offset[0]) ||
        !std::isfinite(metadata.offset[1]) || !std::isfinite(metadata.offset[2]))
        return G43ControlResult::InvalidInput;
    G43AimEvent presentation{G43AimEventKind::Presentation};
    presentation.offset = enabled ? metadata.offset : std::array<float, 3>{};
    presentation.value = enabled ? metadata.zoom : 0;
    presentation.enabled = enabled;
    if (!call.Emit(presentation)) return call.result;
    G43AimEvent zoom{G43AimEventKind::CameraZoom}; zoom.amount = 350;
    if (!call.Emit(zoom)) return call.result;
    G43AimEvent fov{G43AimEventKind::PlayerFov}; fov.amount = enabled ? float(metadata.zoom) : 90.F;
    if (!call.Emit(fov)) return call.result;
    G43AimEvent view{G43AimEventKind::ViewAnimation};
    view.value = enabled ? (state.clip ? 20 : 21) : (state.clip ? 16 : 17);
    if (!call.Emit(view)) return call.result;
    state.aiming = enabled;
    state.primary = state.secondary = state.extra = static_cast<float>(double(host.time(host.context)) + .3);
    float duration{};
    if (!call.Run([&]() noexcept { return host.duration(host.context, duration); })) return call.result;
    if (!std::isfinite(duration) || duration < 0) return G43ControlResult::MissingSequence;
    state.idle = duration + host.time(host.context);
    return G43ControlResult::Applied;
}

G43ControlResult ToggleG43Aim(MagazineActionState& state, bool pressed, bool toggleMode,
    const G43AimMetadata& metadata, const G43AimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != G43ControlResult::Applied) return ready;
    const float now = host.time(host.context);
    if (!std::isfinite(now) || !std::isfinite(state.primary) || !std::isfinite(state.secondary) ||
        !std::isfinite(state.extra)) return G43ControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.extra > now ||
        state.action || state.special || !pressed || (!toggleMode && state.aiming))
        return G43ControlResult::Ineligible;
    state.aiming = !state.aiming;
    return ApplyG43Aim(state, state.aiming, metadata, host);
}
}
