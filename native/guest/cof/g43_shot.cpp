// SPDX-License-Identifier: GPL-3.0-or-later
#include "g43_shot.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Finite(const G43Vector& v) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float AddDouble(float now, double delay) noexcept
{
    return static_cast<float>(double(now) + delay);
}
struct Call
{
    const G43ShotHost& host;
    G43ShotResult result{G43ShotResult::ConsumerRejected};
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = G43ShotResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = G43ShotResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const G43ShotEvent& event) noexcept
    {
        return Run([&]() noexcept { return host.effect(host.context, event); });
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

G43ShotResult G43PrimaryAttack(G43ShotState& state, const G43ShotFrame& f,
    const G43ShotHost& host) noexcept
{
    if (!host.current || !host.time || !host.effect || !host.reload || !host.makeVectors ||
        !host.gunPosition || !host.shotAngles || !host.autoAim || !host.randomLong ||
        !host.bullet || !host.soundPosition || !host.sound) return G43ShotResult::MissingHost;
    if (!host.current(host.context)) return G43ShotResult::Cancelled;
    auto& action = state.action;
    Call call{host};
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(action.primary) || !std::isfinite(state.emptyDeadline))
        return G43ShotResult::InvalidInput;
    if (action.primary > now || !f.attackPressed || action.action || action.special)
        return G43ShotResult::Ineligible;
    if (f.waterLevel == 3 || action.clip <= 0)
    {
        if (f.waterLevel != 3 && !f.fireOnEmpty)
            return call.Run([&]() noexcept { return host.reload(host.context); }) ?
                G43ShotResult::ReloadRequested : call.result;
        if (now <= state.emptyDeadline) return G43ShotResult::Ineligible;
        G43ShotEvent view{G43ShotEffect::ViewAnimation};
        view.value = action.aiming ? 11 : 2;
        if (!call.Emit(view) || !call.Emit({G43ShotEffect::EmptyClick})) return call.result;
        state.emptyDeadline = AddDouble(call.Time(), .5);
        return G43ShotResult::Empty;
    }
    if (!std::isfinite(f.damage) || f.damage < -2147483648.F || f.damage >= 2147483648.F ||
        !std::isfinite(state.spread.scalar) || !std::isfinite(state.spreadIncrement) || !Finite(state.spread.vector) ||
        !Finite(f.weaponOrigin) || !Finite(f.weaponViewOffset) || !Finite(f.ownerOrigin) || !Finite(f.viewAngles) ||
        !std::isfinite(f.weaponVelocityY) || !Finite(f.initialBasis.forward) ||
        !Finite(f.initialBasis.right) || !Finite(f.initialBasis.up)) return G43ShotResult::InvalidInput;
    G43ShotEvent shell{G43ShotEffect::Shell};
    for (unsigned i = 0; i != 3; ++i)
    {
        shell.first[i] = ((f.weaponViewOffset[i] + f.weaponOrigin[i]) + f.initialBasis.forward[i] * 80.F) + f.initialBasis.right[i] * 20.F;
        shell.second[i] = (f.initialBasis.up[i] * 200.F + f.initialBasis.right[i] * 220.F) + f.initialBasis.forward[i] * 40.F;
    }
    shell.a = f.weaponVelocityY; shell.value = f.shellModel; shell.other = 1;
    if (!call.Emit(shell)) return call.result;
    G43ShotEvent smoke{G43ShotEffect::Gunsmoke};
    smoke.value = f.weaponEntity; smoke.other = f.viewModel; smoke.a = 1;
    if (!call.Emit(smoke)) return call.result;
    if (!f.infiniteAmmo) --action.clip;
    G43Basis basis{};
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, f.viewAngles, basis); })) return call.result;
    G43Vector gun{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, gun); })) return call.result;
    if (!Finite(gun) || !Finite(basis.right)) return G43ShotResult::InvalidInput;
    G43ShotEvent fire{G43ShotEffect::FireEvent}; fire.first = f.ownerOrigin;
    for (unsigned i = 0; i != 3; ++i) fire.second[i] = gun[i] + basis.right[i] * 32.F;
    fire.value = 26; fire.other = f.eventOwner2814 != 1;
    if (!call.Emit(fire) || !call.Emit({G43ShotEffect::PlayerFire})) return call.result;
    int recoil{};
    if (!call.Run([&]() noexcept { return host.randomLong(host.context, -8, 8, recoil); })) return call.result;
    G43ShotEvent kick{G43ShotEffect::Recoil};
    kick.first = {static_cast<float>(recoil), 5, 5};
    if (!call.Emit(kick)) return call.result;
    const bool crouch = (f.ownerFlags & 0x4000U) != 0;
    G43ShotEvent shake{G43ShotEffect::Shake};
    shake.a = crouch ? 6.F : 8.F; shake.b = crouch ? .5F : .4F; shake.c = .25F; shake.value = 3;
    if (!call.Emit(shake)) return call.result;
    G43ShotEvent view{G43ShotEffect::ViewAnimation};
    if (action.clip == 0) view.value = action.aiming ? 11 : 3;
    else
    {
        const int first = action.aiming ? 13 : 4;
        int variation{};
        if (!call.Run([&]() noexcept { return host.randomLong(host.context, 0, 2, variation); })) return call.result;
        view.value = variation + first;
    }
    if (!call.Emit(view) || !call.Emit({G43ShotEffect::PlayerMove}) ||
        !call.Emit({G43ShotEffect::MuzzleFlash})) return call.result;
    G43Vector angles{};
    if (!call.Run([&]() noexcept { return host.shotAngles(host.context, angles); })) return call.result;
    if (!Finite(angles)) return G43ShotResult::InvalidInput;
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, angles, basis); })) return call.result;
    G43Bullet bullet{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, bullet.source); })) return call.result;
    if (!call.Run([&]() noexcept { return host.autoAim(host.context, 0x1.63a1a8p-3F, bullet.direction); })) return call.result;
    if (!Finite(bullet.source) || !Finite(bullet.direction)) return G43ShotResult::InvalidInput;
    state.spread.scalar = state.spreadIncrement + state.spread.scalar;
    state.spread.recoveryDeadline = AddDouble(call.Time(), .95);
    bullet.spread = state.spread.vector; bullet.damage = static_cast<int>(f.damage); bullet.seed = f.randomSeed;
    if (!call.Run([&]() noexcept { return host.bullet(host.context, bullet); })) return call.result;
    action.idle = call.Time() + 1.F;
    G43ShotEvent shot{G43ShotEffect::ShotState}; shot.a = 6;
    if (!call.Emit(shot)) return call.result;
    G43Vector soundPosition{};
    if (!call.Run([&]() noexcept { return host.soundPosition(host.context, soundPosition); })) return call.result;
    if (!call.Run([&]() noexcept { return host.sound(host.context, soundPosition); })) return call.result;
    G43ShotEvent latch{G43ShotEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(call.Time(), .2);
    if (!call.Emit(latch)) return call.result;
    action.primary = AddDouble(call.Time(), .9);
    action.secondary = AddDouble(call.Time(), .9);
    if (action.clip <= 0) action.primary = AddDouble(call.Time(), .9);
    return G43ShotResult::Fired;
}
}
