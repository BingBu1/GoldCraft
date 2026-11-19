// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayer;
class CBasePlayerItem;
class CWeaponBox;

void CSOL_PrecacheLightEquipment();
bool CSOL_CanReceiveLightEquipment(CBasePlayer& player, int id);
bool CSOL_GiveLightEquipment(CBasePlayer& player, int id);
bool CSOL_SelectOwnedLantern(CBasePlayer& player);
const char* CSOL_LightEquipmentWorldModel(const CBasePlayerItem* item);
int CSOL_FlareStock(const CBasePlayerItem* item); // -1 for ordinary equipment
bool CSOL_TryFlareThrowEvent(CBasePlayer& player);
enum class CSOLBoxPickup { Keep, Taken, Cancelled };
CSOLBoxPickup CSOL_PickupBoxLight(CWeaponBox& box, CBasePlayer& player, CBasePlayerItem& item);
