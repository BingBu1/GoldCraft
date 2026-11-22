// SPDX-License-Identifier: GPL-3.0-or-later
#include "famas_control.hpp"

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
    const FamasControlHost& host;
    bool cancelled{};
    FamasControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event) return FamasControlResult::MissingHost;
        return host.current(host.context) ? FamasControlResult::Applied : FamasControlResult::Cancelled;
    }
    bool Emit(FamasControlEventKind kind, int argument, float delay = 0) noexcept
    {
        if (!host.current(host.context)) { cancelled = true; return false; }
        const bool delivered = host.event(host.context, {kind, argument, delay});
        cancelled = !host.current(host.context);
        return delivered && !cancelled;
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

FamasControlResult StartFamasPushback(MagazineActionState& state, float stamina,
    const FamasControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != FamasControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) ||
        !std::isfinite(state.secondary) || !std::isfinite(stamina))
        return FamasControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.action ||
        state.special || state.aiming || !(stamina > 0)) return FamasControlResult::Ineligible;
    if (!call.Emit(FamasControlEventKind::SchedulePushback, 23, .23F))
        return call.cancelled ? FamasControlResult::Cancelled : FamasControlResult::ConsumerRejected;
    const bool view = call.Emit(FamasControlEventKind::ViewAnimation, 11);
    if (call.cancelled) return FamasControlResult::Cancelled;
    // Original1003C3F5 reads time after scheduling and view animation.
    state.primary = state.secondary = state.idle = call.Time() + 1.F;
    return view ? FamasControlResult::Applied : FamasControlResult::AppliedWithoutView;
}

FamasControlResult StartFamasMagazineReload(MagazineActionState& state,
    const MagazineSequenceTimes& times, const FamasControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != FamasControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) || state.clip < 0)
        return FamasControlResult::InvalidInput;
    // Host skill capacity can exceed the original physical thirty rounds.
    if (state.clip >= host.clipCapacity || state.primary > now || state.action == 1 ||
        state.action > 2 || state.special || state.pendingReload) return FamasControlResult::Ineligible;
    if (state.aiming)
    {
        state.pendingReload = true;
        return FamasControlResult::Deferred;
    }
    const int sequence = state.clip ? 5 : 6;
    if (!(times.present & (std::uint64_t{1} << sequence)) ||
        !std::isfinite(times.duration[sequence]) || times.duration[sequence] < 0)
        return FamasControlResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return FamasControlResult::InvalidInput;
    state.shotPhase = 0;
    state.action = 2;
    state.clip = 0;
    state.ownerAttack = state.primary = state.secondary = state.idle = deadline;
    const bool view = call.Emit(FamasControlEventKind::ViewAnimation, sequence);
    if (call.cancelled) return FamasControlResult::Cancelled;
    state.loading = true;
    return view ? FamasControlResult::Applied : FamasControlResult::AppliedWithoutView;
}
}
