// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Local x86 CreateInterface ABI. Pawn supplies trusted ZP state, never userinfo.
namespace csol {
constexpr const char* WeaponServiceVersion = "CSOLWeaponService001";
class IWeaponService : public IBaseInterface {
public:
    virtual int CanBuy(int player, int weapon) = 0;
    virtual int Buy(int player, int weapon) = 0;
    virtual void SetZombieMode(bool enabled) = 0;
    // 0 unknown, 1 human, 2 zombie. Reset protection on every life transition.
    virtual void SetPlayerRole(int player, int role) = 0;
};
// Separate interface keeps the already shipped WeaponService001 vtable intact.
constexpr const char* WeaponAmmoServiceVersion = "CSOLWeaponAmmo001";
class IWeaponAmmoService : public IBaseInterface {
public:
    // Refill one normal purchase unit for each distinct owned primary ammo type.
    // Does not sell charge/energy. Returns the number of rounds actually added.
    virtual int Refill(int player) = 0;
};
}
