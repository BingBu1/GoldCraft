// SPDX-License-Identifier: GPL-3.0-or-later
#include "g43_control.hpp"

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
    const G43ControlHost& host;
    G43ControlResult result{G43ControlResult::ConsumerRejected};
    G43ControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event) return G43ControlResult::MissingHost;
        return host.current(host.context) ? G43ControlResult::Applied : G43ControlResult::Cancelled;
    }
    bool Emit(G43ControlEventKind kind, int argument = 0, float delay = 0) noexcept
    {
        if (!host.current(host.context)) { result = G43ControlResult::Cancelled; return false; }
        const bool accepted = host.event(host.context, {kind, argument, delay});
        if (!host.current(host.context)) { result = G43ControlResult::Cancelled; return false; }
        return accepted;
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

G43ControlResult StartG43Pushback(MagazineActionState& state, float stamina,
    const G43ControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != G43ControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) ||
        !std::isfinite(state.secondary) || !std::isfinite(stamina))
        return G43ControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.action ||
        state.special || state.aiming || !(stamina > 0)) return G43ControlResult::Ineligible;
    if (!call.Emit(G43ControlEventKind::SchedulePushback, 26, .23F) ||
        !call.Emit(G43ControlEventKind::ViewAnimation, state.clip ? 26 : 27) ||
        !call.Emit(G43ControlEventKind::PlayerWeaponMelee, 26)) return call.result;
    // Original10044F10 reads the clock after both animation consumers.
    state.primary = state.secondary = state.idle = call.Time() + 1.F;
    return G43ControlResult::Applied;
}

G43ControlResult StartG43MagazineReload(MagazineActionState& state,
    const MagazineSequenceTimes& times, const G43ControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != G43ControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) || state.clip < 0 || state.action < 0)
        return G43ControlResult::InvalidInput;
    // Host adaptation: preserve rounds granted by CSOL character skills.
    if (state.clip > 10 || state.reserve <= 0 || state.primary > now ||
        state.action == 1 || state.action > 2 || state.special || state.pendingReload)
        return G43ControlResult::Ineligible;
    if (state.aiming)
    {
        state.pendingReload = true;
        return G43ControlResult::Deferred;
    }
    if (state.clip == 10) return G43ControlResult::Ineligible;
    const bool loaded = state.clip != 0;
    const int sequence = loaded ? 29 : 28;
    if (!(times.present & (std::uint64_t{1} << sequence)) ||
        !std::isfinite(times.duration[sequence]) || times.duration[sequence] < 0)
        return G43ControlResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return G43ControlResult::InvalidInput;
    state.action = 2;
    state.clip = 0;
    state.ownerAttack = state.primary = state.secondary = state.idle = deadline;
    if (!call.Emit(G43ControlEventKind::ViewAnimation, sequence)) return call.result;
    state.loading = true;
    if (!call.Emit(loaded ? G43ControlEventKind::PlayerReloadLoaded :
        G43ControlEventKind::PlayerReloadEmpty, 26)) return call.result;
    return G43ControlResult::Applied;
}
}
