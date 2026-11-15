// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp5_primary.hpp"

#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace guest::cof
{
namespace
{
bool Finite(const MP5Vector& v) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float AddDouble(float now, double delay) noexcept
{
    return static_cast<float>(double(now) + delay);
}
// Call-local lifetime protection, not an admission/resource state machine.
struct Call
{
    const MP5PrimaryHost& host;
    MP5PrimaryResult result{MP5PrimaryResult::ConsumerRejected};
    template<class F> bool Run(F&& f) noexcept
    {
        if (!host.current(host.context)) { result = MP5PrimaryResult::Cancelled; return false; }
        const bool accepted = f();
        if (!host.current(host.context)) { result = MP5PrimaryResult::Cancelled; return false; }
        return accepted;
    }
    bool Emit(const MP5PrimaryEvent& event) noexcept
    {
        return Run([&]() noexcept { return host.effect(host.context, event); });
    }
};
}

MP5PrimaryResult MP5PrimaryAttack(MP5PrimaryState& state, const MP5PrimaryFrame& f,
                                const MP5PrimaryHost& host) noexcept
{
    if (!host.current || !host.effect || !host.reload || !host.makeVectors ||
        !host.gunPosition || !host.autoAim || !host.randomLong || !host.bullet || !host.automaticSound)
        return MP5PrimaryResult::MissingHost;
    if (!host.current(host.context)) return MP5PrimaryResult::Cancelled;
    auto& action = state.action;
    if (!std::isfinite(f.now) || !std::isfinite(action.primary) || !std::isfinite(state.emptyDeadline))
        return MP5PrimaryResult::InvalidInput;
    if (action.primary > f.now || action.action || action.special) return MP5PrimaryResult::Ineligible;
    Call call{host};
    if (f.waterLevel == 3 || action.clip <= 0)
    {
        if (f.waterLevel != 3 && !f.fireOnEmpty)
            return call.Run([&]() noexcept { return host.reload(host.context); }) ?
                MP5PrimaryResult::ReloadRequested : call.result;
        if (f.now <= state.emptyDeadline) return MP5PrimaryResult::Ineligible;
        if (f.waterLevel != 3 || !action.aiming)
        {
            MP5PrimaryEvent view{MP5PrimaryEffect::ViewAnimation};
            view.value = action.aiming ? 20 : 7;
            if (!call.Emit(view)) return call.result;
        }
        if (!call.Emit({MP5PrimaryEffect::EmptyClick})) return call.result;
        state.emptyDeadline = AddDouble(f.now, .25);
        return MP5PrimaryResult::Empty;
    }
    // Host-only data validation; do not constrain legitimate skill-granted ammo
    // to the original 30-round base capacity. Cold resources belong to the host.
    if (!std::isfinite(f.damage) || f.damage < -2147483648.F || f.damage >= 2147483648.F ||
        !std::isfinite(state.spread.scalar) || !Finite(state.spread.vector) ||
        !Finite(f.weaponOrigin) || !Finite(f.weaponViewOffset) || !Finite(f.ownerOrigin) ||
        !Finite(f.viewAngles) || !Finite(f.punchAngles) || !std::isfinite(f.weaponVelocityY) ||
        !Finite(f.initialBasis.forward) || !Finite(f.initialBasis.right) || !Finite(f.initialBasis.up))
        return MP5PrimaryResult::InvalidInput;
    MP5PrimaryEvent shell{MP5PrimaryEffect::Shell};
    for (unsigned i = 0; i != 3; ++i)
    {
        shell.first[i] = ((f.weaponViewOffset[i] + f.weaponOrigin[i]) + f.initialBasis.forward[i] * 80.F) + f.initialBasis.right[i] * 20.F;
        shell.second[i] = (f.initialBasis.up[i] * 200.F + f.initialBasis.right[i] * 220.F) + f.initialBasis.forward[i] * 40.F;
    }
    shell.a = f.weaponVelocityY; shell.value = f.shellModel; shell.other = 1;
    if (!call.Emit(shell)) return call.result;
    MP5PrimaryEvent kill{MP5PrimaryEffect::KillLocalParticles}; kill.value = 1;
    if (!call.Emit(kill)) return call.result;
    MP5PrimaryEvent smoke{MP5PrimaryEffect::Gunsmoke};
    smoke.value = f.weaponEntity; smoke.other = f.viewModel; smoke.a = 1;
    if (!call.Emit(smoke)) return call.result;
    if (!f.infiniteAmmo) --action.clip;
    MP5Basis basis{};
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, f.viewAngles, basis); })) return call.result;
    MP5Vector gun{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, gun); })) return call.result;
    if (!Finite(gun) || !Finite(basis.right)) return MP5PrimaryResult::InvalidInput;
    MP5PrimaryEvent fire{MP5PrimaryEffect::FireEvent}; fire.first = f.ownerOrigin;
    for (unsigned i = 0; i != 3; ++i) fire.second[i] = gun[i] + basis.right[i] * 32.F;
    fire.value = 27; fire.other = f.eventOwner2814 != 1;
    if (!call.Emit(fire) || !call.Emit({MP5PrimaryEffect::PlayerFire})) return call.result;
    int recoil{};
    if (!call.Run([&]() noexcept { return host.randomLong(host.context, -2, 2, recoil); })) return call.result;
    MP5PrimaryEvent kick{MP5PrimaryEffect::Recoil}; kick.first = {float(recoil), 1.F, 1.F};
    if (!call.Emit(kick)) return call.result;
    const bool crouch = (f.ownerFlags & 0x4000U) != 0;
    MP5PrimaryEvent shake{MP5PrimaryEffect::Shake};
    shake.a = crouch ? 2.F : 5.F; shake.b = crouch ? .1F : .25F; shake.c = crouch ? .1F : .15F; shake.value = 2;
    if (!call.Emit(shake)) return call.result;
    int sequence = 10;
    if (!action.aiming)
    {
        if (!call.Run([&]() noexcept { return host.randomLong(host.context, 0, 2, sequence); })) return call.result;
        sequence += 4;
    }
    MP5PrimaryEvent view{MP5PrimaryEffect::ViewAnimation}; view.value = sequence;
    if (!call.Emit(view) || !call.Emit({MP5PrimaryEffect::PlayerMove}) ||
        !call.Emit({MP5PrimaryEffect::MuzzleFlash})) return call.result;
    MP5Vector angles{};
    for (unsigned i = 0; i != 3; ++i) angles[i] = f.viewAngles[i] + f.punchAngles[i];
    if (!call.Run([&]() noexcept { return host.makeVectors(host.context, angles, basis); })) return call.result;
    MP5PrimaryBullet bullet{};
    if (!call.Run([&]() noexcept { return host.gunPosition(host.context, bullet.source); })) return call.result;
    if (!call.Run([&]() noexcept { return host.autoAim(host.context, 0x1.63a1a8p-3F, bullet.direction); })) return call.result;
    if (!Finite(bullet.source) || !Finite(bullet.direction)) return MP5PrimaryResult::InvalidInput;
    state.spread.scalar += .35F;
    state.spread.recoveryDeadline = AddDouble(f.now, .35);
    bullet.spread = state.spread.vector; bullet.damage = static_cast<int>(f.damage); bullet.seed = f.randomSeed;
    if (!call.Run([&]() noexcept { return host.bullet(host.context, bullet); })) return call.result;
    action.idle = f.now + 1.F;
    MP5PrimaryEvent shot{MP5PrimaryEffect::ShotState}; shot.a = 1;
    if (!call.Emit(shot) || !call.Run([&]() noexcept { return host.automaticSound(host.context); })) return call.result;
    MP5PrimaryEvent latch{MP5PrimaryEffect::ShotLatch}; latch.value = 1; latch.a = AddDouble(f.now, .2);
    if (!call.Emit(latch)) return call.result;
    action.primary = AddDouble(f.now, .06); action.secondary = f.now + 1.F;
    if (action.clip <= 0) action.primary = f.now + 1.F;
    return MP5PrimaryResult::Fired;
}
}
