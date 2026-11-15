// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"
#include "spread.hpp"

namespace guest::cof
{
using MP5Vector = std::array<float, 3>;
struct MP5Basis { MP5Vector forward{}, right{}, up{}; };

// Explicit original state, owned by one authoritative weapon instance. The
// host mirrors ammo before external consumers and retains it across switches.
struct MP5PrimaryState
{
    MagazineActionState action;
    SpreadState spread;
    float emptyDeadline{};
};
struct MP5PrimaryFrame
{
    float now{}, damage{};
    int waterLevel{}, shellModel{}, weaponEntity{}, viewModel{};
    std::uint32_t ownerFlags{}, randomSeed{};
    int eventOwner2814{};
    bool fireOnEmpty{}, infiniteAmmo{};
    MP5Vector weaponOrigin{}, weaponViewOffset{}, ownerOrigin{}, viewAngles{};
    float weaponVelocityY{};
    MP5Basis initialBasis;
};
enum class MP5PrimaryEffect : unsigned char
{
    Shell, KillLocalParticles, Gunsmoke, FireEvent, PlayerFire, Recoil, Shake,
    ViewAnimation, PlayerMove, MuzzleFlash, ShotState, ShotLatch, EmptyClick
};
struct MP5PrimaryEvent
{
    MP5PrimaryEffect kind;
    MP5Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
};
struct MP5PrimaryBullet
{
    MP5Vector source{}, direction{}, spread{};
    int damage{};
    std::uint32_t seed{};
    // Original 10088CA0: one shot, distance8192, bullet3, tracer2. The host
    // must use its separately verified cooperative player-hit policy.
};
struct MP5PrimaryHost
{
    void* context{};
    // Main-thread identity checks surround callbacks that can retire owners.
    // Failure affects this call; it never revokes firearm admission.
    bool (*current)(void*) noexcept {};
    bool (*effect)(void*, const MP5PrimaryEvent&) noexcept {};
    bool (*reload)(void*) noexcept {};
    bool (*makeVectors)(void*, const MP5Vector&, MP5Basis&) noexcept {};
    bool (*gunPosition)(void*, MP5Vector&) noexcept {};
    // Read view+punch after recoil and animation consumers, as 1005B8C1 does.
    bool (*shotAngles)(void*, MP5Vector&) noexcept {};
    bool (*autoAim)(void*, float slope, MP5Vector&) noexcept {};
    bool (*randomLong)(void*, int low, int high, int&) noexcept {};
    bool (*bullet)(void*, const MP5PrimaryBullet&) noexcept {};
    bool (*automaticSound)(void*) noexcept {};
};
enum class MP5PrimaryResult : unsigned char
{
    Fired, Empty, ReloadRequested, Ineligible, MissingHost, InvalidInput,
    ConsumerRejected, Cancelled
};

// Matched original hl.dll 1005B400, ordinary cooperative PrimaryAttack.
// This component does not register or admit an MP5. In particular sequence20
// still requires its native animation consumer and full SDK host integration.
MP5PrimaryResult MP5PrimaryAttack(MP5PrimaryState&, const MP5PrimaryFrame&,
                                const MP5PrimaryHost&) noexcept;
}
