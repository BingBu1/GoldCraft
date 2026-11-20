// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerWeapon;
bool CSOL_DeliverSkullBalrog(CBasePlayer& player, int weaponId);
bool CSOL_CanDeliverScenarioStockGun(CBasePlayer& player, int weaponId) noexcept;
bool CSOL_DeliverScenarioStockGun(CBasePlayer& player, int weaponId);
// Host-only foreign weapons may reuse the native allocation/drop/rollback
// transaction without becoming a CSO weapon or bypassing CanHave/AddPlayerItem.
// Classname storage and callback ownership must outlive the synchronous call.
struct CSOLGunPurchaseAdapter
{
    int id{},slot{};
    const char* classname{};
    bool (*ready)(CBasePlayer&,int) noexcept {};
    bool (*identity)(const CBasePlayerWeapon&,int) noexcept {};
    bool (*deployed)(const CBasePlayerWeapon&,int) noexcept {};
    bool oneShotTouch{};
};
bool CSOL_DeliverAdaptedGun(CBasePlayer&,const CSOLGunPurchaseAdapter&);
