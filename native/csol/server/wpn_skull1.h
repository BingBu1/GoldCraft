// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
class CBasePlayerWeapon;
struct weapon_data_s;
void CSOL_InitSkull1();
void CSOL_PrecacheSkull1();
bool CSOL_Skull1Ready();
bool CSOL_Skull1EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_Skull1DeploymentComplete(const CBasePlayerWeapon& weapon);
bool CSOL_Skull1ScenarioKnockback(const CBasePlayerWeapon& weapon,std::array<float,5>& values);
void CSOL_PackSkull1Weapon(const CBasePlayerWeapon* weapon,weapon_data_s* data);
