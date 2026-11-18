// SPDX-License-Identifier: GPL-3.0-or-later
#include "famas_aim.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Valid(const FamasAimMetadata& metadata) noexcept
{
    return std::isfinite(metadata.offset[0]) && std::isfinite(metadata.offset[1]) &&
        std::isfinite(metadata.offset[2]);
}
struct Call
{
    const FamasAimHost& host;
    bool cancelled{}, presentation{true};
    FamasControlResult Ready() const noexcept
    {
        if (!host.current || !host.time) return FamasControlResult::MissingHost;
        return host.current(host.context) ? FamasControlResult::Applied : FamasControlResult::Cancelled;
    }
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { cancelled = true; return false; }
        const bool accepted = f();
        cancelled = !host.current(host.context);
        return accepted && !cancelled;
    }
    bool Emit(const FamasAimEvent& event) noexcept
    {
        const bool delivered = Run([&]() noexcept {
            return host.event && host.event(host.context, event);
        });
        presentation &= delivered;
        return !cancelled;
    }
};
}

FamasControlResult ApplyFamasAim(MagazineActionState& state, bool enabled,
    const FamasAimMetadata& metadata, const FamasAimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != FamasControlResult::Applied) return ready;
    if (!std::isfinite(host.time(host.context)) || !Valid(metadata)) return FamasControlResult::InvalidInput;
    FamasAimEvent presentation{FamasAimEventKind::Presentation};
    presentation.offset = enabled ? metadata.offset : std::array<float, 3>{};
    presentation.value = enabled ? metadata.zoom : 0;
    presentation.enabled = enabled;
    if (!call.Emit(presentation)) return FamasControlResult::Cancelled;
    FamasAimEvent zoom{FamasAimEventKind::CameraZoom}; zoom.amount = 350;
    if (!call.Emit(zoom)) return FamasControlResult::Cancelled;
    FamasAimEvent fov{FamasAimEventKind::PlayerFov}; fov.amount = enabled ? float(metadata.zoom) : 90.F;
    if (!call.Emit(fov)) return FamasControlResult::Cancelled;
    FamasAimEvent view{FamasAimEventKind::ViewAnimation}; view.value = enabled ? 15 : 18;
    if (!call.Emit(view)) return FamasControlResult::Cancelled;
    state.aiming = enabled;
    state.primary = state.secondary = state.extra = static_cast<float>(double(host.time(host.context)) + .3);
    float duration{};
    const bool known = call.Run([&]() noexcept {
        return host.duration && host.duration(host.context, duration);
    });
    if (call.cancelled) return FamasControlResult::Cancelled;
    if (known && std::isfinite(duration) && duration >= 0)
        state.idle = duration + host.time(host.context);
    else call.presentation = false;
    return call.presentation ? FamasControlResult::Applied : FamasControlResult::AppliedWithoutView;
}

FamasControlResult ToggleFamasAim(MagazineActionState& state, bool pressed, bool toggleMode,
    const FamasAimMetadata& metadata, const FamasAimHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != FamasControlResult::Applied) return ready;
    const float now = host.time(host.context);
    if (!std::isfinite(now) || !std::isfinite(state.primary) || !std::isfinite(state.secondary) ||
        !std::isfinite(state.extra) || !Valid(metadata)) return FamasControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.extra > now || state.action ||
        state.special || !pressed || (!toggleMode && state.aiming)) return FamasControlResult::Ineligible;
    state.aiming = !state.aiming;
    return ApplyFamasAim(state, state.aiming, metadata, host);
}
}
