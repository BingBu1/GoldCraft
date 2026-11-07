// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerWeapon;
void CSOL_InitCannon();
void CSOL_PrecacheCannon();
bool CSOL_CannonReady();
bool CSOL_ClipAmmo(const CBasePlayerWeapon* weapon);
int CSOL_GiveClipAmmo(CBasePlayer* player, CBasePlayerWeapon* weapon, int count);
