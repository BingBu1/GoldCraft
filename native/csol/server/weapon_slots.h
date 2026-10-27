// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerItem;
struct ItemInfo;
struct WeaponSlotInfo;
struct weapon_data_s;

unsigned int CSOL_LegacyWeaponMask(int id);
bool CSOL_HasWeapon(CBasePlayer* player, int id);
bool CSOL_HasAnyWeapon(CBasePlayer* player, const CBasePlayerItem* excluded = nullptr);
ItemInfo* CSOL_ItemInfo(int id);
WeaponSlotInfo* CSOL_WeaponSlot(int id);
WeaponSlotInfo* CSOL_WeaponSlot(const char* name);
int CSOL_ExtendedWeaponHudSlot(int id);
bool CSOL_RegisterItemInfo(const ItemInfo& info);
int CSOL_MaxAmmoCarry(const char* ammo);
void CSOL_ResetWeaponRegistry();
void CSOL_WriteWeaponDefinitions();
void CSOL_UpdateWeaponInventory(CBasePlayer* player);
bool CSOL_SendWeaponPickup(CBasePlayer* player, int id);
bool CSOL_SendAmmoPickup(CBasePlayer* player, const char* ammo, int amount);
bool CSOL_AmmoPurchase(int id, int& price, int& count);
bool CSOL_ExtendedWeapon(int id);
bool CSOL_CanUseExtendedWeapons(CBasePlayer* player);
int CSOL_PredictionSlot(int id, bool active);
