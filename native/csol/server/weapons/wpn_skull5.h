// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitSkull5();
void CSOL_PrecacheSkull5();
bool CSOL_Skull5Ready();
bool CSOL_IsSkull5(const CBasePlayerWeapon& weapon);
bool CSOL_Skull5DeploymentComplete(const CBasePlayerWeapon& weapon);
void CSOL_PackSkull5Weapon(const CBasePlayerWeapon* weapon,weapon_data_s* data);
