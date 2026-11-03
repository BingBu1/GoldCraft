// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitM249EX();
void CSOL_PrecacheM249EX();
bool CSOL_M249EXReady();
bool CSOL_M249EXEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_M249EXDeploymentComplete(const CBasePlayerWeapon& weapon);
void CSOL_PackM249EXWeapon(const CBasePlayerWeapon* weapon, weapon_data_s* data);
