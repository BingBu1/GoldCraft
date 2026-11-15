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
}
