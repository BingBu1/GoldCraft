// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitMeleeWeapons();
void CSOL_PrecacheMeleeWeapons();
bool CSOL_MeleeReady(int weaponId);
bool CSOL_MeleeEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
int CSOL_MeleeItemIdentity(const CBasePlayerWeapon& weapon);
// Assign the verified original item identity before acquiring a scenario knife.
bool CSOL_ScenarioSetMeleeIdentity(CBasePlayerWeapon& weapon);
// Native inventory selection ignores Deploy's return value. This confirms
// that this exact melee entity completed its own latest Deploy successfully.
bool CSOL_MeleeDeploymentComplete(const CBasePlayerWeapon& weapon);
void CSOL_PackMeleeWeapon(const CBasePlayerWeapon* weapon, weapon_data_s* data);
