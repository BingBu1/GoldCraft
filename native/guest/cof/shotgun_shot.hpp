// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shotgun_control.hpp"
#include <array>

namespace guest::cof
{
using ShotgunVector = std::array<float, 3>;
struct ShotgunBasis { ShotgunVector forward{}, right{}, up{}; };
struct ShotgunShotState
{
    ShotgunActionState action;
    float emptyDeadline{}, pumpDeadline{};
    bool pumpPending{};
    int shotGate{}, weaponVolume{}, weaponFlash{};
};
struct ShotgunShotFrame
{
    float damage{}, weaponVelocityY{};
    int waterLevel{}, shellModel{}, weaponEntity{}, viewModel{}, eventOwner2814{};
    std::uint32_t randomSeed{};
    bool attackPressed{}, infiniteAmmo{};
    ShotgunVector weaponOrigin{}, weaponViewOffset{}, ownerOrigin{}, viewAngles{};
    ShotgunBasis initialBasis;
};
enum class ShotgunShotEffect : unsigned char
{
    Gunsmoke, FireEvent, PlayerFire, Shake, ViewAnimation, MuzzleFlash,
    Recoil, ShotState, EmptyClick, AmmoWarning, Shell
};
struct ShotgunShotEvent
{
    ShotgunShotEffect kind;
    ShotgunVector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
};
struct ShotgunBullet
{
    ShotgunVector source{}, direction{}, spread{};
    int damage{};
    std::uint32_t seed{};
    // Fixed by original1006DF20; the actual ballistics consumer must execute
    // one eight-pellet transaction, not eight independent single-shot calls.
    static constexpr unsigned Pellets = 8;
    static constexpr float Distance = 2048.F;
};
struct ShotgunShotHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*effect)(void*, const ShotgunShotEvent&) noexcept {};
    bool (*makeVectors)(void*, const ShotgunVector&, ShotgunBasis&) noexcept {};
    bool (*gunPosition)(void*, ShotgunVector&) noexcept {};
    bool (*autoAim)(void*, float slope, ShotgunVector&) noexcept {};
    bool (*randomLong)(void*, int low, int high, int&) noexcept {};
    bool (*bullet)(void*, const ShotgunBullet&) noexcept {};
    bool (*soundPosition)(void*, ShotgunVector&) noexcept {};
    // Original cooperative10116A40: weapons_new/shotgun/shoot.wav,flags0/play1/broadcast1.
    bool (*sound)(void*, const ShotgunVector&) noexcept {};
};
enum class ShotgunShotResult : unsigned char
{
    Fired, Empty, Ineligible, MissingHost, InvalidInput, ConsumerRejected, Cancelled
};

// Complete ordinary Primary1006DF20. Pump scheduling belongs to the weapon;
// the later shell callback reads the then-current basis and weapon geometry.
ShotgunShotResult ShotgunPrimaryAttack(ShotgunShotState&, const ShotgunShotFrame&,
    const ShotgunShotHost&) noexcept;
ShotgunShotResult ShotgunPumpShell(const ShotgunShotFrame&, const ShotgunShotHost&) noexcept;
}
