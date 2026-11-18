// SPDX-License-Identifier: GPL-3.0-or-later
#include "famas_shot.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Finite(const FamasVector& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
float AddDouble(float now, double delay) noexcept
{
    return static_cast<float>(double(now) + delay);
}
struct Call
{
    const FamasShotHost& host;
    FamasShotResult result{FamasShotResult::ConsumerRejected};
    bool presentation{true};
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = FamasShotResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = FamasShotResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const FamasShotEvent& event) noexcept
    {
        const bool delivered = Run([&]() noexcept {
            return host.effect && host.effect(host.context, event);
        });
        presentation &= delivered;
        return result != FamasShotResult::Cancelled;
    }
    float Time() const noexcept { return host.time(host.context); }
};
}

FamasShotResult FamasPrimaryAttack(FamasShotState& state, const FamasShotFrame& f,
    const FamasShotHost& host) noexcept
{
    if (!host.current || !host.time) return FamasShotResult::MissingHost;
    if (!host.current(host.context)) return FamasShotResult::Cancelled;
    auto& action = state.action;
    Call call{host};
    const float now = call.Time();
    if (!std::isfinite(now) || !std::isfinite(action.primary) || !std::isfinite(state.emptyDeadline))
        return FamasShotResult::InvalidInput;
    if (action.primary > now || action.action || action.special) return FamasShotResult::Ineligible;
    if (f.waterLevel == 3 || action.clip <= 0)
    {
        if (f.waterLevel != 3 && !f.fireOnEmpty)
        {
            if (!host.reload) return FamasShotResult::MissingHost;
            return call.Run([&]() noexcept { return host.reload(host.context); }) ?
                FamasShotResult::ReloadRequested : call.result;
        }
        if (now <= state.emptyDeadline) return FamasShotResult::Ineligible;
        if (!call.Emit({FamasShotEffect::EmptyClick})) return call.result;
        if (f.waterLevel == 3)
            state.emptyDeadline = AddDouble(call.Time(), .25);
        else
        {
            const int sequence = action.aiming ? 21 : 20;
            state.emptyDeadline = AddDouble(call.Time(), .5);
            action.primary = AddDouble(call.Time(), .5);
            action.secondary = AddDouble(call.Time(), .5);
            FamasShotEvent view{FamasShotEffect::ViewAnimation}; view.value = sequence;
            if (!call.Emit(view)) return call.result;
        }
        return call.presentation ? FamasShotResult::Empty : FamasShotResult::EmptyWithoutPresentation;
    }
    if (!host.makeVectors || !host.gunPosition || !host.recoil || !host.shotAngles ||
        !host.autoAim || !host.randomLong || !host.bullet) return FamasShotResult::MissingHost;
    if (!std::isfinite(f.damage) || f.damage < -2147483648.F || f.damage >= 2147483648.F ||
        !std::isfinite(state.spread.scalar) || !std::isfinite(state.spreadIncrement) ||
        !Finite(state.spread.vector) || !Finite(f.viewAngles)) return FamasShotResult::InvalidInput;
    if (Finite(f.weaponOrigin) && Finite(f.weaponViewOffset) && std::isfinite(f.weaponVelocityY) &&
        Finite(f.initialBasis.forward) && Finite(f.initialBasis.right) && Finite(f.initialBasis.up))
    {
        FamasShotEvent shell{FamasShotEffect::Shell};
        for (unsigned i = 0; i != 3; ++i)
        {
            shell.first[i] = ((f.weaponViewOffset[i] + f.weaponOrigin[i]) + f.initialBasis.forward[i] * 80.F) + f.initialBasis.right[i] * 20.F;
            shell.second[i] = (f.initialBasis.up[i] * 200.F + f.initialBasis.right[i] * 220.F) + f.initialBasis.forward[i] * 40.F;
        }
        shell.a = f.weaponVelocityY; shell.value = f.shellModel; shell.other = 1;
        if (!call.Emit(shell)) return call.result;
    }
    else call.presentation = false;
    FamasShotEvent smoke{FamasShotEffect::Gunsmoke};
    smoke.value = f.weaponEntity; smoke.other = f.viewModel; smoke.a = 1;
    if (!call.Emit(smoke)) return call.result;
    if (!f.infiniteAmmo) --action.clip;
    FamasBasis basis{};
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, f.viewAngles, basis); })) return call.result;
    FamasVector gun{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, gun); })) return call.result;
    if (!Finite(gun) || !Finite(basis.right)) return FamasShotResult::InvalidInput;
    if (Finite(f.ownerOrigin))
    {
        FamasShotEvent fire{FamasShotEffect::FireEvent}; fire.first = f.ownerOrigin;
        for (unsigned i = 0; i != 3; ++i) fire.second[i] = gun[i] + basis.right[i] * 32.F;
        fire.value = 23; fire.other = f.eventOwner2814 != 1;
        if (!call.Emit(fire)) return call.result;
    }
    else call.presentation = false;
    int recoil{};
    if (!call.Run([&]() noexcept { return host.randomLong(host.context, -2, 2, recoil); }) ||
        !call.Run([&]() noexcept { return host.recoil(host.context, {static_cast<float>(recoil), 1, 1}); })) return call.result;
    const bool crouch = (f.ownerFlags & 0x4000U) != 0;
    FamasShotEvent shake{FamasShotEffect::Shake};
    shake.a = crouch ? 2.F : 5.F; shake.b = crouch ? .1F : .25F;
    shake.c = crouch ? .1F : .15F; shake.value = 2;
    FamasShotEvent view{FamasShotEffect::ViewAnimation}; view.value = action.aiming ? 17 : 4;
    if (!call.Emit(shake) || !call.Emit(view) || !call.Emit({FamasShotEffect::PlayerMove}) ||
        !call.Emit({FamasShotEffect::MuzzleFlash})) return call.result;
    FamasVector angles{};
    if (!call.Run([&]() noexcept { return host.shotAngles(host.context, angles); })) return call.result;
    if (!Finite(angles)) return FamasShotResult::InvalidInput;
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, angles, basis); })) return call.result;
    FamasBullet bullet{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, bullet.source); }) ||
        !call.Run([&]() noexcept { return host.autoAim(host.context, 0x1.63a1a8p-3F, bullet.direction); })) return call.result;
    if (!Finite(bullet.source) || !Finite(bullet.direction)) return FamasShotResult::InvalidInput;
    state.spread.scalar = state.spreadIncrement + state.spread.scalar;
    state.spread.recoveryDeadline = AddDouble(call.Time(), .25);
    bullet.spread = state.spread.vector; bullet.damage = static_cast<int>(f.damage); bullet.seed = f.randomSeed;
    if (!call.Run([&]() noexcept { return host.bullet(host.context, bullet); })) return call.result;
    action.idle = call.Time() + 1.F;
    FamasShotEvent shot{FamasShotEffect::ShotState}; shot.a = 1;
    if (!call.Emit(shot)) return call.result;
    if (host.soundPosition && host.sound)
    {
        FamasVector soundPosition{};
        const bool position = call.Run([&]() noexcept { return host.soundPosition(host.context, soundPosition); });
        if (call.result == FamasShotResult::Cancelled) return call.result;
        if (position && Finite(soundPosition))
        {
            const bool sound = call.Run([&]() noexcept { return host.sound(host.context, soundPosition); });
            if (call.result == FamasShotResult::Cancelled) return call.result;
            call.presentation &= sound;
        }
        else call.presentation = false;
    }
    else call.presentation = false;
    FamasShotEvent latch{FamasShotEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(call.Time(), .2);
    if (!call.Emit(latch)) return call.result;
    const bool last = action.clip <= 0;
    action.primary = AddDouble(call.Time(), .06);
    action.secondary = call.Time() + 1.F;
    if (last) action.primary = call.Time() + 1.F;
    return call.presentation ? FamasShotResult::Fired : FamasShotResult::FiredWithoutPresentation;
}
}
