// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

namespace csol::skull3
{
inline constexpr int Id = 142, DualId = 143;
inline constexpr int Clip = 35, DualClip = 70, Reserve = 200;
inline constexpr int AmmoBuyAmount = 15, AmmoCost = 75, Weight = 25;
inline constexpr float MaxSpeed = 245, DualMaxSpeed = 175, ZoomMaxSpeed = 180;
inline constexpr const char* Classname = "weapon_skull3";
inline constexpr const char* DualClassname = "weapon_skull3d";
inline constexpr const char* AmmoName = "45acpaz";
inline constexpr const char* ViewModel = "models/v_skull3.mdl";
inline constexpr const char* PlayerModel = "models/p_skull3.mdl";
inline constexpr const char* DualPlayerModel = "models/p_skull3dual.mdl";
inline constexpr const char* WorldModel = "models/w_skull3.mdl";
inline constexpr const char* SingleEvent = "events/skull3.sc";
inline constexpr const char* LeftEvent = "events/skull3d_left.sc";
inline constexpr const char* RightEvent = "events/skull3d_right.sc";
inline constexpr float Distance = 8192, FireIdle = 2;
inline constexpr int BulletType = 28, PenetrationPower = 35;
inline constexpr float PenetrationDistance = 4000;
// Host JSON names distinguish two entities in one purchased weapon. The
// original property objects each name these three fields without a suffix.
inline constexpr std::array<std::string_view, 6> DamageFields{
    "Damage", "DamageZombieMode", "DamageScenario",
    "Damage_Dual", "DamageZombieMode_Dual", "DamageScenario_Dual"};
inline constexpr std::array<float, 6> OriginalDamage{30, 37, 56, 36, 48, 74};
inline constexpr bool IsWeapon(int id) { return id == Id || id == DualId; }
inline constexpr int Position(bool dual) { return dual ? 2 : 1; }
inline constexpr int MaxClip(bool dual) { return dual ? DualClip : Clip; }
// Host prediction transport: the installed delta carries fuser3 as signed22
// with scale128, whereas the stock integer slots cannot carry these counts.
inline float PackShots(int shots) { return std::clamp(shots, 0, 1000000) / 128.f; }
inline int UnpackShots(float value) { return static_cast<int>(value * 128.f); }
inline constexpr int Left = 1, DelayedFire = 2, Direction = 4;

struct Deployment
{
    int sequence;
    float nextAttack, idle;
};

// MP 111ADBD0/111AFFC0. Call for the selected inventory entity: its peer's
// clip remains untouched. The caller owns the player's shared ammo/position.
// Original dual parity is tested BEFORE filling and sets, but never clears,
// the left-shot bit. The original ItemInfo flags are zero, not record 0x201B.
inline Deployment Deploy(bool dual, int rememberedPosition, int& clip, int& reserve, int& flags)
{
    if (dual && (clip & 1) == 0) flags |= 1;
    if (rememberedPosition == Position(dual))
        return {dual ? 15 : 2, .75f, 1.5f};
    const int loaded = std::min(MaxClip(dual) - clip, reserve);
    clip += loaded;
    reserve -= loaded;
    return {dual ? 16 : 17, 3.f, 3.f};
}

struct Mode
{
    float interval, reloadTime, rangeModifier, accuracyAdd, accuracyLimit, standSpread;
    int penetration;
};
inline constexpr Mode SingleMode{.085f, 2.3f, .95f, .475f, 1.4f, .025f, 1};
inline constexpr Mode DualMode{.07f, 3.3f, .93f, .6f, 1.5f, .0275f, 5};
inline constexpr const Mode& Parameters(bool dual) { return dual ? DualMode : SingleMode; }

// Primary computes this from the PREVIOUS accuracy before entering Fire.
// Forced-standing is an original player state read only by the single gun.
inline float Spread(bool dual, float accuracy, bool grounded,
    bool forcedAirborne = false, bool forcedStanding = false)
{
    const bool airborne = forcedAirborne || (!grounded && (dual || !forcedStanding));
    return accuracy * (airborne ? .375f : Parameters(dual).standSpread);
}

// Fire increments shots even on empty attempts. Keep x86's signed result
// of the low 32-bit square without invoking signed C++ integer overflow.
inline float AccuracyAfterTrigger(bool dual, int shots)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const auto count = static_cast<std::uint32_t>(shots);
    const auto square = std::bit_cast<std::int32_t>(count * count);
    const float divided = static_cast<float>(square) / 190.f;
    const auto& mode = Parameters(dual);
    return std::min(divided + mode.accuracyAdd, mode.accuracyLimit);
}

struct Kick
{
    float upBase, lateralBase, upModifier, lateralModifier, upMaximum, lateralMaximum;
    int directionChange;
};
// MP v2F0 -> v2EC, applied after event playback and the attack clocks.
// The original client v2EC is ret 1Ch; it must not apply this a second time.
inline constexpr Kick Recoil(bool dual, bool grounded, bool moving, bool ducking)
{
    if (!grounded) return dual ? Kick{1.1f, .58f, .43f, .055f, 5, 3.25f, 9} :
        Kick{1, .525f, .4f, .05f, 4.6f, 2.35f, 9};
    if (moving) return dual ? Kick{.7f, .32f, .275f, .035f, 4.25f, 2.23f, 9} :
        Kick{.6f, .3f, .25f, .035f, 3.5f, 2.25f, 10};
    if (ducking) return dual ? Kick{.275f, .18f, .13f, .02f, 2.25f, 1.25f, 10} :
        Kick{.25f, .175f, .125f, .02f, 2.25f, 1.2f, 10};
    return dual ? Kick{.3f, .22f, .18f, .025f, 3, 1.85f, 10} :
        Kick{.275f, .32f, .17f, .05f, 2.4f, 1.85f, 9};
}
}
