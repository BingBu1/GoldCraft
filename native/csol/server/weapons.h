// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CS leaves the legacy Half-Life Glock slot unused. Slot 31 is WEAPON_SUIT.
// This wire slot is distinct from CSO's original item ID 220.
constexpr int CSOL_WEAPON_TKNIFEEX2 = 2;
class CBasePlayerWeapon;
class CBasePlayerItem;
bool CSOL_IsWeapon(const CBasePlayerWeapon* weapon);
const char* CSOL_WorldModel(const CBasePlayerItem* item);
void CSOL_InitWeapons();
void CSOL_PrecacheWeapons();
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
void CSOL_InitWeaponTests();
void CSOL_PrecacheWeaponTests();
#endif
