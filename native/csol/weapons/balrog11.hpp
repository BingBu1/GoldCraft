// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace csol::balrog11
{
// MP39f962f3 CBalrog11/BALROG11_prop; HWc7bddfda WeaponMgr record203.
inline constexpr int Id = 203, ItemId = 554, Clip = 7, Reserve = 32;
inline constexpr int AmmoBuyAmount = 8, AmmoCost = 130;
inline constexpr int Pellets = 6, ShotsPerCharge = 4, MaximumCharges = 7;
// The original secondary ammo metadata is separate from the weapon's
// internal charge magazine. Do not clamp earned charges to this reserve.
inline constexpr int SecondaryAmmoReserve = 4;
inline constexpr float MaxSpeed = 240, Distance = 3148, Spread = .0735f;
inline constexpr float Interval = .27f, SecondaryInterval = .35f;
inline constexpr float ReloadFirst = .475f, ReloadInsert = .28f, ReloadFinish = .4333333373f;
inline constexpr float FireIdle = 2.25f, EmptyIdle = .75f, IdleTime = 33.66666412f;
inline constexpr float FlameSpeed = 1000, FlameLife = .75f, FlameForwardMargin = 25;
inline constexpr float FlameAngle = 7;
inline constexpr std::array<float, 5> FlameAngles{0, FlameAngle, -FlameAngle, 2 * FlameAngle, -2 * FlameAngle};
inline constexpr const char* Classname = "weapon_balrog11";
inline constexpr const char* AmmoName = "buckshotb";
inline constexpr const char* SecondaryAmmoName = "buckbcs";
inline constexpr const char* Event = "events/balrog11.sc";
inline constexpr const char* ViewModel = "models/v_balrog11.mdl";
inline constexpr const char* PlayerModel = "models/p_balrog11.mdl";
inline constexpr const char* WorldModel = "models/w_balrog11.mdl";
enum Animation { Idle, Shoot1, Shoot2, ShootFlame, Insert, AfterReload, StartReload, Draw };
inline constexpr std::array<std::string_view, 6> DamageFields{
    "Damage", "DamageZombie", "DamageScenario",
    "CannonDamage", "CannonDamageZombie", "CannonDamageScenario"};
inline constexpr std::array<float, 6> OriginalDamage{20, 60, 80, 35, 1125, 2400};

// The same state can be packed to prediction without borrowing a stock
// shotgun's animation or shell counter. Deploy does not discard charges.
struct ChargeState
{
    int consecutiveShots{};
    int charges{};

    bool PrimaryFired()
    {
        if (++consecutiveShots < ShotsPerCharge) return false;
        consecutiveShots = 0;
        if (charges >= MaximumCharges) return false;
        ++charges;
        return true;
    }

    bool ConsumeSecondary()
    {
        consecutiveShots = 0;
        if (charges <= 0) return false;
        --charges;
        return true;
    }

    void Idle() { consecutiveShots = 0; }
};

// CS1.6 weapon_data_t does not transmit iuser2/iuser3. Both counters fit
// losslessly in m_iWeaponState's seven unsigned delta bits.
inline int PackCharge(ChargeState value)
{
    return (value.consecutiveShots & 3) | ((value.charges & 7) << 2);
}
inline ChargeState UnpackCharge(int bits) { return {bits & 3, (bits >> 2) & 7}; }
}
