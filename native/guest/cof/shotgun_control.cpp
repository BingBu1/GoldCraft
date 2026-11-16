// SPDX-License-Identifier: GPL-3.0-or-later
#include "shotgun_control.hpp"

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
    const ShotgunControlHost& host;
    ShotgunControlResult result{ShotgunControlResult::ConsumerRejected};
    ShotgunControlResult Ready() const noexcept
    {
        if (!host.current || !host.time || !host.event) return ShotgunControlResult::MissingHost;
        return host.current(host.context) ? ShotgunControlResult::Applied : ShotgunControlResult::Cancelled;
    }
    float Time() const noexcept { return host.time(host.context); }
    bool Emit(ShotgunControlEventKind kind, int sequence = 0, float delay = 0) noexcept
    {
        if (!host.current(host.context)) { result = ShotgunControlResult::Cancelled; return false; }
        const bool accepted = host.event(host.context, {kind, sequence, delay});
        if (!host.current(host.context)) { result = ShotgunControlResult::Cancelled; return false; }
        return accepted;
    }
};
float After(float time, double delay) noexcept { return static_cast<float>(double(time) + delay); }
}

ShotgunControlResult StartShotgunReload(ShotgunActionState& state, const ShotgunControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    if (!std::isfinite(call.Time()) || !std::isfinite(state.primary)) return ShotgunControlResult::InvalidInput;
    if (state.reserve <= 0 || state.clip == 5) return ShotgunControlResult::Ineligible;
    // Original sets this even when a later action/timer/aim gate rejects reload.
    if (!state.clip) state.emptyStart = 1;
    if (state.action == 1 || state.action > 2 || state.special || state.primary > call.Time() || state.pendingReload)
        return ShotgunControlResult::Ineligible;
    if (state.aiming)
    {
        state.pendingReload = true;
        return ShotgunControlResult::Deferred;
    }
    const int stage = state.reloadStage;
    state.action = 2;
    if (!stage)
    {
        state.reloadAbort = 0;
        if (!call.Emit(ShotgunControlEventKind::ViewAnimation, 2)) return call.result;
        state.reloadStage = 1;
        state.ownerAttack = After(call.Time(), .5);
        state.idle = After(call.Time(), .5);
        state.primary = After(call.Time(), .51);
        if (!call.Emit(ShotgunControlEventKind::PlayerReloadStart)) return call.result;
        state.playerAnimationDeadline = call.Time() + 1.F;
        return ShotgunControlResult::Applied;
    }
    if (stage != 1) return ShotgunControlResult::Deferred;
    if (!std::isfinite(state.idle)) return ShotgunControlResult::InvalidInput;
    if (state.idle > call.Time()) return ShotgunControlResult::Deferred;
    state.reloadStage = 2;
    if (!call.Emit(ShotgunControlEventKind::ViewAnimation, 3) ||
        !call.Emit(ShotgunControlEventKind::PlayerReloadInsert)) return call.result;
    state.playerAnimationDeadline = call.Time() + 1.F;
    state.insertDeadline = After(call.Time(), .6);
    state.idle = After(call.Time(), .65);
    if (!call.Emit(ShotgunControlEventKind::ScheduleShellInsert, 0, .33F)) return call.result;
    return ShotgunControlResult::Applied;
}

void InsertShotgunShell(ShotgunActionState& state) noexcept
{
    state.clip = std::bit_cast<std::int32_t>(std::uint32_t(state.clip) + 1U);
    state.reserve = std::bit_cast<std::int32_t>(std::uint32_t(state.reserve) - 1U);
    state.reloadStage = 1;
}

void ObserveShotgunReloadAttack(ShotgunActionState& state, bool attackPressed) noexcept
{
    if (state.reloadStage && attackPressed && !state.reloadAbort && state.clip >= 1) state.reloadAbort = 1;
}

ShotgunControlResult AdvanceShotgunReload(ShotgunActionState& state, const ShotgunControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.idle)) return ShotgunControlResult::InvalidInput;
    if (!(now > state.idle) || !state.reloadStage) return ShotgunControlResult::Deferred;
    if (!state.reloadAbort && state.clip != 5 && state.reserve) return StartShotgunReload(state, host);
    if (state.emptyStart)
    {
        if (!call.Emit(ShotgunControlEventKind::ViewAnimation, 5)) return call.result;
        state.primary = After(call.Time(), .6);
        if (!call.Emit(ShotgunControlEventKind::PlayerReloadEndEmpty)) return call.result;
    }
    else if (!state.reloadAbort)
    {
        // The completed loaded path reverses the other two callback orders.
        if (!call.Emit(ShotgunControlEventKind::PlayerReloadEndLoaded) ||
            !call.Emit(ShotgunControlEventKind::ViewAnimation, 4)) return call.result;
        state.primary = After(call.Time(), .5);
    }
    else
    {
        if (!call.Emit(ShotgunControlEventKind::ViewAnimation, 4)) return call.result;
        state.primary = After(call.Time(), .5);
        if (!call.Emit(ShotgunControlEventKind::PlayerReloadEndAbort)) return call.result;
    }
    state.action = state.emptyStart = state.reloadStage = 0;
    state.idle = call.Time() + 1.F;
    state.reloadAbort = 0;
    return ShotgunControlResult::Applied;
}

ShotgunControlResult StartShotgunPushback(ShotgunActionState& state, float stamina,
    const ShotgunControlHost& host) noexcept
{
    Call call{host};
    const auto ready = call.Ready();
    if (ready != ShotgunControlResult::Applied) return ready;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.primary) || !std::isfinite(state.secondary) || !std::isfinite(stamina))
        return ShotgunControlResult::InvalidInput;
    if (state.primary > now || state.secondary > now || state.action || state.special || state.aiming || !(stamina > 0))
        return ShotgunControlResult::Ineligible;
    if (!call.Emit(ShotgunControlEventKind::SchedulePushback, 0, .23F) ||
        !call.Emit(ShotgunControlEventKind::ViewAnimation, 18) ||
        !call.Emit(ShotgunControlEventKind::PlayerWeaponMelee)) return call.result;
    state.primary = state.secondary = state.idle = call.Time() + 1.F;
    return ShotgunControlResult::Applied;
}
}
