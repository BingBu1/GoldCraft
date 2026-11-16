// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"
#include "spread.hpp"

namespace guest::cof
{
using G43Vector = std::array<float, 3>;
struct G43Basis { G43Vector forward{}, right{}, up{}; };
struct G43ShotState
{
    MagazineActionState action;
    SpreadState spread;
    float spreadIncrement{}, emptyDeadline{};
};
struct G43ShotFrame
{
    float damage{}, weaponVelocityY{};
    int waterLevel{}, shellModel{}, weaponEntity{}, viewModel{}, eventOwner2814{};
    std::uint32_t ownerFlags{}, randomSeed{};
    bool attackPressed{}, fireOnEmpty{}, infiniteAmmo{};
    G43Vector weaponOrigin{}, weaponViewOffset{}, ownerOrigin{}, viewAngles{};
    G43Basis initialBasis;
};
enum class G43ShotEffect : unsigned char
{
    Shell, Gunsmoke, FireEvent, PlayerFire, Recoil, Shake, ViewAnimation,
    PlayerMove, MuzzleFlash, ShotState, ShotLatch, EmptyClick
};
struct G43ShotEvent
{
    G43ShotEffect kind;
    G43Vector first{}, second{};
    float a{}, b{}, c{};
    int value{}, other{};
};
struct G43Bullet
{
    G43Vector source{}, direction{}, spread{};
    int damage{};
    std::uint32_t seed{};
};
struct G43ShotHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*effect)(void*, const G43ShotEvent&) noexcept {};
    bool (*reload)(void*) noexcept {};
    bool (*makeVectors)(void*, const G43Vector&, G43Basis&) noexcept {};
    bool (*gunPosition)(void*, G43Vector&) noexcept {};
    // Read current v_angle+punch after the recoil consumer; never entry state.
    bool (*shotAngles)(void*, G43Vector&) noexcept {};
    bool (*autoAim)(void*, float slope, G43Vector&) noexcept {};
    bool (*randomLong)(void*, int low, int high, int&) noexcept {};
    bool (*bullet)(void*, const G43Bullet&) noexcept {};
    bool (*soundPosition)(void*, G43Vector&) noexcept {};
    // Original10116A40: weapons_new/g43/g43_fire.wav, flags0/play1/broadcast1.
    bool (*sound)(void*, const G43Vector&) noexcept {};
};
enum class G43ShotResult : unsigned char
{
    Fired, Empty, ReloadRequested, Ineligible, MissingHost, InvalidInput,
    ConsumerRejected, Cancelled
};

// Complete ordinary original10044690. No M16 burst clock or firing mode.
// Native shell consumers retain the original cooperative suppression branch.
G43ShotResult G43PrimaryAttack(G43ShotState&, const G43ShotFrame&, const G43ShotHost&) noexcept;
}
