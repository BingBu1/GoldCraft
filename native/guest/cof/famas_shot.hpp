// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"
#include "spread.hpp"

namespace guest::cof
{
using FamasVector = std::array<float, 3>;
struct FamasBasis { FamasVector forward{}, right{}, up{}; };
struct FamasShotState
{
    MagazineActionState action;
    SpreadState spread;
    float spreadIncrement{}, emptyDeadline{};
};
struct FamasShotFrame
{
    float damage{}, weaponVelocityY{};
    int waterLevel{}, shellModel{}, weaponEntity{}, viewModel{}, eventOwner2814{};
    std::uint32_t ownerFlags{}, randomSeed{};
    bool fireOnEmpty{}, infiniteAmmo{};
    FamasVector weaponOrigin{}, weaponViewOffset{}, ownerOrigin{}, viewAngles{};
    FamasBasis initialBasis;
};
enum class FamasShotEffect : unsigned char
{
    Shell, Gunsmoke, FireEvent, Shake, ViewAnimation, PlayerMove,
    MuzzleFlash, ShotState, ShotLatch, EmptyClick
};
struct FamasShotEvent
{
    FamasShotEffect kind;
    FamasVector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
};
struct FamasBullet
{
    FamasVector source{}, direction{}, spread{};
    int damage{};
    std::uint32_t seed{};
};
struct FamasShotHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*effect)(void*, const FamasShotEvent&) noexcept {};
    bool (*reload)(void*) noexcept {};
    bool (*makeVectors)(void*, const FamasVector&, FamasBasis&) noexcept {};
    bool (*gunPosition)(void*, FamasVector&) noexcept {};
    // These consumers preserve recoil -> current shot angles -> bullet order.
    bool (*recoil)(void*, const FamasVector&) noexcept {};
    bool (*shotAngles)(void*, FamasVector&) noexcept {};
    bool (*autoAim)(void*, float slope, FamasVector&) noexcept {};
    bool (*randomLong)(void*, int low, int high, int&) noexcept {};
    bool (*bullet)(void*, const FamasBullet&) noexcept {};
    bool (*soundPosition)(void*, FamasVector&) noexcept {};
    bool (*sound)(void*, const FamasVector&) noexcept {};
};
enum class FamasShotResult : unsigned char
{
    Fired, FiredWithoutPresentation, Empty, EmptyWithoutPresentation,
    ReloadRequested, Ineligible, MissingHost, InvalidInput, ConsumerRejected, Cancelled
};

// Complete ordinary MP1003BBD0. Original cooperative shell suppression remains
// in the native shell consumer. Optional presentation failure is explicit and
// does not leave mechanical firing/reload state unfinished.
FamasShotResult FamasPrimaryAttack(FamasShotState&, const FamasShotFrame&,
    const FamasShotHost&) noexcept;
}
