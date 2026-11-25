// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
bool CSOL_ZombieHuman(CBasePlayer* player);
// Authenticated zombie role, including dead players with in-flight attacks.
bool CSOL_ZombiePlayer(CBasePlayer* player);
bool CSOL_ZombieMode();
void CSOL_ResetWeaponService();
// Delivery only; the C++ scenario shop validates eligibility and charges money.
bool CSOL_CanDeliverScenarioWeapon(CBasePlayer* player, int weapon);
bool CSOL_DeliverScenarioWeapon(CBasePlayer* player, int weapon);
// Chapter carry-over restore (MP 114D9BD0): grant a saved weapon through
// native pickup. Sibling items in the same slot can coexist.
bool CSOL_RestoreScenarioWeapon(CBasePlayer* player, int weapon);
