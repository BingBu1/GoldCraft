// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayer;

// Preserve native capacities outside the current scenario. Weapon getters and
// GiveAmmo both call this: world ammo pickups need the recipient's level too.
int CSOL_PlayerAmmoCapacity(CBasePlayer* player, const char* ammo, int nativeCapacity);
int CSOL_PlayerArmorCapacity(CBasePlayer* player);
