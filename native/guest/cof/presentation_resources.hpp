// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_damage.hpp"
#include <array>
#include <span>
namespace guest::cof
{
// Authored NativeCS selection-list/selection-sprite/ammo-sprite resources.
// Their original CoF artwork is verified by Prepare-CryOfFearPresentation.py.
inline constexpr std::array<const char*,3> GlockPresentationResources{
    "sprites/weapon_cof_glock.txt","sprites/guest/cof/cglock.spr","sprites/guest/cof/glock_ammo.spr"};
inline constexpr std::array<const char*,3> P345PresentationResources{
    "sprites/weapon_cof_p345.txt","sprites/guest/cof/cp345.spr","sprites/guest/cof/p345_ammo.spr"};
inline constexpr std::array<const char*,3> RevolverPresentationResources{
    "sprites/weapon_cof_revolver.txt","sprites/guest/cof/crever.spr","sprites/guest/cof/revolver_ammo.spr"};
inline constexpr std::array<const char*,3> VP70PresentationResources{
    "sprites/weapon_cof_vp70.txt","sprites/guest/cof/cvp70.spr","sprites/guest/cof/vp70_ammo.spr"};
constexpr unsigned PistolHudAmmoFrames(Pistol weapon) noexcept
{ return weapon==Pistol::Revolver?6u:1u; }
constexpr std::span<const char* const> PistolPresentationResources(Pistol weapon) noexcept
{
    switch(weapon)
    {
    case Pistol::Glock:return GlockPresentationResources;
    case Pistol::P345:return P345PresentationResources;
    case Pistol::Revolver:return RevolverPresentationResources;
    case Pistol::VP70:return VP70PresentationResources;
    default:return {};
    }
}
}
