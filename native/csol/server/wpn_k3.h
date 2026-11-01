// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitK3();
void CSOL_PrecacheK3();
bool CSOL_K3Ready();
bool CSOL_K3EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
void CSOL_PackK3Weapon(const CBasePlayerWeapon* weapon, weapon_data_s* data);
