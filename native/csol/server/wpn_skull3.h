// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayerWeapon;
class CBasePlayerItem;
struct weapon_data_s;
struct CSOLSkull3PurchaseState { int flags{}, clip{}; bool valid{}; };

void CSOL_InitSkull3();
void CSOL_PrecacheSkull3();
bool CSOL_Skull3Ready();
bool CSOL_IsSkull3(const CBasePlayerWeapon& weapon);
bool CSOL_Skull3DeploymentComplete(const CBasePlayerWeapon& weapon);
void CSOL_PackSkull3Weapon(const CBasePlayerWeapon* weapon, weapon_data_s* data);
CSOLSkull3PurchaseState CSOL_SaveSkull3PurchaseState(const CBasePlayerWeapon& weapon);
void CSOL_RestoreSkull3PurchaseState(CBasePlayerWeapon& weapon, const CSOLSkull3PurchaseState& state);
