// SPDX-License-Identifier: GPL-3.0-or-later
#include "magazine_actions.hpp"
#include "server/player_animation.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
constexpr bool Supported(Pistol pistol) noexcept
{
    return pistol == Pistol::Glock || pistol == Pistol::P345 || pistol == Pistol::VP70;
}
constexpr int Capacity(Pistol pistol) noexcept
{
    return pistol == Pistol::Glock ? 15 : pistol == Pistol::P345 ? 8 : 18;
}
constexpr int OriginalId(Pistol pistol) noexcept
{
    return pistol == Pistol::Glock ? 3 : pistol == Pistol::P345 ? 7 : 19;
}
bool Valid(const MagazineActionState& state, float now) noexcept
{
    return std::isfinite(now) && state.clip >= 0 &&
        state.reserve >= 0 && state.action >= 0 && state.special >= 0 &&
        state.shotPhase >= 0 && state.burstShots >= 0 &&
        std::isfinite(state.primary) && std::isfinite(state.secondary) &&
        std::isfinite(state.extra) && std::isfinite(state.idle) && std::isfinite(state.ownerAttack);
}
bool HasSequence(const MagazineSequenceTimes& times, int sequence) noexcept
{
    return static_cast<unsigned>(sequence) < times.duration.size() &&
        (times.present & (std::uint64_t{1} << sequence)) &&
        std::isfinite(times.duration[sequence]) && times.duration[sequence] >= 0;
}
MagazineActionResult CheckHost(const MagazineActionHost& host) noexcept
{
    if (!host.current || !host.event) return MagazineActionResult::MissingHost;
    return host.current(host.context) ? MagazineActionResult::Applied : MagazineActionResult::Cancelled;
}
bool Emit(const MagazineActionHost& host, MagazineEvent event) noexcept
{
    return host.current(host.context) && host.event(host.context, event) && host.current(host.context);
}
}

MagazineActionResult StartMagazineReload(Pistol pistol, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept
{
    if (!Supported(pistol)) return MagazineActionResult::UnsupportedWeapon;
    if (!Valid(state, now)) return MagazineActionResult::InvalidInput;
    const auto ready = CheckHost(host);
    if (ready != MagazineActionResult::Applied) return ready;
    // A host skill may have granted an overfilled magazine. Keep those rounds
    // until used; a physical-magazine reload cannot replace them with fewer.
    if (state.clip > Capacity(pistol)) return MagazineActionResult::Ineligible;
    if (state.reserve <= 0 || state.primary > now || state.action == 1 ||
        state.action > 2 || state.special || state.pendingReload)
        return MagazineActionResult::Ineligible;

    // VP70 tests its secondary clock and full magazine before queuing the
    // aimed reload. Glock/P345 queue even when their magazine is full.
    if (pistol == Pistol::VP70 && (state.secondary > now || state.clip == Capacity(pistol)))
        return MagazineActionResult::Ineligible;
    if (state.aiming)
    {
        state.pendingReload = true;
        return MagazineActionResult::Deferred;
    }
    if (state.clip == Capacity(pistol)) return MagazineActionResult::Ineligible;

    const bool loaded = state.clip != 0;
    const int sequence = (pistol == Pistol::VP70 ? 6 : 4) + !loaded;
    if (!HasSequence(times, sequence)) return MagazineActionResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return MagazineActionResult::InvalidInput;

    state.action = 2;
    if (pistol == Pistol::Glock) state.shotPhase = 0;
    if (pistol == Pistol::VP70)
    {
        state.burstShots = 0;
        if (!Emit(host, {loaded ? MagazineEventKind::PlayerReloadLoaded :
            MagazineEventKind::PlayerReloadEmpty, OriginalId(pistol)}))
            return MagazineActionResult::Cancelled;
    }
    state.clip = 0;
    state.ownerAttack = state.primary = state.secondary = state.idle = deadline;
    if (!Emit(host, {MagazineEventKind::ViewAnimation, sequence}))
        return MagazineActionResult::Cancelled;
    state.loading = true;
    if (pistol != Pistol::VP70 &&
        !Emit(host, {loaded ? MagazineEventKind::PlayerReloadLoaded :
            MagazineEventKind::PlayerReloadEmpty, OriginalId(pistol)}))
        return MagazineActionResult::Cancelled;
    return MagazineActionResult::Applied;
}

MagazineActionResult StartAutomaticMagazineReload(server::LongGun weapon, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept
{
    if (weapon != server::LongGun::TMP && weapon != server::LongGun::MP5)
        return MagazineActionResult::UnsupportedWeapon;
    if (!Valid(state, now)) return MagazineActionResult::InvalidInput;
    const auto ready = CheckHost(host);
    if (ready != MagazineActionResult::Applied) return ready;
    // Preserve host skill grants instead of discarding an overfilled magazine.
    if (state.clip >= 30 || state.reserve <= 0 || state.primary > now ||
        state.action == 1 || state.action > 2 || state.special || state.pendingReload)
        return MagazineActionResult::Ineligible;
    if (state.aiming)
    {
        state.pendingReload = true;
        return MagazineActionResult::Deferred;
    }
    const bool loaded = state.clip != 0;
    const int sequence = (weapon == server::LongGun::TMP ? 5 : 2) + !loaded;
    if (!HasSequence(times, sequence)) return MagazineActionResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return MagazineActionResult::InvalidInput;

    state.shotPhase = 0;
    state.action = 2;
    state.clip = 0;
    state.ownerAttack = state.primary = state.secondary = state.idle = deadline;
    if (!Emit(host, {MagazineEventKind::ViewAnimation, sequence}))
        return MagazineActionResult::Cancelled;
    state.loading = true;
    if (!Emit(host, {loaded ? MagazineEventKind::PlayerReloadLoaded : MagazineEventKind::PlayerReloadEmpty,
        weapon == server::LongGun::TMP ? 14 : 27})) return MagazineActionResult::Cancelled;
    return MagazineActionResult::Applied;
}

MagazineActionResult StartPistolMelee(Pistol pistol, MagazineActionState& state,
    float now, const MagazineActionHost& host) noexcept
{
    if (!Supported(pistol)) return MagazineActionResult::UnsupportedWeapon;
    if (!Valid(state, now) || !std::isfinite(now + 1.0f))
        return MagazineActionResult::InvalidInput;
    const auto ready = CheckHost(host);
    if (ready != MagazineActionResult::Applied) return ready;
    if (state.primary > now || state.secondary > now || state.action || state.special || state.aiming)
        return MagazineActionResult::Ineligible;
    if (!Emit(host, {MagazineEventKind::SchedulePlayerMelee, OriginalId(pistol), 0.3f}) ||
        !Emit(host, {MagazineEventKind::PlayerMelee, OriginalId(pistol)}))
        return MagazineActionResult::Cancelled;
    const int sequence = pistol == Pistol::VP70 ? 21 :
        (pistol == Pistol::Glock ? 27 : 17) + (state.clip == 0);
    if (!Emit(host, {MagazineEventKind::ViewAnimation, sequence}))
        return MagazineActionResult::Cancelled;
    state.primary = state.secondary = state.idle = now + 1.0f;
    return MagazineActionResult::Applied;
}

MagazineActionResult MagazineIdle(Pistol pistol, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept
{
    if (!Supported(pistol)) return MagazineActionResult::UnsupportedWeapon;
    if (!Valid(state, now)) return MagazineActionResult::InvalidInput;
    const auto ready = CheckHost(host);
    if (ready != MagazineActionResult::Applied) return ready;
    if (pistol == Pistol::VP70) return MagazineActionResult::Ineligible;
    if (state.idle > now || state.action || state.special ||
        (pistol == Pistol::P345 && (state.primary > now || state.secondary > now)))
        return MagazineActionResult::Ineligible;
    const int sequence = (state.aiming ? (pistol == Pistol::Glock ? 32 : 21) : 0) + (state.clip == 0);
    if (!HasSequence(times, sequence)) return MagazineActionResult::MissingSequence;
    const float deadline = now + times.duration[sequence];
    if (!std::isfinite(deadline)) return MagazineActionResult::InvalidInput;
    if (!Emit(host, {MagazineEventKind::ViewAnimation, sequence}))
        return MagazineActionResult::Cancelled;
    state.idle = deadline;
    return MagazineActionResult::Applied;
}
}
