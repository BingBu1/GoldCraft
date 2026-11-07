// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitBalrog11();
void CSOL_PrecacheBalrog11();
bool CSOL_Balrog11Ready();
bool CSOL_Balrog11EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_Balrog11DeploymentComplete(const CBasePlayerWeapon& weapon);
void CSOL_PackBalrog11Weapon(const CBasePlayerWeapon* weapon, weapon_data_s* data);
int CSOL_Balrog11Charges(const CBasePlayerWeapon* weapon);
