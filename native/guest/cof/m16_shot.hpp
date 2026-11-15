// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "m16_control.hpp"
#include "spread.hpp"

namespace guest::cof
{
using M16Vector = std::array<float, 3>;
struct M16Basis { M16Vector forward{}, right{}, up{}; };
struct M16ShotState
{
    M16ControlState control;
    SpreadState spread;
    float spreadIncrement{.75F}, emptyDeadline{}, burstDeadline{};
    bool burstPending{};
};
struct M16ShotFrame
{
    float damage{}, weaponVelocityY{};
    int waterLevel{}, shellModel{}, weaponEntity{}, viewModel{}, eventOwner2814{};
    std::uint32_t ownerFlags{}, randomSeed{};
    bool fireOnEmpty{}, infiniteAmmo{};
    M16Vector weaponOrigin{}, weaponViewOffset{}, ownerOrigin{}, viewAngles{};
    M16Basis initialBasis;
};
enum class M16ShotEffect : unsigned char
{
    Shell, Gunsmoke, FireEvent, PlayerFire, Recoil, Shake, ViewAnimation,
    PlayerMove, MuzzleFlash, ShotState, ShotLatch, EmptyClick, ScheduleBurst
};
struct M16ShotEvent
{
    M16ShotEffect kind;
    M16Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
};
// ScheduleBurst.a is the native absolute deadline. The host installs its
// callback from the event; the typed pending state commits after acceptance.
struct M16Bullet
{
    M16Vector source{}, direction{}, spread{};
    int damage{};
    std::uint32_t seed{};
};
struct M16ShotHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*effect)(void*, const M16ShotEvent&) noexcept {};
    bool (*reload)(void*) noexcept {};
    bool (*makeVectors)(void*, const M16Vector&, M16Basis&) noexcept {};
    bool (*gunPosition)(void*, M16Vector&) noexcept {};
    bool (*shotAngles)(void*, M16Vector&) noexcept {};
    bool (*autoAim)(void*, float slope, M16Vector&) noexcept {};
    bool (*randomLong)(void*, int low, int high, int&) noexcept {};
    bool (*bullet)(void*, const M16Bullet&) noexcept {};
    bool (*soundPosition)(void*, M16Vector&) noexcept {};
    // One original10116A40 call per shot: weapons_new/m16/m16_fire.wav,
    // flags0, play1, broadcast1. Cooperative spatial routing belongs here.
    bool (*sound)(void*, const M16Vector&) noexcept {};
};
enum class M16ShotResult : unsigned char
{
    Fired, Empty, ReloadRequested, Ineligible, MissingHost, InvalidInput,
    ConsumerRejected, Cancelled
};

// Complete original10052120, including its own action/empty gates. A scheduled
// shot deliberately does not test the group's primary/secondary deadline.
// This component does not register an M16 or dispatch an owned think callback.
M16ShotResult M16Shoot(M16ShotState&, const M16ShotFrame&, const M16ShotHost&) noexcept;
}
