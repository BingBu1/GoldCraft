// SPDX-License-Identifier: GPL-3.0-or-later
#include "m16_shot.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Finite(const M16Vector& v) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float AddDouble(float now, double delay) noexcept
{
    return static_cast<float>(double(now) + delay);
}
struct Call
{
    const M16ShotHost& host;
    M16ShotResult result{M16ShotResult::ConsumerRejected};
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = M16ShotResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = M16ShotResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const M16ShotEvent& event) noexcept
    {
        return Run([&]() noexcept { return host.effect(host.context, event); });
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

M16ShotResult M16Shoot(M16ShotState& state, const M16ShotFrame& f, const M16ShotHost& host) noexcept
{
    if (!host.current || !host.time || !host.effect || !host.reload || !host.makeVectors ||
        !host.gunPosition || !host.shotAngles || !host.autoAim || !host.randomLong ||
        !host.bullet || !host.soundPosition || !host.sound) return M16ShotResult::MissingHost;
    if (!host.current(host.context)) return M16ShotResult::Cancelled;
    auto& control = state.control;
    auto& action = control.action;
    if (action.action || action.special) return M16ShotResult::Ineligible;
    Call call{host};
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(state.emptyDeadline)) return M16ShotResult::InvalidInput;
    if (f.waterLevel == 3 || action.clip <= 0)
    {
        if (f.waterLevel != 3 && !f.fireOnEmpty)
            return call.Run([&]() noexcept { return host.reload(host.context); }) ?
                M16ShotResult::ReloadRequested : call.result;
        if (now <= state.emptyDeadline) return M16ShotResult::Ineligible;
        M16ShotEvent view{M16ShotEffect::ViewAnimation};
        view.value = action.aiming ? 25 : control.burst ? 24 : 23;
        if (!call.Emit(view) || !call.Emit({M16ShotEffect::EmptyClick})) return call.result;
        state.emptyDeadline = AddDouble(call.Time(), .25);
        return M16ShotResult::Empty;
    }
    if (!std::isfinite(f.damage) || f.damage < -2147483648.F || f.damage >= 2147483648.F ||
        !std::isfinite(state.spread.scalar) || !std::isfinite(state.spreadIncrement) || !Finite(state.spread.vector) ||
        !Finite(f.weaponOrigin) || !Finite(f.weaponViewOffset) || !Finite(f.ownerOrigin) || !Finite(f.viewAngles) ||
        !std::isfinite(f.weaponVelocityY) || !Finite(f.initialBasis.forward) ||
        !Finite(f.initialBasis.right) || !Finite(f.initialBasis.up)) return M16ShotResult::InvalidInput;
    M16ShotEvent shell{M16ShotEffect::Shell};
    for (unsigned i = 0; i != 3; ++i)
    {
        shell.first[i] = ((f.weaponViewOffset[i] + f.weaponOrigin[i]) + f.initialBasis.forward[i] * 80.F) + f.initialBasis.right[i] * 20.F;
        shell.second[i] = (f.initialBasis.up[i] * 200.F + f.initialBasis.right[i] * 220.F) + f.initialBasis.forward[i] * 40.F;
    }
    shell.a = f.weaponVelocityY; shell.value = f.shellModel; shell.other = 1;
    if (!call.Emit(shell)) return call.result;
    M16ShotEvent smoke{M16ShotEffect::Gunsmoke};
    smoke.value = f.weaponEntity; smoke.other = f.viewModel; smoke.a = 1;
    if (!call.Emit(smoke)) return call.result;
    if (!f.infiniteAmmo) --action.clip;
    M16Basis basis{};
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, f.viewAngles, basis); })) return call.result;
    M16Vector gun{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, gun); })) return call.result;
    if (!Finite(gun) || !Finite(basis.right)) return M16ShotResult::InvalidInput;
    M16ShotEvent fire{M16ShotEffect::FireEvent}; fire.first = f.ownerOrigin;
    for (unsigned i = 0; i != 3; ++i) fire.second[i] = gun[i] + basis.right[i] * 32.F;
    fire.value = 9; fire.other = f.eventOwner2814 != 1;
    if (!call.Emit(fire) || !call.Emit({M16ShotEffect::PlayerFire})) return call.result;
    int recoil{};
    if (!call.Run([&]() noexcept { return host.randomLong(host.context, 0, 2, recoil); })) return call.result;
    if (recoil >= 0 && recoil <= 2)
    {
        M16ShotEvent kick{M16ShotEffect::Recoil};
        kick.first = recoil == 1 ? M16Vector{-1,1,1} : recoil == 2 ? M16Vector{1,1,-1} : M16Vector{1,1,1};
        if (!call.Emit(kick)) return call.result;
    }
    const bool crouch = (f.ownerFlags & 0x4000U) != 0;
    M16ShotEvent shake{M16ShotEffect::Shake};
    shake.a = crouch ? 2.F : 5.F; shake.b = crouch ? .1F : .25F; shake.c = crouch ? .1F : .15F; shake.value = 2;
    if (!call.Emit(shake)) return call.result;
    M16ShotEvent view{M16ShotEffect::ViewAnimation};
    view.value = action.aiming ? 21 : control.burst ? 9 : 2;
    if (!call.Emit(view) || !call.Emit({M16ShotEffect::PlayerMove}) ||
        !call.Emit({M16ShotEffect::MuzzleFlash})) return call.result;
    M16Vector angles{};
    if (!call.Run([&]() noexcept { return host.shotAngles(host.context, angles); })) return call.result;
    if (!Finite(angles)) return M16ShotResult::InvalidInput;
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, angles, basis); })) return call.result;
    M16Bullet bullet{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, bullet.source); })) return call.result;
    if (!call.Run([&]() noexcept { return host.autoAim(host.context, 0x1.63a1a8p-3F, bullet.direction); })) return call.result;
    if (!Finite(bullet.source) || !Finite(bullet.direction)) return M16ShotResult::InvalidInput;
    state.spread.scalar = state.spreadIncrement + state.spread.scalar;
    state.spread.recoveryDeadline = AddDouble(call.Time(), .35);
    bullet.spread = state.spread.vector; bullet.damage = static_cast<int>(f.damage); bullet.seed = f.randomSeed;
    if (!call.Run([&]() noexcept { return host.bullet(host.context, bullet); })) return call.result;
    M16ShotEvent shot{M16ShotEffect::ShotState}; shot.a = 3;
    if (!call.Emit(shot)) return call.result;
    M16Vector soundPosition{};
    if (!call.Run([&]() noexcept { return host.soundPosition(host.context, soundPosition); })) return call.result;
    if (!call.Run([&]() noexcept { return host.sound(host.context, soundPosition); })) return call.result;
    M16ShotEvent latch{M16ShotEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(call.Time(), .2);
    if (!call.Emit(latch)) return call.result;
    if (control.burst && ++action.burstShots < 3)
    {
        M16ShotEvent scheduled{M16ShotEffect::ScheduleBurst}; scheduled.a = call.Time() + .05F;
        if (!call.Emit(scheduled)) return call.result;
        state.burstDeadline = scheduled.a;
        state.burstPending = true;
    }
    return M16ShotResult::Fired;
}
}
