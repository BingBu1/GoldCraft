// SPDX-License-Identifier: GPL-3.0-or-later
#include "shotgun_shot.hpp"

#include <bit>
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Finite(const ShotgunVector& v) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float AddDouble(float now, double delay) noexcept { return static_cast<float>(double(now) + delay); }
struct Call
{
    const ShotgunShotHost& host;
    ShotgunShotResult result{ShotgunShotResult::ConsumerRejected};
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = ShotgunShotResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = ShotgunShotResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const ShotgunShotEvent& event) noexcept
    {
        return Run([&]() noexcept { return host.effect(host.context, event); });
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

ShotgunShotResult ShotgunPrimaryAttack(ShotgunShotState& state, const ShotgunShotFrame& f,
    const ShotgunShotHost& host) noexcept
{
    if (!host.current || !host.time || !host.effect || !host.makeVectors || !host.gunPosition ||
        !host.autoAim || !host.randomLong || !host.bullet || !host.soundPosition || !host.sound)
        return ShotgunShotResult::MissingHost;
    if (!host.current(host.context)) return ShotgunShotResult::Cancelled;
    Call call{host};
    auto& action = state.action;
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(action.primary) || !std::isfinite(state.emptyDeadline))
        return ShotgunShotResult::InvalidInput;
    if (action.primary > now || !f.attackPressed || action.action || action.special)
        return ShotgunShotResult::Ineligible;
    const auto click = [&]() noexcept
    {
        if (!(call.Time() > state.emptyDeadline)) return ShotgunShotResult::Ineligible;
        if (!call.Emit({ShotgunShotEffect::EmptyClick})) return call.result;
        state.emptyDeadline = AddDouble(call.Time(), .25);
        return ShotgunShotResult::Empty;
    };
    if (f.waterLevel == 3) return click();
    if (state.shotGate && !action.reloadAbort) action.reloadAbort = 1;
    if (action.clip <= 0)
        return !state.shotGate && !action.clip ? click() : ShotgunShotResult::Ineligible;
    if (state.shotGate) return ShotgunShotResult::Ineligible;
    if (!std::isfinite(f.damage) || f.damage < -2147483648.F || f.damage >= 2147483648.F ||
        !Finite(f.viewAngles) || !Finite(f.ownerOrigin)) return ShotgunShotResult::InvalidInput;
    state.weaponVolume = 1000;
    state.weaponFlash = 256;
    state.pumpPending = true;
    state.pumpDeadline = AddDouble(call.Time(), .45);
    ShotgunShotEvent smoke{ShotgunShotEffect::Gunsmoke};
    smoke.value = f.weaponEntity; smoke.other = f.viewModel; smoke.a = 1;
    if (!call.Emit(smoke)) return call.result;
    if (!f.infiniteAmmo) --action.clip;
    ShotgunBasis basis{};
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, f.viewAngles, basis); })) return call.result;
    ShotgunVector gun{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, gun); })) return call.result;
    if (!Finite(gun) || !Finite(basis.right)) return ShotgunShotResult::InvalidInput;
    ShotgunShotEvent fire{ShotgunShotEffect::FireEvent}; fire.first = f.ownerOrigin;
    for (unsigned i = 0; i != 3; ++i) fire.second[i] = gun[i] + basis.right[i] * 32.F;
    fire.value = 4; fire.other = f.eventOwner2814 != 1;
    if (!call.Emit(fire) || !call.Emit({ShotgunShotEffect::PlayerFire})) return call.result;
    ShotgunShotEvent shake{ShotgunShotEffect::Shake};
    shake.a = 7; shake.b = .35F; shake.c = .15F; shake.value = 2;
    if (!call.Emit(shake)) return call.result;
    ShotgunShotEvent view{ShotgunShotEffect::ViewAnimation}; view.value = action.aiming ? 16 : 1;
    if (!call.Emit(view)) return call.result;
    ShotgunVector position{};
    if (!call.Run([&]() noexcept { return host.soundPosition(host.context, position); })) return call.result;
    if (!call.Run([&]() noexcept { return host.sound(host.context, position); })) return call.result;
    if (!call.Emit({ShotgunShotEffect::MuzzleFlash})) return call.result;
    ShotgunBullet bullet{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, bullet.source); })) return call.result;
    if (!call.Run([&]() noexcept { return host.autoAim(host.context,
        std::bit_cast<float>(std::uint32_t{0x3DB27EB6}), bullet.direction); })) return call.result;
    if (!Finite(bullet.source) || !Finite(bullet.direction)) return ShotgunShotResult::InvalidInput;
    constexpr float spread = std::bit_cast<float>(std::uint32_t{0x3D8EDE55});
    bullet.spread = {spread, spread, spread}; bullet.damage = static_cast<int>(f.damage); bullet.seed = f.randomSeed;
    if (!call.Run([&]() noexcept { return host.bullet(host.context, bullet); })) return call.result;
    int recoil{};
    if (!call.Run([&]() noexcept { return host.randomLong(host.context, -4, 4, recoil); })) return call.result;
    ShotgunShotEvent kick{ShotgunShotEffect::Recoil}; kick.first = {static_cast<float>(recoil), 2, 2};
    if (!call.Emit(kick)) return call.result;
    ShotgunShotEvent shot{ShotgunShotEffect::ShotState}; shot.a = 5;
    if (!call.Emit(shot)) return call.result;
    if (!action.clip && action.reserve <= 0 && !call.Emit({ShotgunShotEffect::AmmoWarning})) return call.result;
    action.primary = call.Time() + 1.F;
    action.idle = action.clip ? AddDouble(call.Time(), 5.) : call.Time() + 1.F;
    action.reloadStage = 0;
    return ShotgunShotResult::Fired;
}

ShotgunShotResult ShotgunPumpShell(const ShotgunShotFrame& f, const ShotgunShotHost& host) noexcept
{
    if (!host.current || !host.effect) return ShotgunShotResult::MissingHost;
    if (!host.current(host.context)) return ShotgunShotResult::Cancelled;
    if (!Finite(f.weaponOrigin) || !Finite(f.weaponViewOffset) || !Finite(f.initialBasis.forward) ||
        !Finite(f.initialBasis.right) || !Finite(f.initialBasis.up) || !std::isfinite(f.weaponVelocityY))
        return ShotgunShotResult::InvalidInput;
    ShotgunShotEvent shell{ShotgunShotEffect::Shell};
    for (unsigned i = 0; i != 3; ++i)
    {
        shell.second[i] = (f.initialBasis.right[i] * 60.F + f.initialBasis.up[i] * 200.F) + f.initialBasis.forward[i] * 40.F;
        shell.first[i] = ((f.weaponViewOffset[i] + f.weaponOrigin[i]) + f.initialBasis.forward[i] * 45.F) + f.initialBasis.right[i] * 30.F;
    }
    shell.a = f.weaponVelocityY; shell.value = f.shellModel; shell.other = 2;
    Call call{host};
    return call.Emit(shell) ? ShotgunShotResult::Fired : call.result;
}
}
