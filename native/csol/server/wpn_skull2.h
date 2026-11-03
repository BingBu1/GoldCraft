// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
class CBasePlayerWeapon;
struct weapon_data_s;
struct CSOLSkull2PurchaseState
{
    int flags{},clip{};
    float deadline{};
    bool valid{};
};
void CSOL_InitSkull2();
void CSOL_PrecacheSkull2();
bool CSOL_Skull2Ready();
bool CSOL_IsSkull2(const CBasePlayerWeapon& weapon);
bool CSOL_Skull2DeploymentComplete(const CBasePlayerWeapon& weapon);
bool CSOL_Skull2MonsterKnockback(const CBasePlayerWeapon& weapon,std::array<float,5>& values);
void CSOL_PackSkull2Weapon(const CBasePlayerWeapon* weapon,weapon_data_s* data);
CSOLSkull2PurchaseState CSOL_SaveSkull2PurchaseState(const CBasePlayerWeapon& weapon);
void CSOL_RestoreSkull2PurchaseState(CBasePlayerWeapon& weapon,const CSOLSkull2PurchaseState& state);
