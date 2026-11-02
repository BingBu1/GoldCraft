// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
// Uses normal Spawn/Touch and inventory hooks. Replaces the knife slot only
// after the new entity has been acquired; the caller owns purchase charging.
// A rejected insertion is compensated only while entity/player identities are
// still current. Third-party transfers and retired maps are never reclaimed.
bool CSOL_DeliverMeleeWeapon(CBasePlayer& player, int weaponId);
