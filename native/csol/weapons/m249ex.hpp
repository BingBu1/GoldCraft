// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace csol::m249ex
{
// MP39f962f3 CM249EX/M249EX_prop; HWc7bddfda WeaponMgr record96.
inline constexpr int Id = 96, ItemId = 327, Clip = 120, Reserve = 240;
inline constexpr int AmmoBuyAmount = 30, AmmoCost = 200, Penetration = 7;
inline constexpr float MaxSpeed = 220, ZoomSpeed = 200;
inline constexpr float NormalFov = 90, ZoomFov = 65;
inline constexpr float Interval = .125f, ZoomInterval = .15f, ReloadTime = 3.8f;
inline constexpr float DeployTime = .75f, DeployIdle = 1.5f, FireIdle = 1.6f, IdleTime = 20;
inline constexpr float ZoomTime = .3f, Distance = 8192;
inline constexpr float EmptyInterval = .2f;
inline constexpr float InitialAccuracy = .25f;
inline constexpr const char* Classname = "weapon_m249ex";
inline constexpr const char* AmmoName = "556az";
inline constexpr const char* Event = "events/m249ex.sc";
inline constexpr const char* ViewModel = "models/v_m249ex.mdl";
inline constexpr const char* PlayerModel = "models/p_m249ex.mdl";
inline constexpr const char* WorldModel = "models/w_m249ex.mdl";
inline constexpr const char* ShellModel = "models/rshell.mdl";
inline constexpr const char* FireSound = "weapons/m249ex-1.wav";
inline constexpr std::array<std::string_view, 3> DamageFields{
    "Damage", "DamageZombieMode", "DamageScenario"};
inline constexpr std::array<float, 3> OriginalDamage{33, 44, 55};
enum Animation { Idle, Shoot1, Shoot2, Reload, Draw };

// fuser3 is signed22 at scale128 in the actual GoldSrc delta. This carries
// every supported shot count exactly; iuser1's scale128 would overflow255.
inline float PackShots(int shots) { return std::clamp(shots, 0, 1000000) / 128.f; }
inline int UnpackShots(float value) { return static_cast<int>(value * 128.f); }

// The current shot consumes the previous accuracy. This value is updated
// even on an empty trigger attempt, matching CM249EX::Fire111117F0.
inline float AccuracyAfterTrigger(int shots)
{
    if (shots >= 5) return .9f;
    const float count = static_cast<float>(std::max(shots, 0));
    return std::min(count * count * count / 190.f + .3f, .9f);
}

inline float Spread(float accuracy, bool grounded, float speed, bool zoomed, bool ducking)
{
    if (!grounded) return .05f + .2f * accuracy;
    if (speed > 195) return .04f + .03f * accuracy;
    if (zoomed) return .02f + .01f * accuracy;
    if (ducking) return .01f + .02f * accuracy;
    return .03f;
}

struct Kick
{
    float upBase, lateralBase, upModifier, lateralModifier, upMaximum, lateralMaximum;
    int directionChange;
};

inline constexpr Kick Recoil(bool grounded, bool moving, bool zoomed, bool ducking)
{
    if (!grounded) return {1.5f, .55f, .3f, .3f, 5, 5, 5};
    if (moving) return {1.1f, .3f, .2f, .06f, 4, 3, 8};
    if (zoomed) return {.8f, .2f, .08f, 0, 2, .2f, 10};
    if (ducking) return {.75f, .1f, .1f, .018f, 3.5f, 1.8f, 9};
    return {.85f, .2f, .2f, .02f, 3.2f, 2.5f, 7};
}
}
