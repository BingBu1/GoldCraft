// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct entvars_s;
bool CSOL_BeamInflictor(const entvars_s* inflictor);
class CBasePlayerWeapon;
class CBasePlayerItem;
class CBasePlayer;
struct weapon_data_s;
void CSOL_InitBeamWeapons();
void CSOL_PrecacheBeamWeapons();
void CSOL_PackBeamWeapon(CBasePlayerWeapon* weapon, weapon_data_s* data);
void CSOL_AdvanceBeamWeapon(CBasePlayerWeapon* weapon, float seconds);
void CSOL_RemoveBeamWeapon(CBasePlayerItem* item);
bool CSOL_BeamGuardBlocks(CBasePlayer* player, float damage);
void CSOL_ResetBeamGuard(CBasePlayer* player);
void CSOL_UpdateBeamGuardHUD(CBasePlayer* player);
