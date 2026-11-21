// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerItem;
class CBasePlayerWeapon;
void CSOL_PrecacheCofArsenal();
bool CSOL_CofArsenalReady(CBasePlayer&, int id) noexcept;
bool CSOL_IsCofArsenal(const CBasePlayerWeapon&, int id) noexcept;
bool CSOL_CofArsenalDeployed(const CBasePlayerWeapon&, int id) noexcept;
const char* CSOL_CofArsenalWorldModel(const CBasePlayerItem*);
bool CSOL_GiveCofArsenalGun(CBasePlayer&, int id);
